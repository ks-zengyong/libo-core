// SwLayAction 实现，迁移自 LibreOffice sw/source/core/layout/layact.cxx
// 核心逻辑与 LO 一致，简化部分依赖

#include "layact.h"
#include "../core/node.h"
#include "../core/doc.h"
#include "../core/format.h"
#include "../render/output_device.h"
#include "../font/font_engine.h"
#include "../frame/objectformatter.h"  // 新增：ObjectFormatter 支持
#include <algorithm>
#include <iostream>

//===----------------------------------------------------------------------===//
// 常量定义（对应 LO）
//===----------------------------------------------------------------------===//

constexpr sal_uInt16 USHRT_MAX_VAL = 0xFFFF; // 对应 USHRT_MAX

//===----------------------------------------------------------------------===//
// SwLayAction 构造/析构
//===----------------------------------------------------------------------===//

SwLayAction::SwLayAction(SwRootFrame* pRoot, SwViewShellImp* pImp)
    : m_pRoot(pRoot)
    , m_pImp(pImp)
    , m_pOptTab(nullptr)
    , m_nPreInvaPage(USHRT_MAX_VAL)
    , m_nStartTicks(std::clock())
    , m_nEndPage(USHRT_MAX_VAL)
    , m_nCheckPageNum(USHRT_MAX_VAL)
{
    // === LO 初始化标志位 ===
    m_bPaintExtraData = false; // 简化：不检查 IsExtraData
    m_bPaint = true;
    m_bComplete = true;
    m_bWaitAllowed = true;
    m_bCheckPages = true;
    m_bInterrupt = false;
    m_bAgain = false;
    m_bNextCycle = false;
    m_bCalcLayout = false;
    m_bIdle = false;
    m_bReschedule = false;
    m_bUpdateExpFields = false;
    m_bBrowseActionStop = false;
    m_bActionInProgress = false;
    mbFormatContentOnInterrupt = false;
}

SwLayAction::SwLayAction(SwRootFrame& rRoot)
    : m_pRoot(&rRoot)
    , m_pImp(nullptr)
    , m_pOptTab(nullptr)
    , m_nPreInvaPage(USHRT_MAX_VAL)
    , m_nStartTicks(std::clock())
    , m_nEndPage(USHRT_MAX_VAL)
    , m_nCheckPageNum(USHRT_MAX_VAL)
{
    // === LO 初始化标志位 ===
    m_bPaintExtraData = false; // 简化：不检查 IsExtraData
    m_bPaint = true;
    m_bComplete = true;
    m_bWaitAllowed = true;
    m_bCheckPages = true;
    m_bInterrupt = false;
    m_bAgain = false;
    m_bNextCycle = false;
    m_bCalcLayout = false;
    m_bIdle = false;
    m_bReschedule = false;
    m_bUpdateExpFields = false;
    m_bBrowseActionStop = false;
    m_bActionInProgress = false;
    mbFormatContentOnInterrupt = false;
}

SwLayAction::~SwLayAction()
{
    // 简化版：不需要 unregister
}

//===----------------------------------------------------------------------===//
// Reset - 重置到默认值
//===----------------------------------------------------------------------===//

void SwLayAction::Reset()
{
    SetAgain(false);
    m_pOptTab = nullptr;
    m_nStartTicks = std::clock();
    m_nPreInvaPage = USHRT_MAX_VAL;
    m_nCheckPageNum = USHRT_MAX_VAL;
    m_nEndPage = USHRT_MAX_VAL;
    m_bPaint = true;
    m_bComplete = true;
    m_bWaitAllowed = true;
    m_bCheckPages = true;
    m_bInterrupt = false;
    m_bNextCycle = false;
    m_bCalcLayout = false;
    m_bIdle = false;
    m_bReschedule = false;
    m_bUpdateExpFields = false;
    m_bBrowseActionStop = false;
}

//===----------------------------------------------------------------------===//
// SetStatBar - 设置进度条
//===----------------------------------------------------------------------===//

void SwLayAction::SetStatBar(bool bNew)
{
    if (bNew)
    {
        m_nEndPage = m_pRoot->GetPageNum();
        m_nEndPage += m_nEndPage * 10 / 100;
    }
    else
    {
        m_nEndPage = USHRT_MAX_VAL;
    }
}

//===----------------------------------------------------------------------===//
// SetAgain - 设置重新排版标志
//===----------------------------------------------------------------------===//

void SwLayAction::SetAgain(bool bAgain)
{
    if (bAgain == m_bAgain)
        return;

    m_bAgain = bAgain;

    // 简化版：不处理 FrameDeleteGuard
    // LO 原版会管理 m_aFrameDeleteGuards
}

//===----------------------------------------------------------------------===//
// PushFormatLayout / PopFormatLayout - 帧栈管理
//===----------------------------------------------------------------------===//

void SwLayAction::PushFormatLayout(SwFrame* pLow)
{
    m_aFrameStack.push_back(pLow);
}

void SwLayAction::PopFormatLayout()
{
    if (!m_aFrameStack.empty())
        m_aFrameStack.pop_back();
}

//===----------------------------------------------------------------------===//
// CheckWaitCursor - 检查等待光标
//===----------------------------------------------------------------------===//

void SwLayAction::CheckWaitCursor()
{
    // 简化版：不创建 SwWait 对象
    // LO 原版会在排版时间过长时显示等待光标
}

//===----------------------------------------------------------------------===//
// Action - 主入口（对应 LO layact.cxx 行 373-433）
//===----------------------------------------------------------------------===//

void SwLayAction::Action(OutputDevice* pRenderContext)
{
    m_bActionInProgress = true;

    // === TurboMode 检查 ===
    // 简化版：跳过 TurboAction
    // LO 原版：if ( IsPaint() && !IsIdle() && TurboAction() )

    if (IsCalcLayout())
        SetCheckPages(false);

    // === 主排版循环 ===
    InternalAction(pRenderContext);

    // === 页面清理 ===
    if (RemoveEmptyBrowserPages())
        SetAgain(true);

    // === SetAgain 循环处理 ===
    while (IsAgain())
    {
        SetAgain(false);
        m_bNextCycle = false;
        InternalAction(pRenderContext);
        if (RemoveEmptyBrowserPages())
            SetAgain(true);
    }

    // === 清理 ===
    // 简化版：不调用 DeleteEmptySct/DeleteEmptyFlys

    SetCheckPages(true);
    m_bActionInProgress = false;
}

void SwLayAction::Action()
{
    // 简化版：不需要 OutputDevice
    Action(nullptr);
}

//===----------------------------------------------------------------------===//
// InternalAction - 内部排版动作（对应 LO layact.cxx 行 489-873）
//===----------------------------------------------------------------------===//

void SwLayAction::InternalAction(OutputDevice* pRenderContext)
{
    // === 初始化 ===
    if (!m_pRoot->Lower())
        return;

    // 计算根 Frame
    m_pRoot->Calc();

    // === 确定起始页面 ===
    SwPageFrame* pPage = nullptr;
    if (IsComplete())
    {
        pPage = static_cast<SwPageFrame*>(m_pRoot->Lower());
    }
    else
    {
        // 简化版：从第一个页面开始
        pPage = static_cast<SwPageFrame*>(m_pRoot->Lower());
    }

    if (!pPage)
        return;

    // === 查找第一个无效页面 ===
    while (pPage && !pPage->IsInvalid())
    {
        pPage = static_cast<SwPageFrame*>(pPage->GetNext());
    }

    sal_uInt16 nFirstPageNum = pPage ? pPage->GetPhyPageNum() : 0;

    // === 主循环 ===
    int nOuterLoopControlRuns = 0;
    const int nOuterLoopControlMax = 10000;

    while ((pPage && !IsInterrupt()) || m_nCheckPageNum != USHRT_MAX_VAL)
    {
        // 循环控制
        if (++nOuterLoopControlRuns > nOuterLoopControlMax)
        {
            std::cerr << "SwLayAction::InternalAction loop limit reached" << std::endl;
            m_bInterrupt = true;
            break;
        }

        // === 处理 CheckPageNum ===
        if ((IsInterrupt() || !pPage) && m_nCheckPageNum != USHRT_MAX_VAL)
        {
            // 简化版：跳过页面检查
            m_nCheckPageNum = USHRT_MAX_VAL;
            continue;
        }

        m_pOptTab = nullptr;

        // === 格式化页面 ===
        if (pPage && pPage->IsInvalid())
        {
            // 格式化布局
            while (!IsInterrupt() && !IsNextCycle() && pPage->IsInvalidLayout())
            {
                pPage->ValidateLayout(); // 简化版：假设有此方法
                FormatLayout(pRenderContext, pPage);
                if (IsAgain())
                    return;
            }

            // 格式化内容
            if (!IsNextCycle() && pPage->IsInvalidContent())
            {
                pPage->ValidateContent(); // 简化版
                if (!FormatContent(pPage))
                {
                    pPage->InvalidateContent();
                    if (IsBrowseActionStop())
                        m_bInterrupt = true;
                }
            }
        }

        // === 查找下一个无效页面 ===
        if (!IsInterrupt())
        {
            SetNextCycle(false);

            // 继续到下一个无效页面
            while (pPage && !pPage->IsInvalid())
            {
                pPage = static_cast<SwPageFrame*>(pPage->GetNext());
            }
        }

        CheckIdleEnd();
    }

    m_pOptTab = nullptr;
}

//===----------------------------------------------------------------------===//
// TurboAction - 快速排版（对应 LO layact.cxx 行 931-947）
//===----------------------------------------------------------------------===//

bool SwLayAction::TurboAction()
{
    // 简化版：不实现 Turbo 模式
    return false;
}

bool SwLayAction::TurboAction_(const SwContentFrame* pCnt)
{
    // 简化版：不实现
    return false;
}

//===----------------------------------------------------------------------===//
// FormatLayout - 布局格式化（对应 LO layact.cxx 行 1290-1537）
//===----------------------------------------------------------------------===//

bool SwLayAction::FormatLayout(OutputDevice* pRenderContext, SwLayoutFrame* pLay, bool bAddRect)
{
    if (IsAgain() || !pLay)
        return false;

    bool bChanged = false;
    bool bAlreadyPainted = false;
    SwRect aFrameAtCompletePaint;

    // === 格式化当前 Frame ===
    if (!pLay->isFrameAreaDefinitionValid() || pLay->IsCompletePaint())
    {
        SwRect aOldFrame(pLay->getFrameArea());

        // 格式化
        pLay->Calc();

        if (aOldFrame != pLay->getFrameArea())
            bChanged = true;

        // 绘制（简化版）
        if (IsPaint() && bAddRect && (pLay->IsCompletePaint() || bChanged))
        {
            // 简化版：不调用 AddPaintRect
            bAlreadyPainted = true;
            aFrameAtCompletePaint = pLay->getFrameArea();
        }

        pLay->ResetCompletePaint();
    }

    if (IsAgain())
        return false;

    CheckWaitCursor();

    // === 递归格式化子 Frame ===
    if (pLay->IsFootnoteFrame())
        return bChanged;

    SwFrame* pLow = pLay->Lower();
    bool bTabChanged = false;

    while (pLow && pLow->GetUpper() == pLay)
    {
        SwFrame* pNext = nullptr;

        if (pLow->IsLayoutFrame())
        {
            if (pLow->IsTabFrame())
            {
                pNext = pLow->GetNext();
                bTabChanged |= FormatLayoutTab(static_cast<SwTabFrame*>(pLow), bAddRect);
            }
            else if (pLow->IsSctFrame())
            {
                // 简化版：跳过空 Section
                SwSectionFrame* pSct = static_cast<SwSectionFrame*>(pLow);
                if (pSct->GetSection())
                {
                    PushFormatLayout(pLow);
                    bChanged |= FormatLayout(pRenderContext, static_cast<SwLayoutFrame*>(pLow), bAddRect);
                    PopFormatLayout();
                }
            }
            else
            {
                PushFormatLayout(pLow);
                bChanged |= FormatLayout(pRenderContext, static_cast<SwLayoutFrame*>(pLow), bAddRect);
                PopFormatLayout();
            }
        }

        if (IsAgain())
            return false;

        if (!pNext)
            pNext = pLow->GetNext();

        pLow = pNext;
    }

    return bChanged || bTabChanged;
}

//===----------------------------------------------------------------------===//
// FormatLayoutTab - 表格格式化（对应 LO layact.cxx 行 1588-1734）
//===----------------------------------------------------------------------===//

