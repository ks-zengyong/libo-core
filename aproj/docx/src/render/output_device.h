#pragma once
// 抽象 OutputDevice 接口 — 与 VCL 的 OutputDevice API 一致
// aproj 和 LibreOffice 使用相同的接口进行渲染，确保代码路径对称
//
// LibreOffice:  PaintSwFrame → OutputDevice::DrawText → GDIMetaFile → RenderInstruction
// aproj:        PaintSwFrame → OutputDevice::DrawText → RenderInstructionOutputDevice → RenderInstruction

#include "../core/types.h"
#include "../core/swrect.h"
#include <string>
#include <cstdint>

// ── 轻量几何类型 ──

struct Point
{
    SwTwips x = 0;
    SwTwips y = 0;
    Point() = default;
    Point(SwTwips _x, SwTwips _y)
        : x(_x)
        , y(_y)
    {
    }
};

struct Size
{
    SwTwips width = 0;
    SwTwips height = 0;
    Size() = default;
    Size(SwTwips w, SwTwips h)
        : width(w)
        , height(h)
    {
    }
};

// ── 轻量颜色类型 ──

struct OutputColor
{
    uint8_t r = 0, g = 0, b = 0;
    bool valid = false;

    OutputColor() = default;
    OutputColor(uint8_t _r, uint8_t _g, uint8_t _b)
        : r(_r)
        , g(_g)
        , b(_b)
        , valid(true)
    {
    }

    uint32_t ToRGB() const
    {
        return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
    }

    static OutputColor black() { return { 0, 0, 0 }; }
    static OutputColor white() { return { 255, 255, 255 }; }
    static OutputColor transparent()
    {
        OutputColor c;
        return c;
    }
};

// ── 轻量字体类型 ──

enum class FontWeight : uint16_t
{
    Normal = 400,
    Bold = 700,
};

enum class FontItalic : uint8_t
{
    None = 0,
    Italic = 1,
};

enum class FontUnderline : uint8_t
{
    None = 0,
    Single = 1,
    Double = 2,
};

struct OutputFont
{
    std::string familyName = "Arial";
    int height = 220; // twips (11pt = 220 twips)
    FontWeight weight = FontWeight::Normal;
    FontItalic italic = FontItalic::None;
    FontUnderline underline = FontUnderline::None;
    uint8_t strikeout = 0;
    OutputColor color = OutputColor::black();

    int GetHeightInHalfPoints() const { return height / 10; } // twips → 半点
};

// ── 抽象 OutputDevice 接口 ──

class OutputDevice
{
public:
    virtual ~OutputDevice() = default;

    // 绘图方法 — 与 VCL OutputDevice API 对称
    virtual void DrawText(const Point& rPt, const std::string& rText) = 0;
    virtual void DrawRect(const SwRect& rRect) = 0;
    virtual void DrawLine(const Point& rStart, const Point& rEnd) = 0;
    virtual void DrawBitmap(const Point& rPt, const Size& rSize) = 0;
    virtual void DrawEllipse(const SwRect& rRect) = 0;

    // 状态设置 — 与 VCL OutputDevice API 对称
    virtual void SetFont(const OutputFont& rFont) = 0;
    virtual void SetTextColor(const OutputColor& rColor) = 0;
    virtual void SetFillColor(const OutputColor& rColor) = 0;
    virtual void SetLineColor(const OutputColor& rColor) = 0;
    virtual void SetClipRegion(const SwRect& rRect) = 0;
    virtual void ResetClipRegion() = 0;
    virtual void Push() = 0;
    virtual void Pop() = 0;
};

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
