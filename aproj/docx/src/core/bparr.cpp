// 简化版 BigPtrArray 实现，对应 LibreOffice 的 sw/source/core/bastyp/bparr.cxx
// 从 LibreOffice 源码简化而来，保留核心功能

#include "bparr.h"
#include <algorithm>
#include <climits>

// 每次扩展块管理数组的增量
static const sal_uInt16 nBlockGrowSize = 20;

BigPtrArray::BigPtrArray()
{
    m_nBlock = m_nCur = 0;
    m_nSize = 0;
    m_nMaxBlock = nBlockGrowSize;
    m_ppInf.reset(new BlockInfo*[m_nMaxBlock]);
}

BigPtrArray::~BigPtrArray()
{
    if (m_nBlock)
    {
        BlockInfo** pp = m_ppInf.get();
        for (sal_uInt16 n = 0; n < m_nBlock; ++n, ++pp)
        {
            delete *pp;
        }
    }
}

void BigPtrArray::Move(sal_Int32 from, sal_Int32 to)
{
    if (from != to)
    {
        sal_uInt16 cur = Index2Block(from);
        BlockInfo* p = m_ppInf[cur];
        BigPtrEntry* pElem = p->mvData[from - p->nStart];
        Insert(pElem, to);
        ImplRemove((to < from) ? (from + 1) : from, 1, false);
    }
}

BigPtrEntry* BigPtrArray::operator[](sal_Int32 idx) const
{
    assert(idx < m_nSize);
    m_nCur = Index2Block(idx);
    BlockInfo* p = m_ppInf[m_nCur];
    return p->mvData[idx - p->nStart];
}

sal_uInt16 BigPtrArray::Index2Block(sal_Int32 pos) const
{
    BlockInfo* p = m_ppInf[m_nCur];
    if (p->nStart <= pos && p->nEnd >= pos)
        return m_nCur;
    if (!pos)
        return 0;

    if (m_nCur < (m_nBlock - 1))
    {
        p = m_ppInf[m_nCur + 1];
        if (p->nStart <= pos && p->nEnd >= pos)
            return m_nCur + 1;
    }
    else if (pos < p->nStart && m_nCur > 0)
    {
        p = m_ppInf[m_nCur - 1];
        if (p->nStart <= pos && p->nEnd >= pos)
            return m_nCur - 1;
    }

    // 二分查找
    sal_uInt16 lower = 0, upper = m_nBlock - 1;
    sal_uInt16 cur = 0;
    for (;;)
    {
        sal_uInt16 n = lower + (upper - lower) / 2;
        cur = (n == cur) ? n + 1 : n;
        p = m_ppInf[cur];
        if (p->nStart <= pos && p->nEnd >= pos)
            return cur;
        if (p->nStart > pos)
            upper = cur;
        else
            lower = cur;
    }
}

void BigPtrArray::UpdIndex(sal_uInt16 pos)
{
    BlockInfo** pp = m_ppInf.get() + pos;
    sal_Int32 idx = (*pp)->nEnd + 1;
    while (++pos < m_nBlock)
    {
        BlockInfo* p = *++pp;
        p->nStart = idx;
        idx += p->nElem;
        p->nEnd = idx - 1;
    }
}

BlockInfo* BigPtrArray::InsBlock(sal_uInt16 pos)
{
    if (m_nBlock == m_nMaxBlock)
    {
        BlockInfo** ppNew = new BlockInfo*[m_nMaxBlock + nBlockGrowSize];
        std::copy(m_ppInf.get(), m_ppInf.get() + m_nMaxBlock, ppNew);
        m_nMaxBlock += nBlockGrowSize;
        m_ppInf.reset(ppNew);
    }
    if (pos != m_nBlock)
    {
        std::copy_backward(m_ppInf.get() + pos, m_ppInf.get() + m_nBlock,
                           m_ppInf.get() + m_nBlock + 1);
    }
    ++m_nBlock;
    BlockInfo* p = new BlockInfo;
    m_ppInf[pos] = p;

    if (pos)
        p->nStart = p->nEnd = m_ppInf[pos - 1]->nEnd + 1;
    else
        p->nStart = p->nEnd = 0;

    p->nEnd--; // 没有元素
    p->nElem = 0;
    p->pBigArr = this;
    return p;
}

