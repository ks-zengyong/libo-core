/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * 共享的 NodeInstruction 构建函数
 *
 * LibreOffice 和 aproj/docx 共用此模块，确保节点指令构建逻辑绝对一致。
 *
 * 公共模块: render_common/ — sw 和 aproj/docx 都引用此文件
 */

#ifndef INCLUDED_RENDER_COMMON_NODE_BUILDER_H
#define INCLUDED_RENDER_COMMON_NODE_BUILDER_H

#include "node_instruction.h"

// ── 辅助: 设置一条指令的公共字段 ──
inline void FillNodeInst(NodeInstruction& inst, NodeCmdType type, int nodeIndex, int nestLevel)
{
    inst.clear();
    inst.type = type;
    inst.nodeIndex = nodeIndex;
    inst.nestLevel = nestLevel;
}

// ── StartNode (容器 START) ──
inline void BuildStartNodeInstruction(NodeInstructionSink& rSink, int nodeIndex, int nestLevel,
                                      int startNodeType, int anchorNodeIndex = -1)
{
    NodeInstruction inst;
    FillNodeInst(inst, NodeCmdType::START_NODE, nodeIndex, nestLevel);
    inst.startNodeType = startNodeType;
    inst.anchorNodeIndex = anchorNodeIndex;
    rSink.OnInstruction(inst);
}

// ── EndNode (容器 END) ──
inline void BuildEndNodeInstruction(NodeInstructionSink& rSink, int nodeIndex, int nestLevel)
{
    NodeInstruction inst;
    FillNodeInst(inst, NodeCmdType::END_NODE, nodeIndex, nestLevel);
    rSink.OnInstruction(inst);
}

// ── TextNode (非容器) ──
inline void BuildTextNodeInstruction(NodeInstructionSink& rSink, int nodeIndex, int nestLevel,
                                     const char* text, int textLen, const char* styleName)
{
    NodeInstruction inst;
    FillNodeInst(inst, NodeCmdType::TEXT_NODE, nodeIndex, nestLevel);
    inst.text = text;
    inst.textLen = textLen;
    inst.styleName = styleName;
    rSink.OnInstruction(inst);
}

// ── GrfNode (非容器) ──
inline void BuildGrfNodeInstruction(NodeInstructionSink& rSink, int nodeIndex, int nestLevel)
{
    NodeInstruction inst;
    FillNodeInst(inst, NodeCmdType::GRF_NODE, nodeIndex, nestLevel);
    rSink.OnInstruction(inst);
}

// ── OLENode (非容器) ──
inline void BuildOLENodeInstruction(NodeInstructionSink& rSink, int nodeIndex, int nestLevel)
{
    NodeInstruction inst;
    FillNodeInst(inst, NodeCmdType::OLE_NODE, nodeIndex, nestLevel);
    rSink.OnInstruction(inst);
}

// ── TableNode (容器 START/END) ──
inline void BuildTableStartInstruction(NodeInstructionSink& rSink, int nodeIndex, int nestLevel,
                                       int rows, int cols)
{
    NodeInstruction inst;
    FillNodeInst(inst, NodeCmdType::TABLE_START, nodeIndex, nestLevel);
    inst.tableRows = rows;
    inst.tableCols = cols;
    rSink.OnInstruction(inst);
}

inline void BuildTableEndInstruction(NodeInstructionSink& rSink, int nodeIndex, int nestLevel)
{
    NodeInstruction inst;
    FillNodeInst(inst, NodeCmdType::TABLE_END, nodeIndex, nestLevel);
    rSink.OnInstruction(inst);
}

// ── SectionNode (容器 START/END) ──
inline void BuildSectionStartInstruction(NodeInstructionSink& rSink, int nodeIndex, int nestLevel)
{
    NodeInstruction inst;
    FillNodeInst(inst, NodeCmdType::SECTION_START, nodeIndex, nestLevel);
    rSink.OnInstruction(inst);
}

inline void BuildSectionEndInstruction(NodeInstructionSink& rSink, int nodeIndex, int nestLevel)
{
    NodeInstruction inst;
    FillNodeInst(inst, NodeCmdType::SECTION_END, nodeIndex, nestLevel);
    rSink.OnInstruction(inst);
}

#endif // INCLUDED_RENDER_COMMON_NODE_BUILDER_H

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
