/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * 共享的 RenderInstruction 构建函数
 *
 * LibreOffice (meta_to_instruction.cxx) 和 aproj (render_output_device.cpp)
 * 共用此模块，确保指令构建逻辑绝对一致。
 *
 * 所有参数使用原始类型 (int, const char*, uint32_t)，
 * 不依赖任何 VCL 或 aproj 类型，两端可直接调用。
 *
 * 容器型节点 (Page / Section / Column / Table / TabRow / TabCell /
 * Header / Footer / FootnoteCont / Fly) 都有各自的 *_START / *_END
 * 构建函数，WalkFrameTreeAndLog 会在递归前后分别调用。
 * nestLevel 由遍历器维护，对应 TSV 行首的缩进。
 *
 * 公共模块: render_common/ — sw 和 aproj/docx 都引用此文件
 */

#ifndef INCLUDED_RENDER_COMMON_INSTRUCTION_BUILDER_H
#define INCLUDED_RENDER_COMMON_INSTRUCTION_BUILDER_H

#include "render_instruction.h"

// ── 辅助: 设置一条指令的公共字段 (pageNum / nestLevel / 矩形) ──
inline void FillRectInst(RenderInstruction& inst, RenderCmdType type, int pageNum, int nestLevel,
                         int x, int y, int w, int h)
{
    inst.type = type;
    inst.pageNum = pageNum;
    inst.nestLevel = nestLevel;
    inst.x = x;
    inst.y = y;
    inst.width = w;
    inst.height = h;
}

// ── VCL 绘制层指令构建 ──

inline void BuildRectInstruction(RenderInstructionSink& rSink, int pageNum, int nestLevel, int x,
                                 int y, int w, int h)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    FillRectInst(inst, RenderCmdType::RECT, pageNum, nestLevel, x, y, w, h);
    rSink.OnInstruction(inst);
}

inline void BuildLineInstruction(RenderInstructionSink& rSink, int pageNum, int nestLevel, int x1,
                                 int y1, int x2, int y2)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    FillRectInst(inst, RenderCmdType::LINE, pageNum, nestLevel, x1, y1, x2, y2);
    rSink.OnInstruction(inst);
}

inline void BuildEllipseInstruction(RenderInstructionSink& rSink, int pageNum, int nestLevel, int x,
                                    int y, int w, int h)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    FillRectInst(inst, RenderCmdType::ELLIPSE, pageNum, nestLevel, x, y, w, h);
    rSink.OnInstruction(inst);
}

inline void BuildBitmapInstruction(RenderInstructionSink& rSink, int pageNum, int nestLevel, int x,
                                   int y, int w, int h)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    FillRectInst(inst, RenderCmdType::BITMAP, pageNum, nestLevel, x, y, w, h);
    rSink.OnInstruction(inst);
}

inline void BuildTextRunInstruction(RenderInstructionSink& rSink, int pageNum, int nestLevel, int x,
                                    int y, const char* text, int textLen, const char* fontName,
                                    int fontSize, uint32_t fontColor, uint8_t fontWeight,
                                    uint8_t fontItalic)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::TEXT_RUN;
    inst.pageNum = pageNum;
    inst.nestLevel = nestLevel;
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

// ── Frame 层语义指令构建 (非容器) ──

inline void BuildTextFrameInstruction(RenderInstructionSink& rSink, int pageNum, int nestLevel,
                                      int x, int y, int w, int h, const char* text, int textLen,
                                      const char* fontName, int fontSize, uint32_t fontColor,
                                      uint8_t fontWeight, uint8_t fontItalic, const char* styleName)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::TEXT_FRAME;
    inst.pageNum = pageNum;
    inst.nestLevel = nestLevel;
    inst.x = x;
    inst.y = y;
    inst.width = w;
    inst.height = h;
    inst.text = text;
    inst.textLen = textLen;
    inst.fontName = fontName;
    inst.fontSize = fontSize;
    inst.fontColor = fontColor;
    inst.fontWeight = fontWeight;
    inst.fontItalic = fontItalic;
    inst.styleName = styleName;
    rSink.OnInstruction(inst);
}

inline void BuildTextLineInstruction(RenderInstructionSink& rSink, int pageNum, int nestLevel,
                                     int x, int y, int w, int h, const char* text, int textLen,
                                     const char* fontName, int fontSize, uint32_t fontColor,
                                     uint8_t fontWeight, uint8_t fontItalic, const char* styleName)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::TEXT_LINE;
    inst.pageNum = pageNum;
    inst.nestLevel = nestLevel;
    inst.x = x;
    inst.y = y;
    inst.width = w;
    inst.height = h;
    inst.text = text;
    inst.textLen = textLen;
    inst.fontName = fontName;
    inst.fontSize = fontSize;
    inst.fontColor = fontColor;
    inst.fontWeight = fontWeight;
    inst.fontItalic = fontItalic;
    inst.styleName = styleName;
    rSink.OnInstruction(inst);
}

