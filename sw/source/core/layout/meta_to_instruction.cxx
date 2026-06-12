/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * MetaAction → RenderInstruction 转换器实现
 */

#include "meta_to_instruction.hxx"
#include <osl/diagnose.hxx>
#include <rtl/string.hxx>

#include <cstring>

// ── 主入口 ──

void MetaToInstructionConverter::Convert(GDIMetaFile& rMtf, RenderInstructionSink& rSink,
                                         int pageNum)
{
    rMtf.WindStart();

    for (size_t i = 0; i < rMtf.GetActionSize(); ++i)
    {
        const MetaAction* pAction = rMtf.GetAction(i);
        if (pAction)
            ConvertAction(pAction, rSink, pageNum);
    }
}

// ── 动作分发 ──

void MetaToInstructionConverter::ConvertAction(const MetaAction* pAction,
                                               RenderInstructionSink& rSink, int pageNum)
{
    switch (pAction->GetType())
    {
        case MetaActionType::FONT:
        {
            const auto* p = static_cast<const MetaFontAction*>(pAction);
            m_aCurrentFont = p->GetFont();
            EmitSetFont(rSink, pageNum);
            break;
        }
        case MetaActionType::TEXTCOLOR:
        {
            const auto* p = static_cast<const MetaTextColorAction*>(pAction);
            m_aTextColor = p->GetColor();
            EmitSetTextColor(rSink, pageNum);
            break;
        }
        case MetaActionType::FILLCOLOR:
        {
            const auto* p = static_cast<const MetaFillColorAction*>(pAction);
            if (p->IsSetting())
                m_aFillColor = p->GetColor();
            m_bFillSet = p->IsSetting();
            EmitSetFillColor(rSink, pageNum);
            break;
        }
        case MetaActionType::LINECOLOR:
        {
            const auto* p = static_cast<const MetaLineColorAction*>(pAction);
            if (p->IsSetting())
                m_aLineColor = p->GetColor();
            m_bLineSet = p->IsSetting();
            EmitSetLineColor(rSink, pageNum);
            break;
        }
        case MetaActionType::TEXT:
        {
            const auto* p = static_cast<const MetaTextAction*>(pAction);
            EmitText(p->GetPoint(), p->GetText(), p->GetIndex(), p->GetLen(), rSink, pageNum);
            break;
        }
        case MetaActionType::TEXTARRAY:
        {
            const auto* p = static_cast<const MetaTextArrayAction*>(pAction);
            EmitText(p->GetPoint(), p->GetText(), p->GetIndex(), p->GetLen(), rSink, pageNum);
            break;
        }
        case MetaActionType::RECT:
        {
            const auto* p = static_cast<const MetaRectAction*>(pAction);
            EmitRect(p->GetRect(), rSink, pageNum);
            break;
        }
        case MetaActionType::LINE:
        {
            const auto* p = static_cast<const MetaLineAction*>(pAction);
            EmitLine(p->GetStartPoint(), p->GetEndPoint(), rSink, pageNum);
            break;
        }
        case MetaActionType::BMPEX:
        case MetaActionType::BMPEXSCALE:
        case MetaActionType::BMPEXSCALEPART:
        {
            // 所有 Bitmap 变体都有 GetBitmap() 和 GetPoint()
            const auto* p = static_cast<const MetaBmpExAction*>(pAction);
            EmitBitmap(p->GetPoint(), p->GetBitmap(), rSink, pageNum);
            break;
        }
        case MetaActionType::BMP:
        {
            const auto* p = static_cast<const MetaBmpAction*>(pAction);
            EmitBitmap(p->GetPoint(), p->GetBitmap(), rSink, pageNum);
            break;
        }
        case MetaActionType::ELLIPSE:
        {
            const auto* p = static_cast<const MetaEllipseAction*>(pAction);
            EmitEllipse(p->GetRect(), rSink, pageNum);
            break;
        }
        case MetaActionType::POLYGON:
        {
            // Polygon 暂不输出几何细节，仅记录存在
            // TODO: 可扩展为输出顶点数组
            break;
        }
        case MetaActionType::POLYPOLYGON:
        {
            // 同上
            break;
        }
        case MetaActionType::PUSH:
        {
            RenderInstruction inst;
            RenderInstruction_clear(&inst);
            inst.type = RenderCmdType::PUSH;
            inst.pageNum = pageNum;
            rSink.OnInstruction(inst);
            break;
        }
        case MetaActionType::POP:
        {
            RenderInstruction inst;
            RenderInstruction_clear(&inst);
            inst.type = RenderCmdType::POP;
            inst.pageNum = pageNum;
            rSink.OnInstruction(inst);
            break;
        }
        case MetaActionType::CLIPREGION:
        case MetaActionType::ISECTRECTCLIPREGION:
        case MetaActionType::ISECTREGIONCLIPREGION:
        {
            RenderInstruction inst;
            RenderInstruction_clear(&inst);
            inst.type = RenderCmdType::SET_CLIP_REGION;
            inst.pageNum = pageNum;
            rSink.OnInstruction(inst);
            break;
        }
        default:
            // 其他动作（MAPMODE, RASTEROP, WALLPAPER 等）暂不转换
            break;
    }
}

// ── 绘制动作 emit ──

void MetaToInstructionConverter::EmitText(const Point& rPt, const OUString& rText, sal_Int32 nIndex,
                                          sal_Int32 nLen, RenderInstructionSink& rSink, int pageNum)
{
    // 提取子串
    OUString aSubText = rText.copy(nIndex, nLen);
    OString utf8Text = OUStringToOString(aSubText, RTL_TEXTENCODING_UTF8);

    // 字体属性
    OString fontName = OUStringToOString(m_aCurrentFont.GetFamilyName(), RTL_TEXTENCODING_UTF8);
    const Size& rSize = m_aCurrentFont.GetFontSize();
    // FontSize Width/Height 是 1/100mm，转换为半点 (1pt = 20twips, 1半点=10twips)
    // 但 Writer 使用 twips，所以直接用 Height (已经是 twips 单位)
    int fontSize = static_cast<int>(rSize.Height() / 10); // twips → 半点

    FontWeight eWeight = m_aCurrentFont.GetWeight();
    uint8_t fontWeight = (eWeight >= WEIGHT_BOLD) ? 700 : 400;

    FontItalic eItalic = m_aCurrentFont.GetItalic();
    uint8_t fontItalic = (eItalic != ITALIC_NONE) ? 1 : 0;

    uint32_t fontColor = (static_cast<uint32_t>(m_aTextColor.GetRed()) << 16)
                         | (static_cast<uint32_t>(m_aTextColor.GetGreen()) << 8)
                         | static_cast<uint32_t>(m_aTextColor.GetBlue());

    // 线程安全的字符串缓冲
    static thread_local std::string s_textBuf;
    static thread_local std::string s_fontBuf;

    s_textBuf = utf8Text.getStr();
    s_fontBuf = fontName.getStr();

    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::TEXT_RUN;
    inst.pageNum = pageNum;
    inst.x = rPt.X();
    inst.y = rPt.Y();
    inst.text = s_textBuf.c_str();
    inst.textLen = static_cast<int>(s_textBuf.size());
    inst.fontName = s_fontBuf.c_str();
    inst.fontSize = fontSize;
    inst.fontColor = fontColor;
    inst.fontWeight = fontWeight;
    inst.fontItalic = fontItalic;

    rSink.OnInstruction(inst);
}

void MetaToInstructionConverter::EmitRect(const tools::Rectangle& rRect,
                                          RenderInstructionSink& rSink, int pageNum)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::RECT;
    inst.pageNum = pageNum;
    inst.x = rRect.Left();
    inst.y = rRect.Top();
    inst.width = rRect.GetWidth();
    inst.height = rRect.GetHeight();

    rSink.OnInstruction(inst);
}

void MetaToInstructionConverter::EmitLine(const Point& rStart, const Point& rEnd,
                                          RenderInstructionSink& rSink, int pageNum)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::LINE;
    inst.pageNum = pageNum;
    inst.x = rStart.X();
    inst.y = rStart.Y();
    inst.width = rEnd.X(); // x2
    inst.height = rEnd.Y(); // y2

    rSink.OnInstruction(inst);
}

void MetaToInstructionConverter::EmitBitmap(const Point& rPt, const Bitmap& rBmp,
                                            RenderInstructionSink& rSink, int pageNum)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::BITMAP;
    inst.pageNum = pageNum;
    inst.x = rPt.X();
    inst.y = rPt.Y();
    inst.width = rBmp.GetSizePixel().Width();
    inst.height = rBmp.GetSizePixel().Height();

    rSink.OnInstruction(inst);
}

void MetaToInstructionConverter::EmitEllipse(const tools::Rectangle& rRect,
                                             RenderInstructionSink& rSink, int pageNum)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::ELLIPSE;
    inst.pageNum = pageNum;
    inst.x = rRect.Left();
    inst.y = rRect.Top();
    inst.width = rRect.GetWidth();
    inst.height = rRect.GetHeight();

    rSink.OnInstruction(inst);
}

// ── 状态变更 emit ──

void MetaToInstructionConverter::EmitSetFont(RenderInstructionSink& rSink, int pageNum)
{
    OString fontName = OUStringToOString(m_aCurrentFont.GetFamilyName(), RTL_TEXTENCODING_UTF8);
    const Size& rSize = m_aCurrentFont.GetFontSize();
    int fontSize = static_cast<int>(rSize.Height() / 10);

    FontWeight eWeight = m_aCurrentFont.GetWeight();
    uint8_t fontWeight = (eWeight >= WEIGHT_BOLD) ? 700 : 400;

    FontItalic eItalic = m_aCurrentFont.GetItalic();
    uint8_t fontItalic = (eItalic != ITALIC_NONE) ? 1 : 0;

    static thread_local std::string s_fontBuf;
    s_fontBuf = fontName.getStr();

    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::SET_FONT;
    inst.pageNum = pageNum;
    inst.fontName = s_fontBuf.c_str();
    inst.fontSize = fontSize;
    inst.fontWeight = fontWeight;
    inst.fontItalic = fontItalic;

    rSink.OnInstruction(inst);
}

void MetaToInstructionConverter::EmitSetTextColor(RenderInstructionSink& rSink, int pageNum)
{
    uint32_t fontColor = (static_cast<uint32_t>(m_aTextColor.GetRed()) << 16)
                         | (static_cast<uint32_t>(m_aTextColor.GetGreen()) << 8)
                         | static_cast<uint32_t>(m_aTextColor.GetBlue());

    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::SET_TEXT_COLOR;
    inst.pageNum = pageNum;
    inst.fontColor = fontColor;

    rSink.OnInstruction(inst);
}

void MetaToInstructionConverter::EmitSetFillColor(RenderInstructionSink& rSink, int pageNum)
{
    uint32_t fillColor = 0;
    if (m_bFillSet)
    {
        fillColor = (static_cast<uint32_t>(m_aFillColor.GetRed()) << 16)
                    | (static_cast<uint32_t>(m_aFillColor.GetGreen()) << 8)
                    | static_cast<uint32_t>(m_aFillColor.GetBlue());
    }

    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::SET_FILL_COLOR;
    inst.pageNum = pageNum;
    inst.fontColor = fillColor; // 复用 fontColor 字段存储颜色值

    rSink.OnInstruction(inst);
}

void MetaToInstructionConverter::EmitSetLineColor(RenderInstructionSink& rSink, int pageNum)
{
    uint32_t lineColor = 0;
    if (m_bLineSet)
    {
        lineColor = (static_cast<uint32_t>(m_aLineColor.GetRed()) << 16)
                    | (static_cast<uint32_t>(m_aLineColor.GetGreen()) << 8)
                    | static_cast<uint32_t>(m_aLineColor.GetBlue());
    }

    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::SET_LINE_COLOR;
    inst.pageNum = pageNum;
    inst.fontColor = lineColor; // 复用 fontColor 字段存储颜色值

    rSink.OnInstruction(inst);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
