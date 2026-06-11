#pragma once
// 简化版 BigPtrArray，对应 LibreOffice 的 sw/inc/bparr.hxx
// 从 LibreOffice 源码简化而来，保留核心功能

#include "types.h"
#include <array>
#include <memory>
#include <cassert>

struct BlockInfo;
class BigPtrArray;

class BigPtrEntry
{
    friend class BigPtrArray;
    BlockInfo* m_pBlock = nullptr;
    sal_uInt16 m_nOffset = 0;

public:
    BigPtrEntry() = default;
    BigPtrEntry(BigPtrEntry const&) = default;
    virtual ~BigPtrEntry() = default;
    BigPtrEntry& operator=(BigPtrEntry const&) = default;

    sal_Int32 GetPos() const;
    BigPtrArray& GetArray() const;
    bool IsDisconnected() const { return m_pBlock == nullptr; }
};

// 每个 Block 存储 1000 个条目
constexpr sal_uInt16 MAXENTRY = 1000;

// 压缩时允许的空闲百分比
constexpr short COMPRESSLVL = 80;

struct BlockInfo final
{
    BigPtrArray* pBigArr = nullptr;
    sal_Int32 nStart = 0;
    sal_Int32 nEnd = 0;
    sal_uInt16 nElem = 0;
    std::array<BigPtrEntry*, MAXENTRY> mvData{};
};

class BigPtrArray
{
protected:
    std::unique_ptr<BlockInfo* []> m_ppInf;
    sal_Int32 m_nSize = 0;
    sal_uInt16 m_nMaxBlock = 0;
    sal_uInt16 m_nBlock = 0;
    mutable sal_uInt16 m_nCur = 0;

    sal_uInt16 Index2Block(sal_Int32) const;
    BlockInfo* InsBlock(sal_uInt16);
    void BlockDel(sal_uInt16);
    void UpdIndex(sal_uInt16);
    void ImplRemove(sal_Int32 pos, sal_Int32 n, bool bClearElement);
    void ImplReplace(sal_Int32 idx, BigPtrEntry* pElem, bool bClearElement);
    sal_uInt16 Compress();

public:
    BigPtrArray();
    ~BigPtrArray();

    sal_Int32 Count() const { return m_nSize; }

    void Insert(BigPtrEntry* p, sal_Int32 pos);
    void Remove(sal_Int32 pos, sal_Int32 n = 1);
    void Move(sal_Int32 from, sal_Int32 to);
    void Replace(sal_Int32 pos, BigPtrEntry* p);

    BigPtrEntry* operator[](sal_Int32) const;
};

inline sal_Int32 BigPtrEntry::GetPos() const
{
    assert(m_pBlock && "BigPtrEntry not in any block");
    assert(this == m_pBlock->mvData[m_nOffset]);
    return m_pBlock->nStart + m_nOffset;
}

inline BigPtrArray& BigPtrEntry::GetArray() const
{
    assert(m_pBlock && "BigPtrEntry not in any block");
    return *m_pBlock->pBigArr;
}
