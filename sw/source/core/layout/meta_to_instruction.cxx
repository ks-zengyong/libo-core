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
#include "instruction_builder.h"
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
            BuildPushInstruction(rSink, pageNum);
            break;
        case MetaActionType::POP:
            BuildPopInstruction(rSink, pageNum);
            break;
        case MetaActionType::CLIPREGION:
        case MetaActionType::ISECTRECTCLIPREGION:
        case MetaActionType::ISECTREGIONCLIPREGION:
            BuildSetClipRegionInstruction(rSink, pageNum);
            break;
        default:
            // 其他动作（MAPMODE, RASTEROP, WALLPAPER 等）暂不转换
            break;
    }
}

// ── 绘制动作 emit ──

void MetaToInstructionConverter::EmitText(const Point& rPt, const OUString& rText, sal_Int32 nIndex,
                                          sal_Int32 nLen, RenderInstructionSink& rSink, int pageNum)
{
    OUString aSubText = rText.copy(nIndex, nLen);
    OString utf8Text = OUStringToOString(aSubText, RTL_TEXTENCODING_UTF8);

    OString fontName = OUStringToOString(m_aCurrentFont.GetFamilyName(), RTL_TEXTENCODING_UTF8);
    const Size& rSize = m_aCurrentFont.GetFontSize();
    int fontSize = static_cast<int>(rSize.Height() / 10);

    FontWeight eWeight = m_aCurrentFont.GetWeight();
    uint8_t fontWeight = (eWeight >= WEIGHT_BOLD) ? 700 : 400;

    FontItalic eItalic = m_aCurrentFont.GetItalic();
    uint8_t fontItalic = (eItalic != ITALIC_NONE) ? 1 : 0;

    uint32_t fontColor = (static_cast<uint32_t>(m_aTextColor.GetRed()) << 16)
                         | (static_cast<uint32_t>(m_aTextColor.GetGreen()) << 8)
                         | static_cast<uint32_t>(m_aTextColor.GetBlue());

    static thread_local std::string s_textBuf;
    static thread_local std::string s_fontBuf;
    s_textBuf = utf8Text.getStr();
    s_fontBuf = fontName.getStr();

    BuildTextRunInstruction(rSink, pageNum, rPt.X(), rPt.Y(), s_textBuf.c_str(),
                            static_cast<int>(s_textBuf.size()), s_fontBuf.c_str(), fontSize,
                            fontColor, fontWeight, fontItalic);
}

void MetaToInstructionConverter::EmitRect(const tools::Rectangle& rRect,
                                          RenderInstructionSink& rSink, int pageNum)
{
    BuildRectInstruction(rSink, pageNum, rRect.Left(), rRect.Top(), rRect.GetWidth(),
                         rRect.GetHeight());
}

void MetaToInstructionConverter::EmitLine(const Point& rStart, const Point& rEnd,
                                          RenderInstructionSink& rSink, int pageNum)
{
    BuildLineInstruction(rSink, pageNum, rStart.X(), rStart.Y(), rEnd.X(), rEnd.Y());
}

void MetaToInstructionConverter::EmitBitmap(const Point& rPt, const Bitmap& rBmp,
                                            RenderInstructionSink& rSink, int pageNum)
{
    BuildBitmapInstruction(rSink, pageNum, rPt.X(), rPt.Y(), rBmp.GetSizePixel().Width(),
                           rBmp.GetSizePixel().Height());
}

void MetaToInstructionConverter::EmitEllipse(const tools::Rectangle& rRect,
                                             RenderInstructionSink& rSink, int pageNum)
{
    BuildEllipseInstruction(rSink, pageNum, rRect.Left(), rRect.Top(), rRect.GetWidth(),
                            rRect.GetHeight());
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

    BuildSetFontInstruction(rSink, pageNum, s_fontBuf.c_str(), fontSize, fontWeight, fontItalic);
}

void MetaToInstructionConverter::EmitSetTextColor(RenderInstructionSink& rSink, int pageNum)
{
    uint32_t fontColor = (static_cast<uint32_t>(m_aTextColor.GetRed()) << 16)
                         | (static_cast<uint32_t>(m_aTextColor.GetGreen()) << 8)
                         | static_cast<uint32_t>(m_aTextColor.GetBlue());

    BuildSetTextColorInstruction(rSink, pageNum, fontColor);
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

    BuildSetFillColorInstruction(rSink, pageNum, fillColor);
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

    BuildSetLineColorInstruction(rSink, pageNum, lineColor);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
