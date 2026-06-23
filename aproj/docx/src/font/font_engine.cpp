// 字体引擎实现
// 使用 HarfBuzz 进行精确字形度量（与 LO 一致）
// 对应 LibreOffice 的 VCL 字体子系统 + sw 的 SwFntObj

#include "font_engine.h"

// stb_truetype 实现（保留用于 LoadFromFile 初始化验证）
#define STB_TRUETYPE_IMPLEMENTATION
#include "../../third_party/stb_truetype.h"

// HarfBuzz 用于字体度量计算（与 LO 一致）
#include <hb.h>
#include <hb-ot.h>

#include <iostream>
#include <cstdio>
#include <cmath>
#include <map>
#include <algorithm>
#include <cstdint>
#include <vector>
#include <string>

// 对应 LO DocumentSettingId::ADD_EXT_LEADING / MS_WORD_COMP_GRID_METRICS
static bool g_bMsWordCompGridMetrics = true;
static bool g_bAddExtLeading = true;

#ifdef _WIN32
#include <windows.h>
#pragma comment(lib, "gdi32.lib")
#undef GetCharWidth
#endif

namespace
{
#ifdef _WIN32
// GDI 96dpi 像素 → twips（1440/96=15），与 LO VCL Windows 路径一致
static SwTwips GdiPixelsToTwips(int nPixels) { return static_cast<SwTwips>(nPixels * 15); }

static int GdiHalfPtToLogPixels(int nFontSizeHalfPt)
{
    return static_cast<int>(std::round(nFontSizeHalfPt * 2.0 / 3.0));
}

static int GdiFontWeightFromName(const std::string& rName)
{
    std::string lower = rName;
    for (auto& c : lower)
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    if (lower.find("semibold") != std::string::npos || lower.find("semi bold") != std::string::npos
        || lower.find("demibold") != std::string::npos)
        return FW_SEMIBOLD;
    if (lower.find("bold") != std::string::npos)
        return FW_BOLD;
    if (lower.find("light") != std::string::npos)
        return FW_LIGHT;
    if (lower.find("medium") != std::string::npos)
        return FW_MEDIUM;
    return FW_NORMAL;
}

static std::wstring Utf8ToWide(const std::string& text)
{
    if (text.empty())
        return {};
    int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr,
                                   0);
    if (wlen <= 0)
        return {};
    std::wstring wide(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), wide.data(), wlen);
    return wide;
}

static int WidePrefixToUtf8Bytes(const std::wstring& wide, int nWideChars)
{
    if (nWideChars <= 0)
        return 0;
    if (nWideChars >= static_cast<int>(wide.size()))
        return static_cast<int>(
            WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0,
                                nullptr, nullptr));
    return static_cast<int>(WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), nWideChars, nullptr, 0,
                                                nullptr, nullptr));
}

// RAII GDI font context — 对应 LO VCL OutputDevice 字体选入 DC
struct GdiFontContext
{
    HDC hdc = nullptr;
    HFONT hFont = nullptr;
    HFONT hOld = nullptr;

    GdiFontContext(const std::string& rFontName, int nFontSizeHalfPt)
    {
        hdc = CreateCompatibleDC(nullptr);
        if (!hdc)
            return;
        int nPixels = GdiHalfPtToLogPixels(nFontSizeHalfPt);
        int nWeight = GdiFontWeightFromName(rFontName);
        hFont = CreateFontA(-nPixels, 0, 0, 0, nWeight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, rFontName.c_str());
        if (hFont)
            hOld = static_cast<HFONT>(SelectObject(hdc, hFont));
    }

    ~GdiFontContext()
    {
        if (hdc)
        {
            if (hOld)
                SelectObject(hdc, hOld);
            if (hFont)
                DeleteObject(hFont);
            DeleteDC(hdc);
        }
    }

    bool IsValid() const { return hdc != nullptr && hFont != nullptr; }

    SwTwips MeasureTextWidthTwips(const std::wstring& wide, int nChars) const
    {
        if (!IsValid() || nChars <= 0)
            return 0;
        SIZE size{};
        if (!GetTextExtentPoint32W(hdc, wide.c_str(), nChars, &size))
            return 0;
        return GdiPixelsToTwips(size.cx);
    }

    int MeasureLineHeightTwips() const
    {
        if (!IsValid())
            return 0;
        TEXTMETRICA tm{};
        if (!GetTextMetricsA(hdc, &tm))
            return 0;
        int nH = tm.tmHeight;
        if (g_bAddExtLeading)
            nH += tm.tmExternalLeading;
        return GdiPixelsToTwips(nH);
    }
};
#endif
} // namespace

//===----------------------------------------------------------------------===//
// FontInstance 实现
//===----------------------------------------------------------------------===//

bool FontInstance::LoadFromFile(const std::string& path, int fontIndex)
{
    ClearHbFont();
    FILE* f = fopen(path.c_str(), "rb");
    if (!f)
        return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    m_data.resize(sz);
    fread(m_data.data(), 1, sz, f);
    fclose(f);
    m_info = new stbtt_fontinfo();
    if (!stbtt_InitFont(m_info, m_data.data(),
                        stbtt_GetFontOffsetForIndex(m_data.data(), fontIndex)))
    {
        delete m_info;
        m_info = nullptr;
        m_data.clear();
        m_valid = false;
        return false;
    }
    m_valid = true;
    return true;
}

FontInstance::~FontInstance()
{
    ClearHbFont();
    delete m_info;
}

