#pragma once
// Font engine — loads TrueType fonts and provides glyph metrics + rendering.
// Uses stb_truetype for font loading and rasterization.
// Mirrors LibreOffice's font subsystem (vcl/source/gdi/).

#include <string>
#include <map>
#include <vector>
#include <cstdint>

// Forward declare stb types (global namespace)
struct stbtt_fontinfo;

namespace docx
{
struct GlyphBitmap
{
    std::vector<uint8_t> pixels; // grayscale alpha
    int width = 0;
    int height = 0;
    int xoff = 0; // offset from pen position to left edge
    int yoff = 0; // offset from baseline to top edge
    float advance = 0; // horizontal advance in pixels
};

struct FontInfo
{
    std::string familyName;
    std::vector<uint8_t> data; // raw TTF bytes
    stbtt_fontinfo* info = nullptr;
    float scale = 0; // stbtt scale factor for 1px

    // Cached metrics
    int ascent = 0;
    int descent = 0;
    int lineGap = 0;
};

class FontEngine
{
public:
    FontEngine();
    ~FontEngine();

    // Load a font by name. Returns true if successful.
    bool loadFont(const std::string& familyName);

    // Get font info (loads on demand)
    FontInfo* getFont(const std::string& familyName);

    // Get string width in pixels at given font size (in half-points)
    float getStringWidth(const std::string& text, const std::string& fontName, int fontSizeHalfPt);

    // Get line height (ascent + descent) in pixels at given font size
    float getLineHeight(const std::string& fontName, int fontSizeHalfPt);

    // Get ascent in pixels
    float getAscent(const std::string& fontName, int fontSizeHalfPt);

    // Render a string to a bitmap at position (x, y) on the given buffer
    void renderString(uint8_t* buffer, int bufW, int bufH, int bufStride, const std::string& text,
                      const std::string& fontName, int fontSizeHalfPt, int x, int y, uint8_t r,
                      uint8_t g, uint8_t b);

    // Render a single character, return glyph bitmap
    GlyphBitmap renderChar(char32_t ch, const std::string& fontName, int fontSizeHalfPt);

    // Font name mapping: DOCX font name → system font file
    static std::string fontNameToFile(const std::string& name);

    // Substitute unknown/placeholder font names with reasonable defaults
    static std::string substituteFont(const std::string& name);

private:
    std::map<std::string, FontInfo> fonts_;
    std::string fontDir_; // C:\Windows\Fonts

    FontInfo* loadFontFile(const std::string& path, const std::string& familyName);
    float halfPtToPixels(int halfPt);
    char32_t nextUTF8(const char*& p);
};

} // namespace docx
