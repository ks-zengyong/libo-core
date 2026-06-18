// 简化版 Frame 实现，对应 LibreOffice 的 sw/source/core/layout/

#include "frame.h"
#include "frmtree.h"
#include "sortedobjs.h"
#include "../core/node.h"
#include "../core/format.h"
#include "../render/output_device.h"
#include <algorithm>
#include <cassert>

//===----------------------------------------------------------------------===//
// SwFrameAreaDefinition
//===----------------------------------------------------------------------===//

sal_uInt32 SwFrameAreaDefinition::snLastFrameId = 0;

//===----------------------------------------------------------------------===//
// SwFrame
//===----------------------------------------------------------------------===//

SwFrame::SwFrame(SwFrameType nType, SwLayoutFrame* pParent)
    : mnFrameType(nType)
    , mpRoot(nullptr)
    , mpUpper(pParent)
    , mpNext(nullptr)
    , mpPrev(nullptr)
    , mpFormat(nullptr)
    , mbVertical(false)
    , mbRightToLeft(false)
    , mbFixSize(false)
    , mbCompletePaint(false)
    , mbRetouche(false)
    , mbInfBody(false)
    , mbInfTab(false)
    , mbInfFly(false)
    , mbInfFootnote(false)
    , mbInfSct(false)
{
    mnFrameId = ++snLastFrameId;
}

SwFrame::~SwFrame()
{
    // 从树中移除
    if (mpUpper)
    {
        Cut();
    }
}

SwRootFrame* SwFrame::getRootFrame()
{
    SwFrame* pFrame = this;
    while (pFrame->GetUpper())
    {
        pFrame = pFrame->GetUpper();
    }
    return pFrame->IsRootFrame() ? static_cast<SwRootFrame*>(pFrame) : nullptr;
}

SwPageFrame* SwFrame::FindPageFrame()
{
    SwFrame* pFrame = this;
    while (pFrame && !pFrame->IsPageFrame())
    {
        pFrame = pFrame->GetUpper();
    }
    return pFrame ? static_cast<SwPageFrame*>(pFrame) : nullptr;
}

SwPageFrame* SwFrame::FindPageFrame() const
{
    const SwFrame* pFrame = this;
    while (pFrame && !pFrame->IsPageFrame())
    {
        pFrame = pFrame->GetUpper();
    }
    return pFrame ? const_cast<SwPageFrame*>(static_cast<const SwPageFrame*>(pFrame)) : nullptr;
}

SwLayoutFrame* SwFrame::FindTabFrame()
{
    SwFrame* pFrame = this;
    while (pFrame && !pFrame->IsTabFrame())
    {
        pFrame = pFrame->GetUpper();
    }
    return pFrame ? static_cast<SwLayoutFrame*>(pFrame) : nullptr;
}

SwLayoutFrame* SwFrame::FindFlyFrame()
{
    SwFrame* pFrame = this;
    while (pFrame && pFrame->GetType() != SwFrameType::Fly)
    {
        pFrame = pFrame->GetUpper();
    }
    return pFrame ? static_cast<SwLayoutFrame*>(pFrame) : nullptr;
}

// 新增：查找 SectionFrame
SwSectionFrame* SwFrame::FindSctFrame()
{
    SwFrame* pFrame = this;
    while (pFrame && !pFrame->IsSctFrame())
    {
        pFrame = pFrame->GetUpper();
    }
    return pFrame ? static_cast<SwSectionFrame*>(pFrame) : nullptr;
}

// 新增：查找 FootnoteFrame
SwFootnoteFrame* SwFrame::FindFootnoteFrame()
{
    SwFrame* pFrame = this;
    while (pFrame && !pFrame->IsFootnoteFrame())
    {
        pFrame = pFrame->GetUpper();
    }
    return pFrame ? static_cast<SwFootnoteFrame*>(pFrame) : nullptr;
}

const SwFootnoteFrame* SwFrame::FindFootnoteFrame() const
{
    return const_cast<SwFrame*>(this)->FindFootnoteFrame();
}

void SwFrame::InsertBefore(SwLayoutFrame* pParent, SwFrame* pSibling)
{
    assert(pParent);
    Cut();
    mpUpper = pParent;
    mpPrev = pSibling ? pSibling->GetPrev() : nullptr;
    mpNext = pSibling;

    if (mpPrev)
        mpPrev->mpNext = this;
    if (mpNext)
        mpNext->mpPrev = this;

    if (!mpPrev && mpUpper)
    {
        // 成为第一个子节点
        // 注意：m_pLower 是 SwLayoutFrame 的成员
    }
}

void SwFrame::InsertBehind(SwLayoutFrame* pParent, SwFrame* pSibling)
{
    assert(pParent);
    Cut();
    mpUpper = pParent;
    mpPrev = pSibling;
    mpNext = pSibling ? pSibling->GetNext() : nullptr;

    if (mpPrev)
        mpPrev->mpNext = this;
    if (mpNext)
        mpNext->mpPrev = this;

    if (!mpPrev && mpUpper)
    {
        // 成为第一个子节点，更新父节点的 m_pLower
        mpUpper->SetLower(this);
    }
}

void SwFrame::RemoveFromLayout() { Cut(); }

void SwFrame::Cut()
{
    if (mpPrev)
        mpPrev->mpNext = mpNext;
    if (mpNext)
        mpNext->mpPrev = mpPrev;

    if (mpUpper && mpUpper->Lower() == this)
    {
        // 我是第一个子节点，更新父节点的 m_pLower
        mpUpper->SetLower(mpNext);
    }

    mpPrev = mpNext = nullptr;
    mpUpper = nullptr;
}

void SwFrame::Paste(SwLayoutFrame* pParent, SwFrame* pSibling)
{
    assert(pParent);
    mpUpper = pParent;
    InsertBehind(pParent, pSibling);
}

SwTwips SwFrame::Grow(SwTwips nDiff)
{
    SwRect aArea(getFrameArea());
    aArea.SetHeight(aArea.Height() + nDiff);
    setFrameArea(aArea);
    return nDiff; // 返回实际增长的量
}

SwTwips SwFrame::Shrink(SwTwips nDiff)
{
    SwRect aArea(getFrameArea());
    SwTwips nActualShrink = std::min(nDiff, aArea.Height());
    aArea.SetHeight(std::max(SwTwips(0), aArea.Height() - nActualShrink));
    setFrameArea(aArea);
    return nActualShrink; // 返回实际收缩的量
}

void SwFrame::ChgSize(const SwRect& rNewSize) { setFrameArea(rNewSize); }

// === 位置计算（对应 LO frame.hxx: MakePos） ===
void SwFrame::MakePos()
{
    // 简化版：计算 Frame 的位置
    // 对应 LO: SwFrame::MakePos

    if (isFrameAreaPositionValid())
        return;

    // 如果有 Upper，从 Upper 计算位置
    if (mpUpper)
    {
        SwRect aArea = getFrameArea();
        SwRect aUpperArea = mpUpper->getFrameArea();
        SwRect aUpperPrtArea = mpUpper->getFramePrintArea();

        // 计算相对于 Upper 的位置
        if (!mpPrev)
        {
            // 第一个子节点：从 Upper 的打印区域开始
            aArea.SetLeft(aUpperArea.Left() + aUpperPrtArea.Left());
            aArea.SetTop(aUpperArea.Top() + aUpperPrtArea.Top());
        }
        else
        {
            // 后续子节点：从 Prev 的底部开始
            SwRect aPrevArea = mpPrev->getFrameArea();
            aArea.SetLeft(aPrevArea.Left());
            aArea.SetTop(aPrevArea.Top() + aPrevArea.Height());
        }

        setFrameArea(aArea);
    }

    setFrameAreaPositionValid(true);
}

void SwFrame::InvalidateSize() { setFrameAreaSizeValid(false); }

void SwFrame::InvalidatePrt() { setFramePrintAreaValid(false); }

void SwFrame::InvalidatePos() { setFrameAreaPositionValid(false); }

void SwFrame::InvalidateAll()
{
    setFrameAreaPositionValid(false);
    setFrameAreaSizeValid(false);
    setFramePrintAreaValid(false);
}

void SwFrame::InvalidateNextPos()
{
    if (mpNext)
    {
        mpNext->InvalidatePos();
    }
}

// === 页面无效化 ===
void SwFrame::InvalidatePage(SwPageFrame* pPage)
{
    // 对应 LO frame.hxx: InvalidatePage
    // 简化版：无效化页面
    if (!pPage)
        pPage = FindPageFrame();
    if (pPage)
    {
        pPage->InvalidateSize();
        pPage->InvalidatePos();
    }
}

void SwFrame::InvalidatePage(const SwPageFrame* pPage)
{
    // 静态版本
    if (pPage)
    {
        const_cast<SwPageFrame*>(pPage)->InvalidateSize();
        const_cast<SwPageFrame*>(pPage)->InvalidatePos();
    }
}

SwContentNode* SwFrame::GetNode() const
{
    // 对于内容 Frame，返回关联的节点
    // 对于布局 Frame，返回 nullptr
    return nullptr;
}

// OptCalc 实现（const 版本，调用非 const 的 Calc）
void SwFrame::OptCalc() const
{
    // 使用 const_cast 来调用非 const 的 Calc
    const_cast<SwFrame*>(this)->Calc();
}

//===----------------------------------------------------------------------===//
// SwFrame 辅助函数实现（迁移自 LO frame.cxx）
//===----------------------------------------------------------------------===//

SwFrame* SwFrame::GetIndPrev() const
{
    // 迁移自 LO frame.hxx: GetIndPrev
    // 获取独立前驱（跳过 Follow 链中的非独立 Frame）
    // 简化版：直接返回 GetPrev()
    return GetPrev();
}

SwFrame* SwFrame::GetIndNext() const
{
    // 迁移自 LO frame.hxx: GetIndNext
    // 获取独立后继（跳过 Follow 链中的非独立 Frame）
    // 简化版：直接返回 GetNext()
    return GetNext();
}

SwContentFrame* SwFrame::FindNextCnt()
{
    // 迁移自 LO frame.hxx: FindNextCnt
    // 查找下一个内容 Frame
    SwFrame* pFrame = GetNext();
    while (pFrame)
    {
        if (pFrame->IsContentFrame())
            return static_cast<SwContentFrame*>(pFrame);
        if (pFrame->IsLayoutFrame())
        {
            SwContentFrame* pCnt = static_cast<SwLayoutFrame*>(pFrame)->ContainsContent();
            if (pCnt)
                return pCnt;
        }
        pFrame = pFrame->GetNext();
    }
    // 向上查找
    SwFrame* pUpper = GetUpper();
    while (pUpper)
    {
        pFrame = pUpper->GetNext();
        while (pFrame)
        {
            if (pFrame->IsContentFrame())
                return static_cast<SwContentFrame*>(pFrame);
            if (pFrame->IsLayoutFrame())
            {
                SwContentFrame* pCnt = static_cast<SwLayoutFrame*>(pFrame)->ContainsContent();
                if (pCnt)
                    return pCnt;
            }
            pFrame = pFrame->GetNext();
        }
        pUpper = pUpper->GetUpper();
    }
    return nullptr;
}

const SwContentFrame* SwFrame::FindNextCnt() const
{
    return const_cast<SwFrame*>(this)->FindNextCnt();
}

SwContentFrame* SwFrame::FindPrevCnt()
{
    // 迁移自 LO frame.hxx: FindPrevCnt
    // 查找上一个内容 Frame
    SwFrame* pFrame = GetPrev();
    while (pFrame)
    {
        if (pFrame->IsContentFrame())
            return static_cast<SwContentFrame*>(pFrame);
        if (pFrame->IsLayoutFrame())
        {
            SwContentFrame* pCnt = static_cast<SwLayoutFrame*>(pFrame)->GetLastContent();
            if (pCnt)
                return pCnt;
        }
        pFrame = pFrame->GetPrev();
    }
    // 向上查找
    SwFrame* pUpper = GetUpper();
    while (pUpper)
    {
        pFrame = pUpper->GetPrev();
        while (pFrame)
        {
            if (pFrame->IsContentFrame())
                return static_cast<SwContentFrame*>(pFrame);
            if (pFrame->IsLayoutFrame())
            {
                SwContentFrame* pCnt = static_cast<SwLayoutFrame*>(pFrame)->GetLastContent();
                if (pCnt)
                    return pCnt;
            }
            pFrame = pFrame->GetPrev();
        }
        pUpper = pUpper->GetUpper();
    }
    return nullptr;
}

const SwContentFrame* SwFrame::FindPrevCnt() const
{
    return const_cast<SwFrame*>(this)->FindPrevCnt();
}

SwLayoutFrame* SwFrame::FindColFrame()
{
    // 迁移自 LO frame.hxx: FindColFrame
    // 查找列 Frame
    SwFrame* pFrame = this;
    while (pFrame && !pFrame->IsColumnFrame())
    {
        pFrame = pFrame->GetUpper();
    }
    return pFrame ? static_cast<SwLayoutFrame*>(pFrame) : nullptr;
}

const SwLayoutFrame* SwFrame::FindColFrame() const
{
    return const_cast<SwFrame*>(this)->FindColFrame();
}

SwLayoutFrame* SwFrame::FindFootnoteBossFrame(bool bFootnoteOnly)
{
    // 迁移自 LO frame.hxx: FindFootnoteBossFrame
    // 简化版：查找页面 Frame
    (void)bFootnoteOnly;
    return FindPageFrame();
}

const SwLayoutFrame* SwFrame::FindFootnoteBossFrame(bool bFootnoteOnly) const
{
    return const_cast<SwFrame*>(this)->FindFootnoteBossFrame(bFootnoteOnly);
}

bool SwFrame::IsMoveable() const
{
    // 迁移自 LO frame.hxx: IsMoveable
    // 简化版：检查是否有 Upper 和是否可以移动
    if (!GetUpper())
        return false;
    // 检查是否有前驱（可以向前移动）
    if (GetIndPrev())
        return true;
    // 检查是否有后继（可以向后移动）
    return GetUpper()->GetUpper() != nullptr;
}

SwLayoutFrame* SwFrame::GetLeaf(MakePageType eMakePage, bool bFwd)
{
    // 迁移自 LO frame.hxx: GetLeaf (行 911-962)
    // 简化版：获取下一个/上一个 Leaf
    if (IsInFootnote())
    {
        // 简化版：不处理脚注特殊情况
        return nullptr;
    }

    // 检查表格/节
    bool bInTab = IsInTab();
    bool bInSct = IsInSct();

    if (bInTab && (!IsTabFrame() || GetUpper()->IsInTab()))
    {
        // 简化版：不处理表格特殊情况
        return nullptr;
    }

    if (bInSct)
    {
        // 简化版：不处理节特殊情况
        return nullptr;
    }

    // 直接获取下一个/上一个布局 Leaf
    if (bFwd)
        return GetNextLayoutLeaf();
    else
        return GetPrevLayoutLeaf();
}

const SwLayoutFrame* SwFrame::GetLeaf(MakePageType eMakePage, bool bFwd, const SwFrame* pAnch) const
{
    // 迁移自 LO frame.hxx: GetLeaf (行 884-909)
    // 简化版：const 版本
    (void)pAnch;
    return const_cast<SwFrame*>(this)->GetLeaf(eMakePage, bFwd);
}

SwLayoutFrame* SwFrame::GetNextLayoutLeaf()
{
    // 迁移自 LO frame.hxx: GetNextLayoutLeaf
    // 简化版：获取下一个布局 Leaf
    SwFrame* pFrame = this;
    while (pFrame)
    {
        // 如果有子节点，进入第一个子节点
        if (pFrame->IsLayoutFrame())
        {
            SwLayoutFrame* pLay = static_cast<SwLayoutFrame*>(pFrame);
            if (pLay->Lower())
            {
                pFrame = pLay->Lower();
                continue;
            }
        }
        // 如果有下一个兄弟，进入下一个兄弟
        if (pFrame->GetNext())
        {
            pFrame = pFrame->GetNext();
            continue;
        }
        // 向上查找
        while (pFrame)
        {
            pFrame = pFrame->GetUpper();
            if (pFrame && pFrame->GetNext())
            {
                pFrame = pFrame->GetNext();
                break;
            }
        }
    }
    return pFrame && pFrame->IsLayoutFrame() ? static_cast<SwLayoutFrame*>(pFrame) : nullptr;
}

SwLayoutFrame* SwFrame::GetPrevLayoutLeaf()
{
    // 迁移自 LO frame.hxx: GetPrevLayoutLeaf
    // 简化版：获取上一个布局 Leaf
    SwFrame* pFrame = this;
    while (pFrame)
    {
        // 如果有前一个兄弟，进入前一个兄弟
        if (pFrame->GetPrev())
        {
            pFrame = pFrame->GetPrev();
            // 如果是布局 Frame，进入最后一个子节点
            while (pFrame->IsLayoutFrame())
            {
                SwLayoutFrame* pLay = static_cast<SwLayoutFrame*>(pFrame);
                if (pLay->Lower())
                {
                    // 找到最后一个子节点
                    SwFrame* pLast = pLay->Lower();
                    while (pLast->GetNext())
                        pLast = pLast->GetNext();
                    pFrame = pLast;
                }
                else
                    break;
            }
            continue;
        }
        // 向上查找
        pFrame = pFrame->GetUpper();
        if (pFrame && pFrame == this)
            break;
    }
    return pFrame && pFrame->IsLayoutFrame() ? static_cast<SwLayoutFrame*>(pFrame) : nullptr;
}

const SwLayoutFrame* SwFrame::GetNextLayoutLeaf() const
{
    return const_cast<SwFrame*>(this)->GetNextLayoutLeaf();
}

const SwLayoutFrame* SwFrame::GetPrevLayoutLeaf() const
{
    return const_cast<SwFrame*>(this)->GetPrevLayoutLeaf();
}

bool SwFrame::WrongPageDesc(SwPageFrame* pNew)
{
    // 迁移自 LO flowfrm.cxx: WrongPageDesc (行 984-1054)
    // 简化版：检查页面描述是否正确
    // 简化版：总是返回 false（页面描述正确）
    (void)pNew;
    return false;
}

SwFrame* SwFrame::FindPrev()
{
    // 迁移自 LO frame.hxx: FindPrev
    // 查找前一个 Frame（在布局树中）
    if (GetPrev())
        return GetPrev();
    // 向上查找
    SwFrame* pUpper = GetUpper();
    while (pUpper)
    {
        if (pUpper->GetPrev())
            return pUpper->GetPrev();
        pUpper = pUpper->GetUpper();
    }
    return nullptr;
}

const SwFrame* SwFrame::FindPrev() const { return const_cast<SwFrame*>(this)->FindPrev(); }

SwFrame* SwFrame::FindNext()
{
    // 迁移自 LO frame.hxx: FindNext
    // 查找下一个 Frame（在布局树中）
    if (GetNext())
        return GetNext();
    // 向上查找
    SwFrame* pUpper = GetUpper();
    while (pUpper)
    {
        if (pUpper->GetNext())
            return pUpper->GetNext();
        pUpper = pUpper->GetUpper();
    }
    return nullptr;
}

const SwFrame* SwFrame::FindNext() const { return const_cast<SwFrame*>(this)->FindNext(); }

//===----------------------------------------------------------------------===//
// SwLayoutFrame
//===----------------------------------------------------------------------===//

SwLayoutFrame::SwLayoutFrame(SwFrameType nType, SwLayoutFrame* pParent)
    : SwFrame(nType, pParent)
    , m_pLower(nullptr)
{
}

SwLayoutFrame::~SwLayoutFrame()
{
    // 删除所有子 Frame
    SwFrame* pFrame = m_pLower;
    while (pFrame)
    {
        SwFrame* pNext = pFrame->GetNext();
        delete pFrame;
        pFrame = pNext;
    }
}

const SwContentFrame* SwLayoutFrame::ContainsContent() const
{
    const SwFrame* pFrame = m_pLower;
    while (pFrame)
    {
        if (pFrame->IsContentFrame())
        {
            return static_cast<const SwContentFrame*>(pFrame);
        }
        if (pFrame->IsLayoutFrame())
        {
            const SwContentFrame* pContent
                = static_cast<const SwLayoutFrame*>(pFrame)->ContainsContent();
            if (pContent)
                return pContent;
        }
        pFrame = pFrame->GetNext();
    }
    return nullptr;
}

SwContentFrame* SwLayoutFrame::ContainsContent()
{
    SwFrame* pFrame = m_pLower;
    while (pFrame)
    {
        if (pFrame->IsContentFrame())
        {
            return static_cast<SwContentFrame*>(pFrame);
        }
        if (pFrame->IsLayoutFrame())
        {
            SwContentFrame* pContent = static_cast<SwLayoutFrame*>(pFrame)->ContainsContent();
            if (pContent)
                return pContent;
        }
        pFrame = pFrame->GetNext();
    }
    return nullptr;
}

SwContentFrame* SwLayoutFrame::GetLastContent()
{
    // 迁移自 LO layfrm.hxx: GetLastContent
    // 查找最后一个内容 Frame
    SwFrame* pFrame = m_pLower;
    SwContentFrame* pLastContent = nullptr;
    while (pFrame)
    {
        if (pFrame->IsContentFrame())
        {
            pLastContent = static_cast<SwContentFrame*>(pFrame);
        }
        else if (pFrame->IsLayoutFrame())
        {
            SwContentFrame* pContent = static_cast<SwLayoutFrame*>(pFrame)->GetLastContent();
            if (pContent)
                pLastContent = pContent;
        }
        pFrame = pFrame->GetNext();
    }
    return pLastContent;
}

const SwContentFrame* SwLayoutFrame::GetLastContent() const
{
    return const_cast<SwLayoutFrame*>(this)->GetLastContent();
}

bool SwLayoutFrame::IsAnLower(const SwFrame* pFrame) const
{
    while (pFrame)
    {
        if (pFrame == this)
            return true;
        pFrame = pFrame->GetUpper();
    }
    return false;
}

void SwLayoutFrame::Format()
{
    // 格式化所有子 Frame
    SwFrame* pFrame = m_pLower;
    while (pFrame)
    {
        pFrame->Format();
        pFrame = pFrame->GetNext();
    }
}

void SwLayoutFrame::MakeAll() { Format(); }

void SwLayoutFrame::PaintSwFrame(OutputDevice* pOutDev)
{
    // 绘制所有子 Frame — 与 LibreOffice 的 SwLayoutFrame::PaintSwFrame 流程对称
    SwFrame* pFrame = m_pLower;
    while (pFrame)
    {
        pFrame->PaintSwFrame(pOutDev);
        pFrame = pFrame->GetNext();
    }
}

void SwLayoutFrame::Cut() { SwFrame::Cut(); }

void SwLayoutFrame::Paste(SwLayoutFrame* pParent, SwFrame* pSibling)
{
    SwFrame::Paste(pParent, pSibling);
}

SwLayoutFrame* SwLayoutFrame::GetNextLayoutLeaf()
{
    // 迁移自 LO layfrm.hxx: GetNextLayoutLeaf
    // 简化版：调用 SwFrame 版本
    return SwFrame::GetNextLayoutLeaf();
}

SwLayoutFrame* SwLayoutFrame::GetPrevLayoutLeaf()
{
    // 迁移自 LO layfrm.hxx: GetPrevLayoutLeaf
    // 简化版：调用 SwFrame 版本
    return SwFrame::GetPrevLayoutLeaf();
}

const SwLayoutFrame* SwLayoutFrame::GetNextLayoutLeaf() const
{
    return SwFrame::GetNextLayoutLeaf();
}

const SwLayoutFrame* SwLayoutFrame::GetPrevLayoutLeaf() const
{
    return SwFrame::GetPrevLayoutLeaf();
}

//===----------------------------------------------------------------------===//
// SwRootFrame
//===----------------------------------------------------------------------===//

SwRootFrame::SwRootFrame()
    : SwLayoutFrame(SwFrameType::Root)
    , mpLastPage(nullptr)
    , mnPhyPageNums(0)
{
}

SwRootFrame::~SwRootFrame() = default;

void SwRootFrame::FormatAll()
{
    // 格式化所有页面
    SwFrame* pFrame = GetLower();
    while (pFrame)
    {
        pFrame->Format();
        pFrame = pFrame->GetNext();
    }
}

//===----------------------------------------------------------------------===//
// SwPageFrame
//===----------------------------------------------------------------------===//

SwPageFrame::SwPageFrame(SwRootFrame* pRoot)
    : SwLayoutFrame(SwFrameType::Page, pRoot)
    , m_nPhyPageNum(0)
    , m_pDesc(nullptr)
    , m_pSortedObjs(nullptr)
{
}

SwPageFrame::~SwPageFrame()
{
    // 删除 SwSortedObjs
    if (m_pSortedObjs)
    {
        delete m_pSortedObjs;
        m_pSortedObjs = nullptr;
    }
}

void SwPageFrame::PreparePage()
{
    // 创建页面结构（Body、Header、Footer）
    // 对应 LibreOffice SwPageFrame 构造函数：
    //   pBodyFrame->ChgSize(getFramePrintArea().SSize());
    //   pBodyFrame->Paste(this);
    // Body 的 frame area 位于页面打印区域位置，print area 从 (0,0) 开始
    if (!GetLower())
    {
        auto* pBody = new SwBodyFrame(this);
        pBody->InsertBehind(this, nullptr);
        // Body 的 frame area = 页面的打印区域（绝对坐标）
        const SwRect& rPrtArea = getFramePrintArea();
        SwRect aBodyRect(getFrameArea().Left() + rPrtArea.Left(),
                         getFrameArea().Top() + rPrtArea.Top(), rPrtArea.Width(),
                         rPrtArea.Height());
        pBody->setFrameArea(aBodyRect);
        // Body 的 print area 从 (0,0) 开始（相对于 Body 自身）
        SwRect aBodyPrtRect(0, 0, rPrtArea.Width(), rPrtArea.Height());
        pBody->setFramePrintArea(aBodyPrtRect);
    }
}

SwPageFrame* SwPageFrame::GetNextPage() const { return static_cast<SwPageFrame*>(GetNext()); }
SwPageFrame* SwPageFrame::GetPrevPage() const { return static_cast<SwPageFrame*>(GetPrev()); }

SwSortedObjs* SwPageFrame::MakeSortedObjs()
{
    if (!m_pSortedObjs)
        m_pSortedObjs = new SwSortedObjs();
    return m_pSortedObjs;
}

void SwPageFrame::RegisterAnchoredFly(SwFlyFrame* pFly, SwFrame* pAnchor)
{
    if (!pFly || !pAnchor)
        return;

    // 使用 SwSortedObjs 存储
    if (!m_pSortedObjs)
        m_pSortedObjs = new SwSortedObjs();

    m_pSortedObjs->Insert(pFly);
    size_t nPos = m_pSortedObjs->GetPos(pFly);
    m_pSortedObjs->SetAnchorFrame(nPos, pAnchor);

    // 兼容旧接口：同时存储到 m_aAnchoredFlies
    m_aAnchoredFlies.emplace_back(pFly, pAnchor);
}

SwHeaderFrame* SwPageFrame::FindHeaderFrame() const
{
    // 在页面子 Frame 中查找页眉 Frame
    SwFrame* pLower = GetLower();
    while (pLower)
    {
        if (pLower->IsHeaderFrame())
            return static_cast<SwHeaderFrame*>(pLower);
        pLower = pLower->GetNext();
    }
    return nullptr;
}

SwFooterFrame* SwPageFrame::FindFooterFrame() const
{
    SwFrame* pLower = GetLower();
    while (pLower)
    {
        if (pLower->IsFooterFrame())
            return static_cast<SwFooterFrame*>(pLower);
        pLower = pLower->GetNext();
    }
    return nullptr;
}

// === 页眉页脚准备（对应 LO hffrm.cxx: PrepareHeader/PrepareFooter 行 684-768） ===
void SwPageFrame::PrepareHeader()
{
    // 创建或移除页眉
    // 对应 LO: SwPageFrame::PrepareHeader

    SwLayoutFrame* pLay = static_cast<SwLayoutFrame*>(Lower());
    if (!pLay)
        return;

    // 简化版：检查是否需要页眉
    // 完整实现需要检查 SwFormatHeader::IsActive()

    // 查找现有页眉
    SwHeaderFrame* pExistingHeader = FindHeaderFrame();

    if (pExistingHeader)
    {
        // 已有页眉，检查是否需要更新
        // 简化版：不检查格式匹配
        return;
    }

    // 创建页眉 Frame
    // 简化版：使用默认高度
    SwTwips nHeaderHeight = 500; // 默认页眉高度（twips）

    SwHeaderFrame* pHeader = new SwHeaderFrame(this);
    pHeader->InsertBefore(this, pLay);

    // 设置页眉区域（页面顶部）
    SwRect aPageArea = getFrameArea();
    SwRect aHeaderRect(aPageArea.Left(), aPageArea.Top(), aPageArea.Width(), nHeaderHeight);
    pHeader->setFrameArea(aHeaderRect);
    pHeader->setFramePrintArea(SwRect(0, 0, aPageArea.Width(), nHeaderHeight));

    // 无效化页面布局
    InvalidateSize();
    InvalidatePos();
}

void SwPageFrame::PrepareFooter()
{
    // 创建或移除页脚
    // 对应 LO: SwPageFrame::PrepareFooter

    SwLayoutFrame* pLay = static_cast<SwLayoutFrame*>(Lower());
    if (!pLay)
        return;

    // 找到最后一个子 Frame
    while (pLay->GetNext())
        pLay = static_cast<SwLayoutFrame*>(pLay->GetNext());

    // 简化版：检查是否需要页脚
    // 完整实现需要检查 SwFormatFooter::IsActive()

    // 查找现有页脚
    SwFooterFrame* pExistingFooter = FindFooterFrame();

    if (pExistingFooter)
    {
        // 已有页脚，检查是否需要更新
        // 简化版：不检查格式匹配
        return;
    }

    // 创建页脚 Frame
    // 简化版：使用默认高度
    SwTwips nFooterHeight = 500; // 默认页脚高度（twips）

    SwFooterFrame* pFooter = new SwFooterFrame(this);
    pFooter->InsertBehind(this, nullptr);

    // 设置页脚区域（页面底部）
    SwRect aPageArea = getFrameArea();
    SwRect aFooterRect(aPageArea.Left(), aPageArea.Top() + aPageArea.Height() - nFooterHeight,
                       aPageArea.Width(), nFooterHeight);
    pFooter->setFrameArea(aFooterRect);
    pFooter->setFramePrintArea(SwRect(0, 0, aPageArea.Width(), nFooterHeight));

    // 无效化页面布局
    InvalidateSize();
    InvalidatePos();
}

// === 奇偶页处理 ===
bool SwPageFrame::HasOddEvenHeaderFooter() const
{
    // 简化版：检查页面描述符是否支持奇偶页不同
    // 完整实现需要检查 SwPageDesc::IsOddEvenPage()
    return false;
}

// === 首页不同处理 ===
bool SwPageFrame::HasFirstPageHeaderFooter() const
{
    // 简化版：检查页面描述符是否支持首页不同
    // 完整实现需要检查 SwPageDesc::IsFirstPage()
    return false;
}

// === 获取页眉页脚类型 ===
SwPageFrame::PageHeaderFooterType SwPageFrame::GetHeaderFooterType() const
{
    // 简化版：根据页面号判断
    sal_uInt16 nPageNum = GetPhyPageNum();

    // 首页检查
    if (nPageNum == 1 && HasFirstPageHeaderFooter())
        return First;

    // 奇偶页检查
    if (HasOddEvenHeaderFooter() && (nPageNum % 2) == 0)
        return Even;

    return Normal;
}

// === Body Frame 查找 ===
SwLayoutFrame* SwPageFrame::FindBodyFrame()
{
    // 查找页面内的 Body Frame
    SwFrame* pLower = GetLower();
    while (pLower)
    {
        if (pLower->IsBodyFrame())
            return static_cast<SwLayoutFrame*>(pLower);
        pLower = pLower->GetNext();
    }
    return nullptr;
}

const SwLayoutFrame* SwPageFrame::FindBodyFrame() const
{
    return const_cast<SwPageFrame*>(this)->FindBodyFrame();
}

SwFootnoteContFrame* SwPageFrame::MakeFootnoteCont()
{
    // 创建脚注容器 Frame，对应 LO ftnfrm.cxx: SwPageFrame::MakeFootnoteCont
    if (m_pFootnoteCont)
        return m_pFootnoteCont;

    m_pFootnoteCont = new SwFootnoteContFrame(this);
    m_pFootnoteCont->InsertBehind(this, nullptr);

    // 设置脚注容器区域（页面底部）
    SwRect aPageArea = getFrameArea();
    SwRect aPrtArea = getFramePrintArea();
    SwTwips nFnHeight = 0; // 初始高度为 0，随脚注添加而增长
    SwRect aFnContArea(aPageArea.Left() + aPrtArea.Left(),
                       aPageArea.Top() + aPrtArea.Top() + aPrtArea.Height() - nFnHeight,
                       aPrtArea.Width(), nFnHeight);
    m_pFootnoteCont->setFrameArea(aFnContArea);
    m_pFootnoteCont->setFramePrintArea(SwRect(0, 0, aPrtArea.Width(), nFnHeight));

    return m_pFootnoteCont;
}

//===----------------------------------------------------------------------===//
// SwBodyFrame
//===----------------------------------------------------------------------===//

SwBodyFrame::SwBodyFrame(SwPageFrame* pParent)
    : SwLayoutFrame(SwFrameType::Body, pParent)
{
}

SwBodyFrame::SwBodyFrame(SwLayoutFrame* pParent)
    : SwLayoutFrame(SwFrameType::Body, pParent)
{
}

SwBodyFrame::~SwBodyFrame() = default;

//===----------------------------------------------------------------------===//
// SwContentFrame
//===----------------------------------------------------------------------===//

SwContentFrame::SwContentFrame(SwFrameType nType, SwLayoutFrame* pParent)
    : SwFrame(nType, pParent)
    , SwFlowFrame(*this)
    , mpNode(nullptr)
    , mpFollow(nullptr)
    , mpMaster(nullptr)
{
}

SwContentFrame::~SwContentFrame()
{
    // 断开 Follow 链
    if (mpFollow)
    {
        mpFollow->mpMaster = nullptr;
    }
    if (mpMaster)
    {
        mpMaster->mpFollow = mpFollow;
    }
}

void SwContentFrame::Format()
{
    // 基本格式化
}

void SwContentFrame::MakeAll() { Format(); }

bool SwContentFrame::ShouldBwdMoved(SwLayoutFrame* pNewUpper, bool& rReformat)
{
    // 迁移自 LO cntfrm.cxx: ShouldBwdMoved
    // 简化版：总是允许向后移动
    (void)pNewUpper;
    rReformat = true;
    return true;
}

//===----------------------------------------------------------------------===//
// SwContentFrame 内容遍历方法
//===----------------------------------------------------------------------===//

SwContentFrame* SwContentFrame::GetNextContentFrame() const
{
    // 对应 LO SwContentFrame::GetNextContentFrame
    // 在布局树中查找下一个内容 Frame

    // 1. 如果有 Follow，返回 Follow
    if (mpFollow)
        return mpFollow;

    // 2. 在当前链中查找下一个
    const SwFrame* pFrame = this;
    while (pFrame)
    {
        // 检查兄弟节点
        const SwFrame* pNext = pFrame->GetNext();
        while (pNext)
        {
            if (pNext->IsContentFrame())
                return const_cast<SwContentFrame*>(static_cast<const SwContentFrame*>(pNext));
            if (pNext->IsLayoutFrame())
            {
                // 在布局 Frame 中查找内容
                SwContentFrame* pContent
                    = static_cast<SwLayoutFrame*>(const_cast<SwFrame*>(pNext))->ContainsContent();
                if (pContent)
                    return pContent;
            }
            pNext = pNext->GetNext();
        }

        // 向上查找
        pFrame = pFrame->GetUpper();
    }

    return nullptr;
}

SwContentFrame* SwContentFrame::GetPrevContentFrame() const
{
    // 对应 LO SwContentFrame::GetPrevContentFrame
    // 在布局树中查找上一个内容 Frame

    // 1. 如果有 Master，返回 Master
    if (mpMaster)
        return mpMaster;

    // 2. 在当前链中查找上一个
    const SwFrame* pFrame = this;
    while (pFrame)
    {
        // 检查兄弟节点
        const SwFrame* pPrev = pFrame->GetPrev();
        while (pPrev)
        {
            if (pPrev->IsContentFrame())
                return const_cast<SwContentFrame*>(static_cast<const SwContentFrame*>(pPrev));
            if (pPrev->IsLayoutFrame())
            {
                // 在布局 Frame 中查找最后一个内容
                SwLayoutFrame* pLay = static_cast<SwLayoutFrame*>(const_cast<SwFrame*>(pPrev));
                SwFrame* pLower = pLay->Lower();
                SwContentFrame* pLastContent = nullptr;
                while (pLower)
                {
                    if (pLower->IsContentFrame())
                        pLastContent = static_cast<SwContentFrame*>(pLower);
                    pLower = pLower->GetNext();
                }
                if (pLastContent)
                    return pLastContent;
            }
            pPrev = pPrev->GetPrev();
        }

        // 向上查找
        pFrame = pFrame->GetUpper();
    }

    return nullptr;
}

//===----------------------------------------------------------------------===//
// SwTextFrame
//===----------------------------------------------------------------------===//

SwTextFrame::SwTextFrame(SwContentNode* pNode, SwLayoutFrame* pParent)
    : SwContentFrame(SwFrameType::Txt, pParent)
    , mnThisLines(0)
    , mnOffset(0)
{
    mpNode = pNode;
}

SwTextFrame::~SwTextFrame() = default;

void SwTextFrame::Format()
{
    SwTextNode* pTextNode = static_cast<SwTextNode*>(GetNode());
    if (!pTextNode)
        return;

    SwTwips nWidth = getFrameArea().Width();
    if (nWidth <= 0)
        nWidth = getFramePrintArea().Width();
    SwTwips nHeight = CalcTextNodeFrameHeight(pTextNode, nWidth);

    SwRect aArea = getFrameArea();
    aArea.SetHeight(nHeight);
    setFrameArea(aArea);
}

void SwTextFrame::MakeAll() { Format(); }

void SwTextFrame::PaintSwFrame(OutputDevice* pOutDev)
{
    // 绘制文本 — 通过 OutputDevice 接口，与 LibreOffice 的 SwTextFrame::PaintSwFrame 对称
    if (!pOutDev || !mpNode)
        return;

    SwTextNode* pTextNode = static_cast<SwTextNode*>(mpNode);
    if (!pTextNode)
        return;

    // 设置字体属性（从文本节点获取）
    OutputFont aFont;
    const std::string* pFont = pTextNode->GetAttr(RES_CHRATR_FONT);
    if (pFont)
        aFont.familyName = *pFont;

    const std::string* pSize = pTextNode->GetAttr(RES_CHRATR_FONTSIZE);
    if (pSize)
        aFont.height = std::stoi(*pSize) * 10; // half-points → twips (×10)

    const std::string* pWeight = pTextNode->GetAttr(RES_CHRATR_WEIGHT);
    if (pWeight && *pWeight == "bold")
        aFont.weight = FontWeight::Bold;

    const std::string* pPosture = pTextNode->GetAttr(RES_CHRATR_POSTURE);
    if (pPosture && *pPosture == "italic")
        aFont.italic = FontItalic::Italic;

    const std::string* pColor = pTextNode->GetAttr(RES_CHRATR_COLOR);
    if (pColor && !pColor->empty())
    {
        try
        {
            uint32_t c = static_cast<uint32_t>(std::stoul(*pColor, nullptr, 16));
            aFont.color = OutputColor(static_cast<uint8_t>((c >> 16) & 0xFF),
                                      static_cast<uint8_t>((c >> 8) & 0xFF),
                                      static_cast<uint8_t>(c & 0xFF));
        }
        catch (...)
        {
        }
    }

    // 设置字体和颜色
    pOutDev->SetFont(aFont);
    pOutDev->SetTextColor(aFont.color);

    // 绘制文本
    std::string text = pTextNode->GetText();
    Point pt(m_aFrameArea.Left(), m_aFrameArea.Top());
    pOutDev->DrawText(pt, text);
}

//===----------------------------------------------------------------------===//
// SwFlowFrame - 分页流动逻辑
// 迁移自 LibreOffice sw/source/core/layout/flowfrm.cxx
//===----------------------------------------------------------------------===//

bool SwFlowFrame::s_bMoveBwdJump = false;

SwFlowFrame::SwFlowFrame(SwFrame& rThis)
    : m_rThis(rThis)
    , m_pFollow(nullptr)
    , m_pPrecede(nullptr)
    , m_bLockJoin(false)
    , m_bUndersized(false)
    , m_bFlyLock(false)
{
}

SwFlowFrame::~SwFlowFrame()
{
    // 迁移自 LO flowfrm.cxx: SwFlowFrame::~SwFlowFrame (行 80-90)
    if (m_pFollow)
    {
        m_pFollow->m_pPrecede = nullptr;
    }
    if (m_pPrecede)
    {
        m_pPrecede->m_pFollow = nullptr;
    }
}

void SwFlowFrame::SetFollow(SwFlowFrame* pFollow)
{
    // 迁移自 LO flowfrm.cxx: SwFlowFrame::SetFollow (行 92-109)
    if (m_pFollow)
    {
        // 断开旧 Follow
        m_pFollow->m_pPrecede = nullptr;
    }
    m_pFollow = pFollow;
    if (m_pFollow)
    {
        // 如果新 Follow 已有 Precede，先断开
        if (m_pFollow->m_pPrecede)
        {
            m_pFollow->m_pPrecede->m_pFollow = nullptr;
        }
        m_pFollow->m_pPrecede = this;
    }
}

bool SwFlowFrame::HasLockedFollow() const
{
    // 迁移自 LO flowfrm.cxx: HasLockedFollow (行 112-122)
    const SwFlowFrame* pFrame = GetFollow();
    while (pFrame)
    {
        if (pFrame->IsJoinLocked())
            return true;
        pFrame = pFrame->GetFollow();
    }
    return false;
}

bool SwFlowFrame::IsAnFollow(const SwFlowFrame* pAssumed) const
{
    // 迁移自 LO flowfrm.cxx: IsAnFollow (行 795-804)
    const SwFlowFrame* pFoll = this;
    do
    {
        if (pAssumed == pFoll)
            return true;
        pFoll = pFoll->GetFollow();
    } while (pFoll);
    return false;
}

SwFrame* SwFlowFrame::FindPrevIgnoreHidden() const
{
    // 迁移自 LO flowfrm.cxx: FindPrevIgnoreHidden (行 364-372)
    SwFrame* pRet = m_rThis.GetPrev();
    while (pRet && pRet->IsHiddenNow())
    {
        pRet = pRet->GetPrev();
    }
    return pRet;
}

SwFrame* SwFlowFrame::FindNextIgnoreHidden() const
{
    // 迁移自 LO flowfrm.cxx: FindNextIgnoreHidden (行 374-382)
    SwFrame* pRet = m_rThis.GetNext();
    while (pRet && pRet->IsHiddenNow())
    {
        pRet = pRet->GetNext();
    }
    return pRet;
}