void FontInstance::ClearHbFont()
{
    if (m_hbFont)
    {
        hb_font_destroy(m_hbFont);
        m_hbFont = nullptr;
    }
    if (m_hbFace)
    {
        hb_face_destroy(m_hbFace);
        m_hbFace = nullptr;
    }
    if (m_hbBlob)
    {
        hb_blob_destroy(m_hbBlob);
        m_hbBlob = nullptr;
    }
}

// 对应 LO LogicalFontInstance::GetHbFont()
hb_font_t* FontInstance::GetHbFont() const
{
    if (m_hbFont)
        return m_hbFont;
    if (m_data.empty())
        return nullptr;
    m_hbBlob = hb_blob_create(reinterpret_cast<const char*>(m_data.data()),
                              static_cast<unsigned int>(m_data.size()), HB_MEMORY_MODE_READONLY,
                              nullptr, nullptr);
    if (!m_hbBlob)
        return nullptr;
    unsigned int nFaceCount = hb_face_count(m_hbBlob);
    unsigned int nBestFaceIndex = 0, nBestUpem = 0;
    if (nFaceCount > 1)
    {
        for (unsigned int i = 0; i < nFaceCount; ++i)
        {
            hb_face_t* tmpFace = hb_face_create(m_hbBlob, i);
            unsigned int upem = hb_face_get_upem(tmpFace);
            if (upem > nBestUpem)
            {
                nBestUpem = upem;
                nBestFaceIndex = i;
            }
            hb_face_destroy(tmpFace);
        }
    }
    m_hbFace = hb_face_create(m_hbBlob, nBestFaceIndex);
    if (!m_hbFace)
        return nullptr;
    m_hbFont = hb_font_create(m_hbFace);
    if (!m_hbFont)
        return nullptr;
    return m_hbFont;
}

stbtt_fontinfo* FontInstance::GetInfo() const { return m_info; }

FontMetric FontInstance::GetMetric(float pixelHeight) const
{
    FontMetric metric;
    if (!m_valid || !m_info)
        return metric;

    // stb_truetype 度量
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(m_info, &ascent, &descent, &lineGap);

    float scale = stbtt_ScaleForMappingEmToPixels(m_info, pixelHeight);

    metric.ascent = static_cast<int>(ascent * scale);
    metric.descent = static_cast<int>(-descent * scale); // descent 是负值，转为正值
    metric.internalLeading = static_cast<int>(lineGap * scale);
    metric.lineHeight = metric.ascent + metric.descent;
    metric.height = static_cast<int>(pixelHeight);

    // 转换为 twips (1 pixel = 1440/96 twips at 96 DPI)
    // 但这里先返回像素值，调用方负责转换

    return metric;
}

SwTwips FontInstance::GetTextWidth(const std::string& text, int fontSizeHalfPt) const
{
    if (text.empty())
        return 0;

#ifdef _WIN32
    GdiFontContext ctx(m_fontName.empty() ? "Calibri" : m_fontName, fontSizeHalfPt);
    if (ctx.IsValid())
    {
        std::wstring wide = Utf8ToWide(text);
        if (!wide.empty())
            return ctx.MeasureTextWidthTwips(wide, static_cast<int>(wide.size()));
    }
#endif

    if (!m_valid)
        return 0;

    hb_font_t* hbFont = GetHbFont();
    if (!hbFont)
        return 0;
    float pixelHeight10 = static_cast<float>(fontSizeHalfPt) * 60.0f;
    hb_face_t* hbFace = hb_font_get_face(hbFont);
    unsigned int upem = hb_face_get_upem(hbFace);
    if (upem == 0)
        return 0;
    int x_scale = static_cast<int>((static_cast<long long>(pixelHeight10) * 65536) / upem);
    hb_font_set_scale(hbFont, x_scale, x_scale);
    hb_buffer_t* buf = hb_buffer_create();
    hb_buffer_add_utf8(buf, text.c_str(), static_cast<int>(text.size()), 0,
                       static_cast<int>(text.size()));
    hb_buffer_guess_segment_properties(buf);
    hb_shape(hbFont, buf, nullptr, 0);
    unsigned int glyphCount = 0;
    hb_glyph_position_t* glyphPositions = hb_buffer_get_glyph_positions(buf, &glyphCount);
    double totalAdvance10 = 0.0;
    for (unsigned int i = 0; i < glyphCount; ++i)
        totalAdvance10 += static_cast<double>(glyphPositions[i].x_advance) / 65536.0;
    hb_buffer_destroy(buf);
    return static_cast<SwTwips>(std::round(totalAdvance10 / 6.0));
}

SwTwips FontInstance::GetCharWidth(char c, int fontSizeHalfPt) const
{
    if (!m_valid)
        return 0;

#ifdef _WIN32
    GdiFontContext ctx(m_fontName.empty() ? "Calibri" : m_fontName, fontSizeHalfPt);
    if (ctx.IsValid())
    {
        wchar_t wc[2] = {static_cast<wchar_t>(static_cast<unsigned char>(c)), 0};
        return ctx.MeasureTextWidthTwips(wc, 1);
    }
#endif

    hb_font_t* hbFont = GetHbFont();
    if (!hbFont)
        return 0;
    float pixelHeight10 = static_cast<float>(fontSizeHalfPt) * 60.0f;
    hb_face_t* hbFace = hb_font_get_face(hbFont);
    unsigned int upem = hb_face_get_upem(hbFace);
    if (upem == 0)
        return 0;
    int x_scale = static_cast<int>((static_cast<long long>(pixelHeight10) * 65536) / upem);
    hb_font_set_scale(hbFont, x_scale, x_scale);
    hb_codepoint_t glyph = 0;
    if (!hb_font_get_glyph(hbFont, static_cast<hb_codepoint_t>(static_cast<unsigned char>(c)), 0,
                           &glyph))
        return 0;
    hb_position_t advance10 = hb_font_get_glyph_h_advance(hbFont, glyph);
    return static_cast<SwTwips>(std::round(static_cast<double>(advance10) / 65536.0 / 6.0));
}

