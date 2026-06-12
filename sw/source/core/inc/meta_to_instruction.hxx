/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * MetaAction → RenderInstruction 转换器
 * 将 GDIMetaFile 录制的 MetaAction 序列转换为共享的 RenderInstruction 格式
 */

#pragma once

#include "render_instruction.h"
#include <vcl/gdimtf.hxx>
#include <vcl/metaactiontypes.hxx>
#include <vcl/metaact.hxx>
#include <vcl/font.hxx>
#include <tools/color.hxx>

/**
 * MetaToInstructionConverter — 将 GDIMetaFile 的 MetaAction 转换为 RenderInstruction
 *
 * 维护绘制状态上下文（当前字体、颜色等），遇到绘制动作时
 * 构造带有完整属性的 RenderInstruction 并输出到 RenderInstructionSink。
 *
 * 状态变更动作（SetFont/SetTextColor 等）输出为 SET_FONT/SET_TEXT_COLOR 指令，
 * 供消费方重建绘制上下文。
 */
class MetaToInstructionConverter
{
public:
    MetaToInstructionConverter() = default;

    // 转换整个 GDIMetaFile，输出到 sink
    // pageNum: 当前页码，附加到每条指令
    void Convert(GDIMetaFile& rMtf, RenderInstructionSink& rSink, int pageNum);

private:
    // 状态上下文
    vcl::Font m_aCurrentFont;
    Color m_aTextColor;
    Color m_aFillColor;
    Color m_aLineColor;
    bool m_bFillSet = false;
    bool m_bLineSet = false;

    // 单个动作转换
    void ConvertAction(const MetaAction* pAction, RenderInstructionSink& rSink, int pageNum);

    // 各类型 emit 方法
    void EmitText(const Point& rPt, const OUString& rText, sal_Int32 nIndex, sal_Int32 nLen,
                  RenderInstructionSink& rSink, int pageNum);
    void EmitRect(const tools::Rectangle& rRect, RenderInstructionSink& rSink, int pageNum);
    void EmitLine(const Point& rStart, const Point& rEnd, RenderInstructionSink& rSink,
                  int pageNum);
    void EmitBitmap(const Point& rPt, const Bitmap& rBmp, RenderInstructionSink& rSink,
                    int pageNum);
    void EmitEllipse(const tools::Rectangle& rRect, RenderInstructionSink& rSink, int pageNum);

    // 状态变更 emit
    void EmitSetFont(RenderInstructionSink& rSink, int pageNum);
    void EmitSetTextColor(RenderInstructionSink& rSink, int pageNum);
    void EmitSetFillColor(RenderInstructionSink& rSink, int pageNum);
    void EmitSetLineColor(RenderInstructionSink& rSink, int pageNum);
};

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
