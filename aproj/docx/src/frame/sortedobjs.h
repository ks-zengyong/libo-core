#pragma once
// SwSortedObjs - 浮动对象管理类
// 迁移自 LibreOffice sw/source/core/inc/sortedobjs.hxx
// 简化版：使用 SwFlyFrame* 代替 SwAnchoredObject*

#include <vector>
#include <cstddef>

class SwFlyFrame;
class SwFrame;

/** class for collecting anchored objects

    Anchored objects can be inserted and deleted. The entries can be directly
    accessed via index.
    An anchored object is inserted sorted. The sort criteria are simplified:
    - based on insertion order (no complex anchor position sorting)
    
    This is a simplified version of LO's SwSortedObjs.
*/
class SwSortedObjs
{
private:
    std::vector<SwFlyFrame*> m_aObjs;

public:
    typedef std::vector<SwFlyFrame*>::const_iterator const_iterator;

    SwSortedObjs();
    ~SwSortedObjs();

    size_t size() const;

    /** direct access to the entries

        @param nIndex
        input parameter - index of entry, valid value range [0..size()-1]
    */
    SwFlyFrame* operator[](size_t nIndex) const;
    
    const_iterator begin() const { return m_aObjs.begin(); }
    const_iterator end() const { return m_aObjs.end(); }

    /** Insert a fly frame (simplified: append at end)
        
        @return true if successfully inserted
    */
    bool Insert(SwFlyFrame* pFly);

    /** Remove a fly frame */
    void Remove(SwFlyFrame* pFly);

    /** Check if the list contains the given fly frame */
    bool Contains(const SwFlyFrame* pFly) const;

    /** Get position of fly frame in list
        
        Returns size() if not found.
    */
    size_t GetPos(const SwFlyFrame* pFly) const;

    /** Update the position of the given fly frame in the sorted list
        (simplified: no re-sorting, just validation)
    */
    void Update(const SwFlyFrame* pFly);

    /** Update all entries (re-sort) - simplified: no action */
    void UpdateAll();

    /** Check if the list is sorted (simplified: always true) */
    bool is_sorted() const { return true; }

    /** Get anchor frame for a fly (simplified: stored separately) */
    SwFrame* GetAnchorFrame(size_t nIndex) const;
    
    /** Set anchor frame for a fly */
    void SetAnchorFrame(size_t nIndex, SwFrame* pAnchor);

private:
    // 简化版：存储锚点信息
    std::vector<SwFrame*> m_aAnchors;
};