/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * 共享的渲染指令 TSV 格式化输出 — 实现
 *
 * 每条指令独占一行，行首按照 nestLevel 输出对应数量的空格
 * （每级 2 个空格），之后才是 "TYPE\t字段1\t字段2\t..."。
 * 行尾以 '\n' 结束。文本字段内包含的制表符/换行符会被转义。
 *
 * 公共模块: render_common/ — sw 和 aproj/docx 共用
 */

#include "render_format.h"
#include <string>

namespace
{
// 每级缩进的空格数 (2 空格 = 1 level)
constexpr int kSpacesPerLevel = 2;

void WriteIndent(std::ostream& out, int nestLevel)
{
    if (nestLevel <= 0)
        return;
    int n = nestLevel * kSpacesPerLevel;
    for (int i = 0; i < n; ++i)
        out.put(' ');
}

} // namespace

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
    // 1) 行首缩进 (由 nestLevel 决定)
    WriteIndent(out, inst.nestLevel);

    // 2) 类型名 + 分类型字段
    out << RenderCmdTypeName(inst.type);

    switch (inst.type)
    {
        case RenderCmdType::PAGE_START:
            // PAGE_START <pageNum> <width> <height>  (x,y 恒为 0, 不写)
            out << "\t" << inst.pageNum << "\t" << inst.width << "\t" << inst.height;
            break;
        case RenderCmdType::PAGE_END:
            out << "\t" << inst.pageNum;
            break;

        // 容器型 END: 除了 pageNum 不再写其他字段，避免与 START 重复
        case RenderCmdType::SECTION_END:
        case RenderCmdType::COLUMN_END:
        case RenderCmdType::HEADER_END:
        case RenderCmdType::FOOTER_END:
        case RenderCmdType::FOOTNOTE_CONT_END:
        case RenderCmdType::TABLE_END:
        case RenderCmdType::TABLEROW_END:
        case RenderCmdType::TABLECELL_END:
        case RenderCmdType::FLY_END:
            out << "\t" << inst.pageNum;
            break;

        // 容器型 START: 页面 + 矩形 (x, y, w, h)
        case RenderCmdType::SECTION_START:
        case RenderCmdType::COLUMN_START:
        case RenderCmdType::HEADER_START:
        case RenderCmdType::FOOTER_START:
        case RenderCmdType::FOOTNOTE_CONT_START:
        case RenderCmdType::TABLE_START:
        case RenderCmdType::TABLEROW_START:
        case RenderCmdType::TABLECELL_START:
        case RenderCmdType::FLY_START:
            out << "\t" << inst.pageNum << "\t" << inst.x << "\t" << inst.y << "\t" << inst.width
                << "\t" << inst.height;
            break;

        // 文本 frame / line / run
        case RenderCmdType::TEXT_FRAME:
        case RenderCmdType::TEXT_LINE:
        case RenderCmdType::TEXT_RUN:
            out << "\t" << inst.pageNum << "\t" << inst.x << "\t" << inst.y << "\t" << inst.width
                << "\t" << inst.height << "\t\"" << EscapeForTsv(inst.text) << "\""
                << "\t" << EscapeForTsv(inst.fontName) << "\t" << inst.fontSize << "\t"
                << inst.fontColor << "\t" << static_cast<int>(inst.fontWeight) << "\t"
                << static_cast<int>(inst.fontItalic) << "\t" << static_cast<int>(inst.underline)
                << "\t" << static_cast<int>(inst.strikeout) << "\t" << EscapeForTsv(inst.styleName);
            break;

        // 纯矩形 (image / 图形)
        case RenderCmdType::IMAGE_FRAME:
        case RenderCmdType::FOOTNOTE_FRAME:
        case RenderCmdType::RECT:
        case RenderCmdType::POLYGON:
        case RenderCmdType::BITMAP:
        case RenderCmdType::ELLIPSE:
            out << "\t" << inst.pageNum << "\t" << inst.x << "\t" << inst.y << "\t" << inst.width
                << "\t" << inst.height;
            break;

        // LINE / POLYLINE: (x1, y1) - (x2, y2)
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

    // 3) 行尾
    out << "\n";
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