bool SwFlowFrame::IsKeepFwdMoveAllowed(bool bIgnoreMyOwnKeepValue)
{
    // 迁移自 LO flowfrm.cxx: IsKeepFwdMoveAllowed (行 124-147)
    // 简化版：检查 Keep 属性链
    SwFrame* pFrame = &m_rThis;
    if (!pFrame->IsInFootnote())
    {
        if (bIgnoreMyOwnKeepValue && pFrame->GetIndPrev())
            pFrame = pFrame->GetIndPrev();
        do
        {
            // 简化版：不检查 GetAttrSet()->GetKeep().GetValue()
            // 直接检查是否有前驱
            if (pFrame->IsHiddenNow())
                pFrame = pFrame->GetIndPrev();
            else
                return true;
        } while (pFrame);
    }
    // 检查是否有前驱
    bool bRet = false;
    if (pFrame && pFrame->GetIndPrev())
        bRet = true;
    return bRet;
}

void SwFlowFrame::CheckKeep()
{
    // 迁移自 LO flowfrm.cxx: CheckKeep (行 149-200)
    // 简化版：触发前驱无效化
    SwFrame* pPre = m_rThis.GetIndPrev();
    if (!pPre)
        return;
    while (pPre && pPre->IsHiddenNow())
    {
        pPre = pPre->GetIndPrev();
    }
    if (!pPre)
        return;
    // 简化版：直接无效化前驱位置
    pPre->InvalidatePos();
}

bool SwFlowFrame::IsPageBreak(bool bAct) const
{
    // 迁移自 LO flowfrm.cxx: IsPageBreak (行 1304-1351)
    if (!IsFollow() && m_rThis.IsInDocBody()
        && (!m_rThis.IsInTab() || (m_rThis.IsTabFrame() && !m_rThis.GetUpper()->IsInTab())))
    {
        // 简化版：不检查浏览模式
        // 查找前驱
        const SwFrame* pPrev = m_rThis.FindPrev();
        while (pPrev && (!pPrev->IsInDocBody() || pPrev->IsHiddenNow()))
            pPrev = pPrev->FindPrev();

        if (pPrev)
        {
            if (bAct)
            {
                if (m_rThis.FindPageFrame() == pPrev->FindPageFrame())
                    return false;
            }
            else
            {
                if (m_rThis.FindPageFrame() != pPrev->FindPageFrame())
                    return false;
            }

            // 简化版：检查分隔属性
            auto eBreak = m_rThis.GetBreakItem().GetBreak();
            if (eBreak == SwFrame::SvxBreak::PageBefore || eBreak == SwFrame::SvxBreak::PageBoth)
                return true;
        }
    }
    return false;
}

bool SwFlowFrame::IsColBreak(bool bAct) const
{
    // 迁移自 LO flowfrm.cxx: IsColBreak (行 1366-1405)
    if (!IsFollow() && (m_rThis.IsMoveable() || bAct))
    {
        const SwFrame* pCol = m_rThis.FindColFrame();
        if (pCol)
        {
            const SwFrame* pPrev = m_rThis.FindPrev();
            while (pPrev && ((!pPrev->IsInDocBody() && !m_rThis.IsInFly()) || pPrev->IsHiddenNow()))
                pPrev = pPrev->FindPrev();

            if (pPrev)
            {
                if (bAct)
                {
                    if (pCol == pPrev->FindColFrame())
                        return false;
                }
                else
                {
                    if (pCol != pPrev->FindColFrame())
                        return false;
                }

                auto eBreak = m_rThis.GetBreakItem().GetBreak();
                if (eBreak == SwFrame::SvxBreak::ColumnBefore
                    || eBreak == SwFrame::SvxBreak::ColumnBoth)
                    return true;
            }
        }
    }
    return false;
}

bool SwFlowFrame::IsKeep(bool bCheckIfLastRowShouldKeep) const
{
    // 迁移自 LO flowfrm.cxx: IsKeep (行 257-362)
    // 简化版：检查 Keep 属性
    if (m_rThis.IsHiddenNow())
        return false;

    // Keep 属性在脚注和表格单元格中忽略
    bool bKeep = bCheckIfLastRowShouldKeep
                 || (!m_rThis.IsInFootnote() && (!m_rThis.IsInTab() || m_rThis.IsTabFrame()));

    // 简化版：不检查分隔属性
    return bKeep;
}

SwLayoutFrame* SwFlowFrame::CutTree(SwFrame* pStart)
{
    // 迁移自 LO flowfrm.cxx: CutTree (行 498-570)
    // 简化版：剪切 Frame 链
    SwLayoutFrame* pLay = pStart->GetUpper();
    if (!pLay)
        return nullptr;

    // 剪切链
    if (pStart == pStart->GetUpper()->Lower())
        pStart->GetUpper()->SetLower(nullptr);
    if (pStart->GetPrev())
    {
        pStart->GetPrev()->mpNext = nullptr;
        pStart->mpPrev = nullptr;
    }

    return pLay;
}

bool SwFlowFrame::PasteTree(SwFrame* pStart, SwLayoutFrame* pParent, SwFrame* pSibling,
                            SwFrame* pOldParent)
{
    // 迁移自 LO flowfrm.cxx: PasteTree (行 574-688)
    // 简化版：粘贴 Frame 链
    bool bRet = false;

    if (pSibling)
    {
        pStart->mpPrev = pSibling->GetPrev();
        if (pStart->mpPrev)
            pStart->GetPrev()->mpNext = pStart;
        else
            pParent->SetLower(pStart);
        pSibling->InvalidatePos();
        pSibling->InvalidatePrt();
    }
    else
    {
        pStart->mpPrev = pParent->Lower();
        if (!pStart->mpPrev)
            pParent->SetLower(pStart);
        else
        {
            // 找到最后一个子节点
            SwFrame* pTemp = pParent->Lower();
            while (pTemp)
            {
                if (pTemp->mpNext)
                    pTemp = pTemp->mpNext;
                else
                {
                    pStart->mpPrev = pTemp;
                    pTemp->mpNext = pStart;
                    break;
                }
            }
        }
    }

    // 更新所有 Frame 的 Upper
    SwFrame* pFloat = pStart;
    SwTwips nGrowVal = 0;
    do
    {
        pFloat->mpUpper = pParent;
        pFloat->InvalidateAll();
        pFloat->InvalidateInfFlags();

        nGrowVal += pFloat->getFrameArea().Height();
        if (pFloat->GetNext())
            pFloat = pFloat->GetNext();
        else
            pFloat = nullptr;
    } while (pFloat);

    if (pSibling)
    {
        SwFrame* pLst = pStart;
        while (pLst->GetNext())
            pLst = pLst->GetNext();
        pLst->mpNext = pSibling;
        pSibling->mpPrev = pLst;
    }

    if (nGrowVal)
    {
        if (pOldParent && pOldParent->IsBodyFrame())
            pOldParent->Shrink(nGrowVal);
        pParent->Grow(nGrowVal);
    }

    return bRet;
}

void SwFlowFrame::MoveSubTree(SwLayoutFrame* pParent, SwFrame* pSibling)
{
    // 迁移自 LO flowfrm.cxx: MoveSubTree (行 690-793)
    // 简化版：移动子树
    if (!pParent)
        return;

    SwPageFrame* pOldPage = m_rThis.FindPageFrame();
    SwLayoutFrame* pOldParent = CutTree(&m_rThis);
    PasteTree(&m_rThis, pParent, pSibling, pOldParent);

    SwPageFrame* pPage = m_rThis.FindPageFrame();
    if (pOldPage != pPage)
    {
        m_rThis.InvalidatePos();
    }
}

bool SwFlowFrame::MoveFwd(bool bMakePage, bool bPageBreak, bool bMoveAlways)
{
    // 迁移自 LO flowfrm.cxx: MoveFwd (行 2101-2306)
    // 简化版：向前移动 Frame

    // 检查是否允许移动
    if (!IsFwdMoveAllowed() && !bMoveAlways)
    {
        // 简化版：不处理表格/节特殊情况
        if (!bPageBreak)
            return false;

        const SwFrame* pCol = m_rThis.FindColFrame();
        if (!pCol || !pCol->GetPrev())
            return false;
    }

    // 获取目标 Leaf
    SwLayoutFrame* pNewUpper
        = m_rThis.GetLeaf(bMakePage ? SwFrame::MAKEPAGE_INSERT : SwFrame::MAKEPAGE_NONE, true);

    if (!pNewUpper)
        return true; // 同页

    bool bSamePage = true;
    SwPageFrame* pOldPage = m_rThis.FindPageFrame();
    SwPageFrame* pNewPage = pNewUpper->FindPageFrame();

    if (pNewPage != pOldPage)
        bSamePage = false;

    // 移动子树
    if (pNewUpper != m_rThis.GetUpper())
    {
        MoveSubTree(pNewUpper, pNewUpper->Lower());
    }

    return bSamePage;
}

bool SwFlowFrame::MoveBwd(bool& rbReformat)
{
    // 迁移自 LO flowfrm.cxx: MoveBwd (行 2314-2895)
    // 简化版：向后移动 Frame

    SetMoveBwdJump(false);

    // 检查脚注锁定
    // 简化版：不处理脚注特殊情况

    // 检查表格内文本 Frame
    if (m_rThis.IsTextFrame() && m_rThis.IsInTab())
    {
        const SwLayoutFrame* pUpperFrame = m_rThis.GetUpper();
        while (pUpperFrame)
        {
            if (pUpperFrame->IsTabFrame() || pUpperFrame->IsRowFrame())
                return false;
            if (pUpperFrame->IsColumnFrame() && pUpperFrame->IsInSct())
                break;
            pUpperFrame = pUpperFrame->GetUpper();
        }
    }

    SwPageFrame* pOldPage = m_rThis.FindPageFrame();
    SwLayoutFrame* pNewUpper = nullptr;

    // 检查 PageBreak
    if (IsPageBreak(true) && (!m_rThis.IsInSct() || !m_rThis.FindSctFrame()->IsHiddenNow()))
    {
        // 简化版：查找前一页
        const SwFrame* pFlow = &m_rThis;
        do
        {
            pFlow = pFlow->FindPrev();
        } while (pFlow && (pFlow->FindPageFrame() == pOldPage || !pFlow->IsInDocBody()));

        if (pFlow)
        {
            long nDiff = pOldPage->GetPhyPageNum() - pFlow->FindPageFrame()->GetPhyPageNum();
            if (nDiff > 1)
            {
                pNewUpper = m_rThis.GetLeaf(SwFrame::MAKEPAGE_NONE, false);
            }
        }
    }
    else if (IsColBreak(true))
    {
        // 简化版：处理列分隔
        const SwFrame* pCol = m_rThis.FindColFrame();
        if (pCol)
        {
            pNewUpper = m_rThis.GetLeaf(SwFrame::MAKEPAGE_NONE, false);
        }
    }
    else
    {
        // 无分隔：可以向后流动
        pNewUpper = m_rThis.GetLeaf(SwFrame::MAKEPAGE_NONE, false);
    }

    // Follow 检查：不允许移动到 Master 位置
    if (pNewUpper && IsFollow() && pNewUpper->Lower())
    {
        if (!IsMoveBwdJump())
            pNewUpper = nullptr;
    }

    // 简化版：不调用 ShouldBwdMoved

    if (pNewUpper && pNewUpper != m_rThis.GetUpper())
    {
        // 执行移动
        m_rThis.Cut();
        m_rThis.Paste(pNewUpper, nullptr);

        SwPageFrame* pNewPage = m_rThis.FindPageFrame();
        if (pNewPage != pOldPage)
        {
            rbReformat = true;
        }
        return true;
    }

    return false;
}

bool SwFlowFrame::CheckMoveFwd(bool& rbMakePage, bool bKeep, bool bIgnoreMyOwnKeepValue)
{
    // 迁移自 LO flowfrm.cxx: CheckMoveFwd (行 1994-2091)
    // 简化版：检查是否需要向前移动

    if (m_rThis.IsHiddenNow())
        return false;

    bool bMovedFwd = false;

    if (m_rThis.GetIndPrev())
    {
        // 简化版：不处理 IsPrevObjMove
        if (IsPageBreak(false))
        {
            while (MoveFwd(rbMakePage, true))
                /* 循环 */;
            rbMakePage = false;
            bMovedFwd = true;
        }
        else if (IsColBreak(false))
        {
            const SwPageFrame* pPage = m_rThis.FindPageFrame();
            SwFrame* pCol = m_rThis.FindColFrame();
            do
            {
                MoveFwd(rbMakePage, false);
                SwFrame* pTmp = m_rThis.FindColFrame();
                if (pTmp != pCol)
                {
                    bMovedFwd = true;
                    pCol = pTmp;
                }
                else
                    break;
            } while (IsColBreak(false));
            if (pPage != m_rThis.FindPageFrame())
                rbMakePage = false;
        }
    }
    return bMovedFwd;
}

SwFlowFrame* SwFlowFrame::CastFlowFrame(SwFrame* pFrame)
{
    // 迁移自 LO flowfrm.cxx: CastFlowFrame (行 2897-2906)
    // 多重继承需要使用 reinterpret_cast
    if (pFrame->IsContentFrame())
        return reinterpret_cast<SwFlowFrame*>(static_cast<SwContentFrame*>(pFrame));
    if (pFrame->IsTabFrame())
        return reinterpret_cast<SwFlowFrame*>(static_cast<SwTabFrame*>(pFrame));
    if (pFrame->IsSctFrame())
        return reinterpret_cast<SwFlowFrame*>(static_cast<SwSectionFrame*>(pFrame));
    return nullptr;
}

const SwFlowFrame* SwFlowFrame::CastFlowFrame(const SwFrame* pFrame)
{
    // 迁移自 LO flowfrm.cxx: CastFlowFrame (行 2908-2917)
    // 多重继承需要使用 reinterpret_cast
    if (pFrame->IsContentFrame())
        return reinterpret_cast<const SwFlowFrame*>(static_cast<const SwContentFrame*>(pFrame));
    if (pFrame->IsTabFrame())
        return reinterpret_cast<const SwFlowFrame*>(static_cast<const SwTabFrame*>(pFrame));
    if (pFrame->IsSctFrame())
        return reinterpret_cast<const SwFlowFrame*>(static_cast<const SwSectionFrame*>(pFrame));
    return nullptr;
}

sal_uInt8 SwFlowFrame::BwdMoveNecessary(const SwPageFrame* pPage, const SwRect& rRect)
{
    // 迁移自 LO flowfrm.cxx: BwdMoveNecessary (行 384-494)
    // 简化版：检查是否需要向后移动
    sal_uInt8 nRet = 0;

    // 简化版：不处理浮动对象重叠
    // 返回 0 表示不需要移动

    return nRet;
}

