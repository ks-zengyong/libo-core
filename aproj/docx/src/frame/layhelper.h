#pragma once
// Layout helper classes - migrated from LibreOffice sw/source/core/layout/layhelp.hxx
// Simplified version for aproj, keeping core interfaces consistent with LO

#include "../core/types.h"  // 确保 sal_uLong 等类型已定义
#include "../core/swrect.h"
#include <memory>
#include <vector>
#include <deque>

// Forward declarations
class SwDoc;
class SwFrame;
class SwLayoutFrame;
class SwPageFrame;
class SwSectionFrame;
class SwSectionNode;
class SwNode;

/*
 * SwLayCacheImpl: Contains the page break information and text frame positions
 * of the document (after loading). Used inside the constructor of the layout
 * rootframe to insert content and text frames at the right pages.
 *
 * For every page of the main text (body content, no footnotes, text frames etc.)
 * we have the nodeindex of the first content at the page,
 * the type of content (table or paragraph),
 * and if it's not the first part of the table/paragraph,
 * the row/character-offset inside the table/paragraph.
 */

// Record type constants (same as LO)
#define SW_LAYCACHE_IO_REC_PAGES    'p'
#define SW_LAYCACHE_IO_REC_PARA     'P'
#define SW_LAYCACHE_IO_REC_TABLE    'T'
#define SW_LAYCACHE_IO_REC_FLY      'F'

#define SW_LAYCACHE_IO_VERSION_MAJOR    1
#define SW_LAYCACHE_IO_VERSION_MINOR    1

// SwFlyCache: Stored information about text frames (position and size)
class SwFlyCache : public SwRect
{
public:
    sal_uLong nOrdNum = 0;      // Id to recognize text frames
    sal_uInt16 nPageNum = 0;    // page number

    SwFlyCache() = default;
    
    SwFlyCache(sal_uInt16 nP, sal_uLong nO, SwTwips nXL, SwTwips nYL, SwTwips nWL, SwTwips nHL)
        : SwRect(nXL, nYL, nWL, nHL), nOrdNum(nO), nPageNum(nP) {}
};

typedef std::vector<SwFlyCache> SwPageFlyCache;

class SwLayCacheImpl
{
    std::vector<SwNodeOffset> mIndices;       // Node indices for page breaks
    std::deque<sal_Int32> m_aOffset;          // Text frame char offset or table row index
    std::vector<sal_uInt16> m_aType;          // Content type (para/table)
    SwPageFlyCache m_FlyCache;                 // Fly frame cache
    bool m_bUseFlyCache;

    void Insert(sal_uInt16 nType, SwNodeOffset nIndex, sal_Int32 nOffset);

public:
    SwLayCacheImpl();

    size_t size() const { return mIndices.size(); }

    // Read layout cache from stream (simplified)
    bool Read(/* SvStream& rStream - placeholder for now */);

    SwNodeOffset GetBreakIndex(size_t nIdx) const { return mIndices[nIdx]; }
    sal_Int32 GetBreakOfst(size_t nIdx) const { return m_aOffset[nIdx]; }
    sal_uInt16 GetBreakType(size_t nIdx) const { return m_aType[nIdx]; }

    size_t GetFlyCount() const { return m_FlyCache.size(); }
    SwFlyCache& GetFlyCache(size_t nIdx) { return m_FlyCache[nIdx]; }

    bool IsUseFlyCache() const { return m_bUseFlyCache; }

    // Simplified: add a break entry directly
    void AddBreakEntry(sal_uInt16 nType, SwNodeOffset nIndex, sal_Int32 nOffset);
};

/*
 * SwActualSection: Helps to create the section frames during the InsertCnt_-function
 * by controlling nested sections.
 */
class SwActualSection
{
    SwActualSection* m_pUpper;
    SwSectionFrame* m_pSectFrame;
    SwFrame* m_pLastPos;       // Split it *after* this child frame
    SwSectionNode* m_pSectNode;

public:
    SwActualSection(SwActualSection* pUpper, SwSectionFrame* pSect, SwSectionNode* pNd);

    SwSectionFrame* GetSectionFrame() { return m_pSectFrame; }
    void SetSectionFrame(SwSectionFrame* p) { m_pSectFrame = p; }
    SwSectionNode* GetSectionNode() { return m_pSectNode; }
    void SetUpper(SwActualSection* p) { m_pUpper = p; }
    SwActualSection* GetUpper() { return m_pUpper; }
    void SetLastPos(SwFrame* p) { m_pLastPos = p; }
    SwFrame* GetLastPos() const { return m_pLastPos; }
};

/*
 * SwLayHelper: Helps during the InsertCnt_ function to create new pages.
 * If there's a layout cache available, this information is used.
 */
class SwLayHelper
{
    SwFrame*& mrpFrame;
    SwFrame*& mrpPrv;
    SwPageFrame*& mrpPage;
    SwLayoutFrame*& mrpLay;
    std::unique_ptr<SwActualSection>& mrpActualSection;
    bool mbBreakAfter;
    SwDoc& mrDoc;
    SwLayCacheImpl* mpImpl;
    SwNodeOffset mnStartOfContent;
    size_t mnIndex;          // Index in the page break array
    size_t mnFlyIdx;         // Index in the fly cache array
    bool mbFirst;

    void CheckFlyCache_(SwPageFrame* pPage);

public:
    SwLayHelper(SwDoc& rDoc, SwFrame*& rpF, SwFrame*& rpP, SwPageFrame*& rpPg,
                SwLayoutFrame*& rpL, std::unique_ptr<SwActualSection>& rpA,
                SwNodeOffset nNodeIndex, bool bCache);
    ~SwLayHelper();

    // Calculate or estimate page count
    sal_uLong CalcPageCount();

    // Check if we need to insert a new page at this node index
    bool CheckInsert(SwNodeOffset nNodeIndex);

    // Static helper: insert a page based on break items
    static bool CheckInsertPage(SwPageFrame*& rpPage, SwLayoutFrame*& rpLay,
                                SwFrame*& rpFrame, bool& rIsBreakAfter);

    // Check fly frames at this (new) page and set them to the right position
    void CheckFlyCache(SwPageFrame* pPage)
    {
        if (mpImpl && mnFlyIdx < mpImpl->GetFlyCount())
            CheckFlyCache_(pPage);
    }
};