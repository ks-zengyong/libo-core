/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * 共享的节点指令 TSV 格式化输出 — 实现
 *
 * 公共模块: render_common/ — sw 和 aproj/docx 共用
 */

#include "node_format.h"
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

void WriteNodeInstructionToStream(std::ostream& out, const NodeInstruction& inst)
{
    // 1) 行首缩进 (由 nestLevel 决定)
    WriteIndent(out, inst.nestLevel);

    // 2) 类型名 + 分类型字段
    out << NodeCmdTypeName(inst.type);

    switch (inst.type)
    {
        case NodeCmdType::START_NODE:
            // START_NODE <nodeIndex> <startNodeType> [anchorNodeIndex]
            // 如果是 Fly 节区 (startNodeType==2) 且有锚点引用，输出 anchorNodeIndex
            out << "\t" << inst.nodeIndex << "\t" << StartNodeTypeName(inst.startNodeType);
            if (inst.startNodeType == 2 && inst.anchorNodeIndex >= 0)
            {
                out << "\tanchor=" << inst.anchorNodeIndex;
            }
            break;

        case NodeCmdType::END_NODE:
            // END_NODE <nodeIndex>
            out << "\t" << inst.nodeIndex;
            break;

        case NodeCmdType::TEXT_NODE:
            // TEXT_NODE <nodeIndex> "text" "styleName"
            out << "\t" << inst.nodeIndex << "\t\"" << EscapeForTsv(inst.text) << "\"\t"
                << EscapeForTsv(inst.styleName);
            break;

        case NodeCmdType::GRF_NODE:
        case NodeCmdType::OLE_NODE:
            // GRF_NODE / OLE_NODE <nodeIndex>
            out << "\t" << inst.nodeIndex;
            break;

        case NodeCmdType::TABLE_START:
            // TABLE_START <nodeIndex> <rows> <cols>
            out << "\t" << inst.nodeIndex << "\t" << inst.tableRows << "\t" << inst.tableCols;
            break;

        case NodeCmdType::TABLE_END:
        case NodeCmdType::SECTION_END:
            // TABLE_END / SECTION_END <nodeIndex>
            out << "\t" << inst.nodeIndex;
            break;

        case NodeCmdType::SECTION_START:
            // SECTION_START <nodeIndex>
            out << "\t" << inst.nodeIndex;
            break;

        case NodeCmdType::ANCHOR_REF_START:
            // ANCHOR_REF_START <nodeIndex> (flyNodeIndex=X)
            out << "\t" << inst.nodeIndex << "\t(flyNodeIndex=" << inst.flyNodeIndex << ")";
            break;

        case NodeCmdType::ANCHOR_REF_END:
            // ANCHOR_REF_END <nodeIndex>
            out << "\t" << inst.nodeIndex;
            break;

        default:
            out << "\t" << inst.nodeIndex;
            break;
    }

    // 3) 行尾
    out << "\n";
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
