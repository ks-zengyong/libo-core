// RenderInstructionOutputDevice 实现 — 将 Draw* 调用转换为 RenderInstruction
// 使用共享的 instruction_builder.h 确保与 LibreOffice 侧指令构建逻辑完全一致

#include "render_output_device.h"
#include "instruction_builder.h"
#include <cstring>

RenderInstructionOutputDevice::RenderInstructionOutputDevice(RenderInstructionSink& rSink,
                                                             int pageNum)
    : m_rSink(rSink)
    , m_nPageNum(pageNum)
{
}

// ── 绘图方法 ──

void RenderInstructionOutputDevice::DrawText(const Point& rPt, const std::string& rText)
{
    if (rText.empty())
        return;

    BuildTextRunInstruction(
        m_rSink, m_nPageNum, /*nestLevel=*/0, rPt.x, rPt.y, rText.c_str(),
        static_cast<int>(rText.size()), m_aCurrentFont.familyName.c_str(),
        m_aCurrentFont.GetHeightInHalfPoints(), m_aTextColor.valid ? m_aTextColor.ToRGB() : 0,
        static_cast<uint8_t>(m_aCurrentFont.weight), static_cast<uint8_t>(m_aCurrentFont.italic));
}

void RenderInstructionOutputDevice::DrawRect(const SwRect& rRect)
{
    BuildRectInstruction(m_rSink, m_nPageNum, /*nestLevel=*/0, rRect.Left(), rRect.Top(),
                         rRect.Width(), rRect.Height());
}

void RenderInstructionOutputDevice::DrawLine(const Point& rStart, const Point& rEnd)
{
    BuildLineInstruction(m_rSink, m_nPageNum, /*nestLevel=*/0, rStart.x, rStart.y, rEnd.x, rEnd.y);
}

void RenderInstructionOutputDevice::DrawBitmap(const Point& rPt, const Size& rSize)
{
    BuildBitmapInstruction(m_rSink, m_nPageNum, /*nestLevel=*/0, rPt.x, rPt.y, rSize.width,
                           rSize.height);
}

void RenderInstructionOutputDevice::DrawEllipse(const SwRect& rRect)
{
    BuildEllipseInstruction(m_rSink, m_nPageNum, /*nestLevel=*/0, rRect.Left(), rRect.Top(),
                            rRect.Width(), rRect.Height());
}

// ── 状态设置 ──

void RenderInstructionOutputDevice::SetFont(const OutputFont& rFont)
{
    m_aCurrentFont = rFont;
    BuildSetFontInstruction(m_rSink, m_nPageNum, /*nestLevel=*/0, m_aCurrentFont.familyName.c_str(),
                            m_aCurrentFont.GetHeightInHalfPoints(),
                            static_cast<uint8_t>(m_aCurrentFont.weight),
                            static_cast<uint8_t>(m_aCurrentFont.italic));
}

void RenderInstructionOutputDevice::SetTextColor(const OutputColor& rColor)
{
    m_aTextColor = rColor;
    BuildSetTextColorInstruction(m_rSink, m_nPageNum, /*nestLevel=*/0,
                                 rColor.valid ? rColor.ToRGB() : 0);
}

void RenderInstructionOutputDevice::SetFillColor(const OutputColor& rColor)
{
    m_aFillColor = rColor;
    m_bFillSet = rColor.valid;
    BuildSetFillColorInstruction(m_rSink, m_nPageNum, /*nestLevel=*/0,
                                 rColor.valid ? rColor.ToRGB() : 0);
}

void RenderInstructionOutputDevice::SetLineColor(const OutputColor& rColor)
{
    m_aLineColor = rColor;
    m_bLineSet = rColor.valid;
    BuildSetLineColorInstruction(m_rSink, m_nPageNum, /*nestLevel=*/0,
                                 rColor.valid ? rColor.ToRGB() : 0);
}

void RenderInstructionOutputDevice::SetClipRegion(const SwRect& rRect)
{
    (void)rRect; // 裁剪区域暂不记录几何
    BuildSetClipRegionInstruction(m_rSink, m_nPageNum, /*nestLevel=*/0);
}

void RenderInstructionOutputDevice::ResetClipRegion()
{
    BuildSetClipRegionInstruction(m_rSink, m_nPageNum, /*nestLevel=*/0);
}

void RenderInstructionOutputDevice::Push()
{
    BuildPushInstruction(m_rSink, m_nPageNum, /*nestLevel=*/0);
}

void RenderInstructionOutputDevice::Pop()
{
    BuildPopInstruction(m_rSink, m_nPageNum, /*nestLevel=*/0);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
