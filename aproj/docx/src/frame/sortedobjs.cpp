// SwSortedObjs 实现
// 迁移自 LibreOffice sw/source/core/layout/sortedobjs.cxx
// 简化版：使用 SwFlyFrame* 代替 SwAnchoredObject*

#include "sortedobjs.h"
#include <algorithm>
#include <cassert>

SwSortedObjs::SwSortedObjs()
{
}

SwSortedObjs::~SwSortedObjs()
{
}

size_t SwSortedObjs::size() const
{
    return m_aObjs.size();
}

SwFlyFrame* SwSortedObjs::operator[](size_t nIndex) const
{
    if (nIndex >= size())
    {
        // 简化版：返回 nullptr，不触发 OSL_FAIL
        return nullptr;
    }
    return m_aObjs[nIndex];
}

bool SwSortedObjs::Insert(SwFlyFrame* pFly)
{
    if (!pFly)
        return false;

    if (Contains(pFly))
    {
        // list already contains object
        return true;
    }

    // 简化版：直接追加，不排序
    m_aObjs.push_back(pFly);
    m_aAnchors.push_back(nullptr); // 锚点稍后设置

    return Contains(pFly);
}

void SwSortedObjs::Remove(SwFlyFrame* pFly)
{
    auto aIter = std::find(m_aObjs.begin(), m_aObjs.end(), pFly);

    if (aIter == m_aObjs.end())
    {
        // object not found
        return;
    }

    size_t nIndex = aIter - m_aObjs.begin();
    m_aObjs.erase(aIter);
    
    // 同步删除锚点信息
    if (nIndex < m_aAnchors.size())
    {
        m_aAnchors.erase(m_aAnchors.begin() + nIndex);
    }
}

bool SwSortedObjs::Contains(const SwFlyFrame* pFly) const
{
    return std::find(m_aObjs.begin(), m_aObjs.end(), pFly) != m_aObjs.end();
}

size_t SwSortedObjs::GetPos(const SwFlyFrame* pFly) const
{
    auto aIter = std::find(m_aObjs.begin(), m_aObjs.end(), pFly);

    if (aIter != m_aObjs.end())
    {
        return static_cast<size_t>(aIter - m_aObjs.begin());
    }

    return size();
}

void SwSortedObjs::Update(const SwFlyFrame* pFly)
{
    // 简化版：不执行重排序
    (void)pFly;
}

void SwSortedObjs::UpdateAll()
{
    // 简化版：不执行重排序
}

SwFrame* SwSortedObjs::GetAnchorFrame(size_t nIndex) const
{
    if (nIndex >= m_aAnchors.size())
        return nullptr;
    return m_aAnchors[nIndex];
}

void SwSortedObjs::SetAnchorFrame(size_t nIndex, SwFrame* pAnchor)
{
    if (nIndex >= m_aAnchors.size())
        return;
    m_aAnchors[nIndex] = pAnchor;
}