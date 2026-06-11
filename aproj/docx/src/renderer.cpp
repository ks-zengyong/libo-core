#include "renderer.h"

// stb_image_write is included via the main .cpp file that defines STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// stb_image for decoding embedded images
#include "stb_image.h"

#include <cstring>
#include <cmath>
#include <algorithm>
#include <iostream>

namespace docx
{
// ── Bitmap ─────────────────────────────────────────────────────
void Bitmap::init(int w, int h)
{
    width = w;
    height = h;
    stride = w * 4;
    pixels.resize(stride * h, 0);
}

void Bitmap::fill(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    for (int y = 0; y < height; y++)
    {
        uint8_t* row = pixels.data() + y * stride;
        for (int x = 0; x < width; x++)
        {
            row[x * 4 + 0] = r;
            row[x * 4 + 1] = g;
            row[x * 4 + 2] = b;
            row[x * 4 + 3] = a;
        }
    }
}

void Bitmap::setPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    if (x < 0 || x >= width || y < 0 || y >= height)
        return;
    uint8_t* dst = pixels.data() + y * stride + x * 4;
    if (a == 255)
    {
        dst[0] = r;
        dst[1] = g;
        dst[2] = b;
        dst[3] = a;
    }
    else
    {
        float alpha = a / 255.0f;
        dst[0] = (uint8_t)(dst[0] * (1 - alpha) + r * alpha);
        dst[1] = (uint8_t)(dst[1] * (1 - alpha) + g * alpha);
        dst[2] = (uint8_t)(dst[2] * (1 - alpha) + b * alpha);
        dst[3] = 255;
    }
}

void Bitmap::drawRect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    for (int i = x; i < x + w && i < width; i++)
    {
        setPixel(i, y, r, g, b, a);
        setPixel(i, y + h - 1, r, g, b, a);
    }
    for (int j = y; j < y + h && j < height; j++)
    {
        setPixel(x, j, r, g, b, a);
        setPixel(x + w - 1, j, r, g, b, a);
    }
}

void Bitmap::drawLine(int x1, int y1, int x2, int y2, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    // Bresenham's line algorithm
    int dx = abs(x2 - x1);
    int dy = abs(y2 - y1);
    int sx = x1 < x2 ? 1 : -1;
    int sy = y1 < y2 ? 1 : -1;
    int err = dx - dy;
    while (true)
    {
        setPixel(x1, y1, r, g, b, a);
        if (x1 == x2 && y1 == y2)
            break;
        int e2 = 2 * err;
        if (e2 > -dy)
        {
            err -= dy;
            x1 += sx;
        }
        if (e2 < dx)
        {
            err += dx;
            y1 += sy;
        }
    }
}

bool Bitmap::savePNG(const std::string& path)
{
    // stb_image_write expects top-to-bottom, our buffer is already top-to-bottom
    // Write as RGBA
    return stbi_write_png(path.c_str(), width, height, 4, pixels.data(), stride) != 0;
}

// ── Renderer ───────────────────────────────────────────────────
Bitmap Renderer::render(const RootFrame& root, FontEngine& fonts)
{
    fonts_ = &fonts;
    dpi_ = root.dpi;

    // Calculate total dimensions
    int maxW = 0;
    float totalH = 0;
    float gap = 20; // gap between pages in pixels

    for (auto& page : root.pages)
    {
        if ((int)page.width > maxW)
            maxW = (int)page.width;
        totalH += page.height + gap;
    }
    totalH -= gap; // no gap after last page

    if (maxW <= 0)
        maxW = 800;
    if (totalH <= 0)
        totalH = 600;

    Bitmap bmp;
    bmp.init(maxW, (int)ceilf(totalH));

    // Fill with light gray background (the "desktop")
    bmp.fill(240, 240, 240);

    bmp_ = &bmp;

    // Render each page
    float yOffset = 0;
    for (auto& page : root.pages)
    {
        renderPage(page, yOffset);
        yOffset += page.height + gap;
    }

    return bmp;
}

void Renderer::renderPage(const PageFrame& page, float yOffset)
{
    // Draw page shadow (slight offset)
    int px = (int)(bmp_->width - page.width) / 2; // center the page
    int py = (int)yOffset;

    // Shadow
    for (int y = py + 3; y < py + (int)page.height + 3 && y < bmp_->height; y++)
    {
        for (int x = px + 3; x < px + (int)page.width + 3 && x < bmp_->width; x++)
        {
            if (x >= 0 && y >= 0)
            {
                bmp_->setPixel(x, y, 180, 180, 180);
            }
        }
    }

    // Page background (white)
    for (int y = py; y < py + (int)page.height && y < bmp_->height; y++)
    {
        for (int x = px; x < px + (int)page.width && x < bmp_->width; x++)
        {
            if (x >= 0 && y >= 0)
            {
                bmp_->setPixel(x, y, 255, 255, 255);
            }
        }
    }

    // Render header
    if (page.headerHeight > 0)
    {
        renderHeaderFooter(page.header, px, py);
    }

    // Render body content
    renderBody(page.body, px, py);

    // Render footer
    if (page.footerHeight > 0)
    {
        renderHeaderFooter(page.footer, px, py);
    }
}

void Renderer::renderHeaderFooter(const HeaderFooterFrame& hf, float xOffset, float yOffset)
{
    for (auto& tf : hf.textFrames)
    {
        renderTextFrame(tf, xOffset, yOffset);
    }
}

void Renderer::renderBody(const BodyFrame& body, float xOffset, float yOffset)
{
    for (auto& item : body.items)
    {
        switch (item.type)
        {
            case BodyFrame::ContentType::Text:
                if (item.index < body.textFrames.size())
                {
                    renderTextFrame(body.textFrames[item.index], xOffset, yOffset);
                }
                break;
            case BodyFrame::ContentType::Table:
                if (item.index < body.tableFrames.size())
                {
                    renderTableFrame(body.tableFrames[item.index], xOffset, yOffset);
                }
                break;
            case BodyFrame::ContentType::Image:
                if (item.index < body.imageFrames.size())
                {
                    renderImageFrame(body.imageFrames[item.index], xOffset, yOffset);
                }
                break;
        }
    }
}

void Renderer::renderTextFrame(const TextFrame& tf, float xOffset, float yOffset)
{
    for (auto& line : tf.lines)
    {
        for (auto& lr : line.runs)
        {
            if (lr.text.empty() || lr.text == "\t" || lr.text == " ")
                continue;

            // Check if this is an image run
            if (lr.sourceRun && lr.sourceRun->isDrawing())
            {
                // Render inline image
                int imgIdx = lr.sourceRun->drawingImageIndex;
                // Image rendering would need access to document images
                // For now, draw a placeholder rectangle
                int ix = (int)(xOffset + tf.x + lr.x);
                int iy = (int)(yOffset + tf.y + line.y);
                int iw = (int)lr.width;
                int ih = (int)line.height;
                bmp_->drawRect(ix, iy, iw, ih, 200, 200, 200);
                continue;
            }

            int fontSize = lr.fontSize > 0 ? lr.fontSize : 22;
            int px = (int)(xOffset + tf.x + lr.x);
            int py = (int)(yOffset + tf.y + line.y);

            fonts_->renderString(bmp_->pixels.data(), bmp_->width, bmp_->height, bmp_->stride,
                                 lr.text, lr.fontName, fontSize, px, py, lr.color.r, lr.color.g,
                                 lr.color.b);
        }
    }
}

void Renderer::renderTableFrame(const TableFrame& tf, float xOffset, float yOffset)
{
    // Draw table borders
    int tx = (int)(xOffset + tf.x);
    int ty = (int)(yOffset + tf.y);
    int tw = (int)tf.width;
    int th = (int)tf.height;

    // Outer border
    bmp_->drawRect(tx, ty, tw, th, 0, 0, 0);

    // Row lines
    float rowY = 0;
    for (auto& row : tf.rows)
    {
        rowY += row.height;
        int ly = ty + (int)rowY;
        if (ly < bmp_->height)
        {
            bmp_->drawLine(tx, ly, tx + tw, ly, 0, 0, 0);
        }

        // Cell content
        for (auto& cell : row.cells)
        {
            // Cell borders
            int cx = tx + (int)cell.x;
            int cy = ty + (int)(rowY - row.height);
            int cw = (int)cell.width;
            int ch = (int)cell.height;
            bmp_->drawRect(cx, cy, cw, ch, 180, 180, 180);

            // Cell content
            for (auto& tf : cell.textFrames)
            {
                renderTextFrame(tf, xOffset + cell.x, yOffset + rowY - row.height);
            }
        }
    }
}

void Renderer::renderImageFrame(const ImageFrame& imgf, float xOffset, float yOffset)
{
    if (!imgf.image || imgf.image->data.empty())
        return;

    // Decode image
    int w, h, channels;
    unsigned char* imgData = stbi_load_from_memory(
        imgf.image->data.data(), (int)imgf.image->data.size(), &w, &h, &channels, 4 // force RGBA
    );
    if (!imgData)
        return;

    // Scale to frame size
    int dx = (int)(xOffset + imgf.x);
    int dy = (int)(yOffset + imgf.y);
    int dw = (int)imgf.width;
    int dh = (int)imgf.height;

    if (dw <= 0 || dh <= 0)
    {
        stbi_image_free(imgData);
        return;
    }

    // Simple nearest-neighbor scaling
    for (int y = 0; y < dh; y++)
    {
        for (int x = 0; x < dw; x++)
        {
            int sx = x * w / dw;
            int sy = y * h / dh;
            if (sx < w && sy < h)
            {
                unsigned char* src = imgData + (sy * w + sx) * 4;
                bmp_->setPixel(dx + x, dy + y, src[0], src[1], src[2], src[3]);
            }
        }
    }

    stbi_image_free(imgData);
}

} // namespace docx
