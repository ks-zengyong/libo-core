// SwObjectFormatter 实现
// 迁移自 LibreOffice sw/source/core/layout/objectformatter.cxx
// 简化版：使用 SwFlyFrame* 代替 SwAnchoredObject*

#include "objectformatter.h"
#include "frame.h"
#include "sortedobjs.h"
#include "../layout/layact.h"
#include "../core/swrect.h"
#include <iostream>

//===----------------------------------------------------------------------===//
// SwObjectFormatter 基类实现
//===----------------------------------------------------------------------===//

SwObjectFormatter::SwObjectFormatter(const SwPageFrame& rPageFrame,
                                     SwLayAction* pLayAction,
                                     bool bCollectPgNumOfAnchors)
    : m_rPageFrame(rPageFrame)
    , mbConsiderWrapOnObjPos(false) // 简化版：不考虑环绕
    , mpLayAction(pLayAction)
{
    // 简化版：不收集锚点页码
    // LO 原版会根据 bCollectPgNumOfAnchors 创建 mpPgNumAndTypeOfAnchors
}

SwObjectFormatter::~SwObjectFormatter()
{
}

std::unique_ptr<SwObjectFormatter> SwObjectFormatter::CreateObjFormatter(
    SwFrame& rAnchorFrame,
    const SwPageFrame& rPageFrame,
    SwLayAction* pLayAction)
{
    std::unique_ptr<SwObjectFormatter> pObjFormatter;

    if (rAnchorFrame.IsTextFrame())
    {
        pObjFormatter = SwObjectFormatterTextFrame::CreateObjFormatter(
            static_cast<SwTextFrame&>(rAnchorFrame),
            rPageFrame, pLayAction);
    }
    else if (rAnchorFrame.IsLayoutFrame())
    {
        pObjFormatter = SwObjectFormatterLayFrame::CreateObjFormatter(
            static_cast<SwLayoutFrame&>(rAnchorFrame),
            rPageFrame, pLayAction);
    }
    else
    {
        // 简化版：不处理其他类型
        std::cerr << "SwObjectFormatter::CreateObjFormatter - unexpected anchor frame type" << std::endl;
    }

    return pObjFormatter;
}

bool SwObjectFormatter::FormatObjsAtFrame(SwFrame& rAnchorFrame,
                                          const SwPageFrame& rPageFrame,
                                          SwLayAction* pLayAction)
{
    bool bSuccess = true;

    // 创建对应的对象格式化器
    std::unique_ptr<SwObjectFormatter> pObjFormatter =
        SwObjectFormatter::CreateObjFormatter(rAnchorFrame, rPageFrame, pLayAction);

    if (pObjFormatter)
    {
        // 格式化锚定的浮动对象
        bSuccess = pObjFormatter->DoFormatObjs();
    }

    return bSuccess;
}

bool SwObjectFormatter::FormatObj(SwFlyFrame& rFly,
                                  SwFrame* pAnchorFrame,
                                  const SwPageFrame* pPageFrame,
                                  SwLayAction* pLayAction)
{
    bool bSuccess = true;

    // 确定锚点 Frame
    SwFrame* pAnchor = pAnchorFrame;
    if (!pAnchor)
    {
        // 简化版：从 FlyFrame 获取锚点
        // LO 原版：rFly.AnchorFrame()
        pAnchor = rFly.GetUpper();
    }

    if (!pAnchor)
    {
        std::cerr << "SwObjectFormatter::FormatObj - missing anchor frame" << std::endl;
        return false;
    }

    // 确定页面 Frame
    const SwPageFrame* pPage = pPageFrame;
    if (!pPage)
    {
        // 简化版：从锚点获取页面
        pPage = pAnchor->FindPageFrame();
    }

    if (!pPage)
    {
        std::cerr << "SwObjectFormatter::FormatObj - missing page frame" << std::endl;
        return false;
    }

    // 创建对应的对象格式化器
    std::unique_ptr<SwObjectFormatter> pObjFormatter =
        SwObjectFormatter::CreateObjFormatter(*pAnchor, *pPage, pLayAction);

    if (pObjFormatter)
    {
        // 格式化给定的浮动对象
        // 简化版：不检查锚点前移
        bSuccess = pObjFormatter->DoFormatObj(rFly, false);
    }

    return bSuccess;
}