inline void BuildImageFrameInstruction(RenderInstructionSink& rSink, int pageNum, int nestLevel,
                                       int x, int y, int w, int h)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    FillRectInst(inst, RenderCmdType::IMAGE_FRAME, pageNum, nestLevel, x, y, w, h);
    rSink.OnInstruction(inst);
}

inline void BuildFootnoteFrameInstruction(RenderInstructionSink& rSink, int pageNum, int nestLevel,
                                          int x, int y, int w, int h)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    FillRectInst(inst, RenderCmdType::FOOTNOTE_FRAME, pageNum, nestLevel, x, y, w, h);
    rSink.OnInstruction(inst);
}

// ── Frame 层语义指令构建 (容器 START / END) ──
// 容器: Page / Section / Column / Table / TabRow / TabCell / Header / Footer /
//       FootnoteCont / Fly

// page 容器: 页面宽高
inline void BuildPageStartInstruction(RenderInstructionSink& rSink, int pageNum, int nestLevel,
                                      int width, int height)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    FillRectInst(inst, RenderCmdType::PAGE_START, pageNum, nestLevel, 0, 0, width, height);
    rSink.OnInstruction(inst);
}
inline void BuildPageEndInstruction(RenderInstructionSink& rSink, int pageNum, int nestLevel)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::PAGE_END;
    inst.pageNum = pageNum;
    inst.nestLevel = nestLevel;
    rSink.OnInstruction(inst);
}

// section / column 容器: 节/列的矩形
inline void BuildSectionStartInstruction(RenderInstructionSink& rSink, int pageNum, int nestLevel,
                                         int x, int y, int w, int h)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    FillRectInst(inst, RenderCmdType::SECTION_START, pageNum, nestLevel, x, y, w, h);
    rSink.OnInstruction(inst);
}
inline void BuildSectionEndInstruction(RenderInstructionSink& rSink, int pageNum, int nestLevel)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::SECTION_END;
    inst.pageNum = pageNum;
    inst.nestLevel = nestLevel;
    rSink.OnInstruction(inst);
}

inline void BuildColumnStartInstruction(RenderInstructionSink& rSink, int pageNum, int nestLevel,
                                        int x, int y, int w, int h)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    FillRectInst(inst, RenderCmdType::COLUMN_START, pageNum, nestLevel, x, y, w, h);
    rSink.OnInstruction(inst);
}
inline void BuildColumnEndInstruction(RenderInstructionSink& rSink, int pageNum, int nestLevel)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::COLUMN_END;
    inst.pageNum = pageNum;
    inst.nestLevel = nestLevel;
    rSink.OnInstruction(inst);
}

// header / footer
inline void BuildHeaderStartInstruction(RenderInstructionSink& rSink, int pageNum, int nestLevel,
                                        int x, int y, int w, int h)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    FillRectInst(inst, RenderCmdType::HEADER_START, pageNum, nestLevel, x, y, w, h);
    rSink.OnInstruction(inst);
}
inline void BuildHeaderEndInstruction(RenderInstructionSink& rSink, int pageNum, int nestLevel)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::HEADER_END;
    inst.pageNum = pageNum;
    inst.nestLevel = nestLevel;
    rSink.OnInstruction(inst);
}

inline void BuildFooterStartInstruction(RenderInstructionSink& rSink, int pageNum, int nestLevel,
                                        int x, int y, int w, int h)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    FillRectInst(inst, RenderCmdType::FOOTER_START, pageNum, nestLevel, x, y, w, h);
    rSink.OnInstruction(inst);
}
inline void BuildFooterEndInstruction(RenderInstructionSink& rSink, int pageNum, int nestLevel)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::FOOTER_END;
    inst.pageNum = pageNum;
    inst.nestLevel = nestLevel;
    rSink.OnInstruction(inst);
}

// footnote 容器
inline void BuildFootnoteContStartInstruction(RenderInstructionSink& rSink, int pageNum,
                                              int nestLevel, int x, int y, int w, int h)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    FillRectInst(inst, RenderCmdType::FOOTNOTE_CONT_START, pageNum, nestLevel, x, y, w, h);
    rSink.OnInstruction(inst);
}
inline void BuildFootnoteContEndInstruction(RenderInstructionSink& rSink, int pageNum,
                                            int nestLevel)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::FOOTNOTE_CONT_END;
    inst.pageNum = pageNum;
    inst.nestLevel = nestLevel;
    rSink.OnInstruction(inst);
}

