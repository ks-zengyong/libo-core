#pragma once
// Renderer — renders the frame tree to a bitmap and saves as PNG.
// Mirrors LibreOffice's SwRootFrame::PaintSwFrame() → SwTextFrame::PaintSwFrame().

#include "frame.h"
#include "font_engine.h"
#include <vector>
#include <cstdint>
#include <string>

namespace docx
{
struct Bitmap
{
    std::vector<uint8_t> pixels; // RGBA format
    int width = 0;
    int height = 0;
    int stride = 0; // bytes per row

    void init(int w, int h);
    void fill(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    void setPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    void drawRect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    void drawLine(int x1, int y1, int x2, int y2, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    bool savePNG(const std::string& path);
};

class Renderer
{
public:
    // Render the entire frame tree to a bitmap.
    Bitmap render(const RootFrame& root, FontEngine& fonts);

private:
    Bitmap* bmp_ = nullptr;
    FontEngine* fonts_ = nullptr;
    float dpi_ = 96.0f;

    void renderPage(const PageFrame& page, float yOffset);
    void renderBody(const BodyFrame& body, float xOffset, float yOffset);
    void renderHeaderFooter(const HeaderFooterFrame& hf, float xOffset, float yOffset);
    void renderTextFrame(const TextFrame& tf, float xOffset, float yOffset);
    void renderTableFrame(const TableFrame& tf, float xOffset, float yOffset);
    void renderImageFrame(const ImageFrame& imgf, float xOffset, float yOffset);
};

} // namespace docx
