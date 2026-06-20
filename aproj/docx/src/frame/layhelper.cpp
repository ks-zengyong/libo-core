// Layout helper classes implementation - migrated from LibreOffice sw/source/core/layout/laycache.cxx
// Simplified version for aproj, keeping core interfaces consistent with LO

#include "layhelper.h"
#include "frame.h"
#include "frmtree.h"  // for InsertNewPage
#include "../core/types.h"
#include "../core/doc.h"
#include "../core/node.h"
#include <limits>
#include <iostream>

// ============================================================================
// SwLayCacheImpl implementation
// ============================================================================

SwLayCacheImpl::SwLayCacheImpl()
    : m_bUseFlyCache(false)
{
}

void SwLayCacheImpl::Insert(sal_uInt16 nType, SwNodeOffset nIndex, sal_Int32 nOffset)
{
    m_aType.push_back(nType);
    mIndices.push_back(nIndex);
    m_aOffset.push_back(nOffset);
}

void SwLayCacheImpl::AddBreakEntry(sal_uInt16 nType, SwNodeOffset nIndex, sal_Int32 nOffset)
{
    Insert(nType, nIndex, nOffset);
}

bool SwLayCacheImpl::Read(/* SvStream& rStream - placeholder */)
{
    // Simplified: In LO, this reads from a binary stream
    // For aproj, we will implement a simpler version later
    // Currently returns false to indicate no cache available
    return false;
}

// ============================================================================
// SwActualSection implementation
// ============================================================================

SwActualSection::SwActualSection(SwActualSection* pUp, SwSectionFrame* pSect, SwSectionNode* pNd)
    : m_pUpper(pUp)
    , m_pSectFrame(pSect)
    , m_pLastPos(nullptr)
    , m_pSectNode(pNd)
{
    // In LO, if m_pSectNode is null, it's derived from pSect's format content
    // Simplified: we assume pNd is provided directly
}

// ============================================================================
// SwLayHelper implementation
// ============================================================================

SwLayHelper::SwLayHelper(SwDoc& rDoc, SwFrame*& rpF, SwFrame*& rpP, SwPageFrame*& rpPg,
                         SwLayoutFrame*& rpL, std::unique_ptr<SwActualSection>& rpA,
                         SwNodeOffset nNodeIndex, bool bCache)
    : mrpFrame(rpF)
    , mrpPrv(rpP)
    , mrpPage(rpPg)
    , mrpLay(rpL)
    , mrpActualSection(rpA)
    , mbBreakAfter(false)
    , mrDoc(rDoc)
    , mnFlyIdx(0)
    , mbFirst(bCache)
{
    // In LO, this initializes from the document's layout cache
    // Simplified: we set mpImpl to nullptr for now (no cache)
    mpImpl = nullptr;
    mnIndex = std::numeric_limits<size_t>::max();
    mnStartOfContent = 0; // Will be set properly when integrated with SwDoc
}

SwLayHelper::~SwLayHelper()
{
    // In LO, this unlocks the layout cache if it was locked
    // Simplified: nothing to do since we don't have a cache lock
}

sal_uLong SwLayHelper::CalcPageCount()
{
    // In LO, this returns the page count from the layout cache if available,
    // otherwise estimates based on document statistics
    // Simplified: return a default estimate
    sal_uLong nPgCount = 0;

    if (mpImpl)
    {
        nPgCount = mpImpl->size() + 1;
    }
    else
    {
        // Estimate based on content (simplified)
        // In LO, this uses IDocumentStatistics
        nPgCount = 0; // No page insertion for small documents
    }

    return nPgCount;
}

bool SwLayHelper::CheckInsertPage(SwPageFrame*& rpPage, SwLayoutFrame*& rpLay,
                                   SwFrame*& rpFrame, bool& rIsBreakAfter)
{
    // 对应 LO SwLayHelper::CheckInsertPage (laycache.cxx:615-688)
    // LO 逻辑:
    //   1. 从 frame 获取 SvxFormatBreakItem 和 SwFormatPageDesc
    //   2. 检查 PageBefore/PageAfter/PageBoth
    //   3. 如果有 break 或 page desc 变更，调用 InsertNewPage
    //   4. 更新 rpPage/rpLay 到新页面
    //
    // aproj 适配:
    //   - RES_BREAK 属性存储在 TextNode 上（"page"/"section"/"continuous"）
    //   - "page" 对应 LO 的 PageBefore
    //   - 暂不支持 PageAfter/PageBoth（OOXML w:br w:type="page" 已在解析时转为 RES_BREAK="page"）

    bool bEnd = (nullptr == rpPage->GetNext());

    // 从 frame 获取关联的 TextNode，读取 RES_BREAK 属性
    // 对应 LO: rpFrame->GetBreakItem()
    bool bBrk = rIsBreakAfter;
    rIsBreakAfter = false; // Reset

    if (!bBrk && rpFrame && rpFrame->IsTextFrame())
    {
        SwContentNode* pCN = rpFrame->GetNode();
        if (pCN)
        {
            SwTextNode* pTN = pCN->GetTextNode();
            if (pTN)
            {
                const std::string* pBreak = pTN->GetAttr(RES_BREAK);
                if (pBreak && (*pBreak == "page" || *pBreak == "section"))
                {
                    // "page"/"section" 对应 LO PageBefore
                    bBrk = true;
                }
            }
        }
    }

    if (bBrk)
    {
        // 对应 LO: ::InsertNewPage(pDesc, rpPage->GetUpper(), ...)
        // aproj: 使用 InsertNewPage 创建新页面
        SwRootFrame* pRoot = static_cast<SwRootFrame*>(rpPage->GetUpper());
        if (!pRoot)
            return false;

        SwPageFrame* pNewPage = InsertNewPage(pRoot);
        if (!pNewPage)
            return false;

        // 对应 LO: 更新 rpPage 到新页面
        if (bEnd)
        {
            // 在末尾追加：rpPage 已由 InsertNewPage 追加到末尾
            rpPage = pNewPage;
        }
        else
        {
            // 非末尾：移动到下一页
            rpPage = static_cast<SwPageFrame*>(rpPage->GetNext());
            // aproj 无空页概念，跳过 LO 的空页检查
        }

        // 对应 LO: rpLay = rpPage->FindBodyCont()
        // aproj: body 是页面的第一个 lower
        SwFrame* pLower = rpPage->GetLower();
        rpLay = pLower && pLower->IsBodyFrame() ? static_cast<SwLayoutFrame*>(pLower) : nullptr;

        // 对应 LO: while (rpLay->Lower()) rpLay = static_cast<SwLayoutFrame*>(rpLay->Lower());
        // 走到最深的布局叶子（处理 Section/Column 嵌套）
        while (rpLay && rpLay->Lower() && rpLay->Lower()->IsLayoutFrame())
        {
            rpLay = static_cast<SwLayoutFrame*>(rpLay->Lower());
        }

        std::cerr << "[CheckInsertPage] New page created: pageNum="
                  << (rpPage ? rpPage->GetPhyPageNum() : 0) << std::endl;
        return true;
    }

    return false;
}