void SwObjectFormatter::FormatLayout_(SwLayoutFrame& rLayoutFrame)
{
    // 对应 LO layact.cxx FormatLayoutFly/FormatLayout
    // 简化版：调用 Calc
    rLayoutFrame.Calc();

    // 递归格式化下级布局 Frame
    SwFrame* pLowerFrame = rLayoutFrame.GetLower();
    while (pLowerFrame)
    {
        if (pLowerFrame->IsLayoutFrame())
        {
            FormatLayout_(*static_cast<SwLayoutFrame*>(pLowerFrame));
        }
        pLowerFrame = pLowerFrame->GetNext();
    }
}

void SwObjectFormatter::FormatObjContent(SwFlyFrame& rFly)
{
    // 对应 LO objectformatter.cxx FormatObjContent
    // 格式化 FlyFrame 的内容

    SwContentFrame* pContent = rFly.ContainsContent();
    while (pContent)
    {
        // 格式化内容
        pContent->OptCalc();

        // 格式化内容文本 Frame 上的浮动对象
        if (pContent->IsTextFrame())
        {
            SwPageFrame* pContentPage = pContent->FindPageFrame();
            if (pContentPage)
            {
                if (!SwObjectFormatter::FormatObjsAtFrame(*pContent, *pContentPage, GetLayAction()))
                {
                    // 重新开始格式化
                    pContent = rFly.ContainsContent();
                    continue;
                }
            }
        }

        // 继续下一个内容
        pContent = pContent->GetNextContentFrame();
    }
}

void SwObjectFormatter::FormatObj_(SwFlyFrame& rFly)
{
    // 对应 LO objectformatter.cxx FormatObj_
    // 执行浮动对象的内在格式化

    // 收集对象信息（简化版）
    if (!m_aCollectedObjs.empty())
    {
        m_aCollectedObjs.push_back(&rFly);
        SwPageFrame* pPageOfAnchor = rFly.GetUpper() ? 
            static_cast<SwPageFrame*>(rFly.GetUpper()->FindPageFrame()) : nullptr;
        m_aPageNumsOfAnchor.push_back(pPageOfAnchor ? pPageOfAnchor->GetPhyPageNum() : 0);
        m_aAnchoredAtMaster.push_back(true); // 简化版
    }

    // 循环控制
    int nLoopControlRuns = 0;
    const int nLoopControlMax = 15;

    do
    {
        // 格式化 FlyFrame 布局
        if (mpLayAction)
        {
            mpLayAction->FormatLayoutFly(&rFly);
            // 检查布局动作是否需要重新开始
            if (mpLayAction->IsAgain())
            {
                break;
            }
        }
        else
        {
            FormatLayout_(rFly);
        }

        // 格式化 FlyFrame 内的浮动对象
        SwPageFrame* pFlyPage = rFly.FindPageFrame();
        if (pFlyPage)
        {
            SwObjectFormatter::FormatObjsAtFrame(rFly, *pFlyPage, mpLayAction);
        }

        // 格式化 FlyFrame 内容
        if (mpLayAction)
        {
            mpLayAction->FormatFlyContent(&rFly);
            if (mpLayAction->IsAgain())
            {
                break;
            }
        }
        else
        {
            FormatObjContent(rFly);
        }

        // 循环控制
        if (++nLoopControlRuns >= nLoopControlMax)
        {
            std::cerr << "LoopControl in SwObjectFormatter::FormatObj_" << std::endl;
            // 简化版：验证 Frame
            rFly.Validate();
            nLoopControlRuns = 0;
        }

    // 继续直到 Frame 有效
    } while (!rFly.isFrameAreaDefinitionValid());

    // 简化版：不处理 RestartLayoutProcess
}

