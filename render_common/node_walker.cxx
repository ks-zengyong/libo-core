/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * 共享的 Node 树遍历器 — 实现
 *
 * 遍历规则：
 *   对 INodesArray 做线性遍历，根据节点类型生成对应指令。
 *   容器型节点 (StartNode/TableNode/SectionNode) 输出一对 START/END，
 *   中间递归处理其子节点。
 *
 *   锚点引用处理（不内联展开，仅在节点行标注 refs=...）：
 *   阶段 1：收集所有 Fly 节区的 anchor 映射 (anchorNodeIndex -> [flyStartIdx, ...])
 *   阶段 2：正常遍历，每个节点输出时若有 Fly 引用它，则追加 refs=...
 *
 * 公共模块: render_common/ — sw 和 aproj/docx 共用
 */

#include "node_walker.h"
#include "node_builder.h"

#include <map>
#include <vector>
#include <string>
#include <sstream>

namespace
{

// 递归遍历容器内的子节点
// 从 startIndex 开始，到 endIndex 结束 (不含 endIndex)
void WalkNodeRange(const INodesArray* pNodes, int startIndex, int endIndex,
                   NodeInstructionSink& rSink, int nestLevel,
                   const std::map<int, std::vector<int>>* pAnchorMap)
{
    for (int i = startIndex; i < endIndex; ++i)
    {
        INode* pNode = pNodes->GetNode(i);
        if (!pNode)
            continue;

        // 查找当前节点是否被 Fly 节区引用
        const char* refs = nullptr;
        std::string refsStr;
        if (pAnchorMap)
        {
            auto it = pAnchorMap->find(pNode->GetIndex());
            if (it != pAnchorMap->end() && !it->second.empty())
            {
                std::ostringstream oss;
                for (size_t j = 0; j < it->second.size(); ++j)
                {
                    if (j > 0)
                        oss << ",";
                    oss << it->second[j];
                }
                refsStr = oss.str();
                refs = refsStr.c_str();
            }
        }

        // 重要：先检查 IsTableNode()，再检查 IsStartNode()
        // 因为 SwTableNode 继承自 SwStartNode，IsStartNode() 使用位掩码判断会返回 true
        // 所以必须先检查 IsTableNode()，让 TableNode 走到正确的分支
        if (pNode->IsTableNode())
        {
            // TableNode: 输出 TABLE_START + 递归 + TABLE_END
            int endIdx = pNode->GetEndNodeIndex();
            BuildTableStartInstruction(rSink, pNode->GetIndex(), nestLevel,
                                       pNode->GetTableRows(), pNode->GetTableCols(),
                                       refs);

            // 防御：endIdx 无效（<= i）时跳过递归，避免 i = -1 导致崩溃
            if (endIdx > i + 1)
            {
                WalkNodeRange(pNodes, i + 1, endIdx, rSink, nestLevel + 1, pAnchorMap);
            }

            if (endIdx > i)
            {
                BuildTableEndInstruction(rSink, endIdx, nestLevel);
                i = endIdx;
            }
            else
            {
                // endIdx 无效，跳过此 TableNode
                i = i + 1;
            }
        }
        else if (pNode->IsSectionNode())
        {
            // SectionNode: 输出 SECTION_START + 递归 + SECTION_END
            int endIdx = pNode->GetEndNodeIndex();
            BuildSectionStartInstruction(rSink, pNode->GetIndex(), nestLevel, refs);

            if (endIdx > i + 1)
            {
                WalkNodeRange(pNodes, i + 1, endIdx, rSink, nestLevel + 1, pAnchorMap);
            }

            if (endIdx > i)
            {
                BuildSectionEndInstruction(rSink, endIdx, nestLevel);
                i = endIdx;
            }
            else
            {
                // endIdx 无效，跳过此 SectionNode
                i = i + 1;
            }
        }
        else if (pNode->IsStartNode())
        {
            // StartNode: 输出 START_NODE，然后递归到对应 EndNode 之前
            // 注意：TableNode 和 SectionNode 已在上面处理，这里只处理普通 StartNode
            int endIdx = pNode->GetEndNodeIndex();
            BuildStartNodeInstruction(rSink, pNode->GetIndex(), nestLevel,
                                      pNode->GetStartNodeType(),
                                      pNode->GetAnchorNodeIndex(),
                                      refs);

            // 递归子节点
            if (endIdx > i + 1)
            {
                WalkNodeRange(pNodes, i + 1, endIdx, rSink, nestLevel + 1, pAnchorMap);
            }

            // 输出 EndNode（仅当 endIdx 有效时）
            if (endIdx > i)
            {
                INode* pEndNode = pNodes->GetNode(endIdx);
                if (pEndNode)
                {
                    BuildEndNodeInstruction(rSink, pEndNode->GetIndex(), nestLevel);
                }
                // 跳过已处理的子节点和 EndNode
                i = endIdx;
            }
            else
            {
                // endIdx 无效，跳过此 StartNode
                i = i + 1;
            }
        }
        else if (pNode->IsTextNode())
        {
            BuildTextNodeInstruction(rSink, pNode->GetIndex(), nestLevel, pNode->GetText(),
                                     pNode->GetTextLen(), pNode->GetStyleName(), refs);
        }
        else if (pNode->IsGrfNode())
        {
            BuildGrfNodeInstruction(rSink, pNode->GetIndex(), nestLevel, refs);
        }
        else if (pNode->IsOLENode())
        {
            BuildOLENodeInstruction(rSink, pNode->GetIndex(), nestLevel, refs);
        }
        else if (pNode->IsEndNode())
        {
            // 孤立的 EndNode (不应出现，但安全处理)
            BuildEndNodeInstruction(rSink, pNode->GetIndex(), nestLevel, refs);
        }
        // 其他未知类型: 忽略

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

    // ── 阶段 1：收集 Fly anchor 反向映射
    //   anchorNodeIndex -> [flyStartNodeIndex, ...]
    //   含义：哪些 Fly 节区以这个节点为锚点
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

    // ── 阶段 2：正常遍历（不在锚点处内联展开，仅在节点行追加 refs=...）
    WalkNodeRange(pNodes, bodyStart, bodyEnd, rSink, /*nestLevel=*/0, &anchorMap);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
