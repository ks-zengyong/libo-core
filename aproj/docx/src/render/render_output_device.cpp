// RenderInstructionOutputDevice 实现 — 将 Draw* 调用转换为 RenderInstruction
// 与 LibreOffice 的 GDIMetaFile → MetaAction → RenderInstruction 路径对称

#include "render_output_device.h"
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

    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::TEXT_RUN;
    inst.pageNum = m_nPageNum;
    inst.x = rPt.x;
    inst.y = rPt.y;
    inst.text = rText.c_str();
    inst.textLen = static_cast<int>(rText.size());

    // 字体属性
    inst.fontName = m_aCurrentFont.familyName.c_str();
    inst.fontSize = m_aCurrentFont.GetHeightInHalfPoints();
    inst.fontColor = m_aTextColor.valid ? m_aTextColor.ToRGB() : 0;
    inst.fontWeight = static_cast<uint8_t>(m_aCurrentFont.weight);
    inst.fontItalic = static_cast<uint8_t>(m_aCurrentFont.italic);

    m_rSink.OnInstruction(inst);
}

void RenderInstructionOutputDevice::DrawRect(const SwRect& rRect)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::RECT;
    inst.pageNum = m_nPageNum;
    inst.x = rRect.Left();
    inst.y = rRect.Top();
    inst.width = rRect.Width();
    inst.height = rRect.Height();

    m_rSink.OnInstruction(inst);
}

void RenderInstructionOutputDevice::DrawLine(const Point& rStart, const Point& rEnd)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::LINE;
    inst.pageNum = m_nPageNum;
    inst.x = rStart.x;
    inst.y = rStart.y;
    inst.width = rEnd.x; // x2
    inst.height = rEnd.y; // y2

    m_rSink.OnInstruction(inst);
}

void RenderInstructionOutputDevice::DrawBitmap(const Point& rPt, const Size& rSize)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::BITMAP;
    inst.pageNum = m_nPageNum;
    inst.x = rPt.x;
    inst.y = rPt.y;
    inst.width = rSize.width;
    inst.height = rSize.height;

    m_rSink.OnInstruction(inst);
}

void RenderInstructionOutputDevice::DrawEllipse(const SwRect& rRect)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::ELLIPSE;
    inst.pageNum = m_nPageNum;
    inst.x = rRect.Left();
    inst.y = rRect.Top();
    inst.width = rRect.Width();
    inst.height = rRect.Height();

    m_rSink.OnInstruction(inst);
}

// ── 状态设置 ──

void RenderInstructionOutputDevice::SetFont(const OutputFont& rFont)
{
    m_aCurrentFont = rFont;

    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::SET_FONT;
    inst.pageNum = m_nPageNum;
    inst.fontName = m_aCurrentFont.familyName.c_str();
    inst.fontSize = m_aCurrentFont.GetHeightInHalfPoints();
    inst.fontWeight = static_cast<uint8_t>(m_aCurrentFont.weight);
    inst.fontItalic = static_cast<uint8_t>(m_aCurrentFont.italic);

    m_rSink.OnInstruction(inst);
}

void RenderInstructionOutputDevice::SetTextColor(const OutputColor& rColor)
{
    m_aTextColor = rColor;

    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::SET_TEXT_COLOR;
    inst.pageNum = m_nPageNum;
    inst.fontColor = rColor.valid ? rColor.ToRGB() : 0;

    m_rSink.OnInstruction(inst);
}

void RenderInstructionOutputDevice::SetFillColor(const OutputColor& rColor)
{
    m_aFillColor = rColor;
    m_bFillSet = rColor.valid;

    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::SET_FILL_COLOR;
    inst.pageNum = m_nPageNum;
    inst.fontColor = rColor.valid ? rColor.ToRGB() : 0;

    m_rSink.OnInstruction(inst);
}

void RenderInstructionOutputDevice::SetLineColor(const OutputColor& rColor)
{
    m_aLineColor = rColor;
    m_bLineSet = rColor.valid;

    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::SET_LINE_COLOR;
    inst.pageNum = m_nPageNum;
    inst.fontColor = rColor.valid ? rColor.ToRGB() : 0;

    m_rSink.OnInstruction(inst);
}

void RenderInstructionOutputDevice::SetClipRegion(const SwRect& rRect)
{
    (void)rRect; // 裁剪区域暂不记录几何
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::SET_CLIP_REGION;
    inst.pageNum = m_nPageNum;

    m_rSink.OnInstruction(inst);
}

void RenderInstructionOutputDevice::ResetClipRegion()
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::SET_CLIP_REGION;
    inst.pageNum = m_nPageNum;

    m_rSink.OnInstruction(inst);
}

void RenderInstructionOutputDevice::Push()
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::PUSH;
    inst.pageNum = m_nPageNum;

    m_rSink.OnInstruction(inst);
}

void RenderInstructionOutputDevice::Pop()
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::POP;
    inst.pageNum = m_nPageNum;

    m_rSink.OnInstruction(inst);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