//===----------------------------------------------------------------------===//
// SwTabFrame, SwRowFrame, SwCellFrame, SwSectionFrame
// SwColumnFrame, SwHeaderFrame, SwFooterFrame, SwFootnoteContFrame,
// SwFootnoteFrame, SwFlyFrame, SwNoTextFrame
//===----------------------------------------------------------------------===//

SwTabFrame::SwTabFrame(SwLayoutFrame* pParent)
    : SwLayoutFrame(SwFrameType::Tab, pParent)
    , SwFlowFrame(*this)
    , m_bSplitable(true)
    , m_bHasFollowFlowLine(false)
    , m_nRowsToRepeat(0)
{
}

SwTabFrame::~SwTabFrame() = default;

void SwTabFrame::Format() { SwLayoutFrame::Format(); }

void SwTabFrame::MakeAll() { Format(); }

bool SwTabFrame::ShouldBwdMoved(SwLayoutFrame* pNewUpper, bool& rReformat)
{
    // 迁移自 LO tabfrm.cxx: ShouldBwdMoved
    // 简化版：总是允许向后移动
    (void)pNewUpper;
    rReformat = true;
    return true;
}

// === 表格拆分（迁移自 LO tabfrm.cxx: 1103-1641） ===
bool SwTabFrame::Split(SwTwips nSplitHeight, bool bTryToSplit)
{
    // 简化版表格拆分实现
    // 对应 LO: SwTabFrame::Split

    // 检查是否有行 Frame
    SwRowFrame* pRow = static_cast<SwRowFrame*>(Lower());
    if (!pRow)
        return false;

    // 计算剩余空间
    SwTwips nRemainingSpace = nSplitHeight;

    // 找到不能完整放入的行
    sal_uInt16 nRowCount = 0;
    while (pRow && pRow->GetNext())
    {
        SwTwips nRowHeight = pRow->getFrameArea().Height();
        if (nRemainingSpace < nRowHeight)
            break;
        nRemainingSpace -= nRowHeight;
        ++nRowCount;
        pRow = static_cast<SwRowFrame*>(pRow->GetNext());
    }

    // 检查是否允许拆分行
    bool bSplitRowAllowed = bTryToSplit && nRemainingSpace > 0;
    if (bSplitRowAllowed && !pRow->IsRowSplitAllowed())
        bSplitRowAllowed = false;

    // 没有更多行可拆分或移动
    if (!pRow)
        return true;

    // 简化版：不实现完整的 Follow Flow Line 逻辑
    // 仅标记需要拆分
    SetFollowFlowLine(true);

    return true;
}

// === 表格合并（迁移自 LO tabfrm.cxx: 1646-1703） ===
void SwTabFrame::Join()
{
    // 简化版表格合并实现
    // 对应 LO: SwTabFrame::Join

    SwTabFrame* pFoll = static_cast<SwTabFrame*>(GetFollow());
    if (!pFoll)
        return;

    // 从 Follow 中剪切出来
    pFoll->Cut();

    // 将 Follow 的行移动到当前表格
    SwFrame* pRow = pFoll->Lower();
    SwFrame* pPrv = GetLastLower();
    SwTwips nHeight = 0;

    while (pRow)
    {
        SwFrame* pNxt = pRow->GetNext();
        nHeight += pRow->getFrameArea().Height();
        pRow->RemoveFromLayout();
        pRow->InsertBehind(this, pPrv);
        pPrv = pRow;
        pRow = pNxt;
    }

    // 更新 Follow 链
    SetFollow(pFoll->GetFollow());
    SetFollowFlowLine(pFoll->HasFollowFlowLine());

    // 删除 Follow Frame
    delete pFoll;

    // 增长表格高度
    Grow(nHeight);
}

// === 高度计算 ===
SwTwips SwTabFrame::CalcHeight() const
{
    // 计算所有行的高度总和
    SwTwips nHeight = 0;
    const SwFrame* pRow = Lower();
    while (pRow)
    {
        nHeight += pRow->getFrameArea().Height();
        pRow = pRow->GetNext();
    }
    return nHeight;
}

SwTwips SwTabFrame::CalcHeightOfFirstContentLine() const
{
    // 对应 LO: SwTabFrame::CalcHeightOfFirstContentLine
    // 简化版：返回第一行高度
    const SwFrame* pRow = Lower();
    if (pRow)
        return pRow->getFrameArea().Height();
    return 0;
}

// === 表格属性 ===
sal_uInt16 SwTabFrame::GetRowCount() const
{
    sal_uInt16 nCount = 0;
    const SwFrame* pRow = Lower();
    while (pRow)
    {
        ++nCount;
        pRow = pRow->GetNext();
    }
    return nCount;
}

SwRowFrame* SwTabFrame::GetFirstNonHeadlineRow()
{
    // 对应 LO: SwTabFrame::GetFirstNonHeadlineRow
    // 简化版：跳过重复标题行
    SwFrame* pRow = Lower();
    sal_uInt16 nRepeat = GetRowsToRepeat();
    while (pRow && nRepeat > 0)
    {
        pRow = pRow->GetNext();
        --nRepeat;
    }
    return static_cast<SwRowFrame*>(pRow);
}

const SwRowFrame* SwTabFrame::GetFirstNonHeadlineRow() const
{
    SwFrame* pRow = Lower();
    sal_uInt16 nRepeat = GetRowsToRepeat();
    while (pRow && nRepeat > 0)
    {
        pRow = pRow->GetNext();
        --nRepeat;
    }
    return static_cast<const SwRowFrame*>(pRow);
}

SwFrame* SwTabFrame::GetLastLower()
{
    SwFrame* pLast = Lower();
    if (!pLast)
        return nullptr;
    while (pLast->GetNext())
        pLast = pLast->GetNext();
    return pLast;
}

SwRowFrame::SwRowFrame(SwTabFrame* pParent)
    : SwLayoutFrame(SwFrameType::Row, pParent)
    , m_nHeight(0)
    , m_bRowSplitAllowed(true)
    , m_bFollowFlowRow(false)
    , m_bRowSpanLine(false)
    , m_bKeepWithNext(false)
    , m_bFixSize(false)
    , m_pFollowRow(nullptr)
{
}

SwRowFrame::~SwRowFrame() = default;

// === 行格式化（迁移自 LO rowfrm.cxx） ===
void SwRowFrame::Format()
{
    // 简化版行格式化
    // 对应 LO: SwRowFrame::Format

    // 计算行高度（基于单元格内容）
    SwTwips nMaxHeight = 0;
    SwFrame* pCell = Lower();
    while (pCell)
    {
        SwTwips nCellHeight = pCell->getFrameArea().Height();
        if (nCellHeight > nMaxHeight)
            nMaxHeight = nCellHeight;
        pCell = pCell->GetNext();
    }

    // 如果有固定高度，使用固定高度
    if (HasFixSize() && m_nHeight > 0)
        nMaxHeight = m_nHeight;

    // 更新行高度
    if (nMaxHeight > 0)
    {
        SwRect aArea = getFrameArea();
        aArea.SetHeight(nMaxHeight);
        setFrameArea(aArea);
        m_nHeight = nMaxHeight;
    }

    // 格式化子单元格
    SwLayoutFrame::Format();
}

void SwRowFrame::MakeAll() { Format(); }

// === 行高度计算 ===
SwTwips SwRowFrame::CalcHeight() const
{
    // 计算单元格最大高度
    SwTwips nMaxHeight = 0;
    const SwFrame* pCell = Lower();
    while (pCell)
    {
        SwTwips nCellHeight = pCell->getFrameArea().Height();
        if (nCellHeight > nMaxHeight)
            nMaxHeight = nCellHeight;
        pCell = pCell->GetNext();
    }
    return nMaxHeight;
}

void SwRowFrame::SetHeight(SwTwips nHeight)
{
    m_nHeight = nHeight;
    SwRect aArea = getFrameArea();
    aArea.SetHeight(nHeight);
    setFrameArea(aArea);
}

SwCellFrame::SwCellFrame(SwRowFrame* pParent)
    : SwLayoutFrame(SwFrameType::Cell, pParent)
    , m_nLayoutRowSpan(1)
    , m_pFollowCell(nullptr)
    , m_pPreviousCell(nullptr)
    , m_bLeaveUpperAllowed(false)
{
}

SwCellFrame::~SwCellFrame() = default;

// === 单元格格式化 ===
void SwCellFrame::Format()
{
    // 简化版单元格格式化
    // 对应 LO: SwCellFrame::Format

    // 格式化子内容
    SwLayoutFrame::Format();
}

// === 跨行跨列支持 ===
const SwCellFrame& SwCellFrame::FindStartEndOfRowSpanCell(bool bStart) const
{
    // 对应 LO: SwCellFrame::FindStartEndOfRowSpanCell
    // 简化版：返回自身
    (void)bStart;
    return *this;
}

SwSectionFrame::SwSectionFrame(SwLayoutFrame* pParent)
    : SwLayoutFrame(SwFrameType::Section, pParent)
    , SwFlowFrame(*this)
    , m_pSection(nullptr)
    , m_bColLocked(false)
    , m_bContentLock(false)
    , m_bSplit(false)
    , m_bHidden(false)
{
}

SwSectionFrame::~SwSectionFrame() = default;

bool SwSectionFrame::ShouldBwdMoved(SwLayoutFrame* pNewUpper, bool& rReformat)
{
    // 迁移自 LO sectfrm.cxx: ShouldBwdMoved (行 980-984)
    // 简化版：总是允许向后移动
    (void)pNewUpper;
    rReformat = true;
    return true;
}

// === 初始化 Section（对应 LO SwSectionFrame::Init 行 125-182） ===
void SwSectionFrame::Init()
{
    // 简化版：创建基本的 Column/Body 层级
    // 在 LO 中，Init() 会根据 Section 的属性创建列布局
    // 这里我们创建一个单列布局作为默认值

    // 检查是否已有 Upper
    if (!GetUpper())
        return;

    // 获取 Upper 的打印区域宽度
    SwTwips nWidth = GetUpper()->getFramePrintArea().Width();

    // 设置节的初始尺寸
    SwRect aFrameRect(0, 0, nWidth, 0);
    setFrameArea(aFrameRect);
    setFramePrintArea(SwRect(0, 0, nWidth, 0));

    // 创建单列布局（简化版，不处理多列）
    auto* pColFrame = new SwColumnFrame(this);
    pColFrame->InsertBehind(this, nullptr);
    pColFrame->SetColWidth(nWidth);

    auto* pColBody = new SwBodyFrame(pColFrame);
    pColBody->InsertBehind(pColFrame, nullptr);
}

void SwSectionFrame::Format()
{
    // 节 Frame 排版：调整子 Frame 以适应节区域
    // 对应 LO SwSectionFrame::Format (行 1464-1729)

    // 检查是否隐藏
    if (IsHidden())
    {
        setFrameAreaSizeValid(true);
        setFramePrintAreaValid(true);
        return;
    }

    // 检查打印区域有效性
    if (!isFramePrintAreaValid())
    {
        setFramePrintAreaValid(true);
        // 简化版：不处理 LRSpace
    }

    // 检查尺寸有效性
    if (isFrameAreaSizeValid())
        return;

    setFrameAreaSizeValid(true);

    // 检查是否需要最大化
    bool bMaximize = ToMaximize(false);

    // 获取 Upper 的宽度
    if (GetUpper())
    {
        SwTwips nWidth = GetUpper()->getFramePrintArea().Width();
        SwRect aArea = getFrameArea();
        aArea.SetWidth(nWidth);
        setFrameArea(aArea);

        SwRect aPrtArea = getFramePrintArea();
        aPrtArea.SetWidth(nWidth);
        setFramePrintArea(aPrtArea);
    }

    // 检查剪切限制
    CheckClipping(false, bMaximize);

    // 格式化子 Frame
    SwLayoutFrame::Format();
}

void SwSectionFrame::MakeAll()
{
    // 对应 LO SwSectionFrame::MakeAll (行 837-978)
    // 简化版：检查锁定状态后格式化

    if (IsJoinLocked() || IsColLocked())
        return;

    // 检查隐藏状态
    if (IsHidden())
    {
        setFrameAreaPositionValid(true);
        setFrameAreaSizeValid(true);
        setFramePrintAreaValid(true);
        return;
    }

    // 锁定 Join
    LockJoin();

    // 合合相邻的 Follow
    while (GetNext() && GetNext() == GetFollow())
    {
        MergeNext(static_cast<SwSectionFrame*>(GetNext()));
    }

    // 格式化
    Format();

    // 解锁 Join
    UnlockJoin();
}

// === 节拆分（对应 LO SwSectionFrame::SplitSect 行 559-616） ===
SwSectionFrame* SwSectionFrame::SplitSect(SwFrame* pFrameStartAfter, SwFrame* pFramePutAfter)
{
    // 简化版节拆分实现
    // 在 LO 中，SplitSect 会保存内容、创建新节、恢复内容

    // 确定拆分点
    SwFrame* pSav = nullptr;
    if (pFrameStartAfter)
    {
        pSav = pFrameStartAfter->GetNext();
    }
    else
    {
        pSav = Lower();
    }

    // 如果没有内容需要移动，返回 nullptr
    if (!pSav)
        return nullptr;

    // 创建新节 Frame
    if (!pFramePutAfter)
        pFramePutAfter = this;

    SwSectionFrame* pNew = new SwSectionFrame(pFramePutAfter->GetUpper());
    pNew->SetSection(m_pSection);
    pNew->InsertBehind(pFramePutAfter->GetUpper(), pFramePutAfter);
    pNew->Init();

    // 移动内容到新节
    SwFrame* pLower = Lower();
    if (pLower && pLower->IsColumnFrame())
    {
        // 多列布局：找到 Body Frame
        SwLayoutFrame* pColLay = static_cast<SwLayoutFrame*>(pLower);
        SwFrame* pBodyFrame = pColLay->Lower();
        if (pBodyFrame && pBodyFrame->IsBodyFrame())
        {
            // 移动内容
            while (pSav)
            {
                SwFrame* pNext = pSav->GetNext();
                pSav->RemoveFromLayout();
                pSav->InsertBehind(pNew, nullptr);
                pSav = pNext;
            }
        }
    }
    else
    {
        // 单列布局：直接移动
        while (pSav)
        {
            SwFrame* pNext = pSav->GetNext();
            pSav->RemoveFromLayout();
            pSav->InsertBehind(pNew, nullptr);
            pSav = pNext;
        }
    }

    // 设置 Follow 链
    if (HasFollow())
    {
        pNew->SetFollow(GetFollow());
        SetFollow(nullptr);
    }

    SetSplit(true);
    InvalidateSize();

    return pNew;
}

// === 节合并（对应 LO SwSectionFrame::MergeNext 行 512-547） ===
void SwSectionFrame::MergeNext(SwSectionFrame* pNxt)
{
    // 简化版节合并实现
    if (!pNxt || IsJoinLocked())
        return;

    // 检查是否属于同一节
    if (GetSection() != pNxt->GetSection())
        return;

    // 移动内容
    SwFrame* pLower = pNxt->Lower();
    SwFrame* pLast = Lower();

    // 找到最后一个子 Frame
    while (pLast && pLast->GetNext())
        pLast = pLast->GetNext();

    // 如果是多列布局，找到 Body Frame
    if (pLast && pLast->IsColumnFrame())
    {
        SwLayoutFrame* pColLay = static_cast<SwLayoutFrame*>(pLast);
        SwFrame* pBodyFrame = pColLay->Lower();
        if (pBodyFrame && pBodyFrame->IsBodyFrame())
            pLast = static_cast<SwLayoutFrame*>(pBodyFrame)->Lower();
        if (pLast)
            while (pLast->GetNext())
                pLast = pLast->GetNext();
    }

    // 移动 pNxt 的内容到当前节
    while (pLower)
    {
        SwFrame* pNext = pLower->GetNext();
        pLower->RemoveFromLayout();
        pLower->InsertBehind(this, pLast);
        pLast = pLower;
        pLower = pNext;
    }

    // 更新 Follow 链
    SetFollow(pNxt->GetFollow());
    pNxt->SetFollow(nullptr);

    // 删除 pNxt
    pNxt->RemoveFromLayout();
    delete pNxt;

    InvalidateSize();
}

// === 查找最后内容（对应 LO SwSectionFrame::FindLastContent 行 1029-1064） ===
SwContentFrame* SwSectionFrame::FindLastContent()
{
    // 简化版：查找节中的最后一个内容 Frame
    SwFrame* pLower = Lower();
    SwContentFrame* pLastContent = nullptr;

    while (pLower)
    {
        if (pLower->IsContentFrame())
        {
            pLastContent = static_cast<SwContentFrame*>(pLower);
        }
        else if (pLower->IsLayoutFrame())
        {
            SwContentFrame* pContent = static_cast<SwLayoutFrame*>(pLower)->GetLastContent();
            if (pContent)
                pLastContent = pContent;
        }
        pLower = pLower->GetNext();
    }

    // 如果有 Follow，继续查找
    if (!pLastContent && HasFollow())
    {
        SwSectionFrame* pFoll = static_cast<SwSectionFrame*>(GetFollow());
        if (pFoll)
            pLastContent = pFoll->FindLastContent();
    }

    return pLastContent;
}

const SwContentFrame* SwSectionFrame::FindLastContent() const
{
    return const_cast<SwSectionFrame*>(this)->FindLastContent();
}

// === 可增长检查（对应 LO SwSectionFrame::Growable 行 2309-2317） ===
bool SwSectionFrame::Growable() const
{
    // 简化版：检查节是否还能增长
    if (!GetUpper())
        return false;

    // 检查是否还有空间
    SwTwips nUpperHeight = GetUpper()->getFramePrintArea().Height();
    SwTwips nMyHeight = getFrameArea().Height();

    if (nUpperHeight > nMyHeight)
        return true;

    // 检查 Upper 是否能增长
    return GetUpper()->IsLayoutFrame();
}

// === 获取外层节 ===
SwSectionFrame* SwSectionFrame::GetOuterSection()
{
    // 查找包含此节的外层节
    SwFrame* pUpper = GetUpper();
    while (pUpper)
    {
        if (pUpper->IsSctFrame())
            return static_cast<SwSectionFrame*>(pUpper);
        pUpper = pUpper->GetUpper();
    }
    return nullptr;
}

const SwSectionFrame* SwSectionFrame::GetOuterSection() const
{
    return const_cast<SwSectionFrame*>(this)->GetOuterSection();
}

// === 隐藏检查 ===
bool SwSectionFrame::IsHidden() const
{
    // 简化版：检查节是否隐藏
    return m_bHidden || (m_pSection && m_pSection->CalcHiddenFlag());
}

// === 超额检查 ===
bool SwSectionFrame::IsSuperfluous() const
{
    // 简化版：检查节是否是多余的（空节）
    if (IsHidden())
        return true;

    // 检查是否有内容
    return !ContainsContent() && !HasFollow();
}

// === 简单格式化（对应 LO SwSectionFrame::SimpleFormat 行 1291-1323） ===
void SwSectionFrame::SimpleFormat()
{
    // 快速格式化节，不进行完整排版
    if (IsJoinLocked() || IsColLocked())
        return;

    LockJoin();

    // 设置位置
    if (!isFrameAreaPositionValid() && GetUpper())
    {
        SwRect aArea = getFrameArea();
        SwRect aUpperArea = GetUpper()->getFramePrintArea();
        aArea.SetLeft(aUpperArea.Left());
        aArea.SetTop(aUpperArea.Top());
        setFrameArea(aArea);
        setFrameAreaPositionValid(true);
    }

    // 设置尺寸
    if (GetUpper())
    {
        SwTwips nWidth = GetUpper()->getFramePrintArea().Width();
        SwRect aArea = getFrameArea();
        aArea.SetWidth(nWidth);
        setFrameArea(aArea);

        SwRect aPrtArea = getFramePrintArea();
        aPrtArea.SetWidth(nWidth);
        setFramePrintArea(aPrtArea);
    }

    UnlockJoin();
}

// === 内容移动和删除（对应 LO SwSectionFrame::MoveContentAndDelete 行 731-835） ===
void SwSectionFrame::MoveContentAndDelete(SwSectionFrame* pDel, bool bSave)
{
    // 简化版：移动节内容并删除节 Frame
    if (!pDel)
        return;

    SwLayoutFrame* pUp = pDel->GetUpper();

    // 保存内容
    SwFrame* pSave = nullptr;
    if (bSave)
    {
        SwFrame* pLower = pDel->Lower();
        while (pLower)
        {
            SwFrame* pNext = pLower->GetNext();
            pLower->RemoveFromLayout();
            pLower->InsertBehind(nullptr, pSave); // 暂存
            pSave = pLower;
            pLower = pNext;
        }
    }

    // 删除节 Frame
    pDel->RemoveFromLayout();
    delete pDel;

    // 恢复内容
    if (pSave && pUp)
    {
        while (pSave)
        {
            SwFrame* pNext = pSave->GetNext();
            pSave->RemoveFromLayout();
            pSave->InsertBehind(pUp, nullptr);
            pSave = pNext;
        }
    }
}

// === 查找 Master ===
SwSectionFrame* SwSectionFrame::FindMaster()
{
    // 查找节的 Master（如果当前是 Follow）
    if (!IsFollow())
        return this;

    SwFlowFrame* pPrecede = GetPrecede();
    if (pPrecede && pPrecede->GetFrame().IsSctFrame())
        return static_cast<SwSectionFrame*>(&pPrecede->GetFrame());

    return nullptr;
}

SwSectionFrame* SwSectionFrame::GetFollow() const
{
    // 获取 Follow 节
    SwFlowFrame* pFoll = SwFlowFrame::GetFollow();
    if (pFoll && pFoll->GetFrame().IsSctFrame())
        return static_cast<SwSectionFrame*>(&pFoll->GetFrame());
    return nullptr;
}

// === 增长/收缩 ===
SwTwips SwSectionFrame::Grow_(SwTwips nDist, bool bTst)
{
    // 简化版：增长节高度
    if (IsColLocked() || IsHidden())
        return 0;

    if (!bTst)
    {
        Grow(nDist);
    }

    return nDist;
}

SwTwips SwSectionFrame::Shrink_(SwTwips nDist, bool bTst)
{
    // 简化版：收缩节高度
    if (IsColLocked())
        return 0;

    if (!bTst)
    {
        Shrink(nDist);
    }

    return nDist;
}

// === 列数检查 ===
bool SwSectionFrame::HasMultiColumns() const
{
    // 检查节是否有多个列
    SwFrame* pLower = Lower();
    if (!pLower || !pLower->IsColumnFrame())
        return false;

    return pLower->GetNext() != nullptr;
}

// === 节描述检查 ===
bool SwSectionFrame::ToMaximize(bool bCheckFollow) const
{
    // 简化版：检查节是否需要最大化
    if (HasFollow())
    {
        if (!bCheckFollow)
            return true;

        // 检查 Follow 是否有内容
        SwSectionFrame* pFoll = GetFollow();
        if (pFoll && pFoll->IsSuperfluous())
            return false;

        return true;
    }

    return false;
}

// === 内部方法 ===
bool SwSectionFrame::HasToBreak(const SwFrame* pFrame) const
{
    // 简化版：检查是否需要打破另一个节
    if (!pFrame || !pFrame->IsSctFrame())
        return false;

    // 简化版：不处理嵌套节
    return false;
}

void SwSectionFrame::CheckClipping(bool bGrow, bool bMaximize)
{
    // 简化版：检查剪切限制
    if (!GetUpper())
        return;

    SwTwips nUpperHeight = GetUpper()->getFramePrintArea().Height();
    SwTwips nMyHeight = getFrameArea().Height();

    if (bMaximize && nUpperHeight > nMyHeight)
    {
        // 扩展到 Upper 的底部
        SwRect aArea = getFrameArea();
        aArea.SetHeight(nUpperHeight);
        setFrameArea(aArea);

        SwRect aPrtArea = getFramePrintArea();
        aPrtArea.SetHeight(nUpperHeight);
        setFramePrintArea(aPrtArea);
    }
}

SwTwips SwSectionFrame::InnerHeight() const
{
    // 简化版：计算内部高度
    SwTwips nHeight = 0;
    SwFrame* pLower = Lower();
    while (pLower)
    {
        nHeight += pLower->getFrameArea().Height();
        pLower = pLower->GetNext();
    }
    return nHeight;
}

SwColumnFrame::SwColumnFrame(SwLayoutFrame* pParent)
    : SwLayoutFrame(SwFrameType::Column, pParent)
    , m_nColWidth(0)
    , m_nLeftSpacing(0)
    , m_nRightSpacing(0)
    , m_nMaxFootnoteHeight(LONG_MAX)
    , m_nColIndex(0)
{
}

SwColumnFrame::~SwColumnFrame() = default;

void SwColumnFrame::Format()
{
    // 列 Frame 的排版：调整子 BodyFrame 的尺寸以适应列宽
    // 对应 LO colfrm.cxx: SwColumnFrame::Format

    // 检查打印区域有效性
    if (!isFramePrintAreaValid())
    {
        setFramePrintAreaValid(true);
        // 简化版：不处理列间距
    }

    // 检查尺寸有效性
    if (isFrameAreaSizeValid())
        return;

    setFrameAreaSizeValid(true);

    // 设置列宽
    SwFrame* pLower = GetLower();
    if (pLower && pLower->IsBodyFrame())
    {
        SwRect aColArea = getFrameArea();
        SwRect aBodyRect(0, 0, aColArea.Width(), aColArea.Height());
        pLower->setFrameArea(aBodyRect);
        pLower->setFramePrintArea(aBodyRect);
    }

    // 格式化子 Frame
    SwLayoutFrame::Format();
}

void SwColumnFrame::MakeAll()
{
    // 简化版：格式化列
    Format();
}

// === 列宽计算 ===
SwTwips SwColumnFrame::CalcColWidth() const
{
    // 计算列宽度（包括间距）
    return getFrameArea().Width();
}

void SwColumnFrame::SetColWidth(SwTwips nWidth)
{
    // 设置列宽度
    m_nColWidth = nWidth;

    // 更新 Frame 区域
    SwRect aArea = getFrameArea();
    aArea.SetWidth(nWidth);
    setFrameArea(aArea);

    // 更新打印区域
    SwRect aPrtArea = getFramePrintArea();
    aPrtArea.SetWidth(nWidth - m_nLeftSpacing - m_nRightSpacing);
    setFramePrintArea(aPrtArea);

    // 更新子 Body Frame
    SwFrame* pLower = GetLower();
    if (pLower && pLower->IsBodyFrame())
    {
        SwRect aBodyRect(0, 0, aPrtArea.Width(), aArea.Height());
        pLower->setFrameArea(aBodyRect);
        pLower->setFramePrintArea(aBodyRect);
    }
}

// === Body Frame 查找 ===
SwLayoutFrame* SwColumnFrame::FindBodyCont()
{
    // 查找列内的 Body Frame
    SwFrame* pLower = GetLower();
    while (pLower)
    {
        if (pLower->IsBodyFrame())
            return static_cast<SwLayoutFrame*>(pLower);
        pLower = pLower->GetNext();
    }
    return nullptr;
}

const SwLayoutFrame* SwColumnFrame::FindBodyCont() const
{
    return const_cast<SwColumnFrame*>(this)->FindBodyCont();
}

// === 脚注容器查找 ===
SwFootnoteContFrame* SwColumnFrame::FindFootnoteCont()
{
    // 查找列内的脚注容器
    SwFrame* pLower = GetLower();
    while (pLower)
    {
        if (pLower->IsFootnoteContFrame())
            return static_cast<SwFootnoteContFrame*>(pLower);
        pLower = pLower->GetNext();
    }
    return nullptr;
}

const SwFootnoteContFrame* SwColumnFrame::FindFootnoteCont() const
{
    return const_cast<SwColumnFrame*>(this)->FindFootnoteCont();
}

// === 列调整 ===
void SwColumnFrame::AdjustColWidth(SwTwips nAvailWidth)
{
    // 调整列宽以适应可用宽度
    if (nAvailWidth <= 0)
        return;

    // 计算新宽度（减去间距）
    SwTwips nNewWidth = nAvailWidth - m_nLeftSpacing - m_nRightSpacing;
    if (nNewWidth < 0)
        nNewWidth = 0;

    SetColWidth(nNewWidth);
}

//===----------------------------------------------------------------------===//
// SwHeadFootFrame - 页眉页脚基类
// 迁移自 LibreOffice sw/source/core/layout/hffrm.cxx
//===----------------------------------------------------------------------===//

SwHeadFootFrame::SwHeadFootFrame(SwFrameType nType, SwLayoutFrame* pParent)
    : SwLayoutFrame(nType, pParent)
    , mbColLocked(false)
    , mbFixSize(false)
{
}