bool SwLayHelper::CheckInsert(SwNodeOffset nNodeIndex)
{
    // 对应 LO SwLayHelper::CheckInsert (laycache.cxx:697-880)
    // LO 主分页入口：处理表格/文本分拆、Follow 链、分页符、Section 帧
    // aproj 适配：无 layout cache (mpImpl=nullptr)，因此 do-while 仅执行一次，
    //   保留表格行计数、CheckInsertPage 调用、mrpPrv 高度填充、Section 帧处理

    bool bRet = false;
    nNodeIndex -= mnStartOfContent;

    // Step 1: 统计表格行数 (对应 LO laycache.cxx:701-711)
    sal_uInt16 nRows = 0;
    if (mrpFrame && mrpFrame->IsTabFrame())
    {
        SwFrame* pLow = static_cast<SwTabFrame*>(mrpFrame)->Lower();
        do
        {
            ++nRows;
            pLow = pLow ? pLow->GetNext() : nullptr;
        } while (pLow);
    }

    // Step 2: 首次调用跳过 (对应 LO laycache.cxx:712-717)
    // LO: 检查 layout cache 是否匹配当前节点；aproj 无 cache，直接跳过
    if (mbFirst && mpImpl && mnIndex < mpImpl->size())
    {
        // aproj: 无 cache 实现，mbFirst 仅在首次调用时为 true
        mbFirst = false;
    }

    // Step 3: 主处理循环 (对应 LO laycache.cxx:718-877)
    if (!mbFirst)
    {
        do
        {
            // LO: 若 mpImpl 存在，处理分拆 (text/table split) 和 break cache
            // aproj: 无 cache，跳过 split 处理

            // Step 3a: 调用 CheckInsertPage (对应 LO laycache.cxx:824-825)
            SwPageFrame* pLastPage = mrpPage;
            if (CheckInsertPage(mrpPage, mrpLay, mrpFrame, mbBreakAfter))
            {
                // Step 3b: 检查 fly cache (对应 LO laycache.cxx:827)
                CheckFlyCache_(pLastPage);

                // Step 3c: 填充前一帧高度 (对应 LO laycache.cxx:828-832)
                // LO: 若 mrpPrv 是 TextFrame 且高度未验证，设为父布局打印区高度
                // 这确保分页时上一帧填满剩余页面空间
                if (mrpPrv && mrpPrv->IsTextFrame() && !mrpPrv->isFrameAreaSizeValid())
                {
                    SwLayoutFrame* pUp = mrpPrv->GetUpper();
                    if (pUp)
                    {
                        // aproj: 复制 frame area，修改高度，写回
                        // 对应 LO: aFrm.Height(mrpPrv->GetUpper()->getFramePrintArea().Height())
                        SwTwips nFillHeight = pUp->getFramePrintArea().Height();
                        SwRect aArea = mrpPrv->getFrameArea();
                        aArea.SetHeight(nFillHeight);
                        mrpPrv->setFrameArea(aArea);
                    }
                }

                bRet = true;
                mrpPrv = nullptr;

                // Step 3d: Section 帧处理 (对应 LO laycache.cxx:837-873)
                // LO: 若存在 ActualSection，在新页面创建 Section 帧并移动内容
                // aproj: Section 架构不同（Task 3.x 处理），此处保留接口
                if (mrpActualSection && mrpActualSection->GetSectionFrame())
                {
                    // TODO Task 3.3: 迁移 LO Section 帧分页逻辑
                    // 当前 aproj Section 由 ProcessMultiColumnSection 处理（待删除）
                    // 暂保留空实现，避免破坏现有分页
                }
            }

            // LO: do-while 条件 - cache 中仍有当前节点的 break 条目
            // aproj: 无 cache，循环仅执行一次
        } while (mpImpl && mnIndex < mpImpl->size() &&
                 mpImpl->GetBreakIndex(mnIndex) == nNodeIndex);
    }

    mbFirst = false;
    return bRet;
}

void SwLayHelper::CheckFlyCache_(SwPageFrame* pPage)
{
    // In LO, this checks fly frames at a new page and sets their positions
    // from the fly cache
    // Simplified: placeholder implementation
    if (!mpImpl || !pPage)
        return;

    // Would check fly cache entries and position fly frames
    // For now, this is a no-op placeholder
}