bool SwObjectFormatter::FormatObjsAtFrame_(SwTextFrame* pMasterTextFrame)
{
    // 对应 LO objectformatter.cxx FormatObjsAtFrame_
    // 格式化锚定在锚点 Frame 上的所有浮动对象

    // 确定锚点 Frame
    SwFrame* pAnchorFrame = &GetAnchorFrame();
    if (GetAnchorFrame().IsTextFrame() &&
        static_cast<SwTextFrame&>(GetAnchorFrame()).IsFollow() &&
        pMasterTextFrame)
    {
        pAnchorFrame = pMasterTextFrame;
    }

    // 检查是否有浮动对象
    SwSortedObjs* pObjs = pAnchorFrame->GetDrawObjs();
    if (!pObjs)
    {
        // 没有浮动对象需要格式化
        return true;
    }

    bool bSuccess = true;

    // 遍历所有浮动对象
    for (size_t i = 0; i < pObjs->size(); ++i)
    {
        SwFlyFrame* pFly = (*pObjs)[i];

        // 检查对象的锚点是否在当前页面
        // 简化版：直接检查
        SwPageFrame* pPageOfAnchor = pFly->GetUpper() ?
            static_cast<SwPageFrame*>(pFly->GetUpper()->FindPageFrame()) : nullptr;

        if (pPageOfAnchor && pPageOfAnchor == &GetPageFrame())
        {
            // 格式化对象
            if (!DoFormatObj(*pFly))
            {
                bSuccess = false;
                break;
            }

            // 检查对象列表是否在格式化过程中改变
            if (!pObjs || i > pObjs->size())
            {
                break;
            }
            else
            {
                size_t nActPos = pObjs->GetPos(pFly);
                if (nActPos == pObjs->size() || nActPos > i)
                {
                    --i;
                }
                else if (nActPos < i)
                {
                    i = nActPos;
                }
            }
        }
    }

    return bSuccess;
}

SwFlyFrame* SwObjectFormatter::GetCollectedObj(sal_uInt32 nIndex)
{
    if (nIndex < m_aCollectedObjs.size())
        return m_aCollectedObjs[nIndex];
    return nullptr;
}

sal_uInt32 SwObjectFormatter::GetPgNumOfCollected(sal_uInt32 nIndex)
{
    if (nIndex < m_aPageNumsOfAnchor.size())
        return m_aPageNumsOfAnchor[nIndex];
    return 0;
}

bool SwObjectFormatter::IsCollectedAnchoredAtMaster(sal_uInt32 nIndex)
{
    if (nIndex < m_aAnchoredAtMaster.size())
        return m_aAnchoredAtMaster[nIndex];
    return true;
}

sal_uInt32 SwObjectFormatter::CountOfCollected()
{
    return static_cast<sal_uInt32>(m_aCollectedObjs.size());
}

//===----------------------------------------------------------------------===//
// SwObjectFormatterTextFrame 实现
//===----------------------------------------------------------------------===//

SwObjectFormatterTextFrame::SwObjectFormatterTextFrame(
    SwTextFrame& rAnchorTextFrame,
    const SwPageFrame& rPageFrame,
    SwTextFrame* pMasterAnchorTextFrame,
    SwLayAction* pLayAction)
    : SwObjectFormatter(rPageFrame, pLayAction, true) // 收集锚点页码
    , m_rAnchorTextFrame(rAnchorTextFrame)
    , mpMasterAnchorTextFrame(pMasterAnchorTextFrame)
{
}

SwObjectFormatterTextFrame::~SwObjectFormatterTextFrame()
{
}

std::unique_ptr<SwObjectFormatterTextFrame> SwObjectFormatterTextFrame::CreateObjFormatter(
    SwTextFrame& rAnchorTextFrame,
    const SwPageFrame& rPageFrame,
    SwLayAction* pLayAction)
{
    std::unique_ptr<SwObjectFormatterTextFrame> pObjFormatter;

    // 确定文本 Frame 的 'master'（如果是 follow）
    SwTextFrame* pMasterOfAnchorFrame = nullptr;
    if (rAnchorTextFrame.IsFollow())
    {
        // 简化版：不查找 master
        // LO 原版：rAnchorTextFrame.FindMaster()
    }

    // 创建对象格式化器，如果有浮动对象注册在锚点 Frame
    // 简化版：总是创建
    // LO 原版检查 GetDrawObjs()
    pObjFormatter.reset(
        new SwObjectFormatterTextFrame(rAnchorTextFrame, rPageFrame,
                                       pMasterOfAnchorFrame, pLayAction));

    return pObjFormatter;
}

SwFrame& SwObjectFormatterTextFrame::GetAnchorFrame()
{
    return m_rAnchorTextFrame;
}

bool SwObjectFormatterTextFrame::DoFormatObj(SwFlyFrame& rFly, bool bCheckForMovedFwd)
{
    // 对应 LO objectformattertxtfrm.cxx DoFormatObj
    // 格式化单个浮动对象

    // 检查布局动作是否需要重新开始
    if (GetLayAction() && GetLayAction()->IsAgain())
    {
        return false;
    }

    bool bSuccess = true;

    // 简化版：不检查 IsFormatPossible
    // LO 原版：rFly.IsFormatPossible()

    // 格式化对象
    FormatObj_(rFly);

    // 检查布局动作是否需要重新开始
    if (GetLayAction() && GetLayAction()->IsAgain())
    {
        return false;
    }

    // 简化版：不处理环绕影响和锚点前移
    // LO 原版有复杂的环绕影响检查逻辑

    return bSuccess;
}

