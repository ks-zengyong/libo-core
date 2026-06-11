// 简化版 SwDoc 实现

#include "doc.h"
#include "node.h"
#include <algorithm>

//===----------------------------------------------------------------------===//
// SwDoc
//===----------------------------------------------------------------------===//

SwDoc::SwDoc()
{
    m_pNodes = std::make_unique<SwNodes>(*this);
    InitDefaultStyles();
}

SwDoc::~SwDoc() = default;

void SwDoc::InitDefaultStyles()
{
    // 创建默认段落样式
    m_pDefaultTextFormatColl = MakeTextFormatColl("Standard");
    m_pDefaultTextFormatColl->SetAutoFormat(false);

    // 创建默认页面描述符
    m_pDefaultPageDesc = MakePageDesc("Standard");
}

SwTextFormatColl* SwDoc::MakeTextFormatColl(const std::string& rName)
{
    // 检查是否已存在
    auto it = m_aTextFormatColls.find(rName);
    if (it != m_aTextFormatColls.end())
        return it->second.get();

    // 创建新样式
    auto pColl = std::make_unique<SwTextFormatColl>(rName);
    auto* pResult = pColl.get();

    // 设置父样式
    if (m_pDefaultTextFormatColl && pResult != m_pDefaultTextFormatColl)
    {
        pResult->SetDerivedFrom(m_pDefaultTextFormatColl);
    }

    m_aTextFormatColls[rName] = std::move(pColl);
    return pResult;
}

SwTextFormatColl* SwDoc::FindTextFormatColl(const std::string& rName) const
{
    auto it = m_aTextFormatColls.find(rName);
    return it != m_aTextFormatColls.end() ? it->second.get() : nullptr;
}

SwTextFormatColl* SwDoc::GetTextFormatColl(sal_uInt16 nPoolId) const
{
    auto it = m_aPoolFormatColls.find(nPoolId);
    return it != m_aPoolFormatColls.end() ? it->second : nullptr;
}

SwPageDesc* SwDoc::MakePageDesc(const std::string& rName)
{
    // 检查是否已存在
    for (auto& pDesc : m_aPageDescs)
    {
        if (pDesc->GetName() == rName)
            return pDesc.get();
    }

    // 创建新页面描述符
    auto pDesc = std::make_unique<SwPageDesc>(rName);
    auto* pResult = pDesc.get();
    m_aPageDescs.push_back(std::move(pDesc));
    return pResult;
}

SwPageDesc* SwDoc::FindPageDesc(const std::string& rName) const
{
    for (const auto& pDesc : m_aPageDescs)
    {
        if (pDesc->GetName() == rName)
            return pDesc.get();
    }
    return nullptr;
}

SwPageDesc* SwDoc::GetPageDesc(sal_uInt16 nIdx) const
{
    if (nIdx < m_aPageDescs.size())
        return m_aPageDescs[nIdx].get();
    return nullptr;
}

SwContentNode* SwDoc::GetContentNode(SwNodeOffset nIdx) const
{
    if (nIdx < 0 || nIdx >= m_pNodes->Count())
        return nullptr;
    SwNode* pNd = (*m_pNodes)[nIdx];
    return pNd ? pNd->GetContentNode() : nullptr;
}

void SwDoc::SetAttr(sal_uInt16 nWhich, const AttrValue& rValue) { m_aAttrs[nWhich] = rValue; }

const AttrValue* SwDoc::GetAttr(sal_uInt16 nWhich) const
{
    auto it = m_aAttrs.find(nWhich);
    return it != m_aAttrs.end() ? &it->second : nullptr;
}