void BigPtrArray::BlockDel(sal_uInt16 nDel)
{
    m_nBlock = m_nBlock - nDel;
    if (m_nMaxBlock - m_nBlock > nBlockGrowSize)
    {
        nDel = ((m_nBlock / nBlockGrowSize) + 1) * nBlockGrowSize;
        BlockInfo** ppNew = new BlockInfo*[nDel];
        std::copy(m_ppInf.get(), m_ppInf.get() + m_nBlock, ppNew);
        m_ppInf.reset(ppNew);
        m_nMaxBlock = nDel;
    }
}

void BigPtrArray::Insert(BigPtrEntry* pElem, sal_Int32 pos)
{
    BlockInfo* p;
    sal_uInt16 cur;
    if (!m_nSize)
    {
        cur = 0;
        p = InsBlock(cur);
    }
    else if (pos == m_nSize)
    {
        cur = m_nBlock - 1;
        p = m_ppInf[cur];
        if (p->nElem == MAXENTRY)
            p = InsBlock(++cur);
    }
    else
    {
        cur = Index2Block(pos);
        p = m_ppInf[cur];
    }

    if (p->nElem == MAXENTRY)
    {
        BlockInfo* q;
        if (cur < (m_nBlock - 1) && m_ppInf[cur + 1]->nElem < MAXENTRY)
        {
            q = m_ppInf[cur + 1];
            if (q->nElem)
            {
                int nCount = q->nElem;
                auto pFrom = q->mvData.begin() + nCount;
                auto pTo = pFrom + 1;
                while (nCount--)
                {
                    *--pTo = *--pFrom;
                    ++((*pTo)->m_nOffset);
                }
            }
            q->nStart--;
            q->nEnd--;
        }
        else
        {
            if (m_nBlock > (m_nSize / (MAXENTRY / 2)) && cur >= Compress())
            {
                Insert(pElem, pos);
                return;
            }
            q = InsBlock(cur + 1);
        }

        BigPtrEntry* pLast = p->mvData[MAXENTRY - 1];
        pLast->m_nOffset = 0;
        pLast->m_pBlock = q;
        q->mvData[0] = pLast;
        q->nElem++;
        q->nEnd++;
        p->nEnd--;
        p->nElem--;
    }

    pos -= p->nStart;
    assert(pos < MAXENTRY);
    if (pos != p->nElem)
    {
        int nCount = p->nElem - sal_uInt16(pos);
        auto pFrom = p->mvData.begin() + p->nElem;
        auto pTo = pFrom + 1;
        while (nCount--)
        {
            *--pTo = *--pFrom;
            ++(*pTo)->m_nOffset;
        }
    }
    pElem->m_nOffset = sal_uInt16(pos);
    pElem->m_pBlock = p;
    p->mvData[pos] = pElem;
    p->nEnd++;
    p->nElem++;
    m_nSize++;
    if (cur != (m_nBlock - 1))
        UpdIndex(cur);
    m_nCur = cur;
}

void BigPtrArray::Remove(sal_Int32 pos, sal_Int32 n) { ImplRemove(pos, n, true); }

void BigPtrArray::ImplRemove(sal_Int32 pos, sal_Int32 n, bool bClearElement)
{
    sal_uInt16 nBlkdel = 0;
    sal_uInt16 cur = Index2Block(pos);
    sal_uInt16 nBlk1 = cur;
    sal_uInt16 nBlk1del = USHRT_MAX;
    BlockInfo* p = m_ppInf[cur];
    pos -= p->nStart;

    sal_Int32 nElem = n;
    while (nElem)
    {
        sal_uInt16 nel = p->nElem - sal_uInt16(pos);
        if (sal_Int32(nel) > nElem)
            nel = sal_uInt16(nElem);
        if (bClearElement)
            for (sal_uInt16 i = 0; i < nel; ++i)
            {
                p->mvData[pos + i]->m_pBlock = nullptr;
                p->mvData[pos + i]->m_nOffset = 0;
            }
        if ((pos + nel) < sal_Int32(p->nElem))
        {
            auto pTo = p->mvData.begin() + pos;
            auto pFrom = pTo + nel;
            int nCount = p->nElem - nel - sal_uInt16(pos);
            while (nCount--)
            {
                *pTo = *pFrom++;
                (*pTo)->m_nOffset = (*pTo)->m_nOffset - nel;
                ++pTo;
            }
        }
        p->nEnd -= nel;
        p->nElem = p->nElem - nel;
        if (!p->nElem)
        {
            nBlkdel++;
            if (USHRT_MAX == nBlk1del)
                nBlk1del = cur;
        }
        nElem -= nel;
        if (!nElem)
            break;
        p = m_ppInf[++cur];
        pos = 0;
    }

    if (nBlkdel)
    {
        for (sal_uInt16 i = nBlk1del; i < (nBlk1del + nBlkdel); i++)
            delete m_ppInf[i];
        if ((nBlk1del + nBlkdel) < m_nBlock)
        {
            std::copy(m_ppInf.get() + nBlk1del + nBlkdel, m_ppInf.get() + m_nBlock,
                      m_ppInf.get() + nBlk1del);
            if (!nBlk1)
            {
                p = m_ppInf[0];
                p->nStart = 0;
                p->nEnd = p->nElem - 1;
            }
            else
            {
                --nBlk1;
            }
        }
        BlockDel(nBlkdel);
    }

    m_nSize -= n;
    if (nBlk1 != (m_nBlock - 1) && m_nSize)
        UpdIndex(nBlk1);
    m_nCur = nBlk1;

    if (m_nBlock > (m_nSize / (MAXENTRY / 2)))
        Compress();
}