int FontInstance::GetTextBreak(const std::string& text, int fontSizeHalfPt, SwTwips maxWidth) const
{
    if (text.empty())
        return 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        char ch = text[i];
        if (ch == '\n' || ch == '\f' || ch == '\v')
            return static_cast<int>(i) + 1;
    }

#ifdef _WIN32
    GdiFontContext ctx(m_fontName.empty() ? "Calibri" : m_fontName, fontSizeHalfPt);
    if (ctx.IsValid())
    {
        std::wstring wide = Utf8ToWide(text);
        const int wlen = static_cast<int>(wide.size());
        if (wlen == 0)
            return -1;

        int lo = 0, hi = wlen + 1;
        int nFitChars = 0;
        while (lo < hi)
        {
            int mid = (lo + hi) / 2;
            if (mid > wlen)
                mid = wlen;
            if (ctx.MeasureTextWidthTwips(wide, mid) <= maxWidth)
            {
                nFitChars = mid;
                lo = mid + 1;
            }
            else
            {
                hi = mid;
            }
        }

        if (nFitChars >= wlen)
            return -1;

        int nWordBreak = nFitChars;
        while (nWordBreak > 0 && wide[static_cast<size_t>(nWordBreak - 1)] != L' '
               && wide[static_cast<size_t>(nWordBreak - 1)] != L'\t')
            --nWordBreak;

        int nBreakChars = nWordBreak > 0 ? nWordBreak : (nFitChars > 0 ? nFitChars : 1);
        int nUtf8Break = WidePrefixToUtf8Bytes(wide, nBreakChars);
        return nUtf8Break > 0 ? nUtf8Break : 1;
    }
#endif

    hb_font_t* hbFont = GetHbFont();
    if (!hbFont)
        return -1;
    float pixelHeight10 = static_cast<float>(fontSizeHalfPt) * 60.0f;
    hb_face_t* hbFace = hb_font_get_face(hbFont);
    unsigned int upem = hb_face_get_upem(hbFace);
    if (upem == 0)
        return -1;
    int x_scale = static_cast<int>((static_cast<long long>(pixelHeight10) * 65536) / upem);
    hb_font_set_scale(hbFont, x_scale, x_scale);
    hb_buffer_t* buf = hb_buffer_create();
    hb_buffer_add_utf8(buf, text.c_str(), static_cast<int>(text.size()), 0,
                       static_cast<int>(text.size()));
    hb_buffer_guess_segment_properties(buf);
    hb_shape(hbFont, buf, nullptr, 0);
    unsigned int glyphCount = 0;
    hb_glyph_info_t* glyphInfos = hb_buffer_get_glyph_infos(buf, &glyphCount);
    hb_glyph_position_t* glyphPositions = hb_buffer_get_glyph_positions(buf, &glyphCount);
    double maxAdvance10 = static_cast<double>(maxWidth) * 6.0;
    double accumulated10 = 0.0;
    int lastBreakGlyph = -1;
    for (unsigned int i = 0; i < glyphCount; ++i)
    {
        double nextAdvance = static_cast<double>(glyphPositions[i].x_advance) / 65536.0;
        if (accumulated10 + nextAdvance > maxAdvance10 && accumulated10 > 0.0)
        {
            hb_buffer_destroy(buf);
            if (lastBreakGlyph > 0)
            {
                unsigned int breakCluster = glyphInfos[lastBreakGlyph].cluster;
                if (breakCluster > 0 && breakCluster < text.size())
                    return static_cast<int>(breakCluster);
            }
            if (i > 0)
            {
                unsigned int cluster = glyphInfos[i].cluster;
                return cluster > 0 ? static_cast<int>(cluster) : 1;
            }
            return 1;
        }
        if (glyphInfos[i].cluster < text.size())
        {
            char originalChar = text[glyphInfos[i].cluster];
            if (originalChar == ' ' || originalChar == '\t')
                lastBreakGlyph = static_cast<int>(i) + 1;
        }
        accumulated10 += nextAdvance;
    }
    hb_buffer_destroy(buf);
    return -1;
}

// 对应 LO DocumentSettingId::MS_WORD_COMP_GRID_METRICS
// DOCX 中 <w:useFELayout/> 触发此标志。aproj 暂默认 true（DOCX 常见设置）
// 后续 Task 可从 settings.xml 解析此标志。
void FontEngine::SetMsWordCompGridMetrics(bool bSet) { g_bMsWordCompGridMetrics = bSet; }
bool FontEngine::GetMsWordCompGridMetrics() { return g_bMsWordCompGridMetrics; }