bool SwLayAction::FormatLayoutTab(SwTabFrame* pTab, bool bAddRect)
{
    if (IsAgain() || !pTab || !pTab->Lower())
        return false;

    bool bChanged = false;
    bool bPainted = false;

    // === 格式化表格 ===
    if (!pTab->isFrameAreaDefinitionValid() || pTab->IsCompletePaint())
    {
        SwRect aOldRect(pTab->getFrameArea());
        pTab->Calc();

        if (aOldRect != pTab->getFrameArea())
            bChanged = true;

        // 绘制（简化版）
        if (IsPaint() && bAddRect && pTab->IsCompletePaint())
        {
            bAddRect = false;
            bPainted = true;
        }

        if (pTab->IsCompletePaint() && !m_pOptTab)
            m_pOptTab = pTab;

        pTab->ResetCompletePaint();
    }

    CheckWaitCursor();

    if (IsAgain())
        return false;

    // === 格式化子 Frame ===
    if (pTab->isFrameAreaDefinitionValid())
    {
        SwLayoutFrame* pLow = static_cast<SwLayoutFrame*>(pTab->Lower());
        while (pLow)
        {
            bChanged |= FormatLayout(nullptr, pLow, bAddRect);
            if (IsAgain())
                return false;
            pLow = static_cast<SwLayoutFrame*>(pLow->GetNext());
        }
    }

    return bChanged;
}

//===----------------------------------------------------------------------===//
// FormatLayoutFly - 浮动框格式化（对应 LO layact.cxx 行 1539-1585）
//===----------------------------------------------------------------------===//

void SwLayAction::FormatLayoutFly(SwFlyFrame* pFly)
{
    if (IsAgain() || !pFly)
        return;

    bool bChanged = false;
    bool bAddRect = true;

    // === 格式化浮动框 ===
    if (!pFly->isFrameAreaDefinitionValid() || pFly->IsCompletePaint())
    {
        SwRect aOldRect(pFly->getFrameArea());
        pFly->Calc();

        bChanged = aOldRect != pFly->getFrameArea();

        if (IsPaint() && (pFly->IsCompletePaint() || bChanged))
        {
            // 简化版：不调用 AddPaintRect
        }

        if (bChanged)
            pFly->Invalidate();
        else
            pFly->Validate();

        bAddRect = false;
        pFly->ResetCompletePaint();
    }

    if (IsAgain())
        return;

    // === 格式化子 Frame ===
    SwFrame* pLow = pFly->Lower();
    while (pLow)
    {
        if (pLow->IsLayoutFrame())
        {
            if (pLow->IsTabFrame())
                FormatLayoutTab(static_cast<SwTabFrame*>(pLow), bAddRect);
            else
                FormatLayout(nullptr, static_cast<SwLayoutFrame*>(pLow), bAddRect);
        }
        pLow = pLow->GetNext();
    }
}

//===----------------------------------------------------------------------===//
// FormatContent - 内容格式化（对应 LO layact.cxx 行 1736-2017）
//===----------------------------------------------------------------------===//

bool SwLayAction::FormatContent(SwPageFrame* pPage)
{
    if (!pPage)
        return true;

    const SwContentFrame* pContent = pPage->ContainsContent();

    while (pContent && pPage->IsAnLower(pContent))
    {
        // === 检查是否需要完整格式化 ===
        bool bFull = !pContent->isFrameAreaDefinitionValid() || pContent->IsCompletePaint();

        if (bFull)
        {
            // === 格式化内容 ===
            const SwLayoutFrame* pOldUpper = pContent->GetUpper();
            const bool bOldPaint = IsPaint();

            FormatContent_(pContent, pPage);

            // === 检查页面变化 ===
            if (pOldUpper != pContent->GetUpper())
            {
                sal_uInt16 nCurNum = pContent->FindPageFrame()->GetPhyPageNum();
                if (nCurNum < pPage->GetPhyPageNum())
                    m_nPreInvaPage = nCurNum;

                if (!IsCalcLayout() && pPage->GetPhyPageNum() > nCurNum + 1)
                {
                    SetNextCycle(true);
                    if (!mbFormatContentOnInterrupt)
                        return false;
                }
            }

            if (IsAgain())
                return false;

            CheckIdleEnd();

            if (IsInterrupt() && !mbFormatContentOnInterrupt)
                return false;
        }

        pContent = pContent->GetNextContentFrame();
    }

    CheckWaitCursor();
    return !IsInterrupt() || mbFormatContentOnInterrupt;
}

//===----------------------------------------------------------------------===//
// FormatContent_ - 内容格式化辅助（对应 LO layact.cxx 行 2019-2045）
//===----------------------------------------------------------------------===//

void SwLayAction::FormatContent_(const SwContentFrame* pContent, const SwPageFrame* pPage)
{
    if (!pContent)
        return;

    bool bDrawObjsOnly = pContent->isFrameAreaDefinitionValid() && !pContent->IsCompletePaint();

    if (!bDrawObjsOnly && IsPaint())
    {
        SwRect aOldRect(pContent->UnionFrame());
        pContent->OptCalc(); // 简化版：假设有此方法

        if (IsAgain())
            return;

        // 简化版：不调用 PaintContent
    }
    else
    {
        pContent->OptCalc();
    }
}

//===----------------------------------------------------------------------===//
// FormatFlyContent - 浮动框内容格式化（对应 LO layact.cxx 行 2047-2090）
//===----------------------------------------------------------------------===//

void SwLayAction::FormatFlyContent(SwFlyFrame* pFly)
{
    if (!pFly)
        return;

    const SwContentFrame* pContent = pFly->ContainsContent();

    while (pContent)
    {
        FormatContent_(pContent, pContent->FindPageFrame());

        if (IsAgain())
            return;

        CheckIdleEnd();

        if (IsInterrupt() && !mbFormatContentOnInterrupt)
            return;

        pContent = pContent->GetNextContentFrame();
    }

    CheckWaitCursor();
}

//===----------------------------------------------------------------------===//
// FormatObj_ - 格式化单个浮动对象（ObjectFormatter 支持）
//===----------------------------------------------------------------------===//

void SwLayAction::FormatObj_(SwFlyFrame& rFly)
{
    // 对应 LO layact.cxx FormatObj_
    // 通过 ObjectFormatter 格式化单个浮动对象
    
    FormatLayoutFly(&rFly);
    
    if (IsAgain())
        return;
    
    FormatFlyContent(&rFly);
}

//===----------------------------------------------------------------------===//
// FormatObjsAtFrame - 格式化锚定对象（ObjectFormatter 支持）
//===----------------------------------------------------------------------===//

bool SwLayAction::FormatObjsAtFrame(SwFrame& rAnchorFrame, SwPageFrame& rPageFrame)
{
    // 对应 LO layact.cxx FormatObjsAtFrame
    // 使用 ObjectFormatter 格式化锚定在给定 Frame 上的所有浮动对象
    
    return SwObjectFormatter::FormatObjsAtFrame(rAnchorFrame, rPageFrame, this);
}

//===----------------------------------------------------------------------===//
// IsShortCut - 快捷路径检查（对应 LO layact.cxx 行 1061-1287）
//===----------------------------------------------------------------------===//

bool SwLayAction::IsShortCut(SwPageFrame*& prPage)
{
    // 简化版：不实现快捷路径
    return false;
}

//===----------------------------------------------------------------------===//
// CheckFirstVisPage - 检查第一个可见页面（对应 LO layact.cxx 行 435-471）
//===----------------------------------------------------------------------===//

SwPageFrame* SwLayAction::CheckFirstVisPage(SwPageFrame* pPage)
{
    // 简化版：直接返回传入页面
    return pPage;
}

//===----------------------------------------------------------------------===//
// RemoveEmptyBrowserPages - 删除空浏览页面（对应 LO layact.cxx 行 298-324）
//===----------------------------------------------------------------------===//

bool SwLayAction::RemoveEmptyBrowserPages()
{
    // 简化版：不实现
    return false;
}

//===----------------------------------------------------------------------===//
// PaintContent / PaintWithoutFlys / PaintContent_ - 绘制相关（简化版）
//===----------------------------------------------------------------------===//

void SwLayAction::PaintContent(const SwContentFrame* pCnt, const SwPageFrame* pPage,
                               const SwRect& rOldRect, SwTwips nOldBottom)
{
    // 简化版：不实现绘制
}

bool SwLayAction::PaintWithoutFlys(const SwRect& rRect, const SwContentFrame* pCnt,
                                   const SwPageFrame* pPage)
{
    // 简化版：不实现
    return false;
}

bool SwLayAction::PaintContent_(const SwContentFrame* pContent, const SwPageFrame* pPage,
                               const SwRect& rRect)
{
    // 简化版：不实现
    return false;
}

//===----------------------------------------------------------------------===//
// TextFormatter - 文本格式化器（保留原有实现）
//===----------------------------------------------------------------------===//

TextFormatter::TextFormatter(FontEngine* pFontEngine)
    : m_pFontEngine(pFontEngine)
{
}

TextFormatter::~TextFormatter() = default;

