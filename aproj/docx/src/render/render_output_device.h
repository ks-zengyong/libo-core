#pragma once
// RenderInstructionOutputDevice — 将 OutputDevice 绘制调用转换为 RenderInstruction
// 实现与 LibreOffice 的 GDIMetaFile → RenderInstruction 路径对称的 aproj 侧路径

#include "output_device.h"
#include "render_instruction.h"
#include <string>
#include <vector>

// RenderInstructionOutputDevice: 将 Draw* 调用转换为 RenderInstruction
class RenderInstructionOutputDevice : public OutputDevice
{
public:
    RenderInstructionOutputDevice(RenderInstructionSink& rSink, int pageNum);

    // 设置当前页码（翻页时调用）
    void SetPageNum(int pageNum) { m_nPageNum = pageNum; }
    int GetPageNum() const { return m_nPageNum; }

    // ── OutputDevice 接口实现 ──
    void DrawText(const Point& rPt, const std::string& rText) override;
    void DrawRect(const SwRect& rRect) override;
    void DrawLine(const Point& rStart, const Point& rEnd) override;
    void DrawBitmap(const Point& rPt, const Size& rSize) override;
    void DrawEllipse(const SwRect& rRect) override;

    void SetFont(const OutputFont& rFont) override;
    void SetTextColor(const OutputColor& rColor) override;
    void SetFillColor(const OutputColor& rColor) override;
    void SetLineColor(const OutputColor& rColor) override;
    void SetClipRegion(const SwRect& rRect) override;
    void ResetClipRegion() override;
    void Push() override;
    void Pop() override;

private:
    RenderInstructionSink& m_rSink;
    int m_nPageNum;

    // 状态上下文
    OutputFont m_aCurrentFont;
    OutputColor m_aTextColor;
    OutputColor m_aFillColor;
    OutputColor m_aLineColor;
    bool m_bFillSet = false;
    bool m_bLineSet = false;
};

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