// 对应 LO FontMetricData::ShouldUseWinMetrics (fontmetric.cxx:401-422)
// LO 检查配置列表 Office::Common::Misc::FontsUseWinMetrics，
// 若字体标识符（familyName,ascent,descent,typoAscent,typoDescent,winAscent,winDescent）
// 在列表中，则返回 true（使用 Win metrics）。
// 默认列表来自 officecfg/registry/schema/org/openoffice/Office/Common.xcs:4916-4931
// 包含: Celticmd, DIN Light, B Nazanin (Regular+Bold)
static bool ShouldUseWinMetrics(const std::string& rFamilyName, int nAscent, int nDescent,
                                int nTypoAscent, int nTypoDescent, int nWinAscent, int nWinDescent)
{
    // 构建字体标识符（与 LO ShouldUseWinMetrics 一致）
    // 格式: familyName,ascent,descent,typoAscent,typoDescent,winAscent,winDescent
    // 注意: LO 传入的 nAscent/nDescent 是 hhea 的值（含符号），不是 mnAscent/mnDescent
    char buf[512];
    snprintf(buf, sizeof(buf), "%s,%d,%d,%d,%d,%d,%d", rFamilyName.c_str(), nAscent, nDescent,
             nTypoAscent, nTypoDescent, nWinAscent, nWinDescent);
    std::string aFontIdentifier(buf);

    // LO 默认配置列表（officecfg/registry/schema/org/openoffice/Office/Common.xcs:4925-4930）
    static const std::vector<std::string> s_WinMetricFontList = {
        "Celticmd,1571,-567,1571,-547,2126,559", // tdf#148122
        "DIN Light,1509,-503,1509,-483,1997,483", // DIN Light (ttf version)
        "B Nazanin,1343,-705,1990,-1045,1990,1045", // tdf#155297 Regular
        "B Nazanin,1341,-707,2126,-1120,2126,1120", // tdf#155297 Bold
    };

    for (const auto& rEntry : s_WinMetricFontList)
    {
        if (rEntry == aFontIdentifier)
            return true;
    }
    return false;
}

// 对应 LO DocumentSettingId::ADD_EXT_LEADING
// DOCX 默认启用 ext leading（LO GetFontLeading 在 ADD_EXT_LEADING=true 时返回 m_nExtLeading）
void FontEngine::SetAddExtLeading(bool bSet) { g_bAddExtLeading = bSet; }

// 对应 LO lcl_ApplyCjkHeightAdjustment (fntcache.cxx:272-293)
// 检查 OS/2 表 ulCodePageRange1 的 CJK code page 位（CP932/CP936/CP949/CP950 = bit 17-20）
// 对应 VCL CodePageCoverage enum (fontcapabilities.hxx:169-172)
static bool IsCjkFont(hb_face_t* pFace)
{
    if (!pFace)
        return false;

    hb_blob_t* os2Blob = hb_face_reference_table(pFace, HB_TAG('O', 'S', '/', '2'));
    unsigned int length = hb_blob_get_length(os2Blob);
    // ulCodePageRange1 at offset 78, 需要 82 字节
    if (length < 82)
    {
        hb_blob_destroy(os2Blob);
        return false;
    }

    const unsigned char* os2Data
        = reinterpret_cast<const unsigned char*>(hb_blob_get_data(os2Blob, &length));
    bool bIsCjk = false;
    if (os2Data && length >= 82)
    {
        // ulCodePageRange1 是 big-endian uint32 at offset 78
        uint32_t ulCodePageRange1 = (static_cast<uint32_t>(os2Data[78]) << 24)
                                    | (static_cast<uint32_t>(os2Data[79]) << 16)
                                    | (static_cast<uint32_t>(os2Data[80]) << 8)
                                    | static_cast<uint32_t>(os2Data[81]);
        // CP932=bit17, CP936=bit18, CP949=bit19, CP950=bit20
        uint32_t nCjkMask = (1U << 17) | (1U << 18) | (1U << 19) | (1U << 20);
        bIsCjk = (ulCodePageRange1 & nCjkMask) != 0;
    }
    hb_blob_destroy(os2Blob);
    return bIsCjk;
}

