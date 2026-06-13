// 字体引擎实现
// 使用 stb_truetype 进行精确字形宽度测量
// 对应 LibreOffice 的 VCL 字体子系统 + sw 的 SwFntObj

#include "font_engine.h"

// stb_truetype 实现（只在一个 .cpp 中定义）
#define STB_TRUETYPE_IMPLEMENTATION
#include "../../third_party/stb_truetype.h"

#include <iostream>
#include <cstdio>
#include <cmath>

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

    // 尝试从 OS/2 表获取 fsSelection 和度量值
    // LO 的逻辑 (vcl/source/font/fontmetric.cxx):
    // 1. 先用 hhea 表 (ascent, descent, lineGap)
    // 2. 如果 OS/2 存在且 fsSelection bit 7 (USE_TYPO_METRICS) 设置，用 sTypo 值
    // 3. 否则可能用 usWinAscent/usWinDescent
    int os2Ascent = 0, os2Descent = 0, os2LineGap = 0;
    int winAscent = 0, winDescent = 0;
    bool hasOS2 = false;
    bool useTypoMetrics = false;

    if (!m_data.empty() && m_info)
    {
        int fontOffset = 0;
        if (m_data.size() >= 4 && m_data[0] == 't' && m_data[1] == 't' && m_data[2] == 'c'
            && m_data[3] == 'f')
        {
            fontOffset = stbtt_GetFontOffsetForIndex(m_data.data(), 0);
        }
        if (fontOffset >= 0 && fontOffset + 12 <= (int)m_data.size())
        {
            const unsigned char* font = m_data.data() + fontOffset;
            int numTables = (font[4] << 8) | font[5];
            for (int i = 0; i < numTables; i++)
            {
                int entryOffset = fontOffset + 12 + i * 16;
                if (entryOffset + 16 > (int)m_data.size())
                    break;
                const unsigned char* entry = m_data.data() + entryOffset;
                if (entry[0] == 'O' && entry[1] == 'S' && entry[2] == '/' && entry[3] == '2')
                {
                    int tableOffset
                        = (entry[8] << 24) | (entry[9] << 16) | (entry[10] << 8) | entry[11];
                    int tableLength
                        = (entry[12] << 24) | (entry[13] << 16) | (entry[14] << 8) | entry[15];
                    int absTableOffset = fontOffset + tableOffset;
                    if (tableLength >= 74 && absTableOffset + 74 <= (int)m_data.size())
                    {
                        const unsigned char* os2 = m_data.data() + absTableOffset;
                        // fsSelection at offset 62 (version 0+)
                        int fsSelection = (os2[62] << 8) | os2[63];
                        useTypoMetrics = (fsSelection & (1 << 7)) != 0;
                        // usWinAscent/usWinDescent at offset 64/66
                        winAscent = (os2[64] << 8) | os2[65];
                        winDescent = (os2[66] << 8) | os2[67];
                        // sTypoAscender/sTypoDescender/sTypoLineGap at offset 68/70/72
                        os2Ascent = (short)((os2[68] << 8) | os2[69]);
                        os2Descent = (short)((os2[70] << 8) | os2[71]);
                        os2LineGap = (short)((os2[72] << 8) | os2[73]);
                        hasOS2 = true;
                    }
                    break;
                }
            }
        }
    }

    if (hasOS2 && useTypoMetrics && os2Ascent >= 0 && os2Descent <= 0)
    {
        // 使用 OS/2 sTypo 值 (LO 的 bUseTypoMetrics 路径)
        float emSize = 1.0f / stbtt_ScaleForMappingEmToPixels(m_info, 1.0f);
        int totalFontUnits = os2Ascent + abs(os2Descent) + os2LineGap;
        int result = static_cast<int>(fontSizeTwips * totalFontUnits / emSize);
        fprintf(
            stderr,
            "[FontEngine] USE_TYPO: font=%s halfPt=%d typo=%d/%d/%d total=%d em=%.0f result=%d\n",
            m_fontName.c_str(), fontSizeHalfPt, os2Ascent, os2Descent, os2LineGap, totalFontUnits,
            emSize, result);
        return result;
    }

    // 默认使用 hhea 度量 (与 LO 一致)
    // LO 还会添加 external leading (GetFontLeading)
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(m_info, &ascent, &descent, &lineGap);
    float scale = stbtt_ScaleForMappingEmToPixels(m_info, fontSizeTwips);
    int baseHeight = static_cast<int>((ascent - descent + lineGap) * scale);

    // 尝试通过 Windows GDI 获取 external leading
    int extLeading = 0;
#ifdef _WIN32
    if (!m_fontName.empty())
    {
        HDC hdc = CreateCompatibleDC(NULL);
        if (hdc)
        {
            float pixels = static_cast<float>(fontSizeHalfPt) * 2.0f / 3.0f;
            int nHeight = -static_cast<int>(pixels * 72.0 / 96.0 + 0.5);
            HFONT hFont
                = CreateFontA(nHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH | FF_DONTCARE, m_fontName.c_str());
            if (hFont)
            {
                HFONT hOld = (HFONT)SelectObject(hdc, hFont);
                TEXTMETRIC tm = {};
                GetTextMetrics(hdc, &tm);
                extLeading = tm.tmExternalLeading;
                SelectObject(hdc, hOld);
                DeleteObject(hFont);
            }
            DeleteDC(hdc);
        }
    }
#endif
    return baseHeight + extLeading;
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
