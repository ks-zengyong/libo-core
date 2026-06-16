/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * 共享的 Node 树遍历器 — 实现
 *
 * 遍历规则：
 *   对 INodesArray 做线性遍历，根据节点类型生成对应指令。
 *   容器型节点 (StartNode/TableNode/SectionNode) 输出一对 START/END，
 *   中间递归处理其子节点。
 *
 * 公共模块: render_common/ — sw 和 aproj/docx 共用
 */

#include "node_walker.h"
#include "node_builder.h"

namespace
{
// 递归遍历容器内的子节点
// 从 startIndex 开始，到 endIndex 结束 (不含 endIndex)
void WalkNodeRange(const INodesArray* pNodes, int startIndex, int endIndex,
                   NodeInstructionSink& rSink, int nestLevel)
{
    for (int i = startIndex; i < endIndex; ++i)
    {
        INode* pNode = pNodes->GetNode(i);
        if (!pNode)
            continue;

        // 重要：先检查 IsTableNode()，再检查 IsStartNode()
        // 因为 SwTableNode 继承自 SwStartNode，IsStartNode() 使用位掩码判断会返回 true
        // 所以必须先检查 IsTableNode()，让 TableNode 走到正确的分支
        if (pNode->IsTableNode())
        {
            // TableNode: 输出 TABLE_START + 递归 + TABLE_END
            int endIdx = pNode->GetEndNodeIndex();
            BuildTableStartInstruction(rSink, pNode->GetIndex(), nestLevel, pNode->GetTableRows(),
                                       pNode->GetTableCols());

            if (endIdx > i + 1)
            {
                WalkNodeRange(pNodes, i + 1, endIdx, rSink, nestLevel + 1);
            }

            BuildTableEndInstruction(rSink, endIdx, nestLevel);
            i = endIdx;
        }
        else if (pNode->IsSectionNode())
        {
            // SectionNode: 输出 SECTION_START + 递归 + SECTION_END
            int endIdx = pNode->GetEndNodeIndex();
            BuildSectionStartInstruction(rSink, pNode->GetIndex(), nestLevel);

            if (endIdx > i + 1)
            {
                WalkNodeRange(pNodes, i + 1, endIdx, rSink, nestLevel + 1);
            }

            BuildSectionEndInstruction(rSink, endIdx, nestLevel);
            i = endIdx;
        }
        else if (pNode->IsStartNode())
        {
            // StartNode: 输出 START_NODE，然后递归到对应 EndNode 之前
            // 注意：TableNode 和 SectionNode 已在上面处理，这里只处理普通 StartNode
            int endIdx = pNode->GetEndNodeIndex();
            BuildStartNodeInstruction(rSink, pNode->GetIndex(), nestLevel,
                                      pNode->GetStartNodeType(), pNode->GetAnchorNodeIndex());

            // 递归子节点
            if (endIdx > i + 1)
            {
                WalkNodeRange(pNodes, i + 1, endIdx, rSink, nestLevel + 1);
            }

            // 输出 EndNode
            INode* pEndNode = pNodes->GetNode(endIdx);
            if (pEndNode)
            {
                BuildEndNodeInstruction(rSink, pEndNode->GetIndex(), nestLevel);
            }

            // 跳过已处理的子节点和 EndNode
            i = endIdx;
        }
        else if (pNode->IsTextNode())
        {
            BuildTextNodeInstruction(rSink, pNode->GetIndex(), nestLevel, pNode->GetText(),
                                     pNode->GetTextLen(), pNode->GetStyleName());
        }
        else if (pNode->IsGrfNode())
        {
            BuildGrfNodeInstruction(rSink, pNode->GetIndex(), nestLevel);
        }
        else if (pNode->IsOLENode())
        {
            BuildOLENodeInstruction(rSink, pNode->GetIndex(), nestLevel);
        }
        else if (pNode->IsEndNode())
        {
            // 孤立的 EndNode (不应出现，但安全处理)
            BuildEndNodeInstruction(rSink, pNode->GetIndex(), nestLevel);
        }
        // 其他未知类型: 忽略
    }
}

} // namespace

void WalkNodesAndLog(const INodesArray* pNodes, NodeInstructionSink& rSink)
{
    if (!pNodes)
        return;

    // 从 Body 区域开始遍历 (跳过 PostIts/Inserts/Autotext/Redlines 等)
    int bodyStart = pNodes->GetBodyStartIndex();
    int bodyEnd = pNodes->GetBodyEndIndex();

    WalkNodeRange(pNodes, bodyStart, bodyEnd, rSink, /*nestLevel=*/0);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