int FontInstance::GetTextHeight(int fontSizeHalfPt) const
{
    if (!m_valid || !m_info)
        return 0;

    // LO 使用 DPI=8640（参考设备），1 twip = 8640/1440 = 6 device pixels
    // 字号换算: halfPt → twips: 1 halfPt = 0.5pt = 10 twips
    //           twips → device pixels: twips * 6
    // 故 mnHeight (device pixels) = halfPt * 10 * 6 = halfPt * 60
    // 对应 LO: fExactHeight = LogicHeightToDeviceSubPixel(nFontHeightTwips)
    //                       = nFontHeightTwips * (1/1440) * 8640 = nFontHeightTwips * 6
    float pixelHeight10 = static_cast<float>(fontSizeHalfPt) * 60.0f;

    // 使用 HarfBuzz 获取字体度量（与 LO 一致）
    // 参考 LibreOffice 的 vcl/source/font/fontmetric.cxx: ImplCalcLineSpacing
    // LO 流程（DPI=8640, 1/10 像素精度）：
    //   1. fScale = pixelHeight10 / emSize（1/10 像素缩放因子）
    //   2. pixelAscent10 = round(fontUnitAscent * fScale)（1/10 像素）
    //   3. pixelDescent10 = round(fontUnitDescent * fScale)
    //   4. pixelExtLeading10 = round(fontUnitExtLeading * fScale)
    //   5. twips = 1/10px / 6（1/10px → twips: 1440/8640 = 1/6）
    //   6. CJK 字体: prht * 127 / 100（MS_WORD_COMP_GRID_METRICS）
    //   7. ret = prht + ext（ADD_EXT_LEADING=true）
    //
    // 注意：LO 默认使用 hhea 度量（非 GDI Win 度量），故不优先 GDI。
    // GDI 的 tmAscent/tmDescent 来自 OS/2 Win metrics，与 LO 的 hhea 路径不一致。
    if (!m_data.empty())
    {
        // 创建 HarfBuzz blob 和 face
        hb_blob_t* blob = hb_blob_create(reinterpret_cast<const char*>(m_data.data()),
                                         static_cast<unsigned int>(m_data.size()),
                                         HB_MEMORY_MODE_READONLY, nullptr, nullptr);
        if (blob)
        {
            // TTC 文件可能包含多个字体（如 simsun.ttc 包含 SimSun 和 NSimSun）
            // 不同字体的 upem 可能不同，需要选择正确的字体
            // LO 通过 GDI 字体匹配选择正确字体，aproj 通过选择最大 upem 的字体来近似
            unsigned int nFaceCount = hb_face_count(blob);
            unsigned int nBestFaceIndex = 0;
            unsigned int nBestUpem = 0;
            if (nFaceCount > 1)
            {
                for (unsigned int i = 0; i < nFaceCount; ++i)
                {
                    hb_face_t* tmpFace = hb_face_create(blob, i);
                    unsigned int upem = hb_face_get_upem(tmpFace);
                    if (upem > nBestUpem)
                    {
                        nBestUpem = upem;
                        nBestFaceIndex = i;
                    }
                    hb_face_destroy(tmpFace);
                }
            }

            hb_face_t* face = hb_face_create(blob, nBestFaceIndex);
            hb_blob_destroy(blob);

            if (face)
            {
                hb_font_t* font = hb_font_create(face);
                hb_face_destroy(face);

                if (font)
                {
                    double fAscent = 0, fDescent = 0, fExtLeading = 0;
                    hb_position_t nWinAscent = 0, nWinDescent = 0;
                    hb_position_t nHheaLineGap = 0;
                    bool bHheaValid = false;

                    // 检查是否为可变字体（有 fvar 表）
                    hb_blob_t* fvar = hb_face_reference_table(hb_font_get_face(font),
                                                              HB_TAG('f', 'v', 'a', 'r'));
                    bool isVariable = hb_blob_get_length(fvar) > 0;
                    hb_blob_destroy(fvar);

                    if (isVariable)
                    {
                        // 可变字体：直接使用 HarfBuzz 的度量值（已包含 variation）
                        hb_position_t nAscent, nDescent, nLineGap;
                        if (hb_ot_metrics_get_position(font, HB_OT_METRICS_TAG_HORIZONTAL_ASCENDER,
                                                       &nAscent)
                            && hb_ot_metrics_get_position(
                                   font, HB_OT_METRICS_TAG_HORIZONTAL_DESCENDER, &nDescent)
                            && hb_ot_metrics_get_position(
                                   font, HB_OT_METRICS_TAG_HORIZONTAL_LINE_GAP, &nLineGap))
                        {
                            fAscent = nAscent;
                            fDescent = -nDescent;
                            fExtLeading = nLineGap;
                        }
                    }
                    else
                    {
                        // 非可变字体：按 LO 逻辑选择最佳度量值
                        // 1. 先用 hhea 表
                        hb_position_t nAscent = 0, nDescent = 0, nLineGap = 0;
                        constexpr auto ASCENT_HHEA
                            = static_cast<hb_ot_metrics_tag_t>(HB_TAG('H', 'a', 's', 'c'));
                        constexpr auto DESCENT_HHEA
                            = static_cast<hb_ot_metrics_tag_t>(HB_TAG('H', 'd', 's', 'c'));
                        constexpr auto LINEGAP_HHEA
                            = static_cast<hb_ot_metrics_tag_t>(HB_TAG('H', 'l', 'g', 'p'));

                        if (hb_ot_metrics_get_position(font, ASCENT_HHEA, &nAscent)
                            && hb_ot_metrics_get_position(font, DESCENT_HHEA, &nDescent)
                            && hb_ot_metrics_get_position(font, LINEGAP_HHEA, &nLineGap))
                        {
                            // tdf#107605: Some fonts have weird values, check ascender +ve
                            // and descender -ve
                            if (nAscent >= 0 && nDescent <= 0)
                            {
                                fAscent = nAscent;
                                fDescent = -nDescent;
                                fExtLeading = nLineGap;
                                nHheaLineGap = nLineGap;
                                bHheaValid = true;
                            }
                        }

                        // 2. 如果 OS/2 存在，优先使用
                        constexpr auto ASCENT_OS2
                            = static_cast<hb_ot_metrics_tag_t>(HB_TAG('O', 'a', 's', 'c'));
                        constexpr auto DESCENT_OS2
                            = static_cast<hb_ot_metrics_tag_t>(HB_TAG('O', 'd', 's', 'c'));
                        constexpr auto LINEGAP_OS2
                            = static_cast<hb_ot_metrics_tag_t>(HB_TAG('O', 'l', 'g', 'p'));

                        hb_position_t nTypoAscent, nTypoDescent, nTypoLineGap;
                        if (hb_ot_metrics_get_position(font, ASCENT_OS2, &nTypoAscent)
                            && hb_ot_metrics_get_position(font, DESCENT_OS2, &nTypoDescent)
                            && hb_ot_metrics_get_position(font, LINEGAP_OS2, &nTypoLineGap)
                            && hb_ot_metrics_get_position(
                                   font, HB_OT_METRICS_TAG_HORIZONTAL_CLIPPING_ASCENT, &nWinAscent)
                            && hb_ot_metrics_get_position(
                                   font, HB_OT_METRICS_TAG_HORIZONTAL_CLIPPING_DESCENT,
                                   &nWinDescent))
                        {
                            // 对应 LO ImplCalcLineSpacing (fontmetric.cxx:507-514):
                            // 如果 hhea 为空 OR ShouldUseWinMetrics 返回 true，使用 Win metrics
                            if (fAscent == 0.0 && fDescent == 0.0)
                            {
                                fAscent = nWinAscent;
                                fDescent = nWinDescent;
                                fExtLeading = 0;
                            }
                            else if (ShouldUseWinMetrics(m_fontName, static_cast<int>(nAscent),
                                                         static_cast<int>(nDescent),
                                                         static_cast<int>(nTypoAscent),
                                                         static_cast<int>(nTypoDescent),
                                                         static_cast<int>(nWinAscent),
                                                         static_cast<int>(nWinDescent)))
                            {
                                fAscent = nWinAscent;
                                fDescent = nWinDescent;
                                fExtLeading = 0;
                            }

                            // 检查 USE_TYPO_METRICS 标志
                            // 对应 LO ImplCalcLineSpacing (fontmetric.cxx:516-535):
                            // fsSelection bit 7 (USE_TYPO_METRICS) → 覆盖为 Typo metrics
                            bool bUseTypoMetrics = false;
                            int fsSelectionVal = 0;
                            {
                                // 读取 OS/2 表的 fsSelection 字段
                                hb_blob_t* os2Blob = hb_face_reference_table(
                                    hb_font_get_face(font), HB_TAG('O', 'S', '/', '2'));
                                if (hb_blob_get_length(os2Blob) >= 64)
                                {
                                    unsigned int length = 0;
                                    const unsigned char* os2Data
                                        = reinterpret_cast<const unsigned char*>(
                                            hb_blob_get_data(os2Blob, &length));
                                    if (os2Data && length >= 64)
                                    {
                                        fsSelectionVal = (os2Data[62] << 8) | os2Data[63];
                                        bUseTypoMetrics = (fsSelectionVal & (1 << 7)) != 0;
                                    }
                                }
                                hb_blob_destroy(os2Blob);
                            }

                            if (bUseTypoMetrics && nTypoAscent >= 0 && nTypoDescent <= 0)
                            {
                                fAscent = nTypoAscent;
                                fDescent = -nTypoDescent;
                                fExtLeading = nTypoLineGap;
                                // Calibri 等字体 USE_TYPO_METRICS 时 typo line gap 常为 0，
                                // LO FontMetric external leading 仍取 hhea line gap（479≈244+235）
                                if (fExtLeading == 0.0 && bHheaValid && nHheaLineGap > 0)
                                    fExtLeading = nHheaLineGap;
                            }
                        }
                    }

                    // CJK 字体检测（对应 LO lcl_ApplyCjkHeightAdjustment 的 FontCapabilities 检查）
                    hb_face_t* pHbFace = hb_font_get_face(font);
                    bool bIsCjk = IsCjkFont(pHbFace);

                    // 使用 HarfBuzz 的 unitsPerEm（正确处理 TTC 文件）
                    // stbtt 的 emSize 对 TTC 文件可能解析错误（如 simsun.ttc 得到 256 而非 1000）
                    unsigned int nHbUpem = hb_face_get_upem(pHbFace);

                    hb_font_destroy(font);

                    // 计算最终高度（twips）
                    // 与 LO 的 ImplCalcLineSpacing + GetFontHeight 完全一致：
                    //   LO 使用 DPI=8640（1/10 像素精度）:
                    //     1. fScale = pixelHeight10 / emSize（1/10 像素缩放因子）
                    //     2. pixelAscent10 = round(fontUnitAscent * fScale)（1/10 像素）
                    //     3. pixelDescent10 = round(fontUnitDescent * fScale)
                    //     4. pixelExtLeading10 = round(fontUnitExtLeading * fScale)
                    //     5. twips = 1/10px / 6（1/10px → twips: 1440/8640 = 1/6）
                    //   LO GetFontHeight:
                    //     6. prht = round((pixelAscent10 + pixelDescent10) / 6)（twips）
                    //     7. ext = round(pixelExtLeading10 / 6)（twips）
                    //     8. CJK 调整: prht = prht * 127 / 100（MS_WORD_COMP_GRID_METRICS）
                    //     9. ret = prht + ext（ADD_EXT_LEADING=true 时）
                    if (fAscent > 0 || fDescent > 0)
                    {
                        float emSize = static_cast<float>(nHbUpem);
                        double fScale = static_cast<double>(pixelHeight10) / emSize;
                        // 1/10 像素精度（匹配 LO DPI=8640）
                        int nPixelAscent10 = static_cast<int>(std::round(fAscent * fScale));
                        int nPixelDescent10 = static_cast<int>(std::round(fDescent * fScale));
                        int nPixelExtLeading10 = static_cast<int>(std::round(fExtLeading * fScale));

                        // 1/10 像素 → twips: twips = 1/10px / 6（带四舍五入）
                        // 对应 LO OutputDevice mapping: logical = device * 1440 / 8640
                        int nPrtHeight = static_cast<int>(std::round(
                            static_cast<double>(nPixelAscent10 + nPixelDescent10) / 6.0));
                        int nExtLeading = static_cast<int>(
                            std::round(static_cast<double>(nPixelExtLeading10) / 6.0));

                        // CJK 字体高度调整（对应 LO lcl_ApplyCjkHeightAdjustment: *127/100）
                        if (bIsCjk && g_bMsWordCompGridMetrics)
                        {
                            nPrtHeight = (nPrtHeight * 127) / 100;
                        }

                        // ADD_EXT_LEADING: DOCX 默认 true，ext leading 加入行高
                        int result = nPrtHeight;
                        if (g_bAddExtLeading)
                            result += nExtLeading;

                        return result;
                    }
                }
            }
        }
    }

    // 回退到 stb_truetype（与 LO 一致的 1/10 像素精度）
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(m_info, &ascent, &descent, &lineGap);
    float emSize = 1.0f / stbtt_ScaleForMappingEmToPixels(m_info, 1.0f);
    double fScale = static_cast<double>(pixelHeight10) / emSize;
    int nPixelAscent10 = static_cast<int>(std::round(ascent * fScale));
    int nPixelDescent10 = static_cast<int>(std::round(-descent * fScale));
    int nPixelLineGap10 = static_cast<int>(std::round(lineGap * fScale));
    int nPrtHeight
        = static_cast<int>(std::round(static_cast<double>(nPixelAscent10 + nPixelDescent10) / 6.0));
    int nExtLeading = static_cast<int>(std::round(static_cast<double>(nPixelLineGap10) / 6.0));
    int result = nPrtHeight;
    if (g_bAddExtLeading)
        result += nExtLeading;
    return result;
}