bool SwObjectFormatterTextFrame::DoFormatObjs()
{
    // 对应 LO objectformattertxtfrm.cxx DoFormatObjs
    // 格式化所有浮动对象

    // 检查锚点 Frame 是否有效
    if (!m_rAnchorTextFrame.isFrameAreaDefinitionValid())
    {
        if (GetLayAction() &&
            m_rAnchorTextFrame.FindPageFrame() != &GetPageFrame())
        {
            // 通知布局动作重新开始
            GetLayAction()->SetAgain(true);
        }
        else
        {
            std::cerr << "SwObjectFormatterTextFrame::DoFormatObjs - invalid anchor text frame" << std::endl;
        }
        return false;
    }

    bool bSuccess = true;

    // 处理 follow 文本 Frame
    if (m_rAnchorTextFrame.IsFollow())
    {
        // 格式化 master 上的对象
        if (mpMasterAnchorTextFrame)
        {
            bSuccess = FormatObjsAtFrame_(mpMasterAnchorTextFrame);
        }

        if (bSuccess)
        {
            // 格式化当前 Frame 上的对象
            bSuccess = FormatObjsAtFrame_();
        }
    }
    else
    {
        bSuccess = FormatObjsAtFrame_();
    }

    // 简化版：不处理环绕影响检查

    return bSuccess;
}

void SwObjectFormatterTextFrame::InvalidatePrevObjs(SwFlyFrame& rFly)
{
    // 对应 LO objectformattertxtfrm.cxx InvalidatePrevObjs
    // 简化版：不实现
}

void SwObjectFormatterTextFrame::InvalidateFollowObjs(SwFlyFrame& rFly)
{
    // 对应 LO objectformattertxtfrm.cxx InvalidateFollowObjs
    // 简化版：不实现
}

SwFlyFrame* SwObjectFormatterTextFrame::GetFirstObjWithMovedFwdAnchor(
    sal_Int16 nWrapInfluenceOnPosition,
    sal_uInt32& noToPageNum,
    bool& boInFollow)
{
    // 对应 LO objectformattertxtfrm.cxx GetFirstObjWithMovedFwdAnchor
    // 简化版：返回 nullptr
    return nullptr;
}

void SwObjectFormatterTextFrame::FormatAnchorFrameForCheckMoveFwd()
{
    // 对应 LO objectformattertxtfrm.cxx FormatAnchorFrameForCheckMoveFwd
    // 简化版：调用 FormatAnchorFrameAndItsPrevs
    FormatAnchorFrameAndItsPrevs(m_rAnchorTextFrame);
}

bool SwObjectFormatterTextFrame::AtLeastOneObjIsTmpConsiderWrapInfluence()
{
    // 对应 LO objectformattertxtfrm.cxx AtLeastOneObjIsTmpConsiderWrapInfluence
    // 简化版：返回 false
    return false;
}

void SwObjectFormatterTextFrame::FormatAnchorFrameAndItsPrevs(SwTextFrame& rAnchorTextFrame)
{
    // 对应 LO objectformattertxtfrm.cxx FormatAnchorFrameAndItsPrevs
    // 简化版：只格式化锚点 Frame

    // 简化版：不处理 follow
    if (!rAnchorTextFrame.IsFollow())
    {
        // 简化版：不处理 section 和 column
    }

    // 格式化锚点 Frame
    // 简化版：不检查 IsInDtor
    rAnchorTextFrame.Calc();
}

bool SwObjectFormatterTextFrame::CheckMovedFwdCondition(
    SwFlyFrame& rFly,
    SwPageFrame const& rFromPageFrame,
    bool bAnchoredAtMasterBeforeFormatAnchor,
    sal_uInt32& noToPageNum,
    bool& boInFollow)
{
    // 对应 LO objectformattertxtfrm.cxx CheckMovedFwdCondition
    // 简化版：返回 false
    return false;
}

//===----------------------------------------------------------------------===//
// SwObjectFormatterLayFrame 实现
//===----------------------------------------------------------------------===//

SwObjectFormatterLayFrame::SwObjectFormatterLayFrame(
    SwLayoutFrame& rAnchorLayFrame,
    const SwPageFrame& rPageFrame,
    SwLayAction* pLayAction)
    : SwObjectFormatter(rPageFrame, pLayAction)
    , m_rAnchorLayFrame(rAnchorLayFrame)
{
}