SwHeadFootFrame::~SwHeadFootFrame() = default;

// === 计算最小高度（对应 LO lcl_GetFrameMinHeight 行 38-55） ===
SwTwips SwHeadFootFrame::GetMinHeight() const
{
    // 简化版：从格式获取最小高度
    // 对应 LO: SwFormatFrameSize::GetHeightSizeType() == SwFrameSize::Minimum
    SwTwips nMinHeight = 0;

    // 简化版：不检查 SwFormatFrameSize，使用默认值
    // 完整实现需要 SwFrameFormat 支持

    return nMinHeight;
}

// === 计算内容高度（对应 LO lcl_CalcContentHeight 行 57-84） ===
SwTwips SwHeadFootFrame::CalcContentHeight() const
{
    SwTwips nRemaining = 0;
    SwFrame* pFrame = Lower();

    while (pFrame)
    {
        SwTwips nTmp = pFrame->getFrameArea().Height();
        nRemaining += nTmp;

        // 检查是否是 Undersized 的文本 Frame
        if (pFrame->IsTextFrame())
        {
            SwTextFrame* pTextFrame = static_cast<SwTextFrame*>(pFrame);
            if (pTextFrame->IsUndersized())
            {
                // 这个 TextFrame 希望更大一些
                // 简化版：不计算 GetParHeight
                nRemaining += 100; // 简化版：使用固定值
            }
        }
        // 检查是否是 Undersized 的 Section Frame
        else if (pFrame->IsSctFrame())
        {
            SwSectionFrame* pSctFrame = static_cast<SwSectionFrame*>(pFrame);
            if (pSctFrame->IsUndersized())
            {
                nRemaining += 100; // 简化版：使用固定值
            }
        }

        pFrame = pFrame->GetNext();
    }

    return nRemaining;
}

// === 确保最小高度（对应 LO lcl_LayoutFrameEnsureMinHeight 行 86-94） ===
void SwHeadFootFrame::EnsureMinHeight()
{
    SwTwips nMinHeight = GetMinHeight();

    if (getFrameArea().Height() < nMinHeight)
    {
        Grow(nMinHeight - getFrameArea().Height());
    }
}

// === 计算最大可吸收间距 ===
SwTwips SwHeadFootFrame::CalcMaxEatSpacing() const
{
    // 对应 LO GrowFrame/ShrinkFrame 中的间距计算
    SwTwips nMaxEat = 0;

    if (IsHeaderFrame())
    {
        // 页眉：从底部计算可吸收间距
        nMaxEat
            = getFrameArea().Height() - getFramePrintArea().Top() - getFramePrintArea().Height();
    }
    else
    {
        // 页脚：从顶部计算可吸收间距
        nMaxEat = getFramePrintArea().Top();
    }

    if (nMaxEat < 0)
        nMaxEat = 0;

    return nMaxEat;
}

// === 打印区域格式化（对应 LO hffrm.cxx: FormatPrt 行 114-218） ===
void SwHeadFootFrame::FormatPrt(SwTwips& nUL)
{
    // 简化版：不处理 EatSpacing 的复杂逻辑
    // 完整实现需要 SwBorderAttrs 支持

    if (GetEatSpacing())
    {
        // 使用间距吸收逻辑
        SwTwips nMinHeight = GetMinHeight();
        SwTwips nHeight = HasFixSize() ? nMinHeight : CalcContentHeight();

        if (nHeight < nMinHeight)
            nHeight = nMinHeight;

        // 计算可吸收间距
        SwTwips nMaxEat = CalcMaxEatSpacing();
        SwTwips nEat = nHeight - nMinHeight;

        if (nEat > nMaxEat)
            nEat = nMaxEat;

        // 设置打印区域
        SwRect aPrtArea = getFramePrintArea();

        if (IsHeaderFrame())
        {
            // 页眉：调整底部间距
            nUL = nEat;
        }
        else
        {
            // 页脚：调整顶部间距
            aPrtArea.SetTop(nEat);
            nUL = nEat;
        }

        aPrtArea.SetWidth(getFrameArea().Width());
        aPrtArea.SetHeight(getFrameArea().Height() - nUL);

        setFramePrintArea(aPrtArea);
    }
    else
    {
        // 不使用间距吸收：标准打印区域设置
        SwRect aPrtArea = getFramePrintArea();
        aPrtArea.SetWidth(getFrameArea().Width());
        aPrtArea.SetHeight(getFrameArea().Height() - nUL);
        setFramePrintArea(aPrtArea);
    }

    setFramePrintAreaValid(true);
}

// === 尺寸格式化（对应 LO hffrm.cxx: FormatSize 行 220-413） ===
void SwHeadFootFrame::FormatSize(SwTwips nUL)
{
    if (!HasFixSize())
    {
        // 动态尺寸：根据内容调整高度
        if (!IsColLocked())
        {
            setFramePrintAreaValid(true);
            setFrameAreaSizeValid(true);

            SwTwips nMinHeight = GetMinHeight();
            ColLock();

            SwTwips nRemaining = CalcContentHeight();
            if (nRemaining < nMinHeight)
                nRemaining = nMinHeight;

            // 格式化子 Frame
            SwFrame* pFrame = Lower();
            while (pFrame)
            {
                pFrame->Calc();
                pFrame = pFrame->GetNext();
            }

            // 计算高度差异
            SwTwips nDiff = nRemaining - getFramePrintArea().Height();

            if (nDiff > 0)
            {
                Grow(nDiff);
            }
            else if (nDiff < 0)
            {
                Shrink(-nDiff);
            }

            ColUnlock();
        }

        setFramePrintAreaValid(true);
        setFrameAreaSizeValid(true);
    }
    else
    {
        // 固定尺寸：使用预设高度
        // 简化版：不检查 pAttrs->GetSize().Height()
        setFrameAreaSizeValid(true);
        MakePos();
    }
}

// === 格式化（对应 LO hffrm.cxx: Format 行 415-438） ===
void SwHeadFootFrame::Format()
{
    // 检查有效性
    if (isFramePrintAreaValid() && isFrameAreaSizeValid())
        return;

    // 确保最小高度
    EnsureMinHeight();

    // 计算边框（简化版：使用固定值）
    SwTwips nUL = 0; // 简化版：不使用 SwBorderAttrs

    // 格式化打印区域
    if (!isFramePrintAreaValid())
        FormatPrt(nUL);

    // 格式化尺寸
    if (!isFrameAreaSizeValid())
        FormatSize(nUL);

    // 格式化子 Frame
    SwLayoutFrame::Format();
}

// === 增长（对应 LO hffrm.cxx: GrowFrame 行 440-540） ===
SwTwips SwHeadFootFrame::GrowFrame(SwTwips nDist, bool bTst)
{
    SwTwips nResult = 0;

    if (IsColLocked())
    {
        return 0;
    }

    if (!GetEatSpacing())
    {
        // 不使用间距吸收：调用基类 Grow
        nResult = SwFrame::Grow(nDist);
    }
    else
    {
        // 使用间距吸收逻辑
        SwTwips nEat = nDist;
        SwTwips nMaxEat = CalcMaxEatSpacing();

        // 检查最小高度
        SwTwips nMinHeight = GetMinHeight();
        SwTwips nFrameTooSmall = nMinHeight - getFrameArea().Height();

        if (nFrameTooSmall > 0)
            nEat -= nFrameTooSmall;

        // 限制吸收量
        if (nEat < 0)
            nEat = 0;
        else if (nEat > nMaxEat)
            nEat = nMaxEat;

        if (nEat > 0)
        {
            if (!bTst)
            {
                // 调整打印区域
                SwRect aPrtArea = getFramePrintArea();
                if (!IsHeaderFrame())
                {
                    aPrtArea.SetTop(aPrtArea.Top() - nEat);
                    aPrtArea.SetHeight(aPrtArea.Height() - nEat);
                }
                setFramePrintArea(aPrtArea);
                InvalidateAll();
            }
            nResult += nEat;
        }

        // 剩余部分通过 Frame 增长
        if (nDist - nEat > 0)
        {
            nResult += SwFrame::Grow(nDist - nEat);
        }
    }

    if (nResult && !bTst)
        SetCompletePaint();

    return nResult;
}

// === 收缩（对应 LO hffrm.cxx: ShrinkFrame 行 542-652） ===
SwTwips SwHeadFootFrame::ShrinkFrame(SwTwips nDist, bool bTst)
{
    SwTwips nResult = 0;

    if (IsColLocked())
    {
        return 0;
    }

    if (!GetEatSpacing())
    {
        // 不使用间距吸收：调用基类 Shrink
        nResult = SwFrame::Shrink(nDist);
    }
    else
    {
        // 使用间距吸收逻辑
        SwTwips nMinHeight = GetMinHeight();
        SwTwips nOldHeight = getFrameArea().Height();
        SwTwips nRest = 0;

        if (nOldHeight >= nMinHeight)
        {
            SwTwips nBiggerThanMin = nOldHeight - nMinHeight;
            if (nBiggerThanMin < nDist)
            {
                nRest = nDist - nBiggerThanMin;
            }
        }
        else
        {
            nRest = nDist;
        }

        if (nRest > 0)
        {
            // 计算最大收缩量
            SwTwips nMinPrtHeight = nMinHeight;
            SwTwips nShrink = nRest;
            SwTwips nMaxShrink = getFramePrintArea().Height() - nMinPrtHeight;

            if (nShrink > nMaxShrink)
                nShrink = nMaxShrink;

            if (!bTst)
            {
                // 调整打印区域
                SwRect aPrtArea = getFramePrintArea();
                if (!IsHeaderFrame())
                {
                    aPrtArea.SetTop(aPrtArea.Top() + nShrink);
                    aPrtArea.SetHeight(aPrtArea.Height() - nShrink);
                }
                setFramePrintArea(aPrtArea);
                InvalidateAll();
            }
            nResult += nShrink;
        }

        // 剩余部分通过 Frame 收缩
        if (nDist - nRest > 0)
        {
            nResult += SwFrame::Shrink(nDist - nRest);
        }
    }

    return nResult;
}

// === 高度计算 ===
SwTwips SwHeadFootFrame::CalcHeight() const { return getFrameArea().Height(); }

// === 动态检查 ===
bool SwHeadFootFrame::IsDynamic() const
{
    // 简化版：检查是否有固定尺寸
    return !HasFixSize();
}

// === 间距吸收检查 ===
bool SwHeadFootFrame::GetEatSpacing() const
{
    // 简化版：默认不使用间距吸收
    // 完整实现需要检查 SwFormatHeaderAndFooterEatSpacing
    return false;
}

// === 获取格式 ===
SwFrameFormat* SwHeadFootFrame::GetHeaderFooterFormat() const { return GetFormat(); }

//===----------------------------------------------------------------------===//
// SwHeaderFrame - 页眉 Frame
//===----------------------------------------------------------------------===//

SwHeaderFrame::SwHeaderFrame(SwLayoutFrame* pParent)
    : SwHeadFootFrame(SwFrameType::Header, pParent)
{
}

SwHeaderFrame::~SwHeaderFrame() = default;

void SwHeaderFrame::Format()
{
    // 页眉格式化：调用基类
    SwHeadFootFrame::Format();
}

SwFrameFormat* SwHeaderFrame::GetHeaderFormat() const { return GetHeaderFooterFormat(); }

bool SwHeaderFrame::IsDynamicHeader() const { return IsDynamic(); }

//===----------------------------------------------------------------------===//
// SwFooterFrame - 页脚 Frame
//===----------------------------------------------------------------------===//

SwFooterFrame::SwFooterFrame(SwLayoutFrame* pParent)
    : SwHeadFootFrame(SwFrameType::Footer, pParent)
{
}

SwFooterFrame::~SwFooterFrame() = default;

void SwFooterFrame::Format()
{
    // 页脚格式化：调用基类
    SwHeadFootFrame::Format();
}

SwFrameFormat* SwFooterFrame::GetFooterFormat() const { return GetHeaderFooterFormat(); }

bool SwFooterFrame::IsDynamicFooter() const { return IsDynamic(); }

//===----------------------------------------------------------------------===//
// SwFootnoteContFrame - 脚注容器 Frame
// 迁移自 LibreOffice sw/source/core/layout/ftnfrm.cxx
//===----------------------------------------------------------------------===//

SwFootnoteContFrame::SwFootnoteContFrame(SwLayoutFrame* pParent)
    : SwLayoutFrame(SwFrameType::FootnoteCont, pParent)
{
}

SwFootnoteContFrame::~SwFootnoteContFrame() = default;

// === 格式化（对应 LO ftnfrm.cxx: Format 行 277-365） ===
void SwFootnoteContFrame::Format()
{
    // 脚注容器排版：计算分隔线高度、调整子 Frame 以适应容器区域
    // 对应 LO: SwFootnoteContFrame::Format

    // 计算分隔线高度（简化版：使用固定值）
    SwTwips nBorder = 100; // 分隔线高度（对应 LO FootnoteSeparatorHeight）

    // 检查打印区域有效性
    if (!isFramePrintAreaValid())
    {
        setFramePrintAreaValid(true);
        SwRect aPrtArea = getFramePrintArea();
        aPrtArea.SetTop(nBorder);
        aPrtArea.SetWidth(getFrameArea().Width());
        aPrtArea.SetHeight(getFrameArea().Height() - nBorder);
        setFramePrintArea(aPrtArea);

        // 如果打印区域高度为负，需要调整
        if (aPrtArea.Height() < 0)
        {
            setFrameAreaSizeValid(false);
        }
    }

    // 检查尺寸有效性
    if (isFrameAreaSizeValid())
        return;

    setFrameAreaSizeValid(true);

    // 计算内容高度
    SwTwips nRemaining = nBorder;
    SwFrame* pFrame = GetLower();
    while (pFrame)
    {
        nRemaining += pFrame->getFrameArea().Height();
        pFrame = pFrame->GetNext();
    }

    // 调整容器高度
    SwTwips nDiff = getFrameArea().Height() - nRemaining;
    if (nDiff > 0)
        ShrinkFrame(nDiff);
    else if (nDiff < 0)
        GrowFrame(-nDiff);

    // 格式化子 Frame
    SwLayoutFrame::Format();
}

// === 高度计算 ===
SwTwips SwFootnoteContFrame::CalcMaxHeight() const
{
    // 计算最大可用高度（对应 LO GrowFrame）
    // 简化版：返回 Upper 的可用空间
    if (!GetUpper())
        return LONG_MAX;

    SwTwips nUpperHeight = GetUpper()->getFramePrintArea().Height();
    SwTwips nMyHeight = getFrameArea().Height();
    return nUpperHeight - nMyHeight;
}