//===----------------------------------------------------------------------===//
// FontEngine 实现
//===----------------------------------------------------------------------===//

FontEngine& FontEngine::Instance()
{
    static FontEngine instance;
    return instance;
}

void FontEngine::InitPathCache()
{
    if (m_pathCacheInitialized)
        return;
    m_pathCacheInitialized = true;

    // 常见字体名 → 文件名映射
    // 对应 VCL 的 PhysicalFontCollection::FindFontFamily
    m_pathCache["Calibri"] = "calibri.ttf";
    m_pathCache["Calibri Bold"] = "calibrib.ttf";
    m_pathCache["Calibri Italic"] = "calibrii.ttf";
    m_pathCache["Calibri Bold Italic"] = "calibriz.ttf";
    m_pathCache["Segoe UI"] = "segoeui.ttf";
    m_pathCache["Segoe UI Bold"] = "segoeuib.ttf";
    m_pathCache["Segoe UI Semibold"] = "seguisb.ttf";
    m_pathCache["Segoe UI Light"] = "segoeuisl.ttf";
    m_pathCache["Segoe UI Emoji"] = "seguiemj.ttf";
    m_pathCache["Arial"] = "arial.ttf";
    m_pathCache["Arial Bold"] = "arialbd.ttf";
    m_pathCache["Times New Roman"] = "times.ttf";
    m_pathCache["Times New Roman Bold"] = "timesbd.ttf";
    m_pathCache["Courier New"] = "cour.ttf";
    m_pathCache["Courier New Bold"] = "courbd.ttf";
    m_pathCache["Verdana"] = "verdana.ttf";
    m_pathCache["Tahoma"] = "tahoma.ttf";
    m_pathCache["Georgia"] = "georgia.ttf";
    m_pathCache["Trebuchet MS"] = "trebuc.ttf";
    m_pathCache["Impact"] = "impact.ttf";

    // CJK 字体（对应 LO SwFont CJK 字体槽，w:eastAsia 解析）
    // Windows 系统字体文件名
    m_pathCache["宋体"] = "simsun.ttc";
    m_pathCache["SimSun"] = "simsun.ttc";
    m_pathCache["NSimSun"] = "simsun.ttc";
    m_pathCache["黑体"] = "simhei.ttf";
    m_pathCache["SimHei"] = "simhei.ttf";
    m_pathCache["微软雅黑"] = "msyh.ttc";
    m_pathCache["Microsoft YaHei"] = "msyh.ttc";
    m_pathCache["楷体"] = "simkai.ttf";
    m_pathCache["KaiTi"] = "simkai.ttf";
    m_pathCache["仿宋"] = "simfang.ttf";
    m_pathCache["FangSong"] = "simfang.ttf";
    m_pathCache["等线"] = "Deng.ttf";
    m_pathCache["DengXian"] = "Deng.ttf";

    // Poppins 字体（Google Fonts，可能不在系统目录）
    m_pathCache["Poppins"] = "Poppins-Regular.ttf";
    m_pathCache["Poppins Bold"] = "Poppins-Bold.ttf";
    m_pathCache["Poppins Medium"] = "Poppins-Medium.ttf";
    m_pathCache["Poppins SemiBold"] = "Poppins-SemiBold.ttf";
    m_pathCache["Poppins Light"] = "Poppins-Light.ttf";

    // LO 自带字体（Liberation 系列，metric-compatible 替代字体）
    // LO 在 Poppins/Arial 等字体缺失时回退到 Liberation Serif
    m_pathCache["Liberation Serif"] = "LiberationSerif-Regular.ttf";
    m_pathCache["Liberation Serif Bold"] = "LiberationSerif-Bold.ttf";
    m_pathCache["Liberation Serif Italic"] = "LiberationSerif-Italic.ttf";
    m_pathCache["Liberation Serif Bold Italic"] = "LiberationSerif-BoldItalic.ttf";
    m_pathCache["Liberation Sans"] = "LiberationSans-Regular.ttf";
    m_pathCache["Liberation Sans Bold"] = "LiberationSans-Bold.ttf";
    m_pathCache["DejaVu Sans"] = "DejaVuSans.ttf";

    // OOXML fontTable altName fallbacks (word/fontTable.xml)
    // LO 对缺失字体的回退策略（通过 LO debug 输出确认）:
    //   Poppins (sans-serif) → DejaVu Sans (sans-serif, LO instdir/share/fonts/truetype/)
    //   fony family → Liberation Serif (serif, 用于空段落 CJK slot)
    // 对应 LO 的 PhysicalFontCollection::FindFontFamily 替代逻辑
    // 注：ParseFontTable 会在解析时覆盖为文档 fontTable 中的 altName
    m_altNameCache["fony family"] = "Liberation Serif";
    m_altNameCache["Poppins"] = "Liberation Sans";
    m_altNameCache["Poppins Medium"] = "Liberation Sans";
    m_altNameCache["Poppins SemiBold"] = "Liberation Sans";
    m_altNameCache["Poppins Light"] = "Liberation Sans";
}

