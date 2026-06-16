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
    , m_pEndOfAutotext(nullptr)
    , m_pEndOfRedlines(nullptr)
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
    // 创建文档的固定节区结构，对应 LibreOffice 的初始化逻辑
    // 结构如下：
    //   [0] StartNode (PostIts)
    //   [1] EndNode (PostIts) -> m_pEndOfPostIts
    //   [2] StartNode (Inserts)
    //   [3] EndNode (Inserts) -> m_pEndOfInserts
    //   [4] StartNode (Autotext)
    //   [5] EndNode (Autotext) -> m_pEndOfAutotext
    //   [6] StartNode (Redlines)
    //   [7] EndNode (Redlines) -> m_pEndOfRedlines
    //   [8] StartNode (Content/Body)
    //   ... 用户内容 ...
    //   [N] EndNode (Content/Body) -> m_pEndOfContent

    // 创建 PostIts 节区
    auto* pPostItsStt = new SwStartNode(*this, SwNodeOffset(0), SwNormalStartNode);
    BigPtrArray::Insert(pPostItsStt, 0);
    auto* pPostItsEnd = new SwEndNode(*this, SwNodeOffset(1), *pPostItsStt);
    BigPtrArray::Insert(pPostItsEnd, 1);
    m_pEndOfPostIts = pPostItsEnd;

    // 创建 Inserts 节区
    auto* pInsertsStt = new SwStartNode(*this, SwNodeOffset(2), SwNormalStartNode);
    BigPtrArray::Insert(pInsertsStt, 2);
    auto* pInsertsEnd = new SwEndNode(*this, SwNodeOffset(3), *pInsertsStt);
    BigPtrArray::Insert(pInsertsEnd, 3);
    m_pEndOfInserts = pInsertsEnd;

    // 创建 Autotext 节区
    auto* pAutotextStt = new SwStartNode(*this, SwNodeOffset(4), SwNormalStartNode);
    BigPtrArray::Insert(pAutotextStt, 4);
    auto* pAutotextEnd = new SwEndNode(*this, SwNodeOffset(5), *pAutotextStt);
    BigPtrArray::Insert(pAutotextEnd, 5);
    m_pEndOfAutotext = pAutotextEnd;

    // 创建 Redlines 节区
    auto* pRedlinesStt = new SwStartNode(*this, SwNodeOffset(6), SwNormalStartNode);
    BigPtrArray::Insert(pRedlinesStt, 6);
    auto* pRedlinesEnd = new SwEndNode(*this, SwNodeOffset(7), *pRedlinesStt);
    BigPtrArray::Insert(pRedlinesEnd, 7);
    m_pEndOfRedlines = pRedlinesEnd;

    // 创建 Content/Body 节区（用户内容在这里）
    auto* pContentStt = new SwStartNode(*this, SwNodeOffset(8), SwNormalStartNode);
    BigPtrArray::Insert(pContentStt, 8);

    // 创建 EndOfContent 哨兵节点
    auto* pContentEnd = new SwEndNode(*this, SwNodeOffset(9), *pContentStt);
    BigPtrArray::Insert(pContentEnd, 9);
    m_pEndOfContent.reset(pContentEnd);
}

SwNode* SwNodes::operator[](SwNodeOffset n) const
{
    return static_cast<SwNode*>(BigPtrArray::operator[](n));
}

void SwNodes::InsertNode(SwNode* pNode, SwNodeOffset nPos) { BigPtrArray::Insert(pNode, nPos); }

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
    SwNodeOffset nPos = rWhere.GetIndex() + SwNodeOffset(1);
    auto* pNew = new SwTextNode(rWhere, pColl);
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

SwTableNode* SwNodes::InsertTable(const SwNode& rNd, sal_uInt16 nBoxes,
                                  SwTextFormatColl* pContentTextColl, sal_uInt16 nLines,
                                  sal_uInt16 nRepeat, SwTextFormatColl* pHeadlineTextColl)
{
    // 简化版表格插入
    (void)nRepeat;
    (void)pHeadlineTextColl;

    SwNodeOffset nPos = rNd.GetIndex() + SwNodeOffset(1);

    // 创建表格节点
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

    // 创建行和单元格
    for (sal_uInt16 nLine = 0; nLine < nLines; ++nLine)
    {
        // 每行是一个 StartNode
        auto* pLineStt = new SwStartNode(*pTable, SwTableBoxStartNode);
        InsertNode(pLineStt, nCurPos);
        ++nCurPos;

        for (sal_uInt16 nBox = 0; nBox < nBoxes; ++nBox)
        {
            // 每个单元格是一个 StartNode + TextNode + EndNode
            auto* pBoxStt = new SwStartNode(*pLineStt, SwTableBoxStartNode);
            InsertNode(pBoxStt, nCurPos);
            ++nCurPos;

            // 单元格内容
            auto* pText = new SwTextNode(*pBoxStt, pContentTextColl);
            InsertNode(pText, nCurPos);
            ++nCurPos;

            auto* pBoxEnd = new SwEndNode(*pText, *pBoxStt);
            InsertNode(pBoxEnd, nCurPos);
            ++nCurPos;
        }

        // 行结束
        auto* pLineEnd = new SwEndNode(*pLineStt, *pLineStt);
        InsertNode(pLineEnd, nCurPos);
        ++nCurPos;
    }

    // 表格结束
    auto* pTableEnd = new SwEndNode(*pTable, *pTable);
    InsertNode(pTableEnd, nCurPos);

    return pTable;
}

SwStartNode* SwNodes::InsertFlySection(SwStartNodeType eType)
{
    // 在 AutoText 区域创建 Fly 节区
    // Fly 节区位于 EndOfAutotext 和 EndOfRedlines 之间
    // 结构：StartNode (Fly) ... EndNode (Fly)

    SwNodeOffset nPos = m_pEndOfAutotext->GetIndex() + SwNodeOffset(1);

    // 创建 Fly StartNode
    auto* pFlyStt = new SwStartNode(*this, nPos, eType);
    InsertNode(pFlyStt, nPos);

    // 创建 Fly EndNode
    auto* pFlyEnd = new SwEndNode(*this, nPos + SwNodeOffset(1), *pFlyStt);
    InsertNode(pFlyEnd, nPos + SwNodeOffset(1));

    return pFlyStt;
}

SwGrfNode* SwNodes::InsertGrfNode(const SwNode& rWhere)
{
    SwNodeOffset nPos = rWhere.GetIndex() + SwNodeOffset(1);
    auto* pGrfNode = new SwGrfNode(rWhere);
    InsertNode(pGrfNode, nPos);
    return pGrfNode;
}

void SwNodes::Delete(const SwNodeIndex& rPos, SwNodeOffset nNodes)
{
    SwNodeOffset nStart = rPos.GetIndex();
    // 简化实现：直接从数组中移除
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
    // 简化实现：总是返回 true
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
