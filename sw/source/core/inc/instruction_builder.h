/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * 共享的 RenderInstruction 构建函数
 *
 * LibreOffice (meta_to_instruction.cxx) 和 aproj (render_output_device.cpp)
 * 共用此模块，确保指令构建逻辑绝对一致。
 *
 * 所有参数使用原始类型（int, const char*, uint32_t），
 * 不依赖任何 VCL 或 aproj 类型，两端可直接调用。
 */

#ifndef INCLUDED_SW_SOURCE_CORE_INC_INSTRUCTION_BUILDER_H
#define INCLUDED_SW_SOURCE_CORE_INC_INSTRUCTION_BUILDER_H

#include "render_instruction.h"

// ── 绘制指令构建 ──

inline void BuildRectInstruction(RenderInstructionSink& rSink, int pageNum, int x, int y, int w,
                                 int h)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::RECT;
    inst.pageNum = pageNum;
    inst.x = x;
    inst.y = y;
    inst.width = w;
    inst.height = h;
    rSink.OnInstruction(inst);
}

inline void BuildLineInstruction(RenderInstructionSink& rSink, int pageNum, int x1, int y1, int x2,
                                 int y2)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::LINE;
    inst.pageNum = pageNum;
    inst.x = x1;
    inst.y = y1;
    inst.width = x2;
    inst.height = y2;
    rSink.OnInstruction(inst);
}

inline void BuildEllipseInstruction(RenderInstructionSink& rSink, int pageNum, int x, int y, int w,
                                    int h)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::ELLIPSE;
    inst.pageNum = pageNum;
    inst.x = x;
    inst.y = y;
    inst.width = w;
    inst.height = h;
    rSink.OnInstruction(inst);
}

inline void BuildBitmapInstruction(RenderInstructionSink& rSink, int pageNum, int x, int y, int w,
                                   int h)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::BITMAP;
    inst.pageNum = pageNum;
    inst.x = x;
    inst.y = y;
    inst.width = w;
    inst.height = h;
    rSink.OnInstruction(inst);
}

inline void BuildTextRunInstruction(RenderInstructionSink& rSink, int pageNum, int x, int y,
                                    const char* text, int textLen, const char* fontName,
                                    int fontSize, uint32_t fontColor, uint8_t fontWeight,
                                    uint8_t fontItalic)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::TEXT_RUN;
    inst.pageNum = pageNum;
    inst.x = x;
    inst.y = y;
    inst.text = text;
    inst.textLen = textLen;
    inst.fontName = fontName;
    inst.fontSize = fontSize;
    inst.fontColor = fontColor;
    inst.fontWeight = fontWeight;
    inst.fontItalic = fontItalic;
    rSink.OnInstruction(inst);
}

// ── 状态指令构建 ──

inline void BuildSetFontInstruction(RenderInstructionSink& rSink, int pageNum, const char* fontName,
                                    int fontSize, uint8_t fontWeight, uint8_t fontItalic)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::SET_FONT;
    inst.pageNum = pageNum;
    inst.fontName = fontName;
    inst.fontSize = fontSize;
    inst.fontWeight = fontWeight;
    inst.fontItalic = fontItalic;
    rSink.OnInstruction(inst);
}

inline void BuildSetTextColorInstruction(RenderInstructionSink& rSink, int pageNum, uint32_t color)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::SET_TEXT_COLOR;
    inst.pageNum = pageNum;
    inst.fontColor = color;
    rSink.OnInstruction(inst);
}

inline void BuildSetFillColorInstruction(RenderInstructionSink& rSink, int pageNum, uint32_t color)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::SET_FILL_COLOR;
    inst.pageNum = pageNum;
    inst.fontColor = color; // 复用 fontColor 字段存储颜色值
    rSink.OnInstruction(inst);
}

inline void BuildSetLineColorInstruction(RenderInstructionSink& rSink, int pageNum, uint32_t color)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::SET_LINE_COLOR;
    inst.pageNum = pageNum;
    inst.fontColor = color; // 复用 fontColor 字段存储颜色值
    rSink.OnInstruction(inst);
}

inline void BuildSetClipRegionInstruction(RenderInstructionSink& rSink, int pageNum)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::SET_CLIP_REGION;
    inst.pageNum = pageNum;
    rSink.OnInstruction(inst);
}

inline void BuildPushInstruction(RenderInstructionSink& rSink, int pageNum)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::PUSH;
    inst.pageNum = pageNum;
    rSink.OnInstruction(inst);
}

inline void BuildPopInstruction(RenderInstructionSink& rSink, int pageNum)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::POP;
    inst.pageNum = pageNum;
    rSink.OnInstruction(inst);
}

#endif // INCLUDED_SW_SOURCE_CORE_INC_INSTRUCTION_BUILDER_H

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