std::string FontEngine::ResolveFontPath(const std::string& fontName)
{
    InitPathCache();

    // LO 自带字体目录（Liberation/DejaVu 等 metric-compatible 替代字体）
    // 对应 LO instdir/share/fonts/truetype/
    static const char* sLoFontDirs[]
        = { "E:/lo/libo-core/instdir/share/fonts/truetype/",
            "C:/Program Files/LibreOffice/share/fonts/truetype/", nullptr };

    auto tryFontFile = [&](const std::string& file) -> std::string {
        // 先查 Windows 系统字体目录
        std::string sysPath = "C:/Windows/Fonts/" + file;
        FILE* fp = fopen(sysPath.c_str(), "rb");
        if (fp)
        {
            fclose(fp);
            return sysPath;
        }
        // 再查 LO 自带字体目录
        for (int i = 0; sLoFontDirs[i]; ++i)
        {
            std::string loPath = std::string(sLoFontDirs[i]) + file;
            fp = fopen(loPath.c_str(), "rb");
            if (fp)
            {
                fclose(fp);
                return loPath;
            }
        }
        return sysPath; // 返回系统路径（即使不存在，保持原行为）
    };

    // 精确匹配
    auto it = m_pathCache.find(fontName);
    if (it != m_pathCache.end())
        return tryFontFile(it->second);

    // 大小写不敏感匹配
    std::string lower = fontName;
    for (auto& c : lower)
        c = tolower(c);

    for (const auto & [ name, file ] : m_pathCache)
    {
        std::string nameLower = name;
        for (auto& c : nameLower)
            c = tolower(c);
        if (nameLower == lower)
            return tryFontFile(file);
    }

    // 部分匹配
    for (const auto & [ name, file ] : m_pathCache)
    {
        std::string nameLower = name;
        for (auto& c : nameLower)
            c = tolower(c);
        if (nameLower.find(lower) != std::string::npos
            || lower.find(nameLower) != std::string::npos)
            return tryFontFile(file);
    }

    // 默认返回 Calibri
    return "C:/Windows/Fonts/calibri.ttf";
}

FontInstance* FontEngine::GetFont(const std::string& fontName)
{
    auto it = m_cache.find(fontName);
    if (it != m_cache.end())
        return it->second.get();

    InitPathCache();

    std::string gdiName = fontName;
    auto altIt = m_altNameCache.find(fontName);
    if (altIt != m_altNameCache.end())
        gdiName = altIt->second;

    std::string path = ResolveFontPath(fontName);
    auto font = std::make_unique<FontInstance>();
    if (!font->LoadFromFile(path))
    {
        if (gdiName != fontName)
        {
            path = ResolveFontPath(gdiName);
            font->LoadFromFile(path);
        }
        if (!font->IsValid() && path.find("calibri") == std::string::npos)
            font->LoadFromFile("C:/Windows/Fonts/calibri.ttf");
    }
    // GDI 使用逻辑字体名（Windows font linking 解析 altName）
    font->SetFontName(fontName);
    FontInstance* ptr = font.get();
    m_cache[fontName] = std::move(font);
    return ptr;
}

SwTwips FontEngine::MeasureTextWidth(const std::string& fontName, int fontSizeHalfPt,
                                     const std::string& text)
{
    FontInstance* font = GetFont(fontName);
    if (!font)
        return -1;
    return font->GetTextWidth(text, fontSizeHalfPt);
}

int FontEngine::MeasureTextHeight(const std::string& fontName, int fontSizeHalfPt)
{
    InitPathCache();
    int nHeight = 0;
    FontInstance* font = GetFont(fontName);
    if (font)
        nHeight = font->GetTextHeight(fontSizeHalfPt);

    auto altIt = m_altNameCache.find(fontName);
    if (altIt != m_altNameCache.end() && altIt->second != fontName)
    {
        FontInstance* altFont = GetFont(altIt->second);
        if (altFont)
        {
            int nAlt = altFont->GetTextHeight(fontSizeHalfPt);
            if (nAlt > nHeight)
                nHeight = nAlt;
        }
    }
    return nHeight;
}

bool FontEngine::HasAltName(const std::string& fontName)
{
    InitPathCache();
    return m_altNameCache.find(fontName) != m_altNameCache.end();
}

void FontEngine::RegisterAltName(const std::string& fontName, const std::string& altName)
{
    if (fontName.empty() || altName.empty())
        return;
    InitPathCache();
    m_altNameCache[fontName] = altName;
    m_cache.erase(fontName);
}

int FontEngine::FindLineBreak(const std::string& fontName, int fontSizeHalfPt,
                              const std::string& text, SwTwips maxWidth)
{
    FontInstance* font = GetFont(fontName);
    if (!font)
        return -1;
    return font->GetTextBreak(text, fontSizeHalfPt, maxWidth);
}
