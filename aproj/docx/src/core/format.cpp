// 简化版样式系统实现

#include "format.h"
#include <algorithm>

//===----------------------------------------------------------------------===//
// SwFormat
//===----------------------------------------------------------------------===//

SwFormat::SwFormat(const std::string& rName, sal_uInt16 nWhichId)
    : m_sName(rName)
    , m_nWhichId(nWhichId)
{
}

void SwFormat::SetAttr(sal_uInt16 nWhich, const AttrValue& rValue) { m_aAttrs[nWhich] = rValue; }

const AttrValue* SwFormat::GetAttr(sal_uInt16 nWhich) const
{
    auto it = m_aAttrs.find(nWhich);
    return it != m_aAttrs.end() ? &it->second : nullptr;
}

const AttrValue* SwFormat::ResolveAttr(sal_uInt16 nWhich) const
{
    // 先在当前格式中查找
    const AttrValue* pVal = GetAttr(nWhich);
    if (pVal)
        return pVal;

    // 沿父链查找
    if (m_pDerivedFrom)
        return m_pDerivedFrom->ResolveAttr(nWhich);

    return nullptr;
}

//===----------------------------------------------------------------------===//
// SwFrameFormat
//===----------------------------------------------------------------------===//

SwFrameFormat::SwFrameFormat(const std::string& rName)
    : SwFormat(rName)
{
}

void SwFrameFormat::MakeFrames()
{
    // 将在 Phase 2 中实现
}

void SwFrameFormat::DelFrames()
{
    // 将在 Phase 2 中实现
}

SwFrameFormat* SwFrameFormat::GetDefault()
{
    static SwFrameFormat s_Default("Standard");
    return &s_Default;
}

//===----------------------------------------------------------------------===//
// SwTextFormatColl
//===----------------------------------------------------------------------===//

SwTextFormatColl::SwTextFormatColl(const std::string& rName)
    : SwFrameFormat(rName)
{
}

//===----------------------------------------------------------------------===//
// SwPageDesc
//===----------------------------------------------------------------------===//

SwPageDesc::SwPageDesc(const std::string& rName)
    : m_sName(rName)
{
}

void SwPageDesc::SetAttr(sal_uInt16 nWhich, const AttrValue& rValue) { m_aAttrs[nWhich] = rValue; }

const AttrValue* SwPageDesc::GetAttr(sal_uInt16 nWhich) const
{
    auto it = m_aAttrs.find(nWhich);
    return it != m_aAttrs.end() ? &it->second : nullptr;
}

SwPageDesc& SwPageDesc::GetDefault()
{
    static SwPageDesc s_Default("Standard");
    return s_Default;
}