// === 脚注管理 ===
sal_uInt16 SwFootnoteContFrame::GetFootnoteCount() const
{
    sal_uInt16 nCount = 0;
    const SwFrame* pFrame = GetLower();
    while (pFrame)
    {
        if (pFrame->IsFootnoteFrame())
            ++nCount;
        pFrame = pFrame->GetNext();
    }
    return nCount;
}

SwFootnoteFrame* SwFootnoteContFrame::GetFirstFootnote()
{
    SwFrame* pFrame = GetLower();
    while (pFrame)
    {
        if (pFrame->IsFootnoteFrame())
            return static_cast<SwFootnoteFrame*>(pFrame);
        pFrame = pFrame->GetNext();
    }
    return nullptr;
}

const SwFootnoteFrame* SwFootnoteContFrame::GetFirstFootnote() const
{
    return const_cast<SwFootnoteContFrame*>(this)->GetFirstFootnote();
}

SwFootnoteFrame* SwFootnoteContFrame::GetLastFootnote()
{
    SwFrame* pFrame = GetLower();
    SwFootnoteFrame* pLast = nullptr;
    while (pFrame)
    {
        if (pFrame->IsFootnoteFrame())
            pLast = static_cast<SwFootnoteFrame*>(pFrame);
        pFrame = pFrame->GetNext();
    }
    return pLast;
}

const SwFootnoteFrame* SwFootnoteContFrame::GetLastFootnote() const
{
    return const_cast<SwFootnoteContFrame*>(this)->GetLastFootnote();
}

// === 查找脚注 ===
const SwFootnoteFrame* SwFootnoteContFrame::FindFootNote() const
{
    // 查找普通脚注（非尾注）
    const SwFrame* pFrame = GetLower();
    while (pFrame)
    {
        if (pFrame->IsFootnoteFrame())
        {
            // 简化版：不区分脚注和尾注
            return static_cast<const SwFootnoteFrame*>(pFrame);
        }
        pFrame = pFrame->GetNext();
    }
    return nullptr;
}

const SwFootnoteFrame* SwFootnoteContFrame::FindEndNote() const
{
    // 查找尾注
    // 简化版：与 FindFootNote 相同
    return FindFootNote();
}

// === 链式脚注管理 ===
SwFootnoteFrame* SwFootnoteContFrame::AddChained(bool bAppend, SwFrame* pNewUpper,
                                                 bool bDefaultFormat)
{
    // 对应 LO ftnfrm.cxx: AddChained (行 192-223)
    // 简化版：创建新的脚注 Frame
    SwFootnoteFrame* pOld = pNewUpper ? pNewUpper->FindFootnoteFrame() : nullptr;
    if (!pOld)
        return nullptr;

    // 创建新脚注 Frame
    SwFootnoteFrame* pNew = new SwFootnoteFrame(pOld->GetUpper(), pOld->GetRef(), pOld->GetAttr());

    // 设置 Master/Follow 链
    if (bAppend)
    {
        if (pOld->GetFollow())
        {
            pNew->SetFollow(pOld->GetFollow());
            pOld->GetFollow()->SetMaster(pNew);
        }
        pOld->SetFollow(pNew);
        pNew->SetMaster(pOld);
    }
    else
    {
        if (pOld->GetMaster())
        {
            pNew->SetMaster(pOld->GetMaster());
            pOld->GetMaster()->SetFollow(pNew);
        }
        pNew->SetFollow(pOld);
        pOld->SetMaster(pNew);
    }

    return pNew;
}

SwFootnoteFrame* SwFootnoteContFrame::AppendChained(SwFrame* pThis, bool bDefaultFormat)
{
    return AddChained(true, pThis, bDefaultFormat);
}

SwFootnoteFrame* SwFootnoteContFrame::PrependChained(SwFrame* pThis, bool bDefaultFormat)
{
    return AddChained(false, pThis, bDefaultFormat);
}

// === 尺寸调整 ===
SwTwips SwFootnoteContFrame::GrowFrame(SwTwips nDist, bool bTst)
{
    // 对应 LO ftnfrm.cxx: GrowFrame (行 367-518)
    // 简化版：增长容器高度
    if (!bTst)
    {
        SwRect aArea = getFrameArea();
        aArea.SetHeight(aArea.Height() + nDist);
        setFrameArea(aArea);

        // 更新打印区域
        SwRect aPrtArea = getFramePrintArea();
        aPrtArea.SetHeight(aPrtArea.Height() + nDist);
        setFramePrintArea(aPrtArea);
    }
    return nDist;
}

SwTwips SwFootnoteContFrame::ShrinkFrame(SwTwips nDiff, bool bTst)
{
    // 对应 LO ftnfrm.cxx: ShrinkFrame (行 520-548)
    // 简化版：收缩容器高度
    if (!bTst)
    {
        SwRect aArea = getFrameArea();
        SwTwips nNewHeight = std::max(SwTwips(0), aArea.Height() - nDiff);
        aArea.SetHeight(nNewHeight);
        setFrameArea(aArea);

        // 更新打印区域
        SwRect aPrtArea = getFramePrintArea();
        aPrtArea.SetHeight(std::max(SwTwips(0), aPrtArea.Height() - nDiff));
        setFramePrintArea(aPrtArea);
    }
    return nDiff;
}

// === 绘制辅助线 ===
void SwFootnoteContFrame::PaintLine(const SwRect& rRect, const SwPageFrame* pPage) const
{
    // 对应 LO ftnfrm.cxx: PaintLine
    // 简化版：不实现绘制
    (void)rRect;
    (void)pPage;
}

//===----------------------------------------------------------------------===//
// SwFootnoteFrame - 脚注 Frame
// 迁移自 LibreOffice sw/source/core/layout/ftnfrm.cxx
//===----------------------------------------------------------------------===//

SwFootnoteFrame::SwFootnoteFrame(SwLayoutFrame* pParent, SwContentFrame* pRef,
                                 SwTextFootnote* pAttr)
    : SwLayoutFrame(SwFrameType::Footnote, pParent)
    , m_pFollow(nullptr)
    , m_pMaster(nullptr)
    , m_pReference(pRef)
    , m_pAttribute(pAttr)
    , m_bBackMoveLocked(false)
    , m_bColLocked(false)
    , m_bUnlockPosOfLowerObjs(true)
{
}

SwFootnoteFrame::~SwFootnoteFrame()
{
    // 断开 Master/Follow 链
    if (m_pFollow)
        m_pFollow->SetMaster(m_pMaster);
    if (m_pMaster)
        m_pMaster->SetFollow(m_pFollow);
}

// === 格式化 ===
void SwFootnoteFrame::Format()
{
    // 脚注排版：调整子 Frame 以适应脚注区域
    SwRect aArea = getFrameArea();
    SwFrame* pLower = GetLower();
    if (pLower)
    {
        SwRect aChildArea(0, 0, aArea.Width(), aArea.Height());
        pLower->setFrameArea(aChildArea);
        pLower->setFramePrintArea(aChildArea);
    }
    SwLayoutFrame::Format();
}

// === 高度计算 ===
SwTwips SwFootnoteFrame::CalcHeight() const
{
    // 计算脚注高度（包括所有子 Frame）
    SwTwips nHeight = 0;
    const SwFrame* pFrame = GetLower();
    while (pFrame)
    {
        nHeight += pFrame->getFrameArea().Height();
        pFrame = pFrame->GetNext();
    }
    return nHeight;
}

// === 从属性获取引用 ===
SwContentFrame* SwFootnoteFrame::GetRefFromAttr() const
{
    // 对应 LO ftnfrm.cxx: GetRefFromAttr (行 3065-3078)
    // 简化版：返回存储的引用
    return m_pReference;
}

// === 比较操作 ===
bool SwFootnoteFrame::operator<(const SwTextFootnote* pTextFootnote) const
{
    // 对应 LO ftnfrm.cxx: operator< (行 98-103)
    // 简化版：比较属性指针
    return m_pAttribute < pTextFootnote;
}

// === 删除禁止检查 ===
bool SwFootnoteFrame::IsDeleteForbidden() const
{
    // 对应 LO ftnfrm.cxx: IsDeleteForbidden (行 587-606)
    if (SwLayoutFrame::IsDeleteForbidden())
        return true;

    const SwLayoutFrame* pUp = GetUpper();
    if (pUp)
    {
        if (GetPrev())
            return false;

        // 最后一个脚注会删除容器
        return !GetNext() && pUp->IsDeleteForbidden();
    }
    return false;
}

// === Cut ===
void SwFootnoteFrame::Cut()
{
    // 对应 LO ftnfrm.cxx: Cut (行 608-657)
    if (GetNext())
        GetNext()->InvalidatePos();
    else if (GetPrev())
        GetPrev()->SetRetouche();

    // 修正 Master/Follow 链
    if (GetFollow())
        GetFollow()->SetMaster(GetMaster());
    if (GetMaster())
        GetMaster()->SetFollow(GetFollow());
    SetFollow(nullptr);
    SetMaster(nullptr);

    // 从布局中移除
    RemoveFromLayout();

    SwLayoutFrame* pUp = GetUpper();
    if (!pUp)
        return;

    // 如果容器为空，删除容器
    if (!pUp->Lower())
    {
        SwPageFrame* pPage = pUp->FindPageFrame();
        if (pPage)
        {
            SwLayoutFrame* pBody = pPage->FindBodyFrame();
            SwContentFrame* pContent = pBody ? pBody->ContainsContent() : nullptr;
            if (pBody && !pContent)
            {
                // 简化版：不设置 Superfluous 标志
            }
        }
        // 删除容器
        pUp->RemoveFromLayout();
        delete pUp;
    }
    else
    {
        if (getFrameArea().Height())
            pUp->Shrink(getFrameArea().Height());
        pUp->SetCompletePaint();
    }
}

// === Paste ===
void SwFootnoteFrame::Paste(SwLayoutFrame* pParent, SwFrame* pSibling)
{
    // 对应 LO ftnfrm.cxx: Paste (行 659-730)
    assert(pParent);
    assert(pParent->IsLayoutFrame());
    assert(pParent != this);
    assert(pSibling != this);
    assert(!GetPrev() && !GetNext() && !GetUpper());

    // 插入到树结构中
    InsertBefore(pParent, pSibling);

    // 无效化
    if (getFrameArea().Width() != pParent->getFramePrintArea().Width())
        InvalidateSize();
    InvalidatePos();

    SwPageFrame* pPage = FindPageFrame();
    InvalidatePage(pPage);

    if (GetNext())
    {
        GetNext()->InvalidatePos();
    }

    if (getFrameArea().Height())
        pParent->Grow(getFrameArea().Height());

    // 处理 Master/Follow 合并
    if (GetPrev() && GetPrev() == GetMaster())
    {
        // 简化版：不移动内容
        SwFrame* pDel = GetPrev();
        pDel->RemoveFromLayout();
        delete pDel;
    }
    if (GetNext() && GetNext() == GetFollow())
    {
        // 简化版：不移动内容
        SwFrame* pDel = GetNext();
        pDel->RemoveFromLayout();
        delete pDel;
    }

    InvalidateNxtFootnoteCnts(pPage);
}

// === 无效化下一个脚注容器 ===
void SwFootnoteFrame::InvalidateNxtFootnoteCnts(const SwPageFrame* pPage)
{
    // 对应 LO ftnfrm.cxx: InvalidateNxtFootnoteCnts (行 563-585)
    if (!GetNext())
        return;

    SwFrame* pCnt = static_cast<SwLayoutFrame*>(GetNext())->ContainsContent();
    if (!pCnt)
        return;

    pCnt->InvalidatePos();
    do
    {
        pCnt->InvalidatePos();
        pCnt->GetUpper()->InvalidateSize();
        pCnt = pCnt->FindNext();
    } while (pCnt && GetUpper()->IsAnLower(pCnt));
}

// === 查找最后内容 ===
SwContentFrame* SwFootnoteFrame::FindLastContent()
{
    // 对应 LO ftnfrm.cxx: FindLastContent (行 3084-3118)
    SwFrame* pLastLower = GetLower();
    SwFrame* pTmp = pLastLower;
    while (pTmp && pTmp->GetNext())
    {
        pTmp = pTmp->GetNext();
        if (!pTmp->IsHiddenNow())
        {
            if (pTmp->IsContentFrame())
                pLastLower = pTmp;
            else if (pTmp->IsLayoutFrame())
            {
                SwContentFrame* pContent = static_cast<SwLayoutFrame*>(pTmp)->ContainsContent();
                if (pContent)
                    pLastLower = pTmp;
            }
        }
    }

    if (pLastLower && pLastLower->IsTabFrame())
    {
        // 简化版：不处理表格
        return nullptr;
    }
    else if (pLastLower && pLastLower->IsSctFrame())
    {
        return static_cast<SwSectionFrame*>(pLastLower)->FindLastContent();
    }
    else
    {
        return dynamic_cast<SwContentFrame*>(pLastLower);
    }
}

const SwContentFrame* SwFootnoteFrame::FindLastContent() const
{
    return const_cast<SwFootnoteFrame*>(this)->FindLastContent();
}

SwFlyFrame::SwFlyFrame(SwLayoutFrame* pParent)
    : SwLayoutFrame(SwFrameType::Fly, pParent)
{
}

SwFlyFrame::~SwFlyFrame() = default;

void SwFlyFrame::Format()
{
    // 浮动框排版：调整子 Frame 以适应浮动框区域
    SwRect aArea = getFrameArea();
    SwFrame* pLower = GetLower();
    if (pLower)
    {
        SwRect aChildArea(0, 0, aArea.Width(), aArea.Height());
        pLower->setFrameArea(aChildArea);
        pLower->setFramePrintArea(aChildArea);
    }
    SwLayoutFrame::Format();
}

// 在浮动对象链末尾挂接一个新的 FlyFrame
void SwLayoutFrame::AppendFly(SwFlyFrame* pFly)
{
    if (!pFly)
        return;
    if (!m_pFirstFly)
    {
        m_pFirstFly = pFly;
        return;
    }
    SwFlyFrame* pCurr = m_pFirstFly;
    while (pCurr->GetNextFly())
        pCurr = pCurr->GetNextFly();
    pCurr->SetNextFly(pFly);
}

SwNoTextFrame::SwNoTextFrame(SwContentNode* pNode, SwLayoutFrame* pParent)
    : SwContentFrame(SwFrameType::NoTxt, pParent)
{
    mpNode = pNode;
}

SwNoTextFrame::~SwNoTextFrame() = default;