void TextFormatter::FormatTextFrame(SwTextFrame* pFrame)
{
    if (!pFrame || !pFrame->GetNode())
        return;

    // 获取文本内容
    SwTextNode* pNode = static_cast<SwTextNode*>(pFrame->GetNode());
    const std::string& text = pNode->GetText();

    // 获取字体信息
    std::string fontName = "Arial";
    int fontSize = 20; // LO 默认 10pt (20 半点)

    // 从节点属性获取字体
    const std::string* pFont = pNode->GetAttr(RES_CHRATR_FONT);
    if (pFont)
        fontName = *pFont;

    const std::string* pSize = pNode->GetAttr(RES_CHRATR_FONTSIZE);
    if (pSize)
        fontSize = std::stoi(*pSize);

    // 计算可用宽度
    SwRect aPrtRect = pFrame->getFramePrintArea();
    int maxWidth = static_cast<int>(aPrtRect.Width());

    // 换行
    auto lines = BreakIntoLines(text, fontName, fontSize, maxWidth);

    // 设置行数
    pFrame->SetLines(static_cast<sal_Int32>(lines.size()));

    // 计算 Frame 高度
    int totalHeight = 0;
    for (const auto& line : lines)
    {
        totalHeight += line.height;
    }

    // 更新 Frame 大小
    SwRect aFrameRect = pFrame->getFrameArea();
    aFrameRect.SetHeight(totalHeight);
    pFrame->setFrameArea(aFrameRect);
}

int TextFormatter::CalcLineHeight(const std::string& fontName, int fontSize)
{
    if (m_pFontEngine)
    {
        return m_pFontEngine->MeasureTextHeight(fontName, fontSize);
    }
    // 默认行高：fontSize * 1.2
    return static_cast<int>(fontSize * 1.2);
}

int TextFormatter::CalcStringWidth(const std::string& text, const std::string& fontName,
                                   int fontSize)
{
    if (m_pFontEngine)
    {
        return static_cast<int>(m_pFontEngine->MeasureTextWidth(fontName, fontSize, text));
    }
    // 默认宽度：每个字符约 0.6 * fontSize
    return static_cast<int>(text.size() * fontSize * 0.6);
}

std::vector<TextFormatter::LineBreak> TextFormatter::BreakIntoLines(const std::string& text,
                                                                    const std::string& fontName,
                                                                    int fontSize, int maxWidth)
{
    std::vector<LineBreak> lines;

    if (text.empty())
    {
        // 空段落也有一行
        LineBreak line;
        line.startPos = 0;
        line.endPos = 0;
        line.width = 0;
        line.height = CalcLineHeight(fontName, fontSize);
        lines.push_back(line);
        return lines;
    }

    int lineHeight = CalcLineHeight(fontName, fontSize);
    int pos = 0;
    int lineStart = 0;

    while (pos < static_cast<int>(text.size()))
    {
        // 查找行尾
        int lineEnd = pos;
        int lineWidth = 0;

        while (lineEnd < static_cast<int>(text.size()))
        {
            // 查找下一个空格或换行
            int wordEnd = lineEnd;
            while (wordEnd < static_cast<int>(text.size()) && text[wordEnd] != ' '
                   && text[wordEnd] != '\n' && text[wordEnd] != '\t')
            {
                ++wordEnd;
            }

            // 计算单词宽度
            std::string word = text.substr(lineEnd, wordEnd - lineEnd);
            int wordWidth = CalcStringWidth(word, fontName, fontSize);

            // 检查是否超过最大宽度
            if (lineWidth + wordWidth > maxWidth && lineWidth > 0)
            {
                break;
            }

            lineWidth += wordWidth;
            lineEnd = wordEnd;

            // 跳过空格
            if (lineEnd < static_cast<int>(text.size()) && text[lineEnd] == ' ')
            {
                lineWidth += CalcStringWidth(" ", fontName, fontSize);
                ++lineEnd;
            }

            // 处理换行符
            if (lineEnd < static_cast<int>(text.size()) && text[lineEnd] == '\n')
            {
                ++lineEnd;
                break;
            }

            // 处理制表符
            if (lineEnd < static_cast<int>(text.size()) && text[lineEnd] == '\t')
            {
                // 跳到下一个制表位（每 0.5 英寸）
                int tabWidth = 480; // 0.5 英寸 = 720 twips ≈ 480 像素
                lineWidth = (lineWidth / tabWidth + 1) * tabWidth;
                ++lineEnd;
            }
        }

        // 创建行
        LineBreak line;
        line.startPos = lineStart;
        line.endPos = lineEnd;
        line.width = lineWidth;
        line.height = lineHeight;
        lines.push_back(line);

        lineStart = lineEnd;
        pos = lineEnd;
    }

    return lines;
}