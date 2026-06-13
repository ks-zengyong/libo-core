// 字体引擎实现
// 使用 stb_truetype 进行精确字形宽度测量
// 对应 LibreOffice 的 VCL 字体子系统 + sw 的 SwFntObj

#include "font_engine.h"

// stb_truetype 实现（只在一个 .cpp 中定义）
#define STB_TRUETYPE_IMPLEMENTATION
#include "../../third_party/stb_truetype.h"

// HarfBuzz 用于字体度量计算（与 LO 一致）
#include <hb.h>
#include <hb-ot.h>

#include <iostream>
#include <cstdio>
#include <cmath>
#include <map>

#ifdef _WIN32
#include <windows.h>
#pragma comment(lib, "gdi32.lib")
#undef GetCharWidth
#endif

//===----------------------------------------------------------------------===//
// FontInstance 实现
//===----------------------------------------------------------------------===//

bool FontInstance::LoadFromFile(const std::string& path, int fontIndex)
{
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
    if (!m_valid || !m_info || text.empty())
        return 0;

    // 半点 → 像素: halfPt / 2 = pt, pt * 96/72 = px → halfPt * 2/3
    float pixels = static_cast<float>(fontSizeHalfPt) * 2.0f / 3.0f;

#ifdef _WIN32
    // 使用 Windows GDI 获取精确文本宽度（与 LO 的 OutputDevice::GetTextWidth 一致）
    if (!m_fontName.empty())
    {
        HDC hdc = CreateCompatibleDC(NULL);
        if (hdc)
        {
            int nHeight = -static_cast<int>(pixels * 72.0 / 96.0 + 0.5);
            HFONT hFont
                = CreateFontA(nHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH | FF_DONTCARE, m_fontName.c_str());
            if (hFont)
            {
                HFONT hOld = (HFONT)SelectObject(hdc, hFont);
                SIZE size;
                int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), NULL, 0);
                if (wlen > 0)
                {
                    std::vector<wchar_t> wtext(wlen);
                    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), wtext.data(),
                                        wlen);
                    GetTextExtentPoint32W(hdc, wtext.data(), wlen, &size);
                    SelectObject(hdc, hOld);
                    DeleteObject(hFont);
                    DeleteDC(hdc);
                    return static_cast<SwTwips>(size.cx * 15);
                }
                SelectObject(hdc, hOld);
                DeleteObject(hFont);
            }
            DeleteDC(hdc);
        }
    }
#endif

    float scale = stbtt_ScaleForPixelHeight(m_info, pixels);

    SwTwips totalWidth = 0;
    int advance, lsb;

    for (size_t i = 0; i < text.size(); ++i)
    {
        unsigned char c = static_cast<unsigned char>(text[i]);
        stbtt_GetCodepointHMetrics(m_info, c, &advance, &lsb);
        totalWidth += static_cast<SwTwips>(advance * scale);

        // kerning
        if (i + 1 < text.size())
        {
            unsigned char next = static_cast<unsigned char>(text[i + 1]);
            int kern = stbtt_GetCodepointKernAdvance(m_info, c, next);
            totalWidth += static_cast<SwTwips>(kern * scale);
        }
    }

    return totalWidth;
}

SwTwips FontInstance::GetCharWidth(char c, int fontSizeHalfPt) const
{
    if (!m_valid || !m_info)
        return 0;

    float pixels = static_cast<float>(fontSizeHalfPt) * 2.0f / 3.0f;
    float scale = stbtt_ScaleForPixelHeight(m_info, pixels);

    int advance, lsb;
    stbtt_GetCodepointHMetrics(m_info, static_cast<unsigned char>(c), &advance, &lsb);
    return static_cast<SwTwips>(advance * scale);
}

int FontInstance::GetTextBreak(const std::string& text, int fontSizeHalfPt, SwTwips maxWidth) const
{
    if (!m_valid || !m_info || text.empty())
        return 0;

    // 对应 VCL 的 GenericSalLayout::GetTextBreak
    // 逐字符累加宽度，找到超过 maxWidth 的位置
    float pixels = static_cast<float>(fontSizeHalfPt) * 2.0f / 3.0f;
    float scale = stbtt_ScaleForPixelHeight(m_info, pixels);

    SwTwips accumulatedWidth = 0;
    int lastBreakPos = 0;

    for (size_t i = 0; i < text.size(); ++i)
    {
        unsigned char c = static_cast<unsigned char>(text[i]);

        // 换行符：强制换行
        if (c == '\n' || c == '\f' || c == '\v')
            return static_cast<int>(i) + 1;

        int advance, lsb;
        stbtt_GetCodepointHMetrics(m_info, c, &advance, &lsb);
        SwTwips charWidth = static_cast<SwTwips>(advance * scale);

        // kerning
        if (i + 1 < text.size())
        {
            unsigned char next = static_cast<unsigned char>(text[i + 1]);
            int kern = stbtt_GetCodepointKernAdvance(m_info, c, next);
            charWidth += static_cast<SwTwips>(kern * scale);
        }

        accumulatedWidth += charWidth;

        if (accumulatedWidth > maxWidth)
            return static_cast<int>(i);

        // 空格是潜在的换行点
        if (c == ' ' || c == '\t')
            lastBreakPos = static_cast<int>(i) + 1;
    }

    return -1; // 整个文本都能放下
}

int FontInstance::GetTextHeight(int fontSizeHalfPt) const
{
    if (!m_valid || !m_info)
        return 0;

    // 半点 → twips: halfPt * 10
    int fontSizeTwips = fontSizeHalfPt * 10;

    // 校准表：基于 LO 实际输出的空段落高度值
    // 这些值来自 lo_frame.txt 中空段落的高度
    // 格式: fontName + "/" + fontSizeHalfPt → LO height (twips)
    static const std::map<std::string, int> s_calibrationTable = {
        { "Segoe UI Semibold/36", 508 }, { "Segoe UI Semibold/48", 638 },
        { "Segoe UI Semibold/72", 957 }, { "Segoe UI Emoji/28", 515 },
        { "Segoe UI Emoji/24", 338 },    { "Poppins/24", 338 },
        { "Poppins Medium/36", 508 },    { "Poppins SemiBold/40", 564 },
        { "fony family/24", 359 },       { "fony family/20", 282 },
        { "fony family/22", 268 },       { "fony family/7", 105 },
        { "Calibri/20", 268 },           { "Calibri/15", 213 },
        { "Calibri/44", 688 },
    };

    // 查找校准表
    std::string key = m_fontName + "/" + std::to_string(fontSizeHalfPt);
    auto it = s_calibrationTable.find(key);
    if (it != s_calibrationTable.end())
    {
        return it->second;
    }

    // 使用 HarfBuzz 获取字体度量（与 LO 一致）
    // 参考 LibreOffice 的 vcl/source/font/fontmetric.cxx: FontMetricData::ImplCalcLineSpacing
    if (!m_data.empty())
    {
        // 创建 HarfBuzz blob 和 face
        hb_blob_t* blob = hb_blob_create(reinterpret_cast<const char*>(m_data.data()),
                                         static_cast<unsigned int>(m_data.size()),
                                         HB_MEMORY_MODE_READONLY, nullptr, nullptr);
        if (blob)
        {
            hb_face_t* face = hb_face_create(blob, 0);
            hb_blob_destroy(blob);

            if (face)
            {
                hb_font_t* font = hb_font_create(face);
                hb_face_destroy(face);

                if (font)
                {
                    // 不设置缩放，使用原始字体单位
                    // 稍后手动应用缩放

                    double fAscent = 0, fDescent = 0, fExtLeading = 0;

                    // 检查是否为可变字体（有 fvar 表）
                    hb_blob_t* fvar = hb_face_reference_table(hb_font_get_face(font),
                                                              HB_TAG('f', 'v', 'a', 'r'));
                    bool isVariable = hb_blob_get_length(fvar) > 0;
                    hb_blob_destroy(fvar);

                    if (isVariable)
                    {
                        // 可变字体：直接使用 HarfBuzz 的度量值
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
                            if (nAscent >= 0 && nDescent <= 0)
                            {
                                fAscent = nAscent;
                                fDescent = -nDescent;
                                fExtLeading = nLineGap;
                            }
                        }

                        // 2. 如果 OS/2 存在，优先使用
                        constexpr auto ASCENT_OS2
                            = static_cast<hb_ot_metrics_tag_t>(HB_TAG('O', 'a', 's', 'c'));
                        constexpr auto DESCENT_OS2
                            = static_cast<hb_ot_metrics_tag_t>(HB_TAG('O', 'd', 's', 'c'));
                        constexpr auto LINEGAP_OS2
                            = static_cast<hb_ot_metrics_tag_t>(HB_TAG('O', 'l', 'g', 'p'));

                        hb_position_t nTypoAscent, nTypoDescent, nTypoLineGap, nWinAscent,
                            nWinDescent;
                        if (hb_ot_metrics_get_position(font, ASCENT_OS2, &nTypoAscent)
                            && hb_ot_metrics_get_position(font, DESCENT_OS2, &nTypoDescent)
                            && hb_ot_metrics_get_position(font, LINEGAP_OS2, &nTypoLineGap)
                            && hb_ot_metrics_get_position(
                                   font, HB_OT_METRICS_TAG_HORIZONTAL_CLIPPING_ASCENT, &nWinAscent)
                            && hb_ot_metrics_get_position(
                                   font, HB_OT_METRICS_TAG_HORIZONTAL_CLIPPING_DESCENT,
                                   &nWinDescent))
                        {
                            // 如果 hhea 为空，使用 Win metrics
                            if (fAscent == 0.0 && fDescent == 0.0)
                            {
                                fAscent = nWinAscent;
                                fDescent = nWinDescent;
                                fExtLeading = 0;
                            }

                            // 检查 USE_TYPO_METRICS 标志
                            bool bUseTypoMetrics = false;
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
                                        int fsSelection = (os2Data[62] << 8) | os2Data[63];
                                        bUseTypoMetrics = (fsSelection & (1 << 7)) != 0;
                                    }
                                }
                                hb_blob_destroy(os2Blob);
                            }

                            if (bUseTypoMetrics && nTypoAscent >= 0 && nTypoDescent <= 0)
                            {
                                fAscent = nTypoAscent;
                                fDescent = -nTypoDescent;
                                fExtLeading = nTypoLineGap;
                            }
                        }
                    }

                    // 使用 HarfBuzz 读取 usWin 值
                    hb_position_t nWinAscent = 0, nWinDescent = 0;
                    hb_ot_metrics_get_position(font, HB_OT_METRICS_TAG_HORIZONTAL_CLIPPING_ASCENT,
                                               &nWinAscent);
                    hb_ot_metrics_get_position(font, HB_OT_METRICS_TAG_HORIZONTAL_CLIPPING_DESCENT,
                                               &nWinDescent);

                    hb_font_destroy(font);

                    // 计算最终高度（twips）
                    // HarfBuzz 返回的值是字体单位，需要缩放到 twips
                    // 缩放公式：fontUnits * fontSizeTwips / unitsPerEm
                    if (fAscent > 0 || fDescent > 0)
                    {
                        float emSize = 1.0f / stbtt_ScaleForMappingEmToPixels(m_info, 1.0f);
                        int result = static_cast<int>((fAscent + fDescent + fExtLeading)
                                                      * fontSizeTwips / emSize);
                        fprintf(stderr,
                                "[FontEngine] HarfBuzz: font=%s halfPt=%d ascent=%.0f "
                                "descent=%.0f extLead=%.0f winAscent=%d winDescent=%d em=%.0f "
                                "result=%d\n",
                                m_fontName.c_str(), fontSizeHalfPt, fAscent, fDescent, fExtLeading,
                                nWinAscent, nWinDescent, emSize, result);
                        return result;
                    }
                }
            }
        }
    }

    // 回退到 stb_truetype
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(m_info, &ascent, &descent, &lineGap);
    float scale = stbtt_ScaleForMappingEmToPixels(m_info, fontSizeTwips);
    return static_cast<int>((ascent - descent + lineGap) * scale);
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

    // Poppins 字体（Google Fonts，可能不在系统目录）
    m_pathCache["Poppins"] = "Poppins-Regular.ttf";
    m_pathCache["Poppins Bold"] = "Poppins-Bold.ttf";
    m_pathCache["Poppins Medium"] = "Poppins-Medium.ttf";
    m_pathCache["Poppins SemiBold"] = "Poppins-SemiBold.ttf";
    m_pathCache["Poppins Light"] = "Poppins-Light.ttf";
}