// table / row / cell
inline void BuildTableStartInstruction(RenderInstructionSink& rSink, int pageNum, int nestLevel,
                                       int x, int y, int w, int h)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    FillRectInst(inst, RenderCmdType::TABLE_START, pageNum, nestLevel, x, y, w, h);
    rSink.OnInstruction(inst);
}
inline void BuildTableEndInstruction(RenderInstructionSink& rSink, int pageNum, int nestLevel)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::TABLE_END;
    inst.pageNum = pageNum;
    inst.nestLevel = nestLevel;
    rSink.OnInstruction(inst);
}

inline void BuildTableRowStartInstruction(RenderInstructionSink& rSink, int pageNum, int nestLevel,
                                          int x, int y, int w, int h)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    FillRectInst(inst, RenderCmdType::TABLEROW_START, pageNum, nestLevel, x, y, w, h);
    rSink.OnInstruction(inst);
}
inline void BuildTableRowEndInstruction(RenderInstructionSink& rSink, int pageNum, int nestLevel)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::TABLEROW_END;
    inst.pageNum = pageNum;
    inst.nestLevel = nestLevel;
    rSink.OnInstruction(inst);
}

inline void BuildTableCellStartInstruction(RenderInstructionSink& rSink, int pageNum, int nestLevel,
                                           int x, int y, int w, int h)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    FillRectInst(inst, RenderCmdType::TABLECELL_START, pageNum, nestLevel, x, y, w, h);
    rSink.OnInstruction(inst);
}
inline void BuildTableCellEndInstruction(RenderInstructionSink& rSink, int pageNum, int nestLevel)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::TABLECELL_END;
    inst.pageNum = pageNum;
    inst.nestLevel = nestLevel;
    rSink.OnInstruction(inst);
}

// floating object (Fly): 最关键的新增容器
inline void BuildFlyStartInstruction(RenderInstructionSink& rSink, int pageNum, int nestLevel,
                                     int x, int y, int w, int h)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    FillRectInst(inst, RenderCmdType::FLY_START, pageNum, nestLevel, x, y, w, h);
    rSink.OnInstruction(inst);
}
inline void BuildFlyEndInstruction(RenderInstructionSink& rSink, int pageNum, int nestLevel)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::FLY_END;
    inst.pageNum = pageNum;
    inst.nestLevel = nestLevel;
    rSink.OnInstruction(inst);
}

// ── 状态变更指令构建 ──

inline void BuildSetFontInstruction(RenderInstructionSink& rSink, int pageNum, int nestLevel,
                                    const char* fontName, int fontSize, uint8_t fontWeight,
                                    uint8_t fontItalic)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::SET_FONT;
    inst.pageNum = pageNum;
    inst.nestLevel = nestLevel;
    inst.fontName = fontName;
    inst.fontSize = fontSize;
    inst.fontWeight = fontWeight;
    inst.fontItalic = fontItalic;
    rSink.OnInstruction(inst);
}

inline void BuildSetTextColorInstruction(RenderInstructionSink& rSink, int pageNum, int nestLevel,
                                         uint32_t color)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::SET_TEXT_COLOR;
    inst.pageNum = pageNum;
    inst.nestLevel = nestLevel;
    inst.fontColor = color;
    rSink.OnInstruction(inst);
}

inline void BuildSetFillColorInstruction(RenderInstructionSink& rSink, int pageNum, int nestLevel,
                                         uint32_t color)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::SET_FILL_COLOR;
    inst.pageNum = pageNum;
    inst.nestLevel = nestLevel;
    inst.fontColor = color;
    rSink.OnInstruction(inst);
}

inline void BuildSetLineColorInstruction(RenderInstructionSink& rSink, int pageNum, int nestLevel,
                                         uint32_t color)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::SET_LINE_COLOR;
    inst.pageNum = pageNum;
    inst.nestLevel = nestLevel;
    inst.fontColor = color;
    rSink.OnInstruction(inst);
}

inline void BuildSetClipRegionInstruction(RenderInstructionSink& rSink, int pageNum, int nestLevel)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::SET_CLIP_REGION;
    inst.pageNum = pageNum;
    inst.nestLevel = nestLevel;
    rSink.OnInstruction(inst);
}

inline void BuildPushInstruction(RenderInstructionSink& rSink, int pageNum, int nestLevel)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::PUSH;
    inst.pageNum = pageNum;
    inst.nestLevel = nestLevel;
    rSink.OnInstruction(inst);
}

inline void BuildPopInstruction(RenderInstructionSink& rSink, int pageNum, int nestLevel)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::POP;
    inst.pageNum = pageNum;
    inst.nestLevel = nestLevel;
    rSink.OnInstruction(inst);
}

#endif // INCLUDED_RENDER_COMMON_INSTRUCTION_BUILDER_H

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
