// 简化版 SwNode 实现，对应 LibreOffice 的 sw/source/core/docnode/node.cxx

#include "node.h"
#include "ndarr.h"
#include <cassert>
#include <algorithm>

//===----------------------------------------------------------------------===//
// SwNode 基类
//===----------------------------------------------------------------------===//

SwNode::SwNode(SwNodes& rNodes, SwNodeOffset nPos, SwNodeType nType)
    : m_nNodeType(nType)
    , m_pStartOfSection(nullptr)
    , m_pFormatColl(nullptr)
{
    (void)rNodes;
    (void)nPos;
}

SwNode::SwNode(const SwNode& rWhere, SwNodeType nType)
    : m_nNodeType(nType)
    , m_pStartOfSection(rWhere.m_pStartOfSection)
    , m_pFormatColl(rWhere.m_pFormatColl)
{
}

SwNode::~SwNode() = default;

SwEndNode* SwNode::EndOfSectionNode()
{
    SwStartNode* pStt = StartOfSectionNode();
    return pStt ? pStt->GetEndOfSection() : nullptr;
}

const SwEndNode* SwNode::EndOfSectionNode() const
{
    const SwStartNode* pStt = StartOfSectionNode();
    return pStt ? pStt->GetEndOfSection() : nullptr;
}

SwNodes& SwNode::GetNodes()
{
    // BigPtrEntry::GetArray() 返回 BigPtrArray&，我们需要转换为 SwNodes&
    // 这是安全的，因为 SwNodes 是 BigPtrArray 的子类
    return static_cast<SwNodes&>(BigPtrEntry::GetArray());
}

const SwNodes& SwNode::GetNodes() const
{
    return static_cast<const SwNodes&>(BigPtrEntry::GetArray());
}

SwDoc& SwNode::GetDoc() { return GetNodes().GetDoc(); }

const SwDoc& SwNode::GetDoc() const { return GetNodes().GetDoc(); }

SwTableNode* SwNode::FindTableNode()
{
    SwStartNode* pStt = m_pStartOfSection;
    while (pStt)
    {
        if (pStt->IsTableNode())
            return static_cast<SwTableNode*>(pStt);
        pStt = pStt->StartOfSectionNode();
    }
    return nullptr;
}

SwSectionNode* SwNode::FindSectionNode()
{
    SwStartNode* pStt = m_pStartOfSection;
    while (pStt)
    {
        if (pStt->IsSectionNode())
            return static_cast<SwSectionNode*>(pStt);
        pStt = pStt->StartOfSectionNode();
    }
    return nullptr;
}

SwStartNode* SwNode::FindStartNodeByType(SwStartNodeType eTyp)
{
    SwStartNode* pStt = m_pStartOfSection;
    while (pStt)
    {
        if (pStt->GetStartNodeType() == eTyp)
            return pStt;
        pStt = pStt->StartOfSectionNode();
    }
    return nullptr;
}

//===----------------------------------------------------------------------===//
// SwStartNode
//===----------------------------------------------------------------------===//

SwStartNode::SwStartNode(SwNodes& rNodes, SwNodeOffset nPos, SwStartNodeType eType)
    : SwNode(rNodes, nPos, SwNodeType::Start)
    , m_pEndOfSection(nullptr)
    , m_eStartNodeType(eType)
{
}

SwStartNode::SwStartNode(const SwNode& rWhere, SwStartNodeType eType)
    : SwNode(rWhere, SwNodeType::Start)
    , m_pEndOfSection(nullptr)
    , m_eStartNodeType(eType)
{
}

//===----------------------------------------------------------------------===//
// SwEndNode
//===----------------------------------------------------------------------===//

SwEndNode::SwEndNode(SwNodes& rNodes, SwNodeOffset nPos, SwStartNode& rSttNd)
    : SwNode(rNodes, nPos, SwNodeType::End)
{
    m_pStartOfSection = &rSttNd;
    rSttNd.m_pEndOfSection = this;
}

SwEndNode::SwEndNode(const SwNode& rWhere, SwStartNode& rSttNd)
    : SwNode(rWhere, SwNodeType::End)
{
    m_pStartOfSection = &rSttNd;
    rSttNd.m_pEndOfSection = this;
}

//===----------------------------------------------------------------------===//
// SwContentNode
//===----------------------------------------------------------------------===//

SwContentNode::SwContentNode(const SwNode& rWhere, SwNodeType nType, SwTextFormatColl* pFormatColl)
    : SwNode(rWhere, nType)
{
    if (pFormatColl)
        ChgFormatColl(pFormatColl);
}

SwContentNode::SwContentNode(SwNodes& rNodes, SwNodeOffset nPos, SwNodeType nType,
                             SwTextFormatColl* pFormatColl)
    : SwNode(rNodes, nPos, nType)
{
    if (pFormatColl)
        ChgFormatColl(pFormatColl);
}

SwContentNode::~SwContentNode() = default;

//===----------------------------------------------------------------------===//
// SwTextNode
//===----------------------------------------------------------------------===//

SwTextNode::SwTextNode(const SwNode& rWhere, SwTextFormatColl* pFormatColl)
    : SwContentNode(rWhere, SwNodeType::Text, pFormatColl)
{
}

SwTextNode::SwTextNode(SwNodes& rNodes, SwNodeOffset nPos, SwTextFormatColl* pFormatColl)
    : SwContentNode(rNodes, nPos, SwNodeType::Text, pFormatColl)
{
}

SwTextNode::~SwTextNode() = default;

SwContentFrame* SwTextNode::MakeFrame(SwFrame* pSib)
{
    // 将在 Phase 2 中实现
    (void)pSib;
    return nullptr;
}

void SwTextNode::SetAttr(sal_uInt16 nWhich, const std::string& rValue)
{
    m_aAttrs[nWhich] = rValue;
}

const std::string* SwTextNode::GetAttr(sal_uInt16 nWhich) const
{
    auto it = m_aAttrs.find(nWhich);
    return it != m_aAttrs.end() ? &it->second : nullptr;
}

//===----------------------------------------------------------------------===//
// SwTableNode
//===----------------------------------------------------------------------===//

SwTableNode::SwTableNode(const SwNode& rWhere)
    : SwStartNode(rWhere, SwTableBoxStartNode)
{
    // 注意：SwTableNode 的节点类型是 Table，不是 Start
    // 这里需要修改节点类型
}

SwTableNode::SwTableNode(SwNodes& rNodes, SwNodeOffset nPos)
    : SwStartNode(rNodes, nPos, SwTableBoxStartNode)
{
}

SwTableNode::~SwTableNode() = default;

void SwTableNode::SetAttr(sal_uInt16 nWhich, const std::string& rValue)
{
    m_aAttrs[nWhich] = rValue;
}

const std::string* SwTableNode::GetAttr(sal_uInt16 nWhich) const
{
    auto it = m_aAttrs.find(nWhich);
    return it != m_aAttrs.end() ? &it->second : nullptr;
}

//===----------------------------------------------------------------------===//
// SwSectionNode
//===----------------------------------------------------------------------===//

SwSectionNode::SwSectionNode(const SwNode& rWhere)
    : SwStartNode(rWhere, SwNormalStartNode)
{
}

SwSectionNode::SwSectionNode(SwNodes& rNodes, SwNodeOffset nPos)
    : SwStartNode(rNodes, nPos, SwNormalStartNode)
{
}

SwSectionNode::~SwSectionNode() = default;
