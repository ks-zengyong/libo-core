// 简化版 SwNodes 实现，对应 LibreOffice 的 sw/source/core/docnode/nodes.cxx

#include "ndarr.h"
#include "node.h"
#include <cassert>
#include <algorithm>

//===----------------------------------------------------------------------===//
// SwNodes
//===----------------------------------------------------------------------===//

SwNodes::SwNodes(SwDoc& rDoc)
    : m_rMyDoc(rDoc)
    , m_pEndOfPostIts(nullptr)
    , m_pEndOfInserts(nullptr)
{
    InitNodes();
}

SwNodes::~SwNodes()
{
    // 删除所有节点
    // 注意：哨兵节点需要特殊处理
}

void SwNodes::InitNodes()
{
    // 初始化节点数组，创建 LO 风格的基础结构：
    //   [0] START_NODE (Normal)
    //   [1] END_NODE                <- 空节 1 (m_pEndOfPostIts)
    //   [2] START_NODE (Normal)
    //   [3] END_NODE                <- 空节 2 (m_pEndOfInserts)
    //
    // Fly 区和正文区在 ParseBody 中动态创建：
    //   [4-?] Normal 节区：Fly 容器 (m_pEndOfAutotext 指向其 EndNode)
    //   [?-?] 空 Normal 节区 (m_pEndOfRedlines 指向其 EndNode)
    //   [?-?] Normal 节区：正文容器 (m_pEndOfContent 指向其 EndNode)

    // 空 Normal section [0-1]
    auto* pStt1 = new SwStartNode(*this, SwNodeOffset(0), SwNormalStartNode);
    BigPtrArray::Insert(pStt1, 0);
    auto* pEnd1 = new SwEndNode(*this, SwNodeOffset(1), *pStt1);
    BigPtrArray::Insert(pEnd1, 1);
    m_pEndOfPostIts = pEnd1;

    // 空 Normal section [2-3]
    auto* pStt2 = new SwStartNode(*this, SwNodeOffset(2), SwNormalStartNode);
    BigPtrArray::Insert(pStt2, 2);
    auto* pEnd2 = new SwEndNode(*this, SwNodeOffset(3), *pStt2);
    BigPtrArray::Insert(pEnd2, 3);
    m_pEndOfInserts = pEnd2;
}

SwNode* SwNodes::operator[](SwNodeOffset n) const
{
    return static_cast<SwNode*>(BigPtrArray::operator[](n));
}

void SwNodes::InsertNode(SwNode* pNode, SwNodeOffset nPos)
{
    BigPtrArray::Insert(pNode, static_cast<sal_Int32>(nPos));
}

sal_uInt16 SwNodes::GetSectionLevel(const SwNode& rIndex)
{
    sal_uInt16 nLevel = 0;
    const SwStartNode* pStt = rIndex.StartOfSectionNode();
    while (pStt)
    {
        ++nLevel;
        pStt = pStt->StartOfSectionNode();
    }
    return nLevel;
}

SwContentNode* SwNodes::GoNext(SwNodeIndex* pIdx)
{
    if (!pIdx || !pIdx->GetNode())
        return nullptr;

    SwNodeOffset nIdx = pIdx->GetIndex() + SwNodeOffset(1);
    SwNodes& rNodes = pIdx->GetNode()->GetNodes();

    while (nIdx < rNodes.Count())
    {
        SwNode* pNd = rNodes[nIdx];
        if (pNd->IsContentNode())
        {
            *pIdx = nIdx;
            return pNd->GetContentNode();
        }
        if (pNd->IsEndNode())
        {
            // 跳过节区结束
            ++nIdx;
            continue;
        }
        ++nIdx;
    }
    return nullptr;
}

SwContentNode* SwNodes::GoPrevious(SwNodeIndex* pIdx)
{
    if (!pIdx || !pIdx->GetNode())
        return nullptr;

    SwNodeOffset nIdx = pIdx->GetIndex() - SwNodeOffset(1);
    SwNodes& rNodes = pIdx->GetNode()->GetNodes();

    while (nIdx >= 0)
    {
        SwNode* pNd = rNodes[nIdx];
        if (pNd->IsContentNode())
        {
            *pIdx = nIdx;
            return pNd->GetContentNode();
        }
        if (pNd->IsStartNode())
        {
            // 跳过节区开始
            --nIdx;
            continue;
        }
        --nIdx;
    }
    return nullptr;
}

void SwNodes::GoStartOfSection(SwNodeIndex* pIdx)
{
    if (!pIdx || !pIdx->GetNode())
        return;
    SwStartNode* pStt = pIdx->GetNode()->StartOfSectionNode();
    if (pStt)
        *pIdx = *pStt;
}

