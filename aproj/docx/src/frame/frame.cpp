// 简化版 Frame 实现，对应 LibreOffice 的 sw/source/core/layout/

#include "frame.h"
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

void SwFrame::Grow(SwTwips nDiff)
{
    SwRect aArea(getFrameArea());
    aArea.SetHeight(aArea.Height() + nDiff);
    setFrameArea(aArea);
}

void SwFrame::Shrink(SwTwips nDiff)
{
    SwRect aArea(getFrameArea());
    aArea.SetHeight(std::max(SwTwips(0), aArea.Height() - nDiff));
    setFrameArea(aArea);
}

void SwFrame::ChgSize(const SwRect& rNewSize) { setFrameArea(rNewSize); }

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

SwContentNode* SwFrame::GetNode() const
{
    // 对于内容 Frame，返回关联的节点
    // 对于布局 Frame，返回 nullptr
    return nullptr;
}

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
{
}

SwPageFrame::~SwPageFrame() = default;

void SwPageFrame::PreparePage()
{
    // 创建页面结构（Body、Header、Footer）
    // 简化实现：只创建 Body
    if (!GetLower())
    {
        auto* pBody = new SwBodyFrame(this);
        pBody->InsertBehind(this, nullptr);
    }
}

SwPageFrame* SwPageFrame::GetNextPage() const { return static_cast<SwPageFrame*>(GetNext()); }

SwPageFrame* SwPageFrame::GetPrevPage() const { return static_cast<SwPageFrame*>(GetPrev()); }

//===----------------------------------------------------------------------===//
// SwBodyFrame
//===----------------------------------------------------------------------===//

SwBodyFrame::SwBodyFrame(SwPageFrame* pParent)
    : SwLayoutFrame(SwFrameType::Body, pParent)
{
}

SwBodyFrame::~SwBodyFrame() = default;

//===----------------------------------------------------------------------===//
// SwContentFrame
//===----------------------------------------------------------------------===//

SwContentFrame::SwContentFrame(SwFrameType nType, SwLayoutFrame* pParent)
    : SwFrame(nType, pParent)
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
    // 文本格式化（将在 Phase 4 中实现）
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
// SwFlowFrame
//===----------------------------------------------------------------------===//

SwFlowFrame::SwFlowFrame(SwFrame& rThis)
    : m_rThis(rThis)
    , m_pFollow(nullptr)
    , m_pPrecede(nullptr)
    , m_bLockJoin(false)
    , m_bUndersized(false)
    , m_bFlyLock(false)
{
}

void SwFlowFrame::SetFollow(SwFlowFrame* pFollow)
{
    m_pFollow = pFollow;
    if (pFollow)
    {
        pFollow->m_pPrecede = this;
    }
}

bool SwFlowFrame::IsPageBreak(bool bAct) const
{
    // 检查是否有硬分页
    // 简化实现
    (void)bAct;
    return false;
}

bool SwFlowFrame::IsColBreak(bool bAct) const
{
    // 检查是否有分栏符
    (void)bAct;
    return false;
}

bool SwFlowFrame::IsKeep() const
{
    // 检查 "与下段同页" 属性
    return false;
}

bool SwFlowFrame::MoveFwd(bool bMakePage, bool bPageBreak)
{
    // 向前移动到下一页面/栏
    (void)bMakePage;
    (void)bPageBreak;
    return false;
}

bool SwFlowFrame::MoveBwd(bool bReformat)
{
    // 向后移动
    (void)bReformat;
    return false;
}

bool SwFlowFrame::s_bMoveBwdJump = false;

//===----------------------------------------------------------------------===//
// SwTabFrame, SwRowFrame, SwCellFrame, SwSectionFrame
//===----------------------------------------------------------------------===//

SwTabFrame::SwTabFrame(SwLayoutFrame* pParent)
    : SwLayoutFrame(SwFrameType::Tab, pParent)
{
}

SwTabFrame::~SwTabFrame() = default;

void SwTabFrame::Format() { SwLayoutFrame::Format(); }

void SwTabFrame::MakeAll() { Format(); }

SwRowFrame::SwRowFrame(SwTabFrame* pParent)
    : SwLayoutFrame(SwFrameType::Row, pParent)
{
}

SwRowFrame::~SwRowFrame() = default;

SwCellFrame::SwCellFrame(SwRowFrame* pParent)
    : SwLayoutFrame(SwFrameType::Cell, pParent)
{
}

SwCellFrame::~SwCellFrame() = default;

SwSectionFrame::SwSectionFrame(SwLayoutFrame* pParent)
    : SwLayoutFrame(SwFrameType::Section, pParent)
{
}

SwSectionFrame::~SwSectionFrame() = default;
