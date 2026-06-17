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
    // 创建 LibreOffice 风格的文档节点结构：
    //   [0] START_NODE (Normal)
    //   [1] END_NODE                <- 空节 1
    //   [2] START_NODE (Normal)
    //   [3] END_NODE                <- 空节 2
    //   [4] START_NODE (Normal)     <- Fly 浮动对象区 (内容在这里)
    //   [5] END_NODE                <- Fly 区结束 (m_pEndOfAutotext 指向此)
    //   [6] START_NODE (Normal)
    //   [7] END_NODE                <- 空节 3 (m_pEndOfRedlines 指向此)
    //   [8] START_NODE (Normal)     <- 正文内容区 (内容在这里)
    //   [9] END_NODE                <- 正文区结束 (m_pEndOfContent 指向此)
    // 解析器负责：
    //   - Fly 节点插入在 m_pEndOfAutotext 之前（即 SwNodeOffset(5) 位置）
    //   - 正文节点插入在 m_pEndOfContent 之前（即 SwNodeOffset(9) 位置）

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

    // Fly 区 [4-5]：浮动对象（图片、文本框、表格等）将插入到 [4] 和 [5] 之间
    auto* pFlyStt = new SwStartNode(*this, SwNodeOffset(4), SwNormalStartNode);
    BigPtrArray::Insert(pFlyStt, 4);
    auto* pFlyEnd = new SwEndNode(*this, SwNodeOffset(5), *pFlyStt);
    BigPtrArray::Insert(pFlyEnd, 5);
    m_pEndOfAutotext = pFlyEnd;

    // 空 Normal section [6-7]
    auto* pStt3 = new SwStartNode(*this, SwNodeOffset(6), SwNormalStartNode);
    BigPtrArray::Insert(pStt3, 6);
    auto* pEnd3 = new SwEndNode(*this, SwNodeOffset(7), *pStt3);
    BigPtrArray::Insert(pEnd3, 7);
    m_pEndOfRedlines = pEnd3;

    // 正文区 [8-9]：正文文本将插入到 [8] 和 [9] 之间
    auto* pBodyStt = new SwStartNode(*this, SwNodeOffset(8), SwNormalStartNode);
    BigPtrArray::Insert(pBodyStt, 8);
    auto* pBodyEnd = new SwEndNode(*this, SwNodeOffset(9), *pBodyStt);
    BigPtrArray::Insert(pBodyEnd, 9);
    m_pEndOfContent.reset(pBodyEnd);
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
    // 在 Fly 区末尾（m_pEndOfAutotext 之前）插入 Fly 节区
    // Fly 节区：Fly StartNode + (内容后续插入) + Fly EndNode
    SwNodeOffset nInsertPos = m_pEndOfAutotext->GetIndex();

    // 创建 Fly StartNode
    SwNode* pPrev = (*this)[nInsertPos - SwNodeOffset(1)];
    if (!pPrev)
        pPrev = (*this)[SwNodeOffset(4)];
    auto* pFlyStt = new SwStartNode(*pPrev, eType);
    pFlyStt->SetAnchorNodeIndex(nAnchorNodeIndex);
    InsertNode(pFlyStt, nInsertPos);

    // 创建 Fly EndNode（与 Fly StartNode 配对）
    SwNodeOffset nEndPos = pFlyStt->GetIndex() + SwNodeOffset(1);
    auto* pFlyEnd = new SwEndNode(*pFlyStt, *pFlyStt);
    InsertNode(pFlyEnd, nEndPos);

    return pFlyStt;
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

SwEndNode* SwNodes::InsertEndNodeAtContent(const SwStartNode& rSttNd)
{
    // 在当前末尾之后插入 EndNode，与 rSttNd 配对
    SwNodeOffset nPos = m_pEndOfContent->GetIndex() + SwNodeOffset(1);
    auto* pEnd = new SwEndNode(*m_pEndOfContent.get(), const_cast<SwStartNode&>(rSttNd));
    InsertNode(pEnd, nPos);
    m_pEndOfContent.reset(pEnd);
    return pEnd;
}

SwStartNode* SwNodes::InsertEmptyNormalSection()
{
    // 在当前末尾之后插入一个 Normal 节区（StartNode + EndNode）
    SwNodeOffset nPos = m_pEndOfContent->GetIndex() + SwNodeOffset(1);
    auto* pStt = new SwStartNode(*m_pEndOfContent.get(), SwNormalStartNode);
    InsertNode(pStt, nPos);
    SwNodeOffset nEndPos = pStt->GetIndex() + SwNodeOffset(1);
    auto* pEnd = new SwEndNode(*pStt, *pStt);
    InsertNode(pEnd, nEndPos);
    m_pEndOfContent.reset(pEnd);
    return pStt;
}

SwStartNode* SwNodes::InsertBodyStartNode()
{
    // 在当前末尾之后插入一个 Normal 节区的 StartNode（不立即插入 EndNode）
    SwNodeOffset nPos = m_pEndOfContent->GetIndex() + SwNodeOffset(1);
    auto* pStt = new SwStartNode(*m_pEndOfContent, SwNormalStartNode);
    InsertNode(pStt, nPos);
    m_pEndOfContent.reset(pStt);
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