void SwNodes::GoEndOfSection(SwNodeIndex* pIdx)
{
    if (!pIdx || !pIdx->GetNode())
        return;
    SwEndNode* pEnd = pIdx->GetNode()->EndOfSectionNode();
    if (pEnd)
        *pIdx = *pEnd;
}

SwTextNode* SwNodes::MakeTextNode(const SwNode& rWhere, SwTextFormatColl* pColl)
{
    // 在 rWhere 之后插入新的文本节点
    // rWhere 可能是 body 区的上一个节点，也可能是 Fly 内部的节点
    SwNodeOffset nPos = rWhere.GetIndex() + SwNodeOffset(1);
    auto* pNew = new SwTextNode(rWhere, pColl);
    InsertNode(pNew, nPos);
    return pNew;
}

SwTextNode* SwNodes::MakeBodyTextNode(SwTextFormatColl* pColl)
{
    // 在正文区末尾（m_pEndOfContent 之前）插入新的文本节点
    SwNode* pBodyEnd = m_pEndOfContent.get();
    SwNode* pPrev = (*this)[pBodyEnd->GetIndex() - SwNodeOffset(1)];
    if (!pPrev)
    {
        pPrev = (*this)[SwNodeOffset(8)];
    }
    SwNodeOffset nPos = pBodyEnd->GetIndex();
    auto* pNew = new SwTextNode(*pPrev, pColl);
    InsertNode(pNew, nPos);
    return pNew;
}

SwStartNode* SwNodes::MakeTextSection(const SwNode& rWhere, SwStartNodeType eSttNdTyp)
{
    // 在 rWhere 之后插入新的节区（StartNode + EndNode）
    SwNodeOffset nPos = rWhere.GetIndex() + SwNodeOffset(1);
    auto* pStt = new SwStartNode(rWhere, eSttNdTyp);
    InsertNode(pStt, nPos);
    auto* pEnd = new SwEndNode(*pStt, *pStt);
    InsertNode(pEnd, nPos + SwNodeOffset(1));
    return pStt;
}

SwEndNode* SwNodes::MakeEndNode(const SwNode& rWhere, SwStartNode& rSttNd)
{
    // 在 rWhere 之后插入 EndNode，与 rSttNd 配对
    SwNodeOffset nPos = rWhere.GetIndex() + SwNodeOffset(1);
    auto* pEnd = new SwEndNode(rWhere, rSttNd);
    InsertNode(pEnd, nPos);
    return pEnd;
}

SwSectionNode* SwNodes::MakeSectionNode(const SwNode& rWhere)
{
    // 在 rWhere 之后插入 SwSectionNode（作为 SwStartNode 的子类）
    // 后续需要用 MakeEndNode 插入对应的 EndNode 来配对
    SwNodeOffset nPos = rWhere.GetIndex() + SwNodeOffset(1);
    auto* pSect = new SwSectionNode(rWhere);
    InsertNode(pSect, nPos);
    return pSect;
}

SwTableNode* SwNodes::InsertTable(const SwNode& rNd, sal_uInt16 nBoxes,
                                  SwTextFormatColl* pContentTextColl, sal_uInt16 nLines,
                                  sal_uInt16 /*nRepeat*/, SwTextFormatColl* /*pHeadlineTextColl*/)
{
    // 在 rNd 之后插入 SwTableNode + 单元格 + EndNode
    SwNodeOffset nPos = rNd.GetIndex() + SwNodeOffset(1);

    auto* pTable = new SwTableNode(rNd);
    InsertNode(pTable, nPos);

    // 初始化 tableData
    SwTableNode::TableData tableData(nLines);
    for (sal_uInt16 r = 0; r < nLines; ++r)
    {
        tableData[r].cells.resize(nBoxes);
    }
    pTable->SetTableData(tableData);

    SwNodeOffset nCurPos = nPos + SwNodeOffset(1);

    // 创建单元格（单层扁平结构，无行级节点）
    for (sal_uInt16 nLine = 0; nLine < nLines; ++nLine)
    {
        for (sal_uInt16 nBox = 0; nBox < nBoxes; ++nBox)
        {
            auto* pBoxStt = new SwStartNode(*pTable, SwTableBoxStartNode);
            InsertNode(pBoxStt, nCurPos);
            ++nCurPos;

            auto* pText = new SwTextNode(*pBoxStt, pContentTextColl);
            InsertNode(pText, nCurPos);
            ++nCurPos;

            auto* pBoxEnd = new SwEndNode(*pText, *pBoxStt);
            InsertNode(pBoxEnd, nCurPos);
            ++nCurPos;
        }
    }

    // 表格结束
    auto* pTableEnd = new SwEndNode(*(*this)[nCurPos - SwNodeOffset(1)], *pTable);
    InsertNode(pTableEnd, nCurPos);

    return pTable;
}

SwStartNode* SwNodes::InsertFlySection(SwStartNodeType eType, int nAnchorNodeIndex)
{
    // 在 Fly Container 内按顺序追加 Fly 节区
    // Fly 节区：Fly StartNode + (内容后续插入) + Fly EndNode
    // 注意：只创建 Fly StartNode，EndNode 由调用者在内容插入后创建

    // 找到插入位置：Fly Container StartNode 之后，已存在的 Fly 之后
    // 如果有 Fly Container StartNode，在其之后找最后一个 Fly EndNode
    // 否则在 m_pEndOfAutotext 之前插入（初始情况）

    SwNodeOffset nInsertPos;
    if (m_pFlyContainerStart)
    {
        // 从 Fly Container StartNode 开始，找到最后一个 Fly EndNode
        // Fly Container 结构：StartNode + (Fly1 Start + ... + Fly1 End) + ... + (FlyN Start + ... + FlyN End) + EndNode
        // 我们需要在最后一个 Fly EndNode 之后插入新 Fly StartNode
        SwNodeOffset nSttIdx = m_pFlyContainerStart->GetIndex();
        SwNodeOffset nEndIdx = m_pEndOfAutotext->GetIndex();

        // 从 Fly Container StartNode + 1 开始，向后扫描
        // 找到最后一个 Fly EndNode（在 Fly Container EndNode 之前）
        nInsertPos = nSttIdx + SwNodeOffset(1);  // 默认在 Fly Container StartNode 之后
        for (SwNodeOffset i = nSttIdx + 1; i < nEndIdx; ++i)
        {
            SwNode* pNd = (*this)[i];
            if (!pNd)
                break;
            // 如果是 EndNode 且不是 Fly Container EndNode，则是 Fly EndNode
            if (pNd->IsEndNode())
            {
                nInsertPos = i + SwNodeOffset(1);  // 在 Fly EndNode 之后
            }
        }
    }
    else
    {
        // 初始情况：在 m_pEndOfAutotext 之前插入
        nInsertPos = m_pEndOfAutotext->GetIndex();
    }

    // 创建 Fly StartNode
    SwNode* pPrev = (*this)[nInsertPos - SwNodeOffset(1)];
    if (!pPrev)
        pPrev = (*this)[SwNodeOffset(4)];
    auto* pFlyStt = new SwStartNode(*pPrev, eType);
    pFlyStt->SetAnchorNodeIndex(nAnchorNodeIndex);
    InsertNode(pFlyStt, nInsertPos);

    // 不创建 Fly EndNode，让调用者在内容插入后创建
    // 这样内容节点会在 Fly StartNode 之后、Fly EndNode 之前

    return pFlyStt;
}

SwEndNode* SwNodes::CloseFlySection(SwStartNode& rFlyStt)
{
    // 在 Fly StartNode 的最后一个内容节点之后创建 Fly EndNode
    // Fly StartNode 的内容节点在其之后连续排列
    // 
    // 由于 InsertGrfNode/MakeTextNode 在 StartNode 之后插入内容，
    // 我们需要在最后一个内容节点之后插入 EndNode。
    // 
    // 简化逻辑：Fly StartNode 之后的所有节点（直到下一个 StartNode 或 Fly Container EndNode）
    // 都是 Fly 的内容。我们在 StartNode 之后找到最后一个非 StartNode/非 EndNode 的节点，
    // 在其后插入 Fly EndNode。

    SwNodeOffset nFlySttIdx = rFlyStt.GetIndex();
    
    // 从 Fly StartNode + 1 开始，找到最后一个内容节点
    // 内容节点 = 非 StartNode 且 非 EndNode 的节点
    SwNodeOffset nLastContentIdx = nFlySttIdx;  // 初始指向 Fly StartNode
    for (SwNodeOffset i = nFlySttIdx + 1; i < Count(); ++i)
    {
        SwNode* pNd = (*this)[i];
        if (!pNd)
            break;
        // 遇到下一个 StartNode 或 EndNode，停止
        if (pNd->IsStartNode() || pNd->IsEndNode())
            break;
        nLastContentIdx = i;
    }

    // 在最后一个内容节点之后插入 Fly EndNode
    SwNodeOffset nEndPos = nLastContentIdx + SwNodeOffset(1);
    auto* pFlyEnd = new SwEndNode(rFlyStt, rFlyStt);
    InsertNode(pFlyEnd, nEndPos);

    // 更新 Fly StartNode 的 EndOfSection 指针
    rFlyStt.SetEndOfSection(pFlyEnd);

    return pFlyEnd;
}

SwGrfNode* SwNodes::InsertGrfNode(const SwNode& rWhere)
{
    SwNodeOffset nPos = rWhere.GetIndex() + SwNodeOffset(1);
    auto* pGrfNode = new SwGrfNode(rWhere);
    InsertNode(pGrfNode, nPos);
    // 若 rWhere 是当前 m_pEndOfContent，则扩展 m_pEndOfContent 指向新节点
    if (rWhere.GetIndex() + 1 == static_cast<sal_Int32>(nPos)
        && &rWhere == m_pEndOfContent.get())
    {
        m_pEndOfContent.reset(pGrfNode);
    }
    return pGrfNode;
}

SwStartNode* SwNodes::AppendNormalSection()
{
    // 在数组末尾追加一个 Normal 节区（StartNode + EndNode 对）
    // 返回 StartNode 指针，调用者可设置哨兵指向 EndNode
    SwNodeOffset nSttPos = BigPtrArray::Count();
    auto* pPrev = (*this)[nSttPos - SwNodeOffset(1)];
    auto* pStt = new SwStartNode(*pPrev, SwNormalStartNode);
    BigPtrArray::Insert(pStt, nSttPos);

    SwNodeOffset nEndPos = pStt->GetIndex() + SwNodeOffset(1);
    auto* pEnd = new SwEndNode(*pStt, *pStt);
    BigPtrArray::Insert(pEnd, nEndPos);

    return pStt;
}

void SwNodes::Delete(const SwNodeIndex& rPos, SwNodeOffset nNodes)
{
    SwNodeOffset nStart = rPos.GetIndex();
    for (SwNodeOffset i = 0; i < nNodes; ++i)
    {
        BigPtrArray::Remove(nStart);
    }
}

void SwNodes::ForEach(SwNodeOffset nStt, SwNodeOffset nEnd, ForEachFn fn)
{
    for (SwNodeOffset i = nStt; i < nEnd && i < Count(); ++i)
    {
        if (!fn((*this)[i]))
            break;
    }
}

bool SwNodes::IsDocNodes() const
{
    return true;
}

//===----------------------------------------------------------------------===//
// SwNodeIndex
//===----------------------------------------------------------------------===//

SwNodeIndex::SwNodeIndex(SwNodes& rNodes, SwNodeOffset nPos)
    : m_pNode(rNodes[nPos])
    , m_nOffset(nPos)
{
}

SwNodeIndex::SwNodeIndex(const SwNode& rNode)
    : m_pNode(const_cast<SwNode*>(&rNode))
    , m_nOffset(rNode.GetIndex())
{
}

SwNodeIndex::SwNodeIndex(const SwNodeIndex& rOther)
    : m_pNode(rOther.m_pNode)
    , m_nOffset(rOther.m_nOffset)
{
}

SwNodeIndex& SwNodeIndex::operator=(const SwNodeIndex& rOther)
{
    m_pNode = rOther.m_pNode;
    m_nOffset = rOther.m_nOffset;
    return *this;
}

SwNodeIndex& SwNodeIndex::operator=(const SwNode& rNode)
{
    m_pNode = const_cast<SwNode*>(&rNode);
    m_nOffset = rNode.GetIndex();
    return *this;
}

SwNodeIndex& SwNodeIndex::operator=(SwNodeOffset nOffset)
{
    m_nOffset = nOffset;
    if (m_pNode)
    {
        m_pNode = m_pNode->GetNodes()[nOffset];
    }
    return *this;
}

SwNodeIndex& SwNodeIndex::operator++()
{
    ++m_nOffset;
    if (m_pNode)
    {
        m_pNode = m_pNode->GetNodes()[m_nOffset];
    }
    return *this;
}

SwNodeIndex& SwNodeIndex::operator--()
{
    --m_nOffset;
    if (m_pNode)
    {
        m_pNode = m_pNode->GetNodes()[m_nOffset];
    }
    return *this;
}

SwNodeIndex SwNodeIndex::operator++(int)
{
    SwNodeIndex tmp(*this);
    ++(*this);
    return tmp;
}

SwNodeIndex SwNodeIndex::operator--(int)
{
    SwNodeIndex tmp(*this);
    --(*this);
    return tmp;
}

SwNodeIndex& SwNodeIndex::operator+=(SwNodeOffset nOffset)
{
    m_nOffset += nOffset;
    if (m_pNode)
    {
        m_pNode = m_pNode->GetNodes()[m_nOffset];
    }
    return *this;
}

SwNodeIndex& SwNodeIndex::operator-=(SwNodeOffset nOffset)
{
    m_nOffset -= nOffset;
    if (m_pNode)
    {
        m_pNode = m_pNode->GetNodes()[m_nOffset];
    }
    return *this;
}