void BigPtrArray::Replace(sal_Int32 idx, BigPtrEntry* pElem) { ImplReplace(idx, pElem, true); }

void BigPtrArray::ImplReplace(sal_Int32 idx, BigPtrEntry* pElem, bool bClearElement)
{
    assert(idx < m_nSize);
    m_nCur = Index2Block(idx);
    BlockInfo* p = m_ppInf[m_nCur];
    pElem->m_nOffset = sal_uInt16(idx - p->nStart);
    pElem->m_pBlock = p;
    if (bClearElement)
    {
        p->mvData[idx - p->nStart]->m_pBlock = nullptr;
        p->mvData[idx - p->nStart]->m_nOffset = 0;
    }
    p->mvData[idx - p->nStart] = pElem;
}

sal_uInt16 BigPtrArray::Compress()
{
    BlockInfo **pp = m_ppInf.get(), **qq = pp;
    BlockInfo* p;
    BlockInfo* pLast = nullptr;
    sal_uInt16 nLast = 0;
    sal_uInt16 nBlkdel = 0;
    sal_uInt16 nFirstChgPos = USHRT_MAX;

    short const nMax
        = MAXENTRY - static_cast<short>(static_cast<long>(MAXENTRY) * COMPRESSLVL / 100);

    for (sal_uInt16 cur = 0; cur < m_nBlock; ++cur)
    {
        p = *pp++;
        sal_uInt16 n = p->nElem;
        if (nLast && (n > nLast) && (nLast < nMax))
            nLast = 0;
        if (nLast)
        {
            if (USHRT_MAX == nFirstChgPos)
                nFirstChgPos = cur;
            if (n > nLast)
                n = nLast;
            auto pElem = pLast->mvData.begin() + pLast->nElem;
            auto pFrom = p->mvData.begin();
            for (sal_uInt16 nCount = n, nOff = pLast->nElem; nCount; --nCount, ++pElem)
            {
                *pElem = *pFrom++;
                (*pElem)->m_pBlock = pLast;
                (*pElem)->m_nOffset = nOff++;
            }
            pLast->nElem = pLast->nElem + n;
            nLast = nLast - n;
            p->nElem = p->nElem - n;
            if (!p->nElem)
            {
                delete p;
                p = nullptr;
                ++nBlkdel;
            }
            else
            {
                pElem = p->mvData.begin();
                pFrom = pElem + n;
                int nCount = p->nElem;
                while (nCount--)
                {
                    *pElem = *pFrom++;
                    (*pElem)->m_nOffset = (*pElem)->m_nOffset - n;
                    ++pElem;
                }
            }
        }
        if (p)
        {
            *qq++ = p;
            if (!nLast && p->nElem < MAXENTRY)
            {
                pLast = p;
                nLast = MAXENTRY - p->nElem;
            }
        }
    }

    if (nBlkdel)
        BlockDel(nBlkdel);

    p = m_ppInf[0];
    p->nEnd = p->nElem - 1;
    UpdIndex(0);

    if (m_nCur >= nFirstChgPos)
        m_nCur = 0;

    return nFirstChgPos;
}