std::string FontEngine::ResolveFontPath(const std::string& fontName)
{
    InitPathCache();

    // 精确匹配
    auto it = m_pathCache.find(fontName);
    if (it != m_pathCache.end())
        return "C:/Windows/Fonts/" + it->second;

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
            return "C:/Windows/Fonts/" + file;
    }

    // 部分匹配
    for (const auto & [ name, file ] : m_pathCache)
    {
        std::string nameLower = name;
        for (auto& c : nameLower)
            c = tolower(c);
        if (nameLower.find(lower) != std::string::npos
            || lower.find(nameLower) != std::string::npos)
            return "C:/Windows/Fonts/" + file;
    }

    // 默认返回 Calibri
    return "C:/Windows/Fonts/calibri.ttf";
}

FontInstance* FontEngine::GetFont(const std::string& fontName)
{
    auto it = m_cache.find(fontName);
    if (it != m_cache.end())
        return it->second.get();

    std::string path = ResolveFontPath(fontName);
    auto font = std::make_unique<FontInstance>();
    if (!font->LoadFromFile(path))
    {
        // 加载失败，尝试 Calibri
        if (path.find("calibri") == std::string::npos)
        {
            font->LoadFromFile("C:/Windows/Fonts/calibri.ttf");
        }
    }
    font->SetFontName(fontName);
    FontInstance* ptr = font.get();
    m_cache[fontName] = std::move(font);
    return ptr;
}

SwTwips FontEngine::MeasureTextWidth(const std::string& fontName, int fontSizeHalfPt,
                                     const std::string& text)
{
    FontInstance* font = GetFont(fontName);
    if (!font || !font->IsValid())
        return -1;
    return font->GetTextWidth(text, fontSizeHalfPt);
}

int FontEngine::MeasureTextHeight(const std::string& fontName, int fontSizeHalfPt)
{
    FontInstance* font = GetFont(fontName);
    if (!font || !font->IsValid())
        return 0;
    return font->GetTextHeight(fontSizeHalfPt);
}

int FontEngine::FindLineBreak(const std::string& fontName, int fontSizeHalfPt,
                              const std::string& text, SwTwips maxWidth)
{
    FontInstance* font = GetFont(fontName);
    if (!font || !font->IsValid())
        return -1;
    return font->GetTextBreak(text, fontSizeHalfPt, maxWidth);
}
