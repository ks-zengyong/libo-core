/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * 共享的渲染指令 TSV 格式化输出 — 实现
 *
 * 公共模块: render_common/ — sw 和 aproj/docx 共用
 */

#include "render_format.h"
#include <string>

// 转义字符串中的换行符和制表符，确保 TSV 每条指令一行
static std::string EscapeForTsv(const char* s)
{
    if (!s)
        return {};
    std::string result;
    for (; *s; ++s)
    {
        switch (*s)
        {
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            case '"':
                result += "\\\"";
                break;
            default:
                if (static_cast<unsigned char>(*s) < 0x20)
                    result += ' ';
                else
                    result += *s;
                break;
        }
    }
    return result;
}

void WriteInstructionToStream(std::ostream& out, const RenderInstruction& inst)
{
    out << RenderCmdTypeName(inst.type);
    switch (inst.type)
    {
        case RenderCmdType::PAGE_START:
            out << "\t" << inst.pageNum << "\t" << inst.width << "\t" << inst.height;
            break;
        case RenderCmdType::PAGE_END:
            out << "\t" << inst.pageNum;
            break;
        case RenderCmdType::TEXT_FRAME:
        case RenderCmdType::TEXT_LINE:
        case RenderCmdType::TEXT_RUN:
            out << "\t" << inst.pageNum << "\t" << inst.x << "\t" << inst.y << "\t" << inst.width
                << "\t" << inst.height << "\t\"" << EscapeForTsv(inst.text) << "\""
                << "\t" << EscapeForTsv(inst.fontName) << "\t" << inst.fontSize << "\t"
                << inst.fontColor << "\t" << static_cast<int>(inst.fontWeight) << "\t"
                << static_cast<int>(inst.fontItalic) << "\t" << static_cast<int>(inst.underline)
                << "\t" << static_cast<int>(inst.strikeout) << "\t"
                << EscapeForTsv(inst.styleName);
            break;
        case RenderCmdType::TABLE_FRAME:
        case RenderCmdType::TABLE_ROW:
        case RenderCmdType::TABLE_CELL:
        case RenderCmdType::IMAGE_FRAME:
        case RenderCmdType::SECTION_FRAME:
        case RenderCmdType::COLUMN_FRAME:
        case RenderCmdType::HEADER_FRAME:
        case RenderCmdType::FOOTER_FRAME:
        case RenderCmdType::FOOTNOTE_CONT_FRAME:
        case RenderCmdType::FOOTNOTE_FRAME:
        case RenderCmdType::FLY_FRAME:
        case RenderCmdType::RECT:
        case RenderCmdType::POLYGON:
        case RenderCmdType::BITMAP:
        case RenderCmdType::ELLIPSE:
            out << "\t" << inst.pageNum << "\t" << inst.x << "\t" << inst.y << "\t" << inst.width
                << "\t" << inst.height;
            break;
        case RenderCmdType::LINE:
        case RenderCmdType::POLYLINE:
            out << "\t" << inst.pageNum << "\t" << inst.x << "\t" << inst.y << "\t"
                << inst.width // x2
                << "\t" << inst.height; // y2
            break;
        // 状态变更指令
        case RenderCmdType::SET_FONT:
            out << "\t" << inst.pageNum << "\t" << (inst.fontName ? inst.fontName : "") << "\t"
                << inst.fontSize << "\t" << static_cast<int>(inst.fontWeight) << "\t"
                << static_cast<int>(inst.fontItalic);
            break;
        case RenderCmdType::SET_TEXT_COLOR:
        case RenderCmdType::SET_FILL_COLOR:
        case RenderCmdType::SET_LINE_COLOR:
            out << "\t" << inst.pageNum << "\t" << inst.fontColor;
            break;
        case RenderCmdType::SET_CLIP_REGION:
        case RenderCmdType::PUSH:
        case RenderCmdType::POP:
            out << "\t" << inst.pageNum;
            break;
    }
    out << "\n";
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */