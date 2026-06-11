#include "font_engine.h"

// stb_truetype implementation is included via main.cpp/test_main.cpp
// We just need the declarations here.
#include "stb_truetype.h"

#include <fstream>
#include <iostream>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <set>

#ifdef _WIN32
#include <windows.h>
#endif

namespace docx
{
// ── Font name → file mapping ───────────────────────────────────
std::string FontEngine::fontNameToFile(const std::string& name)
{
    // Map common DOCX font names to Windows font files
    static const std::map<std::string, std::string> mapping = {
        { "Calibri", "calibri.ttf" },      { "Calibri Light", "calibril.ttf" },
        { "Cambria", "cambria.ttc" },      { "Candara", "candara.ttf" },
        { "Comic Sans MS", "comic.ttf" },  { "Consolas", "consola.ttf" },
        { "Constantia", "constan.ttf" },   { "Corbel", "corbel.ttf" },
        { "Courier New", "cour.ttf" },     { "Georgia", "georgia.ttf" },
        { "Lucida Console", "lucon.ttf" }, { "Segoe UI", "segoeui.ttf" },
        { "Tahoma", "tahoma.ttf" },        { "Times New Roman", "times.ttf" },
        { "Trebuchet MS", "trebuc.ttf" },  { "Verdana", "verdana.ttf" },
        { "Wingdings", "wingding.ttf" },   { "Arial", "arial.ttf" },
        { "Arial Black", "ariblk.ttf" },   { "Impact", "impact.ttf" },
    };

    // Exact match
    auto it = mapping.find(name);
    if (it != mapping.end())
        return it->second;

    // Case-insensitive search
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    for (auto & [ k, v ] : mapping)
    {
        std::string kl = k;
        std::transform(kl.begin(), kl.end(), kl.begin(), ::tolower);
        if (kl == lower)
            return v;
    }

    // Fallback: try name + ".ttf"
    std::string fallback = name;
    std::transform(fallback.begin(), fallback.end(), fallback.begin(), ::tolower);
    fallback.erase(std::remove(fallback.begin(), fallback.end(), ' '), fallback.end());
    return fallback + ".ttf";
}

// Substitute unknown/placeholder font names with reasonable defaults
std::string FontEngine::substituteFont(const std::string& name)
{
    // Common placeholder/invalid font names found in documents
    static const std::map<std::string, std::string> substitutions = {
        { "fony family", "Calibri" },
        { "fony", "Calibri" },
        { "unknown", "Calibri" },
        { "", "Calibri" },
    };

    auto it = substitutions.find(name);
    if (it != substitutions.end())
        return it->second;

    // Check if the font name looks invalid (contains spaces that don't match known fonts)
    // Known fonts with spaces
    static const std::set<std::string> knownWithSpaces = {
        "Arial Black",
        "Calibri Light",
        "Comic Sans MS",
        "Courier New",
        "Lucida Console",
        "Segoe UI",
        "Segoe UI Semibold",
        "Segoe UI Emoji",
        "Segoe UI Light",
        "Times New Roman",
        "Trebuchet MS",
        "MS Gothic",
        "Wingdings",
        "Cambria Math",
        "Cambria",
        "Georgia",
        "Impact",
        "Corbel",
        "Candara",
        "Consolas",
        "Constantia",
        "Corbel",
        "Palatino Linotype",
        "Book Antiqua",
        "Century Gothic",
        "Franklin Gothic Medium",
        "Gill Sans",
        "Goudy Old Style",
    };

    if (knownWithSpaces.count(name))
        return name;

    // If the font name contains spaces and isn't known, it might be invalid
    // Return Calibri as default
    if (name.find(' ') != std::string::npos)
    {
        return "Calibri";
    }

    return name;
}

// ── Constructor / Destructor ───────────────────────────────────
FontEngine::FontEngine()
{
#ifdef _WIN32
    char* buf = nullptr;
    size_t len = 0;
    if (_dupenv_s(&buf, &len, "WINDIR") == 0 && buf)
    {
        fontDir_ = std::string(buf) + "\\Fonts\\";
        free(buf);
    }
    else
    {
        fontDir_ = "C:\\Windows\\Fonts\\";
    }
#else
    fontDir_ = "/usr/share/fonts/truetype/";
#endif
}

FontEngine::~FontEngine()
{
    for (auto & [ name, info ] : fonts_)
    {
        delete info.info;
    }
}

// ── Font loading ───────────────────────────────────────────────
FontInfo* FontEngine::loadFontFile(const std::string& path, const std::string& familyName)
{
    // Check if already loaded
    auto it = fonts_.find(familyName);
    if (it != fonts_.end() && it->second.info)
        return &it->second;

    // Read file
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return nullptr;

    size_t size = file.tellg();
    file.seekg(0);
    std::vector<uint8_t> data(size);
    file.read(reinterpret_cast<char*>(data.data()), size);
    file.close();

    // Initialize stb_truetype
    auto* stbinfo = new stbtt_fontinfo;
    if (!stbtt_InitFont(stbinfo, data.data(), stbtt_GetFontOffsetForIndex(data.data(), 0)))
    {
        delete stbinfo;
        return nullptr;
    }

    FontInfo info;
    info.familyName = familyName;
    info.data = std::move(data);
    info.info = stbinfo;
    info.scale = stbtt_ScaleForMappingEmToPixels(stbinfo, 1.0f);

    // Get vertical metrics
    stbtt_GetFontVMetrics(stbinfo, &info.ascent, &info.descent, &info.lineGap);

    fonts_[familyName] = std::move(info);
    return &fonts_[familyName];
}

bool FontEngine::loadFont(const std::string& familyName)
{
    if (fonts_.count(familyName) && fonts_[familyName].info)
        return true;

    // Substitute unknown font names
    std::string actualName = substituteFont(familyName);
    if (actualName != familyName)
    {
        // Try loading the substitute font first
        if (loadFont(actualName))
        {
            // Copy the substitute font info under the original name
            fonts_[familyName] = fonts_[actualName];
            fonts_[familyName].familyName = familyName;
            return true;
        }
    }

    std::string fileName = fontNameToFile(familyName);
    std::string path = fontDir_ + fileName;

    FontInfo* f = loadFontFile(path, familyName);
    if (f)
        return true;

    // Try fallback: Arial
    if (familyName != "Arial")
    {
        f = loadFontFile(fontDir_ + "arial.ttf", familyName);
        if (f)
            return true;
    }

    // Try fallback: Times New Roman
    if (familyName != "Times New Roman")
    {
        f = loadFontFile(fontDir_ + "times.ttf", familyName);
        if (f)
            return true;
    }

    return false;
}

FontInfo* FontEngine::getFont(const std::string& familyName)
{
    auto it = fonts_.find(familyName);
    if (it != fonts_.end() && it->second.info)
        return &it->second;
    if (loadFont(familyName))
        return &fonts_[familyName];
    return nullptr;
}

// ── Metrics ────────────────────────────────────────────────────
float FontEngine::halfPtToPixels(int halfPt)
{
    // halfPt is in half-points (e.g., 22 = 11pt)
    // At 96 DPI: pixels = points * 96/72 = points * 4/3
    float points = halfPt / 2.0f;
    return points * 96.0f / 72.0f;
}

float FontEngine::getStringWidth(const std::string& text, const std::string& fontName,
                                 int fontSizeHalfPt)
{
    FontInfo* font = getFont(fontName);
    if (!font || text.empty())
        return 0;

    float scale = stbtt_ScaleForMappingEmToPixels(font->info, halfPtToPixels(fontSizeHalfPt));
    float width = 0;

    const char* p = text.c_str();
    char32_t prev = 0;
    while (*p)
    {
        char32_t ch = nextUTF8(p);
        int advance, lsb;
        stbtt_GetGlyphHMetrics(font->info, stbtt_FindGlyphIndex(font->info, ch), &advance, &lsb);
        width += advance * scale;

        if (prev)
        {
            int kern = stbtt_GetCodepointKernAdvance(font->info, prev, ch);
            width += kern * scale;
        }
        prev = ch;
    }

    return width;
}

float FontEngine::getLineHeight(const std::string& fontName, int fontSizeHalfPt)
{
    FontInfo* font = getFont(fontName);
    if (!font)
        return halfPtToPixels(fontSizeHalfPt) * 1.2f;

    float pxHeight = halfPtToPixels(fontSizeHalfPt);
    float scale = stbtt_ScaleForMappingEmToPixels(font->info, pxHeight);
    float lineHeight = (font->ascent - font->descent + font->lineGap) * scale;

    // Debug logging (writes to stderr, read with 2>&1)
    static bool logged = false;
    if (!logged)
    {
        std::cerr << "FONT_DEBUG: name=" << fontName << " halfPt=" << fontSizeHalfPt
                  << " pxHeight=" << pxHeight << " ascent=" << font->ascent
                  << " descent=" << font->descent << " lineGap=" << font->lineGap
                  << " scale=" << scale << " lineHeight=" << lineHeight << std::endl;
        logged = true;
    }

    return lineHeight;
}

float FontEngine::getAscent(const std::string& fontName, int fontSizeHalfPt)
{
    FontInfo* font = getFont(fontName);
    if (!font)
        return halfPtToPixels(fontSizeHalfPt) * 0.8f;

    float scale = stbtt_ScaleForMappingEmToPixels(font->info, halfPtToPixels(fontSizeHalfPt));
    return font->ascent * scale;
}

// ── UTF-8 decoding ─────────────────────────────────────────────
char32_t FontEngine::nextUTF8(const char*& p)
{
    unsigned char c = *p++;
    if (c < 0x80)
        return c;
    if (c < 0xE0)
    {
        char32_t r = (c & 0x1F) << 6;
        r |= (*p++ & 0x3F);
        return r;
    }
    if (c < 0xF0)
    {
        char32_t r = (c & 0x0F) << 12;
        r |= (*p++ & 0x3F) << 6;
        r |= (*p++ & 0x3F);
        return r;
    }
    char32_t r = (c & 0x07) << 18;
    r |= (*p++ & 0x3F) << 12;
    r |= (*p++ & 0x3F) << 6;
    r |= (*p++ & 0x3F);
    return r;
}

// ── Rendering ──────────────────────────────────────────────────
GlyphBitmap FontEngine::renderChar(char32_t ch, const std::string& fontName, int fontSizeHalfPt)
{
    GlyphBitmap glyph;
    FontInfo* font = getFont(fontName);
    if (!font)
        return glyph;

    float pxHeight = halfPtToPixels(fontSizeHalfPt);
    float scale = stbtt_ScaleForMappingEmToPixels(font->info, pxHeight);

    int advance, lsb;
    stbtt_GetGlyphHMetrics(font->info, stbtt_FindGlyphIndex(font->info, ch), &advance, &lsb);

    glyph.advance = advance * scale;

    int w, h, xoff, yoff;
    unsigned char* bitmap
        = stbtt_GetCodepointBitmap(font->info, 0, scale, ch, &w, &h, &xoff, &yoff);
    if (bitmap)
    {
        glyph.width = w;
        glyph.height = h;
        glyph.xoff = xoff;
        glyph.yoff = yoff;
        glyph.pixels.assign(bitmap, bitmap + w * h);
        stbtt_FreeBitmap(bitmap, nullptr);
    }

    return glyph;
}

void FontEngine::renderString(uint8_t* buffer, int bufW, int bufH, int bufStride,
                              const std::string& text, const std::string& fontName,
                              int fontSizeHalfPt, int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
    FontInfo* font = getFont(fontName);
    if (!font || text.empty())
        return;

    float pxHeight = halfPtToPixels(fontSizeHalfPt);
    float scale = stbtt_ScaleForMappingEmToPixels(font->info, pxHeight);
    float ascent = font->ascent * scale;

    float penX = (float)x;
    float baselineY = y + ascent;

    const char* p = text.c_str();
    char32_t prev = 0;
    while (*p)
    {
        char32_t ch = nextUTF8(p);

        // Kerning
        if (prev)
        {
            int kern = stbtt_GetCodepointKernAdvance(font->info, prev, ch);
            penX += kern * scale;
        }

        // Render glyph
        GlyphBitmap glyph = renderChar(ch, fontName, fontSizeHalfPt);

        // Composite onto buffer
        int gx = (int)(penX) + glyph.xoff;
        int gy = (int)(baselineY) + glyph.yoff;

        for (int row = 0; row < glyph.height; row++)
        {
            for (int col = 0; col < glyph.width; col++)
            {
                int dx = gx + col;
                int dy = gy + row;
                if (dx >= 0 && dx < bufW && dy >= 0 && dy < bufH)
                {
                    uint8_t alpha = glyph.pixels[row * glyph.width + col];
                    if (alpha > 0)
                    {
                        uint8_t* dst = buffer + dy * bufStride + dx * 4;
                        // BGRA format (typical for BMP/PNG on Windows)
                        float a = alpha / 255.0f;
                        dst[0] = (uint8_t)(dst[0] * (1 - a) + b * a); // B
                        dst[1] = (uint8_t)(dst[1] * (1 - a) + g * a); // G
                        dst[2] = (uint8_t)(dst[2] * (1 - a) + r * a); // R
                        dst[3] = 255; // A
                    }
                }
            }
        }

        penX += glyph.advance;
        prev = ch;
    }
}

} // namespace docx