SwObjectFormatterLayFrame::~SwObjectFormatterLayFrame()
{
}

std::unique_ptr<SwObjectFormatterLayFrame> SwObjectFormatterLayFrame::CreateObjFormatter(
    SwLayoutFrame& rAnchorLayFrame,
    const SwPageFrame& rPageFrame,
    SwLayAction* pLayAction)
{
    std::unique_ptr<SwObjectFormatterLayFrame> pObjFormatter;

    // 检查锚点类型
    if (!rAnchorLayFrame.IsPageFrame() && !rAnchorLayFrame.IsFlyFrame())
    {
        std::cerr << "SwObjectFormatterLayFrame::CreateObjFormatter - unexpected anchor frame type" << std::endl;
        return nullptr;
    }

    // 创建对象格式化器，如果有浮动对象注册
    // 简化版：总是创建
    pObjFormatter.reset(
        new SwObjectFormatterLayFrame(rAnchorLayFrame, rPageFrame, pLayAction));

    return pObjFormatter;
}

SwFrame& SwObjectFormatterLayFrame::GetAnchorFrame()
{
    return m_rAnchorLayFrame;
}

bool SwObjectFormatterLayFrame::DoFormatObj(SwFlyFrame& rFly, bool bCheckForMovedFwd)
{
    // 对应 LO objectformatterlayfrm.cxx DoFormatObj
    // 对于布局 Frame 锚定的对象，bCheckForMovedFwd 不相关

    FormatObj_(rFly);

    // 检查布局动作是否需要重新开始
    return GetLayAction() == nullptr || !GetLayAction()->IsAgain();
}

bool SwObjectFormatterLayFrame::DoFormatObjs()
{
    // 对应 LO objectformatterlayfrm.cxx DoFormatObjs
    bool bSuccess = FormatObjsAtFrame_();

    if (bSuccess && GetAnchorFrame().IsPageFrame())
    {
        // 锚点是页面 Frame，额外格式化注册在页面上的对象
        bSuccess = AdditionalFormatObjsOnPage();
    }

    return bSuccess;
}

bool SwObjectFormatterLayFrame::AdditionalFormatObjsOnPage()
{
    // 对应 LO objectformatterlayfrm.cxx AdditionalFormatObjsOnPage
    // 格式化注册在页面 Frame 上的所有锚定对象

    if (!GetAnchorFrame().IsPageFrame())
    {
        std::cerr << "SwObjectFormatterLayFrame::AdditionalFormatObjsOnPage - mis-usage" << std::endl;
        return true;
    }

    // 检查布局动作是否需要重新开始
    if (GetLayAction() && GetLayAction()->IsAgain())
    {
        return false;
    }

    SwPageFrame& rPageFrame = static_cast<SwPageFrame&>(GetAnchorFrame());

    // 检查是否有浮动对象
    SwSortedObjs* pObjs = rPageFrame.GetSortedObjs();
    if (!pObjs)
    {
        return true;
    }

    bool bSuccess = true;

    // 遍历所有浮动对象
    for (size_t i = 0; i < pObjs->size(); ++i)
    {
        SwFlyFrame* pFly = (*pObjs)[i];

        // 简化版：不检查 FlyFrame 锚点
        // LO 原版检查 pFly->GetAnchorFrame()->FindFlyFrame()

        // 检查锚点页码
        SwPageFrame* pPageOfAnchor = pFly->GetUpper() ?
            static_cast<SwPageFrame*>(pFly->GetUpper()->FindPageFrame()) : nullptr;

        if (pPageOfAnchor &&
            pPageOfAnchor->GetPhyPageNum() < rPageFrame.GetPhyPageNum())
        {
            // 格式化对象
            if (!DoFormatObj(*pFly))
            {
                bSuccess = false;
                break;
            }

            // 检查对象列表是否改变
            if (!rPageFrame.GetSortedObjs() || i > rPageFrame.GetSortedObjs()->size())
            {
                break;
            }
            else
            {
                size_t nActPos = rPageFrame.GetSortedObjs()->GetPos(pFly);
                if (nActPos == rPageFrame.GetSortedObjs()->size() || nActPos > i)
                {
                    --i;
                }
                else if (nActPos < i)
                {
                    i = nActPos;
                }
            }
        }
    }

    return bSuccess;
}