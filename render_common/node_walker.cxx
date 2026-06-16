/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * 共享的 Node 树遍历器 — 实现
 *
 * 遍历规则：
 *   对 INodesArray 做线性遍历，根据节点类型生成对应指令。
 *   容器型节点 (StartNode/TableNode/SectionNode) 输出一对 START/END，
 *   中间递归处理其子节点。
 *
 *   锚点引用展示（两阶段遍历：
 *   阶段 1：收集所有 Fly 节区的 anchor 映射 (anchorNodeIndex -> [flyStartNodeIndex, ...])
 *   阶段 2：正常遍历，在锚点节点处缩进展示引用的 Fly 节区内容
 *
 * 公共模块: render_common/ — sw 和 aproj/docx 共用
 */

#include "node_walker.h"
#include "node_builder.h"

#include <map>
#include <vector>

namespace
{

// 递归遍历容器内的子节点
// 从 startIndex 开始，到 endIndex 结束 (不含 endIndex)
void WalkNodeRange(const INodesArray* pNodes, int startIndex, int endIndex,
                   NodeInstructionSink& rSink, int nestLevel,
                   const std::map<int, std::vector<int>>* pAnchorMap = nullptr)
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
                WalkNodeRange(pNodes, i + 1, endIdx, rSink, nestLevel + 1, pAnchorMap);
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
                WalkNodeRange(pNodes, i + 1, endIdx, rSink, nestLevel + 1, pAnchorMap);
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
                WalkNodeRange(pNodes, i + 1, endIdx, rSink, nestLevel + 1, pAnchorMap);
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

        // ── 锚点引用展开：在当前节点输出后，检查是否有 Fly anchor 指向它
        if (pAnchorMap)
        {
            auto it = pAnchorMap->find(pNode->GetIndex());
            if (it != pAnchorMap->end())
            {
                // 此节点是 Fly 锚点，缩进展示所有引用的 Fly 节区内容
                for (int flyStartIdx : it->second)
                {
                    INode* pFlyNode = pNodes->GetNode(flyStartIdx);
                    if (!pFlyNode || !pFlyNode->IsStartNode())
                        continue;

                    int flyEndIdx = pFlyNode->GetEndNodeIndex();
                    if (flyEndIdx <= flyStartIdx)
                        continue;

                    // 输出锚点引用标记和 Fly 节区内容
                    BuildAnchorRefStartInstruction(rSink, pFlyNode->GetIndex(), nestLevel, flyStartIdx);
                    if (flyEndIdx > flyStartIdx + 1)
                    {
                        WalkNodeRange(pNodes, flyStartIdx + 1, flyEndIdx, rSink,
                                      nestLevel + 2, nullptr);
                    }
                    BuildAnchorRefEndInstruction(rSink, flyEndIdx, nestLevel);
                }
            }
        }

        delete pNode;
    }
}

} // namespace

void WalkNodesAndLog(const INodesArray* pNodes, NodeInstructionSink& rSink)
{
    if (!pNodes)
        return;

    int bodyStart = pNodes->GetBodyStartIndex();
    int bodyEnd = pNodes->GetBodyEndIndex();

    // ── 阶段 1：收集 Fly anchor 映射
    // anchorNodeIndex -> [flyStartNodeIndex, ...]
    std::map<int, std::vector<int>> anchorMap;

    for (int i = bodyStart; i < bodyEnd; ++i)
    {
        INode* pNode = pNodes->GetNode(i);
        if (!pNode)
            continue;

        // 只处理 Fly 类型的 StartNode（GetStartNodeType() == 2）
        if (pNode->IsStartNode() && !pNode->IsTableNode() && !pNode->IsSectionNode())
        {
            if (pNode->GetStartNodeType() == 2) // Fly
            {
                int anchorIdx = pNode->GetAnchorNodeIndex();
                if (anchorIdx >= 0)
                {
                    anchorMap[anchorIdx].push_back(pNode->GetIndex());
                }
            }
        }
        delete pNode;
    }

    // ── 阶段 2：正常遍历，在锚点节点处缩进展示引用的 Fly 节区内容
    WalkNodeRange(pNodes, bodyStart, bodyEnd, rSink, /*nestLevel=*/0, &anchorMap);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
