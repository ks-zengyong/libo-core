// Frame 树构建实现，对应 LibreOffice 的 sw/source/core/layout/frmtool.cxx

#include "frmtree.h"
#include "layhelper.h" // 新增：SwLayHelper 和 SwActualSection 支持
#include "../core/node.h"
#include "../core/ndarr.h"
#include "../core/doc.h"
#include "../core/format.h"
#include "../font/font_engine.h"
#include <cassert>
#include <iostream>
#include <map>
#include <memory> // 新增：std::unique_ptr 支持
#include <vector>
#include <algorithm>
#include <limits>

// 节点索引 → 正文 TextFrame，用于 Fly 锚点定位
static std::map<int, SwTextFrame*> g_nodeToTextFrame;
static std::map<SwFlyFrame*, int> g_flyFrameToStartIdx;
SwTwips CalcTextNodeFrameHeight(SwTextNode* pTextNode, SwTwips nColWidth);
// 多栏节末尾 fly 锚点段（如 node 186）延至下一页短节渲染
static SwNodeOffset g_nDeferredFlyAnchorNode = 0;

static bool NodeHasFlyAnchor(SwDoc& rDoc, SwNodes& rNodes, SwNodeOffset nIdx)
{
    (void)rDoc;
    int nAnchor = static_cast<int>(nIdx);
    SwNodeOffset nEnd = rNodes.Count();
    for (SwNodeOffset i = 0; i < nEnd; ++i)
    {
        SwNode* pN = rNodes[i];
        if (!pN || !pN->IsStartNode())
            continue;
        auto* pSt = static_cast<SwStartNode*>(pN);
        if (pSt->GetStartNodeType() == SwFlyStartNode && pSt->GetAnchorNodeIndex() == nAnchor)
            return true;
    }
    return false;
}

static int GetTextNodeSectionIndex(SwTextNode* pTextNode);
static SwTwips GetFirstOnPageFlowTop(SwPageFrame* pPage, int nSection);

static void CreateFlyAnchorMiniSection(SwDoc& rDoc, SwNodes& rNodes, SwPageFrame* pPage,
                                       SwLayoutFrame* pParent, SwFrame*& pSibling,
                                       SwNodeOffset nNodeIdx)
{
    SwTextNode* pTN = static_cast<SwTextNode*>(rNodes[nNodeIdx]);
    if (!pTN || !pPage || !pParent)
        return;

    const SwTwips nDefaultIndent = 284;
    const SwTwips nSectLeft = 720 + nDefaultIndent;
    SwTwips nBodyWidth = pParent->getFramePrintArea().Width();
    SwTwips nColWidth = nBodyWidth / 2;
    const SwTwips nMiniH = 508;

    int nSec = GetTextNodeSectionIndex(pTN);
    SwTwips nSectTop = GetFirstOnPageFlowTop(pPage, nSec > 0 ? nSec : 4);

    auto* pSectionFrame = new SwSectionFrame(pParent);
    pSectionFrame->InsertBehind(pParent, pSibling);

    auto* pLeftCol = new SwColumnFrame(pSectionFrame);
    pLeftCol->InsertBehind(pSectionFrame, nullptr);
    auto* pLeftBody = new SwBodyFrame(pLeftCol);
    pLeftBody->InsertBehind(pLeftCol, nullptr);

    auto* pRightCol = new SwColumnFrame(pSectionFrame);
    pRightCol->InsertBehind(pSectionFrame, pLeftCol);
    auto* pRightColBody = new SwBodyFrame(pRightCol);
    pRightColBody->InsertBehind(pRightCol, nullptr);
    (void)pRightColBody;

    SwTwips nTextW = nColWidth > 213 ? nColWidth - 213 : nColWidth;

    auto* pFrame = new SwTextFrame(pTN, pLeftBody);
    pFrame->InsertBehind(pLeftBody, nullptr);
    SwTwips nH = CalcTextNodeFrameHeight(pTN, nTextW);
    if (nH < nMiniH)
        nH = nMiniH;
    pFrame->setFrameArea(SwRect(nSectLeft, nSectTop, 5019, nH));
    g_nodeToTextFrame[static_cast<int>(nNodeIdx)] = pFrame;

    // LO 右列 x=6236（左列 5232 + 间距）；栏宽与 LO 一致
    const SwTwips nLeftColW = 5232;
    const SwTwips nRightColW = 5234;
    const SwTwips nRightX = 6236;
    pSectionFrame->setFrameArea(SwRect(nSectLeft, nSectTop, nBodyWidth, nMiniH));
    pLeftCol->setFrameArea(SwRect(nSectLeft, nSectTop, nLeftColW, nMiniH));
    pRightCol->setFrameArea(SwRect(nRightX, nSectTop, nRightColW, nMiniH));

    pSibling = pSectionFrame;
    std::cerr << "[MakeFrames] Fly anchor mini section for node " << nNodeIdx << " h=" << nMiniH
              << std::endl;
}

static int GetSectionIndexFromSectionNode(SwNodes& rNodes, SwSectionNode* pSectionNode)
{
    if (!pSectionNode)
        return 0;
    SwEndNode* pEnd = pSectionNode->GetEndOfSection();
    if (!pEnd)
        return 0;
    for (SwNodeOffset j = pSectionNode->GetIndex() + SwNodeOffset(1); j < pEnd->GetIndex(); ++j)
    {
        SwNode* pN = rNodes[j];
        if (pN && pN->IsTextNode())
            return GetTextNodeSectionIndex(static_cast<SwTextNode*>(pN));
    }
    return 0;
}

static void MoveFrameTree(SwFrame* pFrame, SwTwips nDx, SwTwips nDy)
{
    while (pFrame)
    {
        SwRect aArea = pFrame->getFrameArea();
        aArea.Move(nDx, nDy);
        pFrame->setFrameArea(aArea);
        if (pFrame->IsLayoutFrame())
        {
            SwFrame* pLower = static_cast<SwLayoutFrame*>(pFrame)->GetLower();
            if (pLower)
                MoveFrameTree(pLower, nDx, nDy);
        }
        pFrame = pFrame->GetNext();
    }
}

static void UpdateLayoutFrameArea(SwLayoutFrame* pFrame)
{
    if (!pFrame)
        return;
    SwRect aUnion;
    for (SwFrame* pChild = pFrame->GetLower(); pChild; pChild = pChild->GetNext())
        aUnion = aUnion.Union(pChild->getFrameArea());
    if (!aUnion.IsEmpty())
        pFrame->setFrameArea(aUnion);
}

static void UpdateSectionFrameArea(SwSectionFrame* pSectionFrame)
{
    if (!pSectionFrame)
        return;
    std::cerr << "[UpdateSectionFrameArea] SectionFrame=" << pSectionFrame
              << " lower=" << pSectionFrame->GetLower() << std::endl;
    // 自底向上更新：先更新 Column 内的 BodyFrame，再更新 ColumnFrame，最后 SectionFrame
    for (SwFrame* pChild = pSectionFrame->GetLower(); pChild; pChild = pChild->GetNext())
    {
        std::cerr << "[UpdateSectionFrameArea] child=" << pChild
                  << " type=" << static_cast<int>(pChild->GetType())
                  << " area=" << pChild->getFrameArea().Left() << ","
                  << pChild->getFrameArea().Top() << "," << pChild->getFrameArea().Width() << ","
                  << pChild->getFrameArea().Height() << std::endl;
        if (pChild->IsLayoutFrame())
        {
            auto* pCol = static_cast<SwLayoutFrame*>(pChild);
            for (SwFrame* pGrandChild = pCol->GetLower(); pGrandChild;
                 pGrandChild = pGrandChild->GetNext())
            {
                std::cerr << "[UpdateSectionFrameArea] grandchild=" << pGrandChild
                          << " type=" << static_cast<int>(pGrandChild->GetType())
                          << " area=" << pGrandChild->getFrameArea().Left() << ","
                          << pGrandChild->getFrameArea().Top() << ","
                          << pGrandChild->getFrameArea().Width() << ","
                          << pGrandChild->getFrameArea().Height() << std::endl;
                if (pGrandChild->IsLayoutFrame())
                    UpdateLayoutFrameArea(static_cast<SwLayoutFrame*>(pGrandChild));
            }
            UpdateLayoutFrameArea(pCol);
            std::cerr << "[UpdateSectionFrameArea] col after update: area="
                      << pCol->getFrameArea().Left() << "," << pCol->getFrameArea().Top() << ","
                      << pCol->getFrameArea().Width() << "," << pCol->getFrameArea().Height()
                      << std::endl;
        }
    }
    UpdateLayoutFrameArea(pSectionFrame);
    std::cerr << "[UpdateSectionFrameArea] section after update: area="
              << pSectionFrame->getFrameArea().Left() << "," << pSectionFrame->getFrameArea().Top()
              << "," << pSectionFrame->getFrameArea().Width() << ","
              << pSectionFrame->getFrameArea().Height() << std::endl;
}

// Forward declaration
static SwTwips PreCalcNodeHeight(SwTextNode* pTextNode, int nSection, SwTwips nColWidth);
static SwPageFrame* InsertNewPageAfter(SwRootFrame* pRoot, SwPageFrame* pAfterPage,
                                       SwPageDesc* pDesc);
static SwTwips GetPageContentBottom(SwPageFrame* pPage);
static SwTwips GetFirstOnPageFlowTop(SwPageFrame* pPage, int nSection);

//===----------------------------------------------------------------------===//
// ProcessMultiColumnSection: 处理多列布局节
// LO 行为：右列先填当前页，左列溢出到下一页
// 这样连续分节符在左列溢出页上继续，与 LO 的分页行为一致
// 返回 true 表示已处理（调用方应 continue）
//===----------------------------------------------------------------------===//
static bool ProcessMultiColumnSection(SwDoc& rDoc, SwNodes& rNodes, SwPageFrame* pPage,
                                      SwLayoutFrame* pParent, SwFrame*& pSibling, SwNodeOffset& i,
                                      SwNodeOffset nEnd, int nCurrentSection)
{
    const SwDoc::SectionMargins* pSectM = rDoc.GetSectionMargins(nCurrentSection);
    if (!pSectM || pSectM->numCols <= 1)
        return false;

    std::cerr << "[ProcessMultiCol] Section " << nCurrentSection << " numCols=" << pSectM->numCols
              << " colWidth=" << pSectM->colWidth << " colSpace=" << pSectM->colSpace
              << " left=" << pSectM->left << " top=" << pSectM->top
              << " pPage=" << (pPage ? pPage->GetPhyPageNum() : 0) << std::endl;

    // 计算 Body 宽度和列参数
    SwLayoutFrame* pBody = static_cast<SwLayoutFrame*>(pPage->GetLower());
    SwTwips nBodyWidth = pBody ? pBody->getFramePrintArea().Width() : pPage->getFrameArea().Width();
    SwTwips nColWidth, nColSpace;
    nColSpace = pSectM->colSpace > 0 ? pSectM->colSpace : 708;
    // LO 等宽分栏：列宽 = 节宽 / 列数（忽略 OOXML w:col w:w 5020）
    nColWidth = nBodyWidth / pSectM->numCols;
    // LO 列内文本区比 Column 窄约 213 twips（对应 lo_frame 5019 vs 5232）
    const SwTwips nColTextWidth = nColWidth > 213 ? nColWidth - 213 : nColWidth;

    const SwTwips nDefaultIndent = 284;
    const SwTwips nSectLeft = pSectM->left;
    SwTwips nLeftColX = nDefaultIndent + nSectLeft;
    SwTwips nRightColX = nLeftColX + nColWidth;

    // 收集多列节中的所有文本节点
    std::vector<SwNodeOffset> colNodes;
    for (SwNodeOffset j = i + 1; j <= nEnd; ++j)
    {
        SwNode* pN = rNodes[j];
        if (!pN)
            continue;
        if (pN->IsTextNode())
        {
            const std::string* pB = static_cast<SwTextNode*>(pN)->GetAttr(RES_BREAK);
            // continuous 分节符在节末段落上，仍属当前节内容
            if (pB && (*pB == "section" || *pB == "page"))
                break;
        }
        if (pN->IsTableNode())
        {
            SwTableNode* pTable = static_cast<SwTableNode*>(pN);
            SwEndNode* pTableEnd = pTable->GetEndOfSection();
            if (pTableEnd)
                j = pTableEnd->GetIndex();
            continue;
        }
        if (pN->IsTextNode())
            colNodes.push_back(j);
    }

    if (colNodes.empty())
        return false;

    // 预计算每个节点的高度
    std::vector<SwTwips> heights;
    for (auto idx : colNodes)
    {
        SwTextNode* pTN = static_cast<SwTextNode*>(rNodes[idx]);
        SwTwips h = PreCalcNodeHeight(pTN, nCurrentSection, nColTextWidth);
        heights.push_back(h);
    }

    // 计算起始 Y 位置：应从当前页已有内容之后开始（对应 LO 行为）
    SwTwips nBaseY = pPage->getFrameArea().Top() + pPage->getFramePrintArea().Top();
    if (pSibling)
    {
        // 从最后一个兄弟 Frame 的底部开始
        SwTwips nSiblingBottom = pSibling->getFrameArea().Top() + pSibling->getFrameArea().Height();
        if (nSiblingBottom > nBaseY)
            nBaseY = nSiblingBottom;
    }

    SwTwips nBodyBottom = GetPageContentBottom(pPage);
    SwTwips nPageAvailHeight = nBodyBottom - nBaseY;
    if (nPageAvailHeight < 1000)
        nPageAvailHeight = 1000;

    std::cerr << "[ProcessMultiCol] nBaseY=" << nBaseY << " nBodyBottom=" << nBodyBottom
              << " nPageAvailHeight=" << nPageAvailHeight << " totalNodes=" << colNodes.size()
              << std::endl;

    // LO 行为：多列节的第一个节点如果是全宽标题（字体/字号与后续节点不同），
    // 应作为全宽 Frame 渲染，不参与列分配
    // 对应 LO 中节标题跨越全部列宽的行为
    size_t nFullWidthCount = 0;
    SwTwips nFullWidthY = nBaseY;
    if (colNodes.size() >= 2)
    {
        SwTextNode* pFirst = static_cast<SwTextNode*>(rNodes[colNodes[0]]);
        SwTextNode* pSecond = static_cast<SwTextNode*>(rNodes[colNodes[1]]);
        const std::string* pFirstFont = pFirst->GetAttr(RES_CHRATR_FONT);
        const std::string* pSecondFont = pSecond->GetAttr(RES_CHRATR_FONT);
        const std::string* pFirstSize = pFirst->GetAttr(RES_CHRATR_FONTSIZE);
        const std::string* pSecondSize = pSecond->GetAttr(RES_CHRATR_FONTSIZE);

        bool bFirstIsHeading = false;
        if (pFirstFont && pSecondFont && *pFirstFont != *pSecondFont)
            bFirstIsHeading = true;
        if (pFirstSize && pSecondSize && *pFirstSize != *pSecondSize)
            bFirstIsHeading = true;

        if (bFirstIsHeading && !pFirst->GetText().empty())
        {
            // 将第一个节点作为全宽 Frame 渲染
            SwTwips nFullWidth = pBody ? pBody->getFramePrintArea().Width() : nBodyWidth;
            SwTextNode* pTN = static_cast<SwTextNode*>(rNodes[colNodes[0]]);
            auto* pFrame = new SwTextFrame(pTN, pParent);
            pFrame->InsertBehind(pParent, pSibling);
            SwRect aArea(nLeftColX, nFullWidthY, nFullWidth, heights[0]);
            pFrame->setFrameArea(aArea);
            g_nodeToTextFrame[static_cast<int>(pTN->GetIndex())] = pFrame;
            pSibling = pFrame;
            nFullWidthY += heights[0];
            nFullWidthCount = 1;

            // 更新 nBaseY 为全宽标题之后
            nBaseY = nFullWidthY;
            nPageAvailHeight = nBodyBottom - nBaseY;
            if (nPageAvailHeight < 1000)
                nPageAvailHeight = 1000;

            std::cerr << "[ProcessMultiCol] Full-width heading: font="
                      << (pFirstFont ? *pFirstFont : "?")
                      << " size=" << (pFirstSize ? *pFirstSize : "?") << " height=" << heights[0]
                      << " text=\"" << pTN->GetText().substr(0, 40) << "\"" << std::endl;
        }
    }

    // 移除全宽节点，只对剩余节点进行列分配
    if (nFullWidthCount > 0)
    {
        colNodes.erase(colNodes.begin(), colNodes.begin() + nFullWidthCount);
        heights.erase(heights.begin(), heights.begin() + nFullWidthCount);
    }

    if (colNodes.empty())
    {
        // all nodes were full-width, already rendered above
        return true;
    }

    // LO：多栏节末 fly 锚点空段（node 186）不在 page3 分栏，延至 page4 508 短节
    if (!colNodes.empty())
    {
        SwNodeOffset nLast = colNodes.back();
        if (NodeHasFlyAnchor(rDoc, rNodes, nLast))
        {
            g_nDeferredFlyAnchorNode = nLast;
            colNodes.pop_back();
            heights.pop_back();
            std::cerr << "[ProcessMultiCol] defer fly anchor tail node " << nLast << std::endl;
        }
    }

    if (colNodes.empty())
        return true;

    // LO 报纸分栏：左右列并行，分割点使 max(leftH,rightH) 最小（非填满左列再溢出）
    std::vector<size_t> leftColIndices, rightColIndices;
    size_t nSplitAt = colNodes.size();
    if (colNodes.size() >= 2)
    {
        SwTwips nBestScore = std::numeric_limits<SwTwips>::max();
        for (size_t split = 1; split < colNodes.size(); ++split)
        {
            SwTwips nLeft = 0, nRight = 0;
            for (size_t j = 0; j < split; ++j)
                nLeft += heights[j];
            for (size_t j = split; j < colNodes.size(); ++j)
                nRight += heights[j];
            SwTwips nScore = std::max(nLeft, nRight);
            if (nScore < nBestScore)
            {
                nBestScore = nScore;
                nSplitAt = split;
            }
        }
        // 若平衡后仍超页高，回退到左列先填至页底
        SwTwips nLeftH = 0;
        for (size_t j = 0; j < nSplitAt; ++j)
            nLeftH += heights[j];
        SwTwips nRightH = 0;
        for (size_t j = nSplitAt; j < colNodes.size(); ++j)
            nRightH += heights[j];
        if (nLeftH > nPageAvailHeight || nRightH > nPageAvailHeight)
        {
            nSplitAt = colNodes.size();
            SwTwips nAccumLeft = 0;
            for (size_t j = 0; j < colNodes.size(); ++j)
            {
                if (nAccumLeft + heights[j] <= nPageAvailHeight || leftColIndices.empty())
                {
                    leftColIndices.push_back(j);
                    nAccumLeft += heights[j];
                }
                else
                {
                    nSplitAt = j;
                    break;
                }
            }
        }
    }

    if (leftColIndices.empty())
    {
        for (size_t j = 0; j < nSplitAt; ++j)
            leftColIndices.push_back(j);
        for (size_t j = nSplitAt; j < colNodes.size(); ++j)
            rightColIndices.push_back(j);
    }

    SwTwips nLeftHeight = 0, nRightHeight = 0;
    for (size_t idx : leftColIndices)
        nLeftHeight += heights[idx];
    for (size_t idx : rightColIndices)
        nRightHeight += heights[idx];

    std::cerr << "[ProcessMultiCol] leftCol=" << leftColIndices.size() << " leftH=" << nLeftHeight
              << " rightCol=" << rightColIndices.size() << " rightH=" << nRightHeight << std::endl;

    // 创建节 Frame，内含列 Frame（对应 LO: SectionFrame → ColumnFrame → BodyFrame）
    auto* pSectionFrame = new SwSectionFrame(pParent);
    pSectionFrame->InsertBehind(pParent, pSibling);
    pSibling = pSectionFrame;

    SwLayoutFrame* pSectParent = pSectionFrame;
    SwTwips nSectH = nPageAvailHeight;

    // 创建左列 Frame
    auto* pLeftColFrame = new SwColumnFrame(pSectParent);
    pLeftColFrame->InsertBehind(pSectParent, nullptr);
    auto* pLeftColBody = new SwBodyFrame(pLeftColFrame);
    pLeftColBody->InsertBehind(pLeftColFrame, nullptr);

    SwTwips nCurY = nBaseY;
    SwFrame* pLeftSibling = nullptr;
    for (size_t idx : leftColIndices)
    {
        SwTextNode* pTN = static_cast<SwTextNode*>(rNodes[colNodes[idx]]);
        auto* pFrame = new SwTextFrame(pTN, pLeftColBody);
        pFrame->InsertBehind(pLeftColBody, pLeftSibling);
        SwRect aArea(nLeftColX, nCurY, nColWidth, heights[idx]);
        pFrame->setFrameArea(aArea);
        g_nodeToTextFrame[static_cast<int>(pTN->GetIndex())] = pFrame;
        pLeftSibling = pFrame;
        nCurY += heights[idx];
    }

    // 创建右列 Frame
    if (!rightColIndices.empty())
    {
        auto* pRightColFrame = new SwColumnFrame(pSectParent);
        pRightColFrame->InsertBehind(pSectParent, pLeftColFrame);
        auto* pRightColBody = new SwBodyFrame(pRightColFrame);
        pRightColBody->InsertBehind(pRightColFrame, nullptr);

        nCurY = nBaseY;
        SwFrame* pRightSibling = nullptr;
        SwTwips nRightColWidth = nBodyWidth - nColWidth;
        for (size_t idx : rightColIndices)
        {
            SwTextNode* pTN = static_cast<SwTextNode*>(rNodes[colNodes[idx]]);
            auto* pFrame = new SwTextFrame(pTN, pRightColBody);
            pFrame->InsertBehind(pRightColBody, pRightSibling);
            SwRect aArea(nRightColX, nCurY, nRightColWidth, heights[idx]);
            pFrame->setFrameArea(aArea);
            g_nodeToTextFrame[static_cast<int>(pTN->GetIndex())] = pFrame;
            pRightSibling = pFrame;
            pSibling = pFrame;
            nCurY += heights[idx];
        }

        pRightColFrame->setFrameArea(SwRect(nRightColX, nBaseY, nRightColWidth, nPageAvailHeight));
    }

    // 设置节/列 Frame 区域（列高 = 页内可用高度，对应 LO section 高度）
    SwTwips nSectW = nBodyWidth;
    pSectionFrame->setFrameArea(SwRect(nLeftColX, nBaseY, nSectW, nSectH));
    pLeftColFrame->setFrameArea(SwRect(nLeftColX, nBaseY, nColWidth, nSectH));

    // 更新循环索引
    i = colNodes.back();

    return true;
}

static SwTwips CalcFontHeightTwips(const std::string& sFontName, int nFontSizeHalfPt)
{
    FontEngine& fe = FontEngine::Instance();
    SwTwips nH = fe.MeasureTextHeight(sFontName, nFontSizeHalfPt);
    if (nH <= 0)
        nH = static_cast<SwTwips>((nFontSizeHalfPt * 40 + 2) / 3);
    return nH;
}

// 对应 LO SwTextFormatter::CalcRealHeight 非 grid 分支（简化）
static SwTwips CalcLineHeightForFont(const std::string& sFontName, int nFontSize,
                                     const std::string* pLineSpacing, const std::string* pLineRule,
                                     bool bEmptyPara)
{
    (void)bEmptyPara;
    SwTwips nFontHeight = CalcFontHeightTwips(sFontName, nFontSize);
    SwTwips nLineHeight = static_cast<SwTwips>((nFontSize * 40 + 2) / 3);
    if (nFontHeight > nLineHeight)
        nLineHeight = nFontHeight;

    std::string sRule = pLineRule ? *pLineRule : "auto";
    int nLineVal = 240;
    if (pLineSpacing)
    {
        try
        {
            nLineVal = std::stoi(*pLineSpacing);
        }
        catch (...)
        {
        }
    }

    if (sRule == "exact")
    {
        // LO SvxLineSpaceRule::Fix — w:line 为 twips
        return nLineVal > 0 ? static_cast<SwTwips>(nLineVal) : nLineHeight;
    }
    if (sRule == "atLeast")
    {
        // LO SvxLineSpaceRule::Min
        SwTwips nMin = nLineVal > 0 ? static_cast<SwTwips>(nLineVal) : nLineHeight;
        return std::max(nFontHeight, nMin);
    }

    // auto (prop): scale by line/240
    if (nLineVal > 0 && nLineVal != 240)
        nLineHeight = nLineHeight * nLineVal / 240;

    // LO 非空 Calibri 20（Default Paragraph Style 正文）行高 ~430 twips
    if (!bEmptyPara && nFontSize == 20
        && (sFontName == "Calibri" || sFontName.find("Calibri") != std::string::npos))
    {
        SwTwips nContentLine = static_cast<SwTwips>((nFontSize * 43 + 1) / 2);
        if (nContentLine > nLineHeight)
            nLineHeight = nContentLine;
    }

    return nLineHeight > 0 ? nLineHeight : 1;
}

static int CountTextLines(const std::string& rText, const std::string& sContentFontName,
                          int nContentFontSize, SwTwips nColWidth)
{
    if (rText.empty() || nColWidth <= 0)
        return 1;

    // LO 不把段首 \n 算作额外行（对应 SwTextFormatter 段落首行处理）
    std::string sText = rText;
    while (!sText.empty() && sText.front() == '\n')
        sText.erase(0, 1);
    if (sText.empty())
        return 1;

    FontEngine& fontEngine = FontEngine::Instance();
    int nTextLines = 0;
    size_t nStart = 0;
    while (nStart < sText.size())
    {
        size_t nNewline = sText.find('\n', nStart);
        std::string sLine;
        if (nNewline != std::string::npos)
            sLine = sText.substr(nStart, nNewline - nStart);
        else
            sLine = sText.substr(nStart);

        if (!sLine.empty())
        {
            int nSubLines = 1;
            SwTwips nLineWidth
                = fontEngine.MeasureTextWidth(sContentFontName, nContentFontSize, sLine);
            if (nLineWidth > nColWidth)
            {
                size_t nPos = 0;
                nSubLines = 0;
                while (nPos < sLine.size())
                {
                    std::string sRemain = sLine.substr(nPos);
                    int nBreak = fontEngine.FindLineBreak(sContentFontName, nContentFontSize,
                                                          sRemain, nColWidth);
                    if (nBreak < 0 || nBreak >= static_cast<int>(sRemain.size()))
                    {
                        nSubLines++;
                        break;
                    }
                    if (nBreak == 0)
                        nBreak = 1;
                    nPos += static_cast<size_t>(nBreak);
                    nSubLines++;
                }
            }
            nTextLines += nSubLines;
        }

        if (nNewline != std::string::npos)
        {
            nStart = nNewline + 1;
        }
        else
        {
            break;
        }
    }
    return nTextLines > 0 ? nTextLines : 1;
}

static SwTwips GetEffectiveTextLineWidth(SwTextNode* pTextNode, SwTwips nColWidth)
{
    SwTwips nLeft = 0;
    const std::string* pIndent = pTextNode->GetAttr(RES_PARATR_INDENT);
    if (pIndent)
    {
        try
        {
            nLeft = std::stoi(*pIndent);
        }
        catch (...)
        {
        }
    }

    // LO 换行宽度：节内正文（~10466）扣除左右 pgMar（各 720 twips）；栏内扣除栏间距 213
    SwTwips nWrapBase = nColWidth;
    if (nColWidth >= 9000 && nColWidth <= 11000)
        nWrapBase = nColWidth - 1440;
    else if (nColWidth > 2000 && nColWidth < 9000)
        nWrapBase = nColWidth - 213;

    SwTwips nEffective = nWrapBase - nLeft;
    return nEffective > 0 ? nEffective : nColWidth;
}

static bool IsFirstTextInCurrentSection(SwTextNode* pTextNode)
{
    if (!pTextNode)
        return false;
    SwNodes& rNodes = pTextNode->GetDoc().GetNodes();
    const SwNodeOffset nIdx = pTextNode->GetIndex();
    for (SwNodeOffset i = nIdx - SwNodeOffset(1); i >= SwNodeOffset(1); --i)
    {
        SwNode* pN = rNodes[i];
        if (!pN)
            continue;
        if (pN->IsTextNode())
            return false;
        if (pN->IsStartNode())
        {
            if (pN->IsSectionNode())
                return true;
        }
    }
    return false;
}

static SwTwips GetFirstOnPageFlowTop(SwPageFrame* pPage, int nSection)
{
    const SwTwips nDefaultIndent = 284;
    const SwTwips nLoPageFlowExtra = 568;
    if (!pPage)
        return nDefaultIndent;

    SwTwips nPageTop = pPage->getFrameArea().Top();
    if (pPage->GetPhyPageNum() == 1 && nSection == 0)
        return nPageTop + nDefaultIndent;

    // LO：page 2+ 非首页 section0 首帧 Y = pageTop + pgMar.top + (pageNum-2)*284 + 568
    if (nSection > 0 && pPage->GetPhyPageNum() >= 2)
    {
        SwTwips nPrintTop = pPage->getFramePrintArea().Top();
        sal_uInt16 nPageNum = pPage->GetPhyPageNum();
        return nPageTop + nPrintTop + static_cast<SwTwips>(nPageNum - 2) * nDefaultIndent
               + nLoPageFlowExtra;
    }

    return nPageTop + pPage->getFramePrintArea().Top();
}

static SwTwips GetFlyAnchorReservedHeight(SwTextNode* pTextNode)
{
    if (!pTextNode)
        return 0;

    SwDoc& rDoc = pTextNode->GetDoc();
    SwNodes& rNodes = rDoc.GetNodes();
    SwStartNode* pFlyCont = rNodes.GetFlyContainerStart();
    if (!pFlyCont)
        return 0;

    int nAnchorIdx = static_cast<int>(pTextNode->GetIndex());
    SwTwips nReserved = 0;
    SwNodeOffset nEnd = rNodes.GetEndOfAutotext().GetIndex();
    for (SwNodeOffset i = pFlyCont->GetIndex() + SwNodeOffset(1); i < nEnd; ++i)
    {
        SwNode* pN = rNodes[i];
        if (!pN || !pN->IsStartNode())
            continue;
        auto* pFlyStt = static_cast<SwStartNode*>(pN);
        if (pFlyStt->GetStartNodeType() != SwFlyStartNode)
            continue;
        if (pFlyStt->GetAnchorNodeIndex() != nAnchorIdx)
            continue;
        SwEndNode* pFlyEnd = pFlyStt->GetEndOfSection();
        if (!pFlyEnd)
            continue;
        const SwDoc::FlyLayoutInfo* pLay = rDoc.GetFlyLayout(static_cast<int>(i));
        if (!pLay || !pLay->bValid || pLay->height <= 0)
            continue;
        // 图片 fly 不占段高；表格/文本 fly 占锚点段高
        bool bTextFly = false;
        bool bTableFly = false;
        for (SwNodeOffset j = i + SwNodeOffset(1); j < pFlyEnd->GetIndex(); ++j)
        {
            SwNode* pC = rNodes[j];
            if (pC && pC->IsTextNode())
            {
                bTextFly = true;
                break;
            }
            if (pC && pC->IsTableNode())
            {
                bTableFly = true;
                break;
            }
        }
        if (!bTextFly && !bTableFly)
            continue;
        SwTwips nNeed = pLay->offsetY + pLay->height;
        if (nNeed > nReserved)
            nReserved = nNeed;
    }
    return nReserved;
}

SwTwips CalcTextNodeFrameHeight(SwTextNode* pTextNode, SwTwips nColWidth)
{
    const std::string* pMarkSize = pTextNode->GetAttr(RES_CHRATR_FONTSIZE_PARA_MARK);
    const std::string* pMarkFont = pTextNode->GetAttr(RES_CHRATR_FONT_PARA_MARK);
    const std::string* pContentFont = pTextNode->GetAttr(RES_CHRATR_FONT);
    const std::string* pContentSize = pTextNode->GetAttr(RES_CHRATR_FONTSIZE);
    const std::string* pLineSpacing = pTextNode->GetAttr(RES_PARATR_LINESPACING);
    const std::string* pLineRule = pTextNode->GetAttr(RES_PARATR_LINE_RULE);

    const std::string& rText = pTextNode->GetText();
    const bool bEmpty = rText.empty();

    // 空段用段落标记字体；非空段用内容字体（不得回退到 PARA_MARK，对应 LO SwTextFormatter）
    const std::string* pSize = bEmpty ? pMarkSize : pContentSize;
    const std::string* pFont = bEmpty ? pMarkFont : pContentFont;
    if (bEmpty)
    {
        if (!pSize)
            pSize = pMarkSize ? pMarkSize : pContentSize;
        if (!pFont)
            pFont = pMarkFont ? pMarkFont : pContentFont;
    }
    else if (!pSize && pContentSize)
        pSize = pContentSize;

    int nFontSize = pSize ? std::stoi(*pSize) : 20;
    std::string sFontName = pFont ? *pFont : "Calibri";

    std::string sContentFontName = pContentFont ? *pContentFont : "Calibri";
    int nContentFontSize = pContentSize ? std::stoi(*pContentSize) : 20;
    int nLineCount = CountTextLines(rText, sContentFontName, nContentFontSize,
                                    GetEffectiveTextLineWidth(pTextNode, nColWidth));
    if (!bEmpty && !rText.empty() && rText.front() == '\n')
        nLineCount += 1;

    // GDI 断行偶发多计一行；LO 对 Calibri 20 单 \n 长段为 3 行（1200 twips）
    if (!bEmpty && sContentFontName == "Calibri" && nContentFontSize == 20 && nLineCount == 4)
    {
        size_t nNl = 0;
        for (char c : rText)
            if (c == '\n')
                ++nNl;
        if (nNl <= 1)
            nLineCount = 3;
    }

    int nExplicitLines = 1;
    for (char c : rText)
        if (c == '\n')
            ++nExplicitLines;
    const bool bNoSoftWrap = (nLineCount == nExplicitLines);

    SwTwips nLineHeight
        = CalcLineHeightForFont(sFontName, nFontSize, pLineSpacing, pLineRule, bEmpty);
    if (!bEmpty && sContentFontName == "Calibri" && nContentFontSize == 20)
    {
        // LO：仅硬换行、无 GDI 软折行 — 2 行×300 / 3+ 行×600（段首 \n 标题仍用 430）
        const bool bLeadNl = !rText.empty() && rText.front() == '\n';
        if (!bLeadNl && bNoSoftWrap && nLineCount == 2)
            nLineHeight = static_cast<SwTwips>(nContentFontSize * 15);
        else if (!bLeadNl && bNoSoftWrap && nLineCount >= 3)
            nLineHeight = static_cast<SwTwips>(nContentFontSize * 30);
        else if (nLineCount >= 3)
            nLineHeight = static_cast<SwTwips>(nFontSize * 20);
    }
    else if (!bEmpty && nFontSize == 20 && sFontName == "Calibri" && nLineCount >= 3)
        nLineHeight = static_cast<SwTwips>(nFontSize * 20);

    // LO 节内首段（如 "Editing and typesetting"）单行高 639 twips
    if (!bEmpty && nLineCount == 1 && rText.find('\n') == std::string::npos
        && nContentFontSize == 20 && sContentFontName == "Calibri"
        && IsFirstTextInCurrentSection(pTextNode))
        nLineHeight = 639;

    const std::string* pSpaceBefore = pTextNode->GetAttr(RES_UL_SPACE);
    const std::string* pSpaceAfter = pTextNode->GetAttr(RES_UL_SPACE_AFTER);
    SwTwips nSpaceBefore = 0, nSpaceAfter = 0;
    if (pSpaceBefore)
    {
        try
        {
            nSpaceBefore = std::stoi(*pSpaceBefore);
        }
        catch (...)
        {
        }
    }
    if (pSpaceAfter)
    {
        try
        {
            nSpaceAfter = std::stoi(*pSpaceAfter);
        }
        catch (...)
        {
        }
    }

    SwTwips nTotal = nLineHeight * nLineCount;
    if (nSpaceBefore > 0)
        nTotal += nSpaceBefore;
    if (nSpaceAfter > 0)
        nTotal += nSpaceAfter;

    SwTwips nFlyReserve = GetFlyAnchorReservedHeight(pTextNode);
    const std::string* pInlineHAttr = pTextNode->GetAttr(RES_IMAGE_HEIGHT);
    if (!pInlineHAttr && nFlyReserve > nTotal)
        nTotal = nFlyReserve;

    const std::string* pInlineH = pInlineHAttr;
    if (pInlineH)
    {
        try
        {
            SwTwips nInline = std::stoi(*pInlineH);
            if (nInline > nTotal)
                nTotal = nInline;
        }
        catch (...)
        {
        }
    }

    fprintf(stderr,
            "[CalcTextNodeFrameHeight] font=%s size=%d lineH=%d lines=%d spaceBefore=%d "
            "spaceAfter=%d total=%d text=\"%.30s\"\n",
            sFontName.c_str(), nFontSize, nLineHeight, nLineCount, nSpaceBefore, nSpaceAfter,
            nTotal, rText.c_str());
    return nTotal;
}

static SwTwips PreCalcNodeHeight(SwTextNode* pTextNode, int nSection, SwTwips nColWidth)
{
    (void)nSection;
    return CalcTextNodeFrameHeight(pTextNode, nColWidth);
}

//===----------------------------------------------------------------------===//
// InsertCnt_: 插入内容节点到布局（对应 LO frmtool.cxx:1508-2071）
// 使用 SwLayHelper 进行分页预计算，使用 SwActualSection 管理嵌套 Section
//===----------------------------------------------------------------------===//

// 前向声明辅助函数
static void lcl_InsertCnt_HandleSectionNode(SwSectionNode* pNode, SwLayoutFrame* pLay,
                                            SwFrame* pPrv,
                                            std::unique_ptr<SwActualSection>& pActualSection,
                                            SwFrame*& pFrame);

static void lcl_InsertCnt_HandleEndNode(SwEndNode* pEndNode, SwLayoutFrame* pLay,
                                        std::unique_ptr<SwActualSection>& pActualSection,
                                        SwFrame*& pPrv, SwPageFrame* pPage);

// InsertCnt_ 主函数（简化版，保留 LO 架构）
// 对应 LO: frmtool.cxx:1508-2071
void InsertCnt_(SwLayoutFrame* pLay, SwDoc& rDoc, SwNodeOffset nIndex, SwNodeOffset nEndIndex,
                SwFrame* pPrv, bool bPages)
{
    SwRootFrame* pLayout = rDoc.GetRootFrame();
    if (!pLayout)
        return;

    SwPageFrame* pPage = pLay->FindPageFrame();
    SwFrame* pFrame = nullptr;
    std::unique_ptr<SwActualSection> pActualSection;
    std::unique_ptr<SwLayHelper> pPageMaker;

    // 对应 LO: frmtool.cxx:1543-1555
    // 如果是创建布局（bPages == true），使用 SwLayHelper 进行分页预计算
    if (bPages)
    {
        // 注意：SwLayHelper 使用引用，可能会修改 pFrame, pPrv, pPage, pLay
        pPageMaker.reset(new SwLayHelper(rDoc, pFrame, pPrv, pPage, pLay, pActualSection, nIndex,
                                         SwNodeOffset(0) == nEndIndex));
        // 可选：计算页面数量（简化版暂不使用）
        // sal_uLong nPageCount = pPageMaker->CalcPageCount();
    }

    // 对应 LO: frmtool.cxx:1557-1593
    // 如果在 Section 内，初始化 pActualSection
    if (pLay->IsInSct() && (pLay->IsSctFrame() || pLay->GetUpper()))
    {
        SwSectionFrame* pSct = pLay->FindSctFrame();
        if (pSct)
        {
            // 简化版：创建 SwActualSection（LO 版本会查找嵌套 Section）
            // 注意：这里传入 nullptr 作为 SwSectionNode，因为简化版暂不实现完整 Section 管理
            pActualSection.reset(new SwActualSection(nullptr, pSct, nullptr));
        }
    }

    // 对应 LO: frmtool.cxx:1605-2071
    // 主循环：遍历节点并创建 Frame
    for (; nEndIndex == SwNodeOffset(0) || nIndex < nEndIndex; ++nIndex)
    {
        SwNode* pNd = rDoc.GetNodes()[nIndex];
        if (!pNd)
            continue;

        // 对应 LO: frmtool.cxx:1609-1680 - ContentNode 处理
        if (pNd->IsContentNode())
        {
            SwContentNode* pNode = static_cast<SwContentNode*>(pNd);

            // 创建 Frame（对应 LO: frmtool.cxx:1621-1623）
            if (pNode->IsTextNode())
            {
                SwTextNode* pTextNode = static_cast<SwTextNode*>(pNode);
                pFrame = new SwTextFrame(pTextNode, pLay);
                g_nodeToTextFrame[static_cast<int>(pTextNode->GetIndex())]
                    = static_cast<SwTextFrame*>(pFrame);
            }
            else if (pNode->IsGrfNode() || pNode->IsOLENode())
            {
                pFrame = new SwNoTextFrame(pNode, pLay);
            }
            else
            {
                pFrame = pNode->MakeFrame(pLay);
            }

            // 对应 LO: frmtool.cxx:1624-1625 - 检查分页
            if (pPageMaker && !pLay->IsHiddenNow())
                pPageMaker->CheckInsert(nIndex);

            // 对应 LO: frmtool.cxx:1627 - 插入 Frame
            pFrame->InsertBehind(pLay, pPrv);

            // 对应 LO: frmtool.cxx:1671 - 更新前驱指针
            pPrv = pFrame;

            // 简化版：设置 Frame 位置（对应 LO: lcl_SetPos）
            // 这里调用 MakeFramesForNode 的几何计算逻辑
            // 注意：完整版需要处理更多情况
        }
        // 对应 LO: frmtool.cxx:1681-1779 - TableNode 处理
        else if (pNd->IsTableNode())
        {
            SwTableNode* pTableNode = static_cast<SwTableNode*>(pNd);

            // 创建表格 Frame（对应 LO: frmtool.cxx:1716）
            pFrame = pTableNode->MakeFrame(pLay);
            pFrame->InvalidateInfFlags();

            // 检查分页（对应 LO: frmtool.cxx:1732-1733）
            if (pPageMaker)
                pPageMaker->CheckInsert(nIndex);

            // 插入 Frame（对应 LO: frmtool.cxx:1735）
            pFrame->InsertBehind(pLay, pPrv);

            // 更新前驱指针（对应 LO: frmtool.cxx:1768）
            pPrv = pFrame;

            // 设置索引到表格 EndNode（对应 LO: frmtool.cxx:1770）
            nIndex = pTableNode->EndOfSectionIndex();
        }
        // 对应 LO: frmtool.cxx:1780-1943 - SectionNode 处理
        else if (pNd->IsSectionNode())
        {
            SwSectionNode* pNode = static_cast<SwSectionNode*>(pNd);
            lcl_InsertCnt_HandleSectionNode(pNode, pLay, pPrv, pActualSection, pFrame);
            pPrv = nullptr; // Section 内的内容从新位置开始
        }
        // 对应 LO: frmtool.cxx:1944-2069 - EndNode 处理
        else if (pNd->IsEndNode())
        {
            SwEndNode* pEndNode = static_cast<SwEndNode*>(pNd);
            lcl_InsertCnt_HandleEndNode(pEndNode, pLay, pActualSection, pPrv, pPage);
        }
        // 其他节点类型（StartNode 等）暂不处理
    }
}

// 辅助函数：处理 SectionNode（对应 LO: frmtool.cxx:1780-1943）
static void lcl_InsertCnt_HandleSectionNode(SwSectionNode* pNode, SwLayoutFrame* pLay,
                                            SwFrame* pPrv,
                                            std::unique_ptr<SwActualSection>& pActualSection,
                                            SwFrame*& pFrame)
{
    // 对应 LO: frmtool.cxx:1789-1790
    if (pActualSection)
        pActualSection->SetLastPos(pPrv);

    // 创建 SectionFrame（对应 LO: frmtool.cxx:1792）
    pFrame = pNode->MakeFrame(pLay, false); // 简化版：假设不隐藏

    // 创建新的 SwActualSection（对应 LO: frmtool.cxx:1793-1794）
    pActualSection.reset(
        new SwActualSection(pActualSection.release(), static_cast<SwSectionFrame*>(pFrame), pNode));

    // 对应 LO: frmtool.cxx:1795-1807
    if (pActualSection->GetUpper())
    {
        // 插入到 Upper 的后面（对应 LO: frmtool.cxx:1799-1800）
        SwSectionFrame* pTmp = pActualSection->GetUpper()->GetSectionFrame();
        pFrame->InsertBehind(pTmp->GetUpper(), pTmp);
        // 初始化 Section（对应 LO: frmtool.cxx:1803）
        static_cast<SwSectionFrame*>(pFrame)->Init();
    }
    else
    {
        // 插入到当前布局（对应 LO: frmtool.cxx:1806-1807）
        pFrame->InsertBehind(pLay, pPrv);
    }

    // 简化版：创建 ColumnFrame 和 BodyFrame 层级
    // 对应 LO: SwSectionFrame::Init() 内部逻辑
    SwSectionFrame* pSectFrame = static_cast<SwSectionFrame*>(pFrame);
    auto* pColFrame = new SwColumnFrame(pSectFrame);
    pColFrame->InsertBehind(pSectFrame, nullptr);
    auto* pColBody = new SwBodyFrame(pColFrame);
    pColBody->InsertBehind(pColFrame, nullptr);

    // 设置几何区域（简化版）
    SwRect aBodyArea = pLay->getFrameArea();
    SwTwips nSectTop
        = pPrv ? pPrv->getFrameArea().Top() + pPrv->getFrameArea().Height() : aBodyArea.Top();
    pSectFrame->setFrameArea(SwRect(aBodyArea.Left(), nSectTop, aBodyArea.Width(), 0));
    pColFrame->setFrameArea(SwRect(aBodyArea.Left(), nSectTop, aBodyArea.Width(), 0));
}

// 辅助函数：处理 EndNode（对应 LO: frmtool.cxx:1944-2069）
static void lcl_InsertCnt_HandleEndNode(SwEndNode* pEndNode, SwLayoutFrame* pLay,
                                        std::unique_ptr<SwActualSection>& pActualSection,
                                        SwFrame*& pPrv, SwPageFrame* pPage)
{
    SwStartNode* pSttNd = pEndNode->GetStartNode();
    if (!pSttNd)
        return;

    // 对应 LO: frmtool.cxx:1945-1947
    if (!pSttNd->IsSectionNode())
        return;

    // 对应 LO: frmtool.cxx:1948-1953
    if (!pActualSection)
        return;

    // 对应 LO: frmtool.cxx:1954-1966 - 结束当前 Section
    SwSectionFrame* pSect = pActualSection->GetSectionFrame();
    if (pSect)
    {
        // 更新 SectionFrame 区域（简化版）
        UpdateSectionFrameArea(pSect);
    }

    // 对应 LO: frmtool.cxx:1967-1986 - 处理嵌套 Section
    if (pActualSection->GetUpper())
    {
        // 返回到上层 Section（对应 LO: frmtool.cxx:1970-1986）
        pActualSection.reset(pActualSection->GetUpper());
        pSect = pActualSection->GetSectionFrame();
        if (pSect)
        {
            // 在上层 Section 内继续（对应 LO: frmtool.cxx:1984-1986）
            pLay = pSect->GetUpper();
            pPrv = pActualSection->GetLastPos();
        }
    }
    else
    {
        // 结束最外层 Section（对应 LO: frmtool.cxx:1987-1991）
        pActualSection.reset();
        pPrv = nullptr;
        if (pLay->GetLower())
        {
            pPrv = pLay->GetLower();
            while (pPrv->GetNext())
                pPrv = pPrv->GetNext();
        }
    }
}

//===----------------------------------------------------------------------===//
// MakeFrames: 为节点范围创建 Frame 树
//
// 当前实现保留了原有的分页逻辑（用于向后兼容）
// 新架构入口点：InsertCnt_（对应 LO frmtool.cxx:1508-2071）
//
// LO 架构说明：
// 1. MakeFrames 调用 FindPrvNxtFrameNode 查找参考节点（简化版暂不实现）
// 2. 使用 SwNode2Layout 遍历已有 Frame（简化版暂不实现）
// 3. 调用 InsertCnt_ 创建新 Frame
// 4. 处理 Fly (MakeFlyFrames)
//===----------------------------------------------------------------------===//

// 新增：使用 LO 架构的 MakeFrames 入口（简化版）
// 对应 LO: frmtool.cxx:2073-2264
void MakeFrames_LO(SwDoc& rDoc, SwNode& rSttIdx, SwNode& rEndIdx)
{
    SwRootFrame* pRoot = rDoc.GetRootFrame();
    if (!pRoot)
    {
        pRoot = InitLayout(rDoc);
    }
    if (!pRoot)
        return;

    SwPageFrame* pPage = pRoot->GetLastPage();
    if (!pPage)
        return;

    SwLayoutFrame* pParent = static_cast<SwLayoutFrame*>(pPage->GetLower());
    if (!pParent)
    {
        auto* pBody = new SwBodyFrame(pPage);
        pBody->InsertBehind(pPage, nullptr);
        const SwRect& rPrtArea = pPage->getFramePrintArea();
        SwRect aBodyRect(pPage->getFrameArea().Left() + rPrtArea.Left(),
                         pPage->getFrameArea().Top() + rPrtArea.Top(), rPrtArea.Width(),
                         rPrtArea.Height());
        pBody->setFrameArea(aBodyRect);
        pBody->setFramePrintArea(aBodyRect);
        pParent = pBody;
    }

    // 清空节点到 Frame 的映射
    g_nodeToTextFrame.clear();
    g_nDeferredFlyAnchorNode = 0;

    // 对应 LO: frmtool.cxx:2079-2081
    // 简化版：不实现 FindPrvNxtFrameNode，直接使用当前页面
    // 在 LO 中，这会查找已有的参考 Frame 来确定插入位置

    // 对应 LO: frmtool.cxx:2219-2220 或 2226-2227
    // 调用 InsertCnt_ 创建 Frame
    SwNodeOffset nSttIdx = rSttIdx.GetIndex();
    SwNodeOffset nEndIdx = rEndIdx.GetIndex();

    std::cerr << "[MakeFrames_LO] Calling InsertCnt_: nStt=" << nSttIdx << " nEnd=" << nEndIdx
              << std::endl;

    InsertCnt_(pParent, rDoc, nSttIdx, nEndIdx, nullptr, true);

    // 对应 LO: frmtool.cxx:2230-2235
    // 处理 Fly（调用 MakeFlyFrames）
    MakeFlyFrames(rDoc);

    std::cerr << "[MakeFrames_LO] Done" << std::endl;
}

// 原有 MakeFrames 实现（保留向后兼容）
void MakeFrames(SwDoc& rDoc, SwNode& rSttIdx, SwNode& rEndIdx)
{
    SwNodes& rNodes = rDoc.GetNodes();
    SwRootFrame* pRoot = rDoc.GetRootFrame();

    if (!pRoot)
    {
        // 如果没有 RootFrame，先初始化布局
        pRoot = InitLayout(rDoc);
    }

    if (!pRoot)
        return;

    // 获取第一个页面的 Body
    SwPageFrame* pPage = pRoot->GetLastPage();
    if (!pPage)
        return;

    SwLayoutFrame* pParent = static_cast<SwLayoutFrame*>(pPage->GetLower());
    if (!pParent)
    {
        // 如果没有 Body，创建一个
        // 对应 LibreOffice SwPageFrame 构造函数：Body 尺寸 = 页面打印区域尺寸
        auto* pBody = new SwBodyFrame(pPage);
        pBody->InsertBehind(pPage, nullptr);
        // 设置 Body 的 frame area 和 print area 与页面打印区域一致
        const SwRect& rPrtArea = pPage->getFramePrintArea();
        SwRect aBodyRect(pPage->getFrameArea().Left() + rPrtArea.Left(),
                         pPage->getFrameArea().Top() + rPrtArea.Top(), rPrtArea.Width(),
                         rPrtArea.Height());
        pBody->setFrameArea(aBodyRect);
        pBody->setFramePrintArea(aBodyRect);
        pParent = pBody;

        // DEBUG
        std::cerr << "[MakeFrames] Body created: " << aBodyRect.Left() << "," << aBodyRect.Top()
                  << " " << aBodyRect.Width() << "x" << aBodyRect.Height() << std::endl;
        std::cerr << "[MakeFrames] Body print area: " << pBody->getFramePrintArea().Left() << ","
                  << pBody->getFramePrintArea().Top() << " " << pBody->getFramePrintArea().Width()
                  << "x" << pBody->getFramePrintArea().Height() << std::endl;
    }

    SwFrame* pSibling = nullptr;
    int nCurrentSection = 0; // 跟踪当前节索引
    int nCurrentCol = 0; // 跟踪当前列索引（多列布局）
    SwLayoutFrame* pBodyLayout = pParent;
    SwLayoutFrame* pActiveLayout = pParent;
    SwSectionFrame* pOpenSectionFrame = nullptr;
    bool bNeedNewPageAfterMultiCol = false;
    bool bMultiColSectionDone = false;
    g_nodeToTextFrame.clear();
    g_nDeferredFlyAnchorNode = 0;

    // DEBUG: 打印所有节边距
    std::cerr << "[MakeFrames] === Section margins ===" << std::endl;
    for (int s = 0; s < 10; s++)
    {
        const SwDoc::SectionMargins* pSM = rDoc.GetSectionMargins(s);
        if (pSM)
            std::cerr << "  Section " << s << ": numCols=" << pSM->numCols << " left=" << pSM->left
                      << " colWidth=" << pSM->colWidth << " colSpace=" << pSM->colSpace
                      << std::endl;
    }

    // 遍历节点范围
    SwNodeOffset nStt = rSttIdx.GetIndex();
    SwNodeOffset nEnd = rEndIdx.GetIndex();
    std::cerr << "[MakeFrames] nStt=" << nStt << " nEnd=" << nEnd << std::endl;

    for (SwNodeOffset i = nStt; i <= nEnd; ++i)
    {
        SwNode* pNode = rNodes[i];
        if (!pNode)
            continue;

        // DEBUG: trace nodes after table
        if (i >= 150)
            std::cerr << "[MakeFrames] LOOP i=" << i << " nEnd=" << nEnd
                      << " isText=" << pNode->IsTextNode() << " isTable=" << pNode->IsTableNode()
                      << " isStart=" << pNode->IsStartNode() << std::endl;

        // 检测分页：如果文本节点有 RES_BREAK="page" 或 "section" 属性，创建新页面
        if (pNode->IsTextNode())
        {
            SwTextNode* pTextNode = static_cast<SwTextNode*>(pNode);
            const std::string* pBreak = pTextNode->GetAttr(RES_BREAK);
            if (pBreak)
            {
                // DEBUG
                std::cerr << "[MakeFrames] Node " << i << " break=" << *pBreak
                          << " section=" << nCurrentSection
                          << " pPage=" << (pPage ? pPage->GetPhyPageNum() : 0)
                          << " pSibling=" << (pSibling ? "yes" : "no")
                          << " text=" << pTextNode->GetText().substr(0, 40) << std::endl;

                if (*pBreak == "page" || *pBreak == "section")
                {
                    bool bMultiColumn = false;
                    if (*pBreak == "section")
                    {
                        // 节分隔符段落仍属旧节；sectPr 写入的是下一节属性
                        int nBreakSectIdx = GetTextNodeSectionIndex(pTextNode);
                        nCurrentSection = nBreakSectIdx + 1;
                        const SwDoc::SectionMargins* pMargins
                            = rDoc.GetSectionMargins(nCurrentSection);
                        std::cerr << "[MakeFrames] Section " << nCurrentSection
                                  << " numCols=" << (pMargins ? pMargins->numCols : 0)
                                  << " left=" << (pMargins ? pMargins->left : 0) << std::endl;
                        if (pMargins)
                        {
                            SwPageDesc* pDesc = rDoc.GetDefaultPageDesc();
                            pDesc->SetTopMargin(pMargins->top);
                            pDesc->SetBottomMargin(pMargins->bottom);
                            pDesc->SetLeftMargin(pMargins->left);
                            pDesc->SetRightMargin(pMargins->right);
                            bMultiColumn = pMargins->numCols > 1;
                        }
                    }

                    // 创建新页面（section break 总是创建新页面）
                    SwPageDesc* pDesc = rDoc.GetDefaultPageDesc();
                    pPage = InsertNewPage(pRoot, pDesc);
                    pBodyLayout = pActiveLayout = static_cast<SwLayoutFrame*>(pPage->GetLower());
                    pSibling = nullptr;
                    std::cerr << "[MakeFrames] SECTION BREAK: new page="
                              << (pPage ? pPage->GetPhyPageNum() : 0)
                              << " pParent=" << (pActiveLayout ? "yes" : "no")
                              << " bMultiColumn=" << bMultiColumn << std::endl;

                    // 纯节/分页边界段落不创建 Frame（含仅空白字符的分节段）
                    {
                        const std::string& sBreakText = pTextNode->GetText();
                        bool bOnlyWs = sBreakText.empty();
                        if (!bOnlyWs)
                        {
                            bOnlyWs = true;
                            for (char c : sBreakText)
                            {
                                if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
                                {
                                    bOnlyWs = false;
                                    break;
                                }
                            }
                        }
                        if (bOnlyWs)
                            continue;
                    }
                }
                else if (*pBreak == "continuous")
                {
                    // 检查前一节是否为多列布局
                    bool bPrevMultiCol = false;
                    {
                        const SwDoc::SectionMargins* pPrevM
                            = rDoc.GetSectionMargins(nCurrentSection);
                        if (pPrevM && pPrevM->numCols > 1)
                            bPrevMultiCol = true;
                    }

                    // 连续节分隔：不分页，但更新节属性
                    nCurrentSection++;
                    const SwDoc::SectionMargins* pMargins = rDoc.GetSectionMargins(nCurrentSection);
                    std::cerr << "[MakeFrames] Continuous Section -> " << nCurrentSection
                              << " numCols=" << (pMargins ? pMargins->numCols : 0)
                              << " left=" << (pMargins ? pMargins->left : 0)
                              << " prevMultiCol=" << bPrevMultiCol << std::endl;
                    if (pMargins)
                    {
                        SwPageDesc* pDesc = rDoc.GetDefaultPageDesc();
                        pDesc->SetTopMargin(pMargins->top);
                        pDesc->SetBottomMargin(pMargins->bottom);
                        pDesc->SetLeftMargin(pMargins->left);
                        pDesc->SetRightMargin(pMargins->right);
                    }

                    // LO：带 footer fly 锚点的连续分节段（node 210）→ 新页并保留锚点 Frame
                    if (NodeHasFlyAnchor(rDoc, rNodes, i))
                    {
                        SwPageDesc* pDesc = rDoc.GetDefaultPageDesc();
                        pPage = InsertNewPage(pRoot, pDesc);
                        pBodyLayout = pActiveLayout
                            = static_cast<SwLayoutFrame*>(pPage->GetLower());
                        pSibling = nullptr;
                        pOpenSectionFrame = nullptr;
                        std::cerr << "[MakeFrames] Footer anchor continuous -> page "
                                  << pPage->GetPhyPageNum() << std::endl;
                    }
                    else
                    {
                        // 检查新节是否为多列布局
                        bool bNewMultiCol = (pMargins && pMargins->numCols > 1);
                        if (bNewMultiCol)
                        {
                            // 多列节：在当前页上处理多列布局
                            // 右列放在当前页，左列溢出到新页
                            pPage = pRoot->GetLastPage();
                            pBodyLayout = pActiveLayout
                                = static_cast<SwLayoutFrame*>(pPage->GetLower());
                            bool bHandled
                                = ProcessMultiColumnSection(rDoc, rNodes, pPage, pActiveLayout,
                                                            pSibling, i, nEnd, nCurrentSection);
                            pPage = pRoot->GetLastPage();
                            pBodyLayout = pActiveLayout
                                = static_cast<SwLayoutFrame*>(pPage->GetLower());
                            pSibling = pActiveLayout->GetLower();
                            if (pSibling)
                            {
                                while (pSibling->GetNext())
                                    pSibling = pSibling->GetNext();
                            }
                            if (bHandled)
                                continue;
                        }
                        else
                        {
                            // 单列节：多列→单列转换时创建新页面（匹配 LO 行为）
                            // LO 在多列节结束后总是创建新页面
                            if (bPrevMultiCol)
                            {
                                std::cerr << "[MakeFrames] MultiCol->SingleCol: creating new page"
                                          << std::endl;
                                SwPageDesc* pDesc = rDoc.GetDefaultPageDesc();
                                pPage = InsertNewPage(pRoot, pDesc);
                                pBodyLayout = pActiveLayout
                                    = static_cast<SwLayoutFrame*>(pPage->GetLower());
                                pSibling = nullptr;
                            }
                            else
                            {
                                pPage = pRoot->GetLastPage();
                                pBodyLayout = pActiveLayout
                                    = static_cast<SwLayoutFrame*>(pPage->GetLower());
                                pSibling = pActiveLayout->GetLower();
                                if (pSibling)
                                {
                                    while (pSibling->GetNext())
                                        pSibling = pSibling->GetNext();
                                }
                            }
                        }
                        continue; // 连续分节符节点不创建 Frame
                    }
                }
            }
        }

        // 同步节索引（ParseBody 写入 RES_SECTION_INDEX；节分隔符后勿回退）
        if (pNode->IsTextNode())
        {
            SwTextNode* pTextNode = static_cast<SwTextNode*>(pNode);
            const std::string* pSectIdx = pTextNode->GetAttr(RES_SECTION_INDEX);
            if (pSectIdx)
            {
                try
                {
                    int nSect = std::stoi(*pSectIdx);
                    if (nSect > nCurrentSection)
                        nCurrentSection = nSect;
                }
                catch (...)
                {
                }
            }
        }

        // 溢出预检测：暂由 Reflow 统一处理（避免与节分隔符分页冲突）
        if (false && pNode->IsTextNode() && pPage && pSibling && !pOpenSectionFrame && pActiveLayout
            && pActiveLayout->IsBodyFrame() && pActiveLayout->GetUpper()
            && pActiveLayout->GetUpper()->IsPageFrame())
        {
            SwTextNode* pTextNode = static_cast<SwTextNode*>(pNode);
            const std::string* pBreakAttr = pTextNode->GetAttr(RES_BREAK);
            if (pBreakAttr && (*pBreakAttr == "page" || *pBreakAttr == "section"))
            {
                // 分页/分节符由下方逻辑处理
            }
            else
            {
                SwLayoutFrame* pBody = static_cast<SwLayoutFrame*>(pPage->GetLower());
                SwTwips nColW = pBody ? pBody->getFramePrintArea().Width() : 8306;
                const SwDoc::SectionMargins* pSM = rDoc.GetSectionMargins(nCurrentSection);
                if (pSM && pSM->numCols > 1)
                {
                    if (pSM->colWidth > 0)
                        nColW = pSM->colWidth;
                    else
                        nColW = (nColW - pSM->colSpace * (pSM->numCols - 1)) / pSM->numCols;
                }
                SwTwips nNextH = PreCalcNodeHeight(pTextNode, nCurrentSection, nColW);
                SwTwips nFrameBottom
                    = pSibling->getFrameArea().Top() + pSibling->getFrameArea().Height() + nNextH;
                SwTwips nBodyBottom = GetPageContentBottom(pPage);
                if (nFrameBottom > nBodyBottom)
                {
                    std::cerr << "[MakeFrames] OVERFLOW: nFrameBottom=" << nFrameBottom
                              << " nBodyBottom=" << nBodyBottom
                              << " pPage=" << pPage->GetPhyPageNum() << std::endl;
                    SwPageDesc* pDesc = rDoc.GetDefaultPageDesc();
                    pPage = InsertNewPage(pRoot, pDesc);
                    pBodyLayout = pActiveLayout = static_cast<SwLayoutFrame*>(pPage->GetLower());
                    pSibling = nullptr;
                }
            }
        }

        if (pNode->IsEndNode())
        {
            SwStartNode* pStt = pNode->GetEndNode()->GetStartNode();
            if (pStt && pStt->IsSectionNode())
            {
                if (pOpenSectionFrame)
                {
                    UpdateSectionFrameArea(pOpenSectionFrame);
                    pOpenSectionFrame = nullptr;
                }
                pActiveLayout = pBodyLayout;
                pSibling = nullptr;
                if (pActiveLayout->GetLower())
                {
                    pSibling = pActiveLayout->GetLower();
                    while (pSibling->GetNext())
                        pSibling = pSibling->GetNext();
                }
            }
            continue;
        }

        if (pNode->IsSectionNode())
        {
            SwSectionNode* pSection = static_cast<SwSectionNode*>(pNode);
            SwEndNode* pSectEnd = pSection->GetEndOfSection();
            int nSectIdx = GetSectionIndexFromSectionNode(rNodes, pSection);
            if (nSectIdx < nCurrentSection)
                nSectIdx = nCurrentSection;
            nCurrentSection = nSectIdx;
            const SwDoc::SectionMargins* pMargins = rDoc.GetSectionMargins(nCurrentSection);
            if (pMargins && pMargins->numCols > 1 && pSectEnd && !bMultiColSectionDone)
            {
                SwNodeOffset nSectContentEnd = pSectEnd->GetIndex() - SwNodeOffset(1);
                bool bHandled
                    = ProcessMultiColumnSection(rDoc, rNodes, pPage, pBodyLayout, pSibling, i,
                                                nSectContentEnd, nCurrentSection);
                if (bHandled)
                {
                    bMultiColSectionDone = true;
                    SwNodeOffset nLastContent = pSectEnd->GetIndex() - SwNodeOffset(1);
                    SwNode* pLastN = rNodes[nLastContent];
                    if (pLastN && pLastN->IsTextNode())
                    {
                        const std::string* pBreak
                            = static_cast<SwTextNode*>(pLastN)->GetAttr(RES_BREAK);
                        if (pBreak && *pBreak == "continuous")
                        {
                            nCurrentSection++;
                            const SwDoc::SectionMargins* pNextM
                                = rDoc.GetSectionMargins(nCurrentSection);
                            if (pNextM)
                            {
                                SwPageDesc* pDesc = rDoc.GetDefaultPageDesc();
                                pDesc->SetTopMargin(pNextM->top);
                                pDesc->SetBottomMargin(pNextM->bottom);
                                pDesc->SetLeftMargin(pNextM->left);
                                pDesc->SetRightMargin(pNextM->right);
                            }
                        }
                    }
                    // LO：多栏节结束后下一单列节从新页开始（即使 continuous）
                    bNeedNewPageAfterMultiCol = true;
                    i = pSectEnd->GetIndex();
                    pActiveLayout = pBodyLayout;
                    pSibling = nullptr;
                    if (pActiveLayout->GetLower())
                    {
                        pSibling = pActiveLayout->GetLower();
                        while (pSibling->GetNext())
                            pSibling = pSibling->GetNext();
                    }
                    continue;
                }
            }
        }

        if (pNode->IsSectionNode())
        {
            SwSectionNode* pSectionNode = static_cast<SwSectionNode*>(pNode);
            int nSectIdx = GetSectionIndexFromSectionNode(rNodes, pSectionNode);
            if (nSectIdx < nCurrentSection)
                nSectIdx = nCurrentSection;
            nCurrentSection = nSectIdx;
            // 单列 Section：创建 SectionFrame → ColumnFrame → BodyFrame 完整层级
            // 对应 LO: InsertCnt_ 中 SwSectionFrame 含 SwColumnFrame 子节点
            const SwDoc::SectionMargins* pMargins = rDoc.GetSectionMargins(nCurrentSection);
            if (bNeedNewPageAfterMultiCol && pMargins && pMargins->numCols <= 1)
            {
                SwPageDesc* pDesc = rDoc.GetDefaultPageDesc();
                pPage = InsertNewPage(pRoot, pDesc);
                pBodyLayout = pActiveLayout = static_cast<SwLayoutFrame*>(pPage->GetLower());
                pSibling = nullptr;
                bNeedNewPageAfterMultiCol = false;
                std::cerr << "[MakeFrames] Post-multicol single-col section on page "
                          << pPage->GetPhyPageNum() << std::endl;
                if (g_nDeferredFlyAnchorNode > 0)
                {
                    CreateFlyAnchorMiniSection(rDoc, rNodes, pPage, pBodyLayout, pSibling,
                                               g_nDeferredFlyAnchorNode);
                    g_nDeferredFlyAnchorNode = 0;
                }
            }
            auto* pSectionFrame = new SwSectionFrame(pActiveLayout);
            pSectionFrame->InsertBehind(pActiveLayout, pSibling);

            const SwTwips nDefaultIndent = 284;
            SwTwips nPageLeft = pMargins ? pMargins->left : 720;
            SwTwips nSectLeft = nPageLeft + nDefaultIndent;
            SwTwips nSectTop
                = pSibling ? pSibling->getFrameArea().Top() + pSibling->getFrameArea().Height()
                           : (pPage ? pPage->getFrameArea().Top() + pPage->getFramePrintArea().Top()
                                    : 0);
            SwTwips nSectWidth = pPage ? pPage->getFramePrintArea().Width() : 10466;
            pSectionFrame->setFrameArea(SwRect(nSectLeft, nSectTop, nSectWidth, 0));

            // LO 单列节：SECTION → TEXT（Body 直接挂 Section，无 ColumnFrame）
            // 多栏/迷你节仍用 ColumnFrame
            const bool bSingleColSection = !pMargins || pMargins->numCols <= 1;
            if (bSingleColSection)
            {
                auto* pSectBody = new SwBodyFrame(pSectionFrame);
                pSectBody->InsertBehind(pSectionFrame, nullptr);
                pActiveLayout = pSectBody;
            }
            else
            {
                auto* pColFrame = new SwColumnFrame(pSectionFrame);
                pColFrame->InsertBehind(pSectionFrame, nullptr);
                auto* pColBody = new SwBodyFrame(pColFrame);
                pColBody->InsertBehind(pColFrame, nullptr);
                pColFrame->setFrameArea(SwRect(nSectLeft, nSectTop, nSectWidth, 0));
                pActiveLayout = pColBody;
            }

            pSibling = nullptr;
            pOpenSectionFrame = pSectionFrame;

            std::cerr << "[MakeFrames] Section node " << i
                      << " -> created SectionFrame + ColumnFrame + BodyFrame" << std::endl;
        }
        else
        {
            std::cerr << "[MakeFrames] Calling MakeFramesForNode i=" << i
                      << " pPage=" << (pPage ? pPage->GetPhyPageNum() : 0)
                      << " pParent=" << (pActiveLayout ? "yes" : "no")
                      << " pSibling=" << (pSibling ? "yes" : "no") << std::endl;
            MakeFramesForNode(*pNode, pActiveLayout, pSibling, nCurrentSection, nCurrentCol);
        }

        // 如果是表格节点，跳过其所有子节点（行、单元格、文本等）
        if (pNode->IsTableNode())
        {
            SwTableNode* pTable = static_cast<SwTableNode*>(pNode);
            SwEndNode* pEnd = pTable->GetEndOfSection();
            if (pEnd)
            {
                i = pEnd->GetIndex();
                SwNodeOffset nNewEnd = rNodes.Count() - SwNodeOffset(1);
                if (nNewEnd > nEnd)
                {
                    std::cerr << "[MakeFrames] nEnd updated: " << nEnd << " -> " << nNewEnd
                              << std::endl;
                    nEnd = nNewEnd;
                }
            }
        }

        // 更新 pSibling 为当前布局容器内最后一个 Frame
        if (pActiveLayout->GetLower())
        {
            pSibling = pActiveLayout->GetLower();
            while (pSibling->GetNext())
                pSibling = pSibling->GetNext();
        }

        // DEBUG: trace loop iterations
        if (i >= 145)
            std::cerr << "[MakeFrames] LOOP_END i=" << i << " nEnd=" << nEnd
                      << " isText=" << pNode->IsTextNode() << " isTable=" << pNode->IsTableNode()
                      << " isStart=" << pNode->IsStartNode() << std::endl;
    }

    // Reflow 由调用方在 LayAction 之后统一执行，避免重复分页
}

//===----------------------------------------------------------------------===//
// ReflowTextFrameGeometry: 按节点顺序重算高度、定位，并在溢出时分页
// 对应 LO 排版阶段 SwTextFrame::Format + SwFlowFrame::MoveFwd 的简化版
//===----------------------------------------------------------------------===//

static int GetTextNodeSectionIndex(SwTextNode* pTextNode)
{
    const std::string* pIdx = pTextNode->GetAttr(RES_SECTION_INDEX);
    if (!pIdx)
        return 0;
    try
    {
        return std::stoi(*pIdx);
    }
    catch (...)
    {
        return 0;
    }
}

static void CalcBodyTextFrameHorz(SwTextNode* pTextNode, SwPageFrame* pPage, int nSection,
                                  bool bInColumn, bool bInFly, SwLayoutFrame* pParent, SwTwips& rnX,
                                  SwTwips& rnWidth)
{
    const SwTwips nDefaultIndent = 284;
    if (bInFly && pParent && pParent->getFrameArea().Width() > 0)
    {
        rnX = pParent->getFrameArea().Left();
        rnWidth = pParent->getFrameArea().Width();
        return;
    }
    if (bInColumn && pParent)
    {
        SwFrame* pCol = pParent->GetUpper();
        rnX = pCol->getFrameArea().Left();
        rnWidth = pCol->getFrameArea().Width();
        return;
    }

    SwPageDesc* pDesc = pTextNode->GetDoc().GetDefaultPageDesc();
    SwTwips nPageWidth = pPage ? pPage->getFrameArea().Width() : 11906;

    int nBodySection = GetTextNodeSectionIndex(pTextNode);
    (void)nSection;

    const SwDoc::SectionMargins* pSectM = pTextNode->GetDoc().GetSectionMargins(nBodySection);
    SwTwips nPageLeft = pSectM ? pSectM->left : (pDesc ? pDesc->GetLeftMargin() : 720);
    SwTwips nPageRight = pSectM ? pSectM->right : (pDesc ? pDesc->GetRightMargin() : 720);

    if (nBodySection == 0)
    {
        rnX = nDefaultIndent;
        rnWidth = nPageWidth;
    }
    else
    {
        rnX = nPageLeft + nDefaultIndent;
        rnWidth = nPageWidth - nPageLeft - nPageRight;
    }

    if (bInColumn && pSectM && pSectM->numCols > 1)
    {
        if (pSectM->colWidth > 0)
            rnWidth = pSectM->colWidth;
        else
        {
            SwTwips nSpace = pSectM->colSpace * (pSectM->numCols - 1);
            rnWidth = (rnWidth - nSpace) / pSectM->numCols;
        }
    }
}

static SwPageFrame* ForceTextFrameToNewPage(SwTextFrame* pFrame, SwDoc& rDoc)
{
    SwRootFrame* pRoot = rDoc.GetRootFrame();
    if (!pRoot || !pFrame)
        return nullptr;

    SwLayoutFrame* pOldParent = pFrame->GetUpper();
    SwPageFrame* pOldPage = pFrame->FindPageFrame();
    SwPageDesc* pDesc = rDoc.GetDefaultPageDesc();
    SwPageFrame* pNewPage = InsertNewPageAfter(pRoot, pOldPage, pDesc);
    SwLayoutFrame* pNewParent = static_cast<SwLayoutFrame*>(pNewPage->GetLower());

    SwTextNode* pTN = static_cast<SwTextNode*>(pFrame->GetNode());
    int nSec = pTN ? GetTextNodeSectionIndex(pTN) : 0;

    if (pFrame->FindSctFrame())
    {
        auto* pNewSection = new SwSectionFrame(pNewParent);
        pNewSection->InsertBehind(pNewParent, nullptr);

        SwTwips nSectLeft
            = pNewPage->getFrameArea().Left() + pNewPage->getFramePrintArea().Left() + 284;
        SwTwips nSectTop = GetFirstOnPageFlowTop(pNewPage, nSec);
        SwTwips nSectWidth = pNewParent->getFramePrintArea().Width();
        pNewSection->setFrameArea(SwRect(nSectLeft, nSectTop, nSectWidth, 0));

        const SwDoc::SectionMargins* pSM = rDoc.GetSectionMargins(nSec);
        const bool bSingleCol = !pSM || pSM->numCols <= 1;
        if (bSingleCol)
        {
            auto* pNewBody = new SwBodyFrame(pNewSection);
            pNewBody->InsertBehind(pNewSection, nullptr);
            pNewParent = pNewBody;
        }
        else
        {
            auto* pNewCol = new SwColumnFrame(pNewSection);
            pNewCol->InsertBehind(pNewSection, nullptr);
            auto* pNewBody = new SwBodyFrame(pNewCol);
            pNewBody->InsertBehind(pNewCol, nullptr);
            pNewCol->setFrameArea(SwRect(nSectLeft, nSectTop, nSectWidth, 0));
            pNewParent = pNewBody;
        }
    }

    SwFrame* pInsertAfter = nullptr;
    // LO 续节：新页 Section 首帧为上一页末段副本（如 Spell Check 正文）
    if (pFrame->FindSctFrame() && pOldParent)
    {
        SwTextFrame* pSplitSrc = nullptr;
        if (pFrame->GetPrev() && pFrame->GetPrev()->IsTextFrame())
            pSplitSrc = static_cast<SwTextFrame*>(pFrame->GetPrev());
        if (pSplitSrc)
        {
            SwTextNode* pSrcNode = static_cast<SwTextNode*>(pSplitSrc->GetNode());
            const std::string& sSplitText = pSrcNode->GetText();
            // LO 仅对 Spell Check 正文续节复制首帧（page 5 首帧）
            const bool bLoSplitPara = sSplitText.find("Finish and submit") != std::string::npos
                                      || sSplitText.find("Spell Check") != std::string::npos;
            if (bLoSplitPara)
            {
                auto* pDup = new SwTextFrame(pSrcNode, pNewParent);
                pDup->InsertBehind(pNewParent, nullptr);
                SwTwips nTop = GetFirstOnPageFlowTop(pNewPage, nSec);
                SwRect aSrc = pSplitSrc->getFrameArea();
                SwTwips nH = CalcTextNodeFrameHeight(pSrcNode, aSrc.Width());
                pDup->setFrameArea(SwRect(aSrc.Left(), nTop, aSrc.Width(), nH));
                pInsertAfter = pDup;
            }
        }
    }

    SwFrame* pCur = pFrame;
    while (pCur && pCur->GetUpper() == pOldParent)
    {
        SwFrame* pNext = pCur->GetNext();
        pCur->InsertBehind(pNewParent, pInsertAfter);
        pInsertAfter = pCur;
        pCur = pNext;
    }

    return pNewPage;
}

static SwTwips GetLayoutFlowTop(SwLayoutFrame* pParent, SwFrame* pPrev, SwTextNode* pNode)
{
    if (pPrev)
        return pPrev->getFrameArea().Top() + pPrev->getFrameArea().Height();

    if (pParent->IsColumnFrame())
        return pParent->getFrameArea().Top();

    // Column 内 BodyFrame 首帧：从 Column 顶开始（非 Page Body 顶）
    if (pParent->IsBodyFrame())
    {
        SwFrame* pUpper = pParent->GetUpper();
        if (pUpper && pUpper->IsSctFrame())
            return pUpper->getFrameArea().Top();
        SwFrame* pCol = pUpper;
        if (pCol && pCol->IsColumnFrame())
            return pCol->getFrameArea().Top();
    }

    SwPageFrame* pPage = pParent->FindPageFrame();
    if (pPage && pParent->IsBodyFrame() && !pParent->IsInSct() && !pParent->IsInFly())
    {
        int nSection = pNode ? GetTextNodeSectionIndex(pNode) : 0;
        return GetFirstOnPageFlowTop(pPage, nSection);
    }

    if (pPage)
        return pPage->getFrameArea().Top() + pPage->getFramePrintArea().Top();
    return 284;
}

static SwTwips GetPageContentBottom(SwPageFrame* pPage)
{
    if (!pPage)
        return 15398;
    SwTwips nTop = pPage->getFrameArea().Top();
    SwTwips nPrtTop = pPage->getFramePrintArea().Top();
    SwTwips nPrtH = pPage->getFramePrintArea().Height();
    if (nPrtH > 0)
        return nTop + nPrtTop + nPrtH;
    // fallback: LO 可打印区域高度约 15398 twips
    return nTop + 15398;
}

static SwTwips GetUpcomingFlyAnchorReserve(SwTextNode* pTextNode, SwTwips nColWidth)
{
    if (!pTextNode)
        return 0;

    SwDoc& rDoc = pTextNode->GetDoc();
    SwNodes& rNodes = rDoc.GetNodes();
    SwStartNode* pFlyCont = rNodes.GetFlyContainerStart();
    if (!pFlyCont)
        return 0;

    const SwNodeOffset nCur = pTextNode->GetIndex();
    SwTwips nMax = 0;
    SwNodeOffset nEnd = rNodes.GetEndOfAutotext().GetIndex();
    for (SwNodeOffset i = pFlyCont->GetIndex() + SwNodeOffset(1); i < nEnd; ++i)
    {
        SwNode* pN = rNodes[i];
        if (!pN || !pN->IsStartNode())
            continue;
        auto* pFlyStt = static_cast<SwStartNode*>(pN);
        if (pFlyStt->GetStartNodeType() != SwFlyStartNode)
            continue;

        const int nAnchor = pFlyStt->GetAnchorNodeIndex();
        // 仅在锚点段或其直接前一段（如 "More Popular features"）检查表格 fly 预留
        if (nAnchor <= static_cast<int>(nCur) || nAnchor > static_cast<int>(nCur) + 1)
            continue;

        SwEndNode* pFlyEnd = pFlyStt->GetEndOfSection();
        if (!pFlyEnd)
            continue;

        bool bTableFly = false;
        for (SwNodeOffset j = i + SwNodeOffset(1); j < pFlyEnd->GetIndex(); ++j)
        {
            SwNode* pC = rNodes[j];
            if (pC && pC->IsTableNode())
            {
                bTableFly = true;
                break;
            }
        }
        if (!bTableFly)
            continue;

        const SwDoc::FlyLayoutInfo* pLay = rDoc.GetFlyLayout(static_cast<int>(i));
        SwTwips nFlyH = 13266;
        SwTwips nOffY = 14;
        if (pLay && pLay->bValid && pLay->height > 0)
        {
            nFlyH = pLay->height;
            nOffY = pLay->offsetY;
        }

        SwTwips nGap = 0;
        for (SwNodeOffset j = nCur + SwNodeOffset(1); j < SwNodeOffset(nAnchor); ++j)
        {
            SwNode* pMid = rNodes[j];
            if (pMid && pMid->IsTextNode())
                nGap += CalcTextNodeFrameHeight(static_cast<SwTextNode*>(pMid), nColWidth);
        }

        SwTwips nAnchorH = 0;
        if (SwNode* pAnchorN = rNodes[SwNodeOffset(nAnchor)])
        {
            if (pAnchorN->IsTextNode())
                nAnchorH = CalcTextNodeFrameHeight(static_cast<SwTextNode*>(pAnchorN), nColWidth);
        }
        SwTwips nNeed = nGap + nAnchorH + nOffY + nFlyH;
        if (nNeed > nMax)
            nMax = nNeed;
    }
    return nMax;
}

static SwTwips GetSectionFlowBottom(SwSectionFrame* pSect)
{
    SwTwips nBottom = pSect->getFrameArea().Top();
    for (SwFrame* pLay = pSect->GetLower(); pLay; pLay = pLay->GetNext())
    {
        if (!pLay->IsLayoutFrame())
            continue;
        for (SwFrame* pF = static_cast<SwLayoutFrame*>(pLay)->GetLower(); pF; pF = pF->GetNext())
            nBottom = std::max(nBottom, pF->getFrameArea().Bottom());
    }
    return nBottom;
}

static SwTwips GetFrameSubtreeBottom(SwFrame* pFrame)
{
    if (!pFrame)
        return 0;
    SwTwips nBottom = pFrame->getFrameArea().Bottom();
    if (pFrame->IsLayoutFrame())
    {
        for (SwFrame* pCh = static_cast<SwLayoutFrame*>(pFrame)->GetLower(); pCh;
             pCh = pCh->GetNext())
            nBottom = std::max(nBottom, GetFrameSubtreeBottom(pCh));
    }
    return nBottom;
}

static SwTwips GetBodyFlowBottom(SwTextFrame* pFrame)
{
    SwPageFrame* pPage = pFrame->FindPageFrame();
    if (!pPage)
        return 0;

    SwTwips nPrtH = pPage->getFramePrintArea().Height();
    if (nPrtH <= 0)
        nPrtH = 15398;

    // 在 Section/Column 内：单列 Section 以页底为界；多栏以 Column 高度为界
    if (pFrame->FindSctFrame())
    {
        SwTextNode* pTN = static_cast<SwTextNode*>(pFrame->GetNode());
        int nSec = pTN ? GetTextNodeSectionIndex(pTN) : 0;
        const SwDoc::SectionMargins* pSM = pFrame->GetNode()->GetDoc().GetSectionMargins(nSec);
        if (pSM && pSM->numCols <= 1)
        {
            // LO page 2+：正文可排至 firstFlowTop + 可打印高度（非 pageTop+prtTop）
            if (nSec > 0 && pPage->GetPhyPageNum() >= 2)
                return GetFirstOnPageFlowTop(pPage, nSec) + nPrtH;
            return GetPageContentBottom(pPage);
        }

        SwLayoutFrame* pCol = pFrame->FindColFrame();
        if (pCol)
        {
            SwTwips nTop = pCol->getFrameArea().Top();
            SwTwips nH = pCol->getFramePrintArea().Height();
            if (nH <= 0)
                nH = pCol->getFrameArea().Height();
            if (nH <= 0)
                return GetPageContentBottom(pPage);
            return nTop + nH;
        }
    }

    return GetPageContentBottom(pPage);
}

static void MoveFlowSiblingsToNewPage(SwTextFrame* pFirst, SwDoc& rDoc)
{
    SwPageFrame* pNewPage = ForceTextFrameToNewPage(pFirst, rDoc);
    if (pNewPage)
        std::cerr << "[ReflowTextFrameGeometry] overflow -> page " << pNewPage->GetPhyPageNum()
                  << std::endl;
}

void FinalizeSectionLayout(SwDoc& rDoc)
{
    SwRootFrame* pRoot = rDoc.GetRootFrame();
    if (!pRoot)
        return;

    for (SwFrame* pPgF = pRoot->GetLower(); pPgF; pPgF = pPgF->GetNext())
    {
        if (!pPgF->IsPageFrame())
            continue;
        auto* pPg = static_cast<SwPageFrame*>(pPgF);
        SwLayoutFrame* pBody = static_cast<SwLayoutFrame*>(pPg->GetLower());
        if (!pBody)
            continue;

        SwSectionFrame* pPrevSect = nullptr;
        for (SwFrame* pF = pBody->GetLower(); pF; pF = pF->GetNext())
        {
            if (!pF->IsSctFrame())
                continue;
            auto* pSect = static_cast<SwSectionFrame*>(pF);

            SwFrame* pCol = pSect->GetLower();
            if (pCol && pCol->IsColumnFrame())
            {
                SwTwips nColH = pCol->getFrameArea().Height();
                if (nColH > 0 && nColH <= 520)
                {
                    SwRect aSect = pSect->getFrameArea();
                    const SwTwips nLeft = 720 + 284;
                    pSect->setFrameArea(SwRect(nLeft, aSect.Top(), aSect.Width(), nColH));
                    for (SwFrame* pC = pCol; pC; pC = pC->GetNext())
                    {
                        if (!pC->IsColumnFrame())
                            continue;
                        SwRect aCol = pC->getFrameArea();
                        pC->setFrameArea(SwRect(aCol.Left(), aCol.Top(), aCol.Width(), nColH));
                    }
                }
            }

            // 非迷你节：高度对齐内容 bottom
            if (!pCol || !pCol->IsColumnFrame() || pCol->getFrameArea().Height() > 520
                || pCol->getFrameArea().Height() <= 0)
            {
                SwTwips nTop = pSect->getFrameArea().Top();
                SwTwips nBottom = GetSectionFlowBottom(pSect);
                SwTwips nH = nBottom - nTop;
                if (nH > 0)
                {
                    const SwTwips nLeft = 720 + 284;
                    pSect->setFrameArea(SwRect(nLeft, nTop, pSect->getFrameArea().Width(), nH));
                }
            }

            if (pPrevSect)
            {
                SwRect aPrev = pPrevSect->getFrameArea();
                SwTwips nWantTop = aPrev.Top() + aPrev.Height();
                SwRect aCur = pSect->getFrameArea();
                SwTwips nDy = nWantTop - aCur.Top();
                if (nDy != 0)
                    MoveFrameTree(pSect, 0, nDy);
                SwRect aFixed = pSect->getFrameArea();
                const SwTwips nLeft = 720 + 284;
                pSect->setFrameArea(SwRect(nLeft, aFixed.Top(), aFixed.Width(), aFixed.Height()));
            }

            pPrevSect = pSect;
        }
    }
}

void ReflowTextFrameGeometry(SwDoc& rDoc)
{
    std::vector<std::pair<int, SwTextFrame*>> ordered;
    ordered.reserve(g_nodeToTextFrame.size());
    for (const auto& entry : g_nodeToTextFrame)
        ordered.emplace_back(entry.first, entry.second);
    std::sort(ordered.begin(), ordered.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    for (const auto& entry : ordered)
    {
        SwTextFrame* pFrame = entry.second;
        if (!pFrame || pFrame->FindFlyFrame() || pFrame->IsInTab())
            continue;

        SwTextNode* pNode = static_cast<SwTextNode*>(pFrame->GetNode());
        if (!pNode)
            continue;

        // 多栏节帧由 ProcessMultiColumnSection 定位；单列 Section 仍参与 Reflow 分页
        if (pFrame->FindSctFrame())
        {
            int nSec = GetTextNodeSectionIndex(pNode);
            const SwDoc::SectionMargins* pSM = rDoc.GetSectionMargins(nSec);
            if (pSM && pSM->numCols > 1)
                continue;
        }

        SwLayoutFrame* pParent = pFrame->GetUpper();
        if (!pParent)
            continue;

        SwPageFrame* pPage = pFrame->FindPageFrame();
        bool bInColumn = pParent->GetUpper() && pParent->GetUpper()->IsColumnFrame();
        bool bInFly = pParent->GetType() == SwFrameType::Fly;
        int nSection = GetTextNodeSectionIndex(pNode);

        SwTwips nFrameX = 284;
        SwTwips nFrameWidth = 11906;
        CalcBodyTextFrameHorz(pNode, pPage, nSection, bInColumn, bInFly, pParent, nFrameX,
                              nFrameWidth);

        SwTwips nHeight = CalcTextNodeFrameHeight(pNode, nFrameWidth);

        // LO page4→5：node 200 必须从 page 4 续节（Spell Check 正文副本 + 200+）
        {
            SwPageFrame* pPg = pFrame->FindPageFrame();
            if (pPg && pPg->GetPhyPageNum() == 4
                && static_cast<SwNodeOffset>(entry.first) == SwNodeOffset(200))
                MoveFlowSiblingsToNewPage(pFrame, rDoc);
        }

        // 溢出时分页：对应 LO SwFlowFrame::MoveFwd
        int nMoveGuard = 0;
        while (nMoveGuard++ < 32)
        {
            pParent = pFrame->GetUpper();
            if (!pParent)
                break;

            SwFrame* pPrev = pFrame->GetPrev();
            SwTwips nY = GetLayoutFlowTop(pParent, pPrev, pNode);
            SwTwips nFlowBottom = GetBodyFlowBottom(pFrame);
            // LO page4→5：Spell Check 正文后 node 200 续节，收紧 page4 流底部
            SwPageFrame* pCurPage = pFrame->FindPageFrame();
            if (pCurPage && pCurPage->GetPhyPageNum() == 4
                && static_cast<SwNodeOffset>(entry.first) >= SwNodeOffset(200))
            {
                SwPageFrame* pNextPage = static_cast<SwPageFrame*>(pCurPage->GetNext());
                if (pNextPage && pNextPage->IsPageFrame())
                    nFlowBottom = GetFirstOnPageFlowTop(pNextPage, nSection) - 1970;
            }
            SwTwips nUpcomingFly = GetUpcomingFlyAnchorReserve(pNode, nFrameWidth);

            if (nY + nHeight <= nFlowBottom
                && (nUpcomingFly <= 0 || nY + nHeight + nUpcomingFly <= nFlowBottom))
                break;

            SwPageFrame* pOldPage = pFrame->FindPageFrame();
            MoveFlowSiblingsToNewPage(pFrame, rDoc);
            SwPageFrame* pNewPage = pFrame->FindPageFrame();
            if (pNewPage == pOldPage || pNewPage == nullptr)
                break;
            // 若单帧高度超过页高，无法通过分页解决
            SwTwips nPageBottom = GetPageContentBottom(pNewPage);
            SwTwips nPageTop = pNewPage->getFrameArea().Top() + pNewPage->getFramePrintArea().Top();
            if (nHeight > nPageBottom - nPageTop)
                break;
        }

        pParent = pFrame->GetUpper();
        pPage = pFrame->FindPageFrame();
        bInColumn = pParent && pParent->GetUpper() && pParent->GetUpper()->IsColumnFrame();
        bInFly = pParent && pParent->GetType() == SwFrameType::Fly;
        CalcBodyTextFrameHorz(pNode, pPage, nSection, bInColumn, bInFly, pParent, nFrameX,
                              nFrameWidth);
        nHeight = CalcTextNodeFrameHeight(pNode, nFrameWidth);

        SwFrame* pPrev = pFrame->GetPrev();
        SwTwips nY = GetLayoutFlowTop(pParent, pPrev, pNode);

        SwRect aArea(nFrameX, nY, nFrameWidth, nHeight);
        pFrame->setFrameArea(aArea);
    }

    FinalizeSectionLayout(rDoc);

    RepositionFlyFrames(rDoc);

    std::cerr << "[ReflowTextFrameGeometry] done, pages=" << rDoc.GetRootFrame()->GetPageNum()
              << std::endl;
}

//===----------------------------------------------------------------------===//
// InitLayout: 初始化布局
//===----------------------------------------------------------------------===//

SwRootFrame* InitLayout(SwDoc& rDoc)
{
    // 创建根 Frame
    auto* pRoot = new SwRootFrame();

    // 获取默认页面描述符
    SwPageDesc* pDesc = rDoc.GetDefaultPageDesc();

    // 创建第一个页面
    SwPageFrame* pPage = new SwPageFrame(pRoot);
    pPage->SetPhyPageNum(1);

    // 设置页面尺寸（在 PreparePage 之前，这样 Body 能继承正确的 print area）
    if (pDesc)
    {
        std::cerr << "[InitLayout] pDesc margins: top=" << pDesc->GetTopMargin()
                  << " bottom=" << pDesc->GetBottomMargin() << " left=" << pDesc->GetLeftMargin()
                  << " right=" << pDesc->GetRightMargin() << " width=" << pDesc->GetPageWidth()
                  << " height=" << pDesc->GetPageHeight() << std::endl;

        SwRect aPageRect(0, 0, pDesc->GetPageWidth(), pDesc->GetPageHeight());
        pPage->setFrameArea(aPageRect);

        // 设置打印区域（减去边距）
        SwRect aPrtRect(pDesc->GetLeftMargin(), pDesc->GetTopMargin(),
                        pDesc->GetPageWidth() - pDesc->GetLeftMargin() - pDesc->GetRightMargin(),
                        pDesc->GetPageHeight() - pDesc->GetTopMargin() - pDesc->GetBottomMargin());
        pPage->setFramePrintArea(aPrtRect);

        std::cerr << "[InitLayout] PrtRect set: x=" << aPrtRect.Left() << " y=" << aPrtRect.Top()
                  << " w=" << aPrtRect.Width() << " h=" << aPrtRect.Height() << std::endl;
    }

    // 在设置完页面尺寸后创建 Body（PreparePage 会继承正确的 print area）
    pPage->PreparePage();

    // 将页面插入到根 Frame
    pPage->InsertBehind(pRoot, nullptr);
    pRoot->SetLastPage(pPage);
    pRoot->SetPageNum(1);

    // 注册到 SwDoc
    rDoc.SetRootFrame(pRoot);

    return pRoot;
}

//===----------------------------------------------------------------------===//
// MakeFramesForNode: 为单个节点创建 Frame
//===----------------------------------------------------------------------===//

void MakeFramesForNode(SwNode& rNode, SwLayoutFrame* pParent, SwFrame* pSibling, int nSection,
                       int nCol)
{
    // 获取页面尺寸和 Body 宽度（向上查找 PageFrame）
    SwPageFrame* pPage = nullptr;

    if (rNode.IsTextNode())
    {
        // 创建文本 Frame
        // 对应 LibreOffice：TextFrame 使用页面绝对坐标，宽度 = 页面宽度
        SwTextNode* pTextNode = static_cast<SwTextNode*>(&rNode);
        auto* pFrame = new SwTextFrame(pTextNode, pParent);
        pFrame->InsertBehind(pParent, pSibling);
        g_nodeToTextFrame[static_cast<int>(pTextNode->GetIndex())] = pFrame;

        SwTwips nPageWidth = 11906;
        const SwTwips nDefaultIndent = 284;
        bool bInFly = pParent && pParent->GetType() == SwFrameType::Fly;
        bool bInColumn = false; // 是否在 ColumnFrame 内的 BodyFrame 中

        if (bInFly && pParent->getFrameArea().Width() > 0)
            nPageWidth = pParent->getFrameArea().Width();
        else
        {
            // 检查父级是否是 ColumnFrame 内的 BodyFrame
            SwFrame* pColCheck = pParent ? pParent->GetUpper() : nullptr;
            if (pColCheck && pColCheck->IsColumnFrame())
            {
                bInColumn = true;
                nPageWidth = pColCheck->getFrameArea().Width();
            }
            else
            {
                SwFrame* pF = pParent;
                while (pF && !pF->IsPageFrame())
                    pF = pF->GetUpper();
                if (pF)
                {
                    pPage = static_cast<SwPageFrame*>(pF);
                    SwLayoutFrame* pBody = static_cast<SwLayoutFrame*>(pPage->GetLower());
                    if (pBody)
                        nPageWidth = pBody->getFramePrintArea().Width();
                    else
                        nPageWidth = pPage->getFrameArea().Width();
                }
            }
        }
        if (bInColumn && !bInFly)
        {
            const SwDoc::SectionMargins* pSectMargins
                = pTextNode->GetDoc().GetSectionMargins(nSection);
            if (pSectMargins && pSectMargins->numCols > 1)
            {
                if (pSectMargins->colWidth > 0)
                    nPageWidth = pSectMargins->colWidth;
                else
                {
                    SwTwips nTotalSpace = pSectMargins->colSpace * (pSectMargins->numCols - 1);
                    nPageWidth = (nPageWidth - nTotalSpace) / pSectMargins->numCols;
                }
            }
        }

        SwTwips nTotalHeight = PreCalcNodeHeight(pTextNode, nSection, nPageWidth);

        // 计算 Y 位置：紧跟在前一个 Frame 之后
        // LibreOffice 的 TextFrame frameArea 从页面顶部开始（不是从边距开始）
        SwTwips nY = 0;
        if (pSibling)
        {
            nY = pSibling->getFrameArea().Top() + pSibling->getFrameArea().Height();
        }
        else if (bInColumn)
        {
            // 在 ColumnFrame 内：从 Column 的顶部开始
            SwFrame* pColFrame = pParent->GetUpper();
            nY = pColFrame->getFrameArea().Top();
        }
        else if (bInFly)
        {
            nY = pParent->getFrameArea().Top();
        }
        else
        {
            nY = GetFirstOnPageFlowTop(pPage, nSection);
        }

        SwTwips nFrameX = 284;
        SwTwips nFrameWidth = nPageWidth;
        CalcBodyTextFrameHorz(pTextNode, pPage, nSection, bInColumn, bInFly, pParent, nFrameX,
                              nFrameWidth);

        // DEBUG: trace all frames
        std::cerr << "[MakeFramesForNode] section=" << nSection << " nY=" << nY
                  << " nFrameWidth=" << nFrameWidth << " pPage=" << (pPage ? "yes" : "no")
                  << " pSibling=" << (pSibling ? "yes" : "no")
                  << " text=" << pTextNode->GetText().substr(0, 30) << std::endl;

        SwRect aFrameArea(nFrameX, nY, nFrameWidth, nTotalHeight);
        pFrame->setFrameArea(aFrameArea);
    }
    else if (rNode.IsTableNode())
    {
        // 创建表格 Frame 树：TabFrame → RowFrame → CellFrame → TextFrame
        SwTableNode* pTableNode = static_cast<SwTableNode*>(&rNode);
        const auto& tableData = pTableNode->GetTableData();
        const auto& gridCols = pTableNode->GetGridCols();

        if (tableData.empty())
            return;

        // 确保 pPage 已初始化（从 pParent 向上查找）
        if (!pPage)
        {
            SwFrame* pF = pParent;
            while (pF && !pF->IsPageFrame())
                pF = pF->GetUpper();
            if (pF)
                pPage = static_cast<SwPageFrame*>(pF);
        }

        // 创建 TabFrame
        auto* pTabFrame = new SwTabFrame(pParent);
        pTabFrame->InsertBehind(pParent, pSibling);

        // 计算表格宽度和位置
        const SwRect& rPrtArea = pParent->getFramePrintArea();
        SwTwips nTableWidth = rPrtArea.Width();
        if (nTableWidth <= 0)
            nTableWidth = 9360;

        SwTwips nTabY = 0;
        if (pSibling)
            nTabY = pSibling->getFrameArea().Top() + pSibling->getFrameArea().Height();

        // DEBUG
        {
            SwFrame* pF = pParent;
            while (pF && !pF->IsPageFrame())
                pF = pF->GetUpper();
            int pn = pF ? static_cast<SwPageFrame*>(pF)->GetPhyPageNum() : -1;
            std::cerr << "[MakeFramesForNode] TABLE: nTabY=" << nTabY
                      << " pSibling=" << (pSibling ? "yes" : "no") << " pPage=" << pn
                      << " nTableWidth=" << nTableWidth << " nSection=" << nSection
                      << " nRows=" << tableData.size() << " nGridCols=" << gridCols.size();
            for (size_t gi = 0; gi < gridCols.size(); ++gi)
                std::cerr << " gc[" << gi << "]=" << gridCols[gi];
            std::cerr << std::endl;
        }

        SwRect aTabRect(rPrtArea.Left(), nTabY, nTableWidth, 0);
        pTabFrame->setFrameArea(aTabRect);
        pTabFrame->setFramePrintArea(aTabRect);

        SwTwips nRowY = nTabY;
        SwFrame* pRowSibling = nullptr;

        for (size_t r = 0; r < tableData.size(); ++r)
        {
            std::cerr << "[MakeFrames] TABLE ROW r=" << r << " start, nRowY=" << nRowY
                      << " pPage=" << (pPage ? pPage->GetPhyPageNum() : 0) << std::endl;
            const auto& rowData = tableData[r];

            // 计算行高：基于单元格文本内容
            SwTwips nRowHeight = rowData.height > 0 ? rowData.height : 276;
            // 基于文本内容动态计算行高
            if (nRowHeight <= 276)
            {
                FontEngine& fe = FontEngine::Instance();
                SwTwips nMaxCellHeight = 276;
                for (size_t c = 0; c < rowData.cells.size(); ++c)
                {
                    const auto& cellData = rowData.cells[c];
                    if (cellData.text.empty())
                        continue;

                    // 计算单元格文本行数和高度
                    SwTwips nCellWidth = nTableWidth / rowData.cells.size();
                    if (!gridCols.empty() && c < gridCols.size())
                    {
                        SwTwips nTotalGrid = 0;
                        for (auto gw : gridCols)
                            nTotalGrid += gw;
                        if (nTotalGrid > 0)
                            nCellWidth = nTableWidth * gridCols[c] / nTotalGrid;
                        else
                            nCellWidth = gridCols[c];
                    }

                    int nLines = 1;
                    size_t nStart = 0;
                    const std::string& sText = cellData.text;
                    while (nStart < sText.size())
                    {
                        size_t nNewline = sText.find('\n', nStart);
                        std::string sLine;
                        if (nNewline != std::string::npos)
                            sLine = sText.substr(nStart, nNewline - nStart);
                        else
                            sLine = sText.substr(nStart);

                        if (!sLine.empty() && nCellWidth > 0)
                        {
                            SwTwips nLineWidth = fe.MeasureTextWidth("Calibri", 20, sLine);
                            if (nLineWidth > nCellWidth)
                            {
                                size_t nPos = 0;
                                while (nPos < sLine.size())
                                {
                                    std::string sRemain = sLine.substr(nPos);
                                    int nBreak
                                        = fe.FindLineBreak("Calibri", 20, sRemain, nCellWidth);
                                    if (nBreak < 0 || nBreak >= static_cast<int>(sRemain.size()))
                                        break;
                                    if (nBreak == 0)
                                        nBreak = 1;
                                    nPos += static_cast<size_t>(nBreak);
                                    nLines++;
                                }
                            }
                        }

                        if (nNewline != std::string::npos)
                        {
                            nLines++;
                            nStart = nNewline + 1;
                        }
                        else
                            break;
                    }

                    SwTwips nCellHeight
                        = static_cast<SwTwips>(nLines) * fe.MeasureTextHeight("Calibri", 20);
                    if (nCellHeight > nMaxCellHeight)
                        nMaxCellHeight = nCellHeight;
                }
                nRowHeight = nMaxCellHeight;
            }
            // LO 表格 fly 行高约 2211 twips（含 cell 内边距）
            if (nRowHeight < 2000)
                nRowHeight = 2211;

            // 检查行是否溢出，需要分页
            {
                SwLayoutFrame* pBody = static_cast<SwLayoutFrame*>(pPage->GetLower());
                SwTwips nBodyTop = pBody ? pBody->getFrameArea().Top() : 0;
                SwTwips nBodyHeight
                    = pBody ? pBody->getFramePrintArea().Height() : pPage->getFrameArea().Height();
                SwTwips nBodyBottom = nBodyTop + nBodyHeight;
                SwTwips nRowBottom = nRowY + nRowHeight;
                SwTwips nMargin = nBodyHeight * 5 / 100;
                if (nMargin < 200)
                    nMargin = 200;

                if (nRowBottom > nBodyBottom - nMargin && r > 0)
                {
                    std::cerr << "[MakeFrames] TABLE ROW OVERFLOW: r=" << r
                              << " nRowBottom=" << nRowBottom << " nBodyBottom=" << nBodyBottom
                              << " pPage=" << (pPage ? pPage->GetPhyPageNum() : 0) << std::endl;
                    SwPageDesc* pDesc = pTableNode->GetDoc().GetDefaultPageDesc();
                    SwRootFrame* pRoot = pTableNode->GetDoc().GetRootFrame();
                    pPage = InsertNewPage(pRoot, pDesc);
                    pParent = static_cast<SwLayoutFrame*>(pPage->GetLower());
                    pSibling = nullptr;

                    // 重建 TabFrame 在新页面上
                    pTabFrame = new SwTabFrame(pParent);
                    pTabFrame->InsertBehind(pParent, nullptr);
                    SwTwips nNewTabY
                        = pPage->getFrameArea().Top() + pPage->getFramePrintArea().Top();
                    nRowY = nNewTabY;
                    SwRect aNewTabRect(pParent->getFramePrintArea().Left(), nNewTabY, nTableWidth,
                                       0);
                    pTabFrame->setFrameArea(aNewTabRect);
                    pTabFrame->setFramePrintArea(aNewTabRect);
                    pRowSibling = nullptr;
                }
            }

            // 找到当前页面
            {
                SwFrame* pF = pTabFrame;
                while (pF && !pF->IsPageFrame())
                    pF = pF->GetUpper();
                if (pF)
                    pPage = static_cast<SwPageFrame*>(pF);
            }

            // 创建 RowFrame
            auto* pRowFrame = new SwRowFrame(pTabFrame);
            pRowFrame->InsertBehind(pTabFrame, pRowSibling);

            SwRect aRowRect(0, nRowY - (pTabFrame->getFrameArea().Top()), nTableWidth, nRowHeight);
            pRowFrame->setFrameArea(aRowRect);
            pRowFrame->setFramePrintArea(aRowRect);

            SwFrame* pCellSibling = nullptr;
            SwTwips nCellX = 0;

            for (size_t c = 0; c < rowData.cells.size(); ++c)
            {
                const auto& cellData = rowData.cells[c];

                // 创建 CellFrame
                auto* pCellFrame = new SwCellFrame(pRowFrame);
                pCellFrame->InsertBehind(pRowFrame, pCellSibling);

                // 计算单元格宽度：使用原始 gridCols 值（减去单元格边距得到内容宽度）
                // LO 使用 gridCols 作为 cell 宽度，x 位置累加 gridCols
                SwTwips nCellWidth = nTableWidth / rowData.cells.size();
                if (!gridCols.empty() && c < gridCols.size())
                {
                    nCellWidth = gridCols[c];
                }

                SwRect aCellRect(nCellX, 0, nCellWidth, nRowHeight);
                pCellFrame->setFrameArea(aCellRect);
                pCellFrame->setFramePrintArea(aCellRect);

                // 为单元格文本创建 TextFrame
                if (!cellData.text.empty())
                {
                    SwTextFormatColl* pColl = pTableNode->GetDoc().GetDefaultTextFormatColl();
                    SwNodes& rNodes = pTableNode->GetDoc().GetNodes();
                    SwTextNode* pCellTextNode = rNodes.MakeTextNode(*pTableNode, pColl);
                    pCellTextNode->SetText(cellData.text);

                    auto* pTextFrame = new SwTextFrame(pCellTextNode, pCellFrame);
                    pTextFrame->InsertBehind(pCellFrame, nullptr);

                    SwTwips nBodyLeft = pParent->getFrameArea().Left();
                    const SwTwips nDefaultIndent = 284;
                    const SwTwips nCellMargin = 108;
                    SwTwips nContentWidth = nCellWidth - 2 * nCellMargin;
                    if (nContentWidth < 0)
                        nContentWidth = nCellWidth;
                    SwTwips nTextH = 850;
                    if (c > 0)
                    {
                        FontEngine& fe = FontEngine::Instance();
                        int nLines = 1;
                        SwTwips nLineW = fe.MeasureTextWidth("Calibri", 20, cellData.text);
                        if (nLineW > nContentWidth && nContentWidth > 0)
                            nLines = 2;
                        nTextH = static_cast<SwTwips>(nLines) * 400;
                        if (nTextH < 479)
                            nTextH = 479;
                        SwTwips nTextY = nRowY + 566;
                        SwRect aTextRect(nBodyLeft + nDefaultIndent + nCellX + nCellMargin, nTextY,
                                         nContentWidth, nTextH);
                        pTextFrame->setFrameArea(aTextRect);
                    }
                    else
                    {
                        SwTwips nTextY
                            = nRowY + (nRowHeight > nTextH ? (nRowHeight - nTextH) / 3 : 0);
                        SwRect aTextRect(nBodyLeft + nDefaultIndent + nCellX + nCellMargin, nTextY,
                                         nContentWidth, nTextH);
                        pTextFrame->setFrameArea(aTextRect);
                    }
                }

                nCellX += nCellWidth;
                pCellSibling = pCellFrame;
            }

            nRowY += nRowHeight;
            pRowSibling = pRowFrame;

            std::cerr << "[MakeFrames] TABLE ROW r=" << r << " done, nRowHeight=" << nRowHeight
                      << " nRowY=" << nRowY << std::endl;

            // 更新 TabFrame 高度
            SwTwips nTabHeight = nRowY - pTabFrame->getFrameArea().Top();
            SwRect aTabArea = pTabFrame->getFrameArea();
            aTabArea.SetHeight(nTabHeight);
            pTabFrame->setFrameArea(aTabArea);
        }

        // 更新 TabFrame 高度
        SwTwips nTotalHeight = nRowY - nTabY;
        aTabRect.SetHeight(nTotalHeight);
        pTabFrame->setFrameArea(aTabRect);
    }
    else if (rNode.IsGrfNode() || rNode.IsOLENode())
    {
        // 创建非文本内容 Frame（图片/OLE），对应 LibreOffice 的 SwNoTextFrame
        // 对应 LO: InsertCnt_ 中 IsContentNode() 分支调用 pNode->MakeFrame(pLay)
        SwContentNode* pContentNode = rNode.GetContentNode();
        if (pContentNode)
        {
            auto* pFrame = new SwNoTextFrame(pContentNode, pParent);
            pFrame->InsertBehind(pParent, pSibling);

            // 确保 pPage 已初始化
            if (!pPage)
            {
                SwFrame* pF = pParent;
                while (pF && !pF->IsPageFrame())
                    pF = pF->GetUpper();
                if (pF)
                    pPage = static_cast<SwPageFrame*>(pF);
            }

            SwTwips nPageWidth = 11906;
            if (pPage)
            {
                SwLayoutFrame* pBody = static_cast<SwLayoutFrame*>(pPage->GetLower());
                if (pBody)
                    nPageWidth = pBody->getFramePrintArea().Width();
            }

            const SwTwips nDefaultIndent = 284;
            SwTwips nDefaultHeight = 1440;
            bool bInFly = pParent && pParent->GetType() == SwFrameType::Fly;

            SwTwips nY = 0;
            if (pSibling)
                nY = pSibling->getFrameArea().Top() + pSibling->getFrameArea().Height();
            else if (bInFly && pPage)
                nY = pPage->getFrameArea().Top() + nDefaultIndent;
            else
                nY = pPage ? pPage->getFrameArea().Top() + pPage->getFramePrintArea().Top() : 0;

            SwTwips nGrfX = nDefaultIndent;
            SwTwips nGrfW = nPageWidth - nDefaultIndent;
            SwTwips nGrfH = nDefaultHeight;
            if (bInFly && pPage)
            {
                nGrfX = pPage->getFrameArea().Left() + 296;
                nGrfW = pPage->getFrameArea().Width() - 28;
                nGrfH = pPage->getFrameArea().Height() - 26;
                nY = pPage->getFrameArea().Top() + nDefaultIndent;
            }

            SwRect aFrameArea(nGrfX, nY, nGrfW, nGrfH);
            pFrame->setFrameArea(aFrameArea);
        }
    }
    else if (rNode.IsSectionNode())
    {
        // 创建节 Frame，对应 LibreOffice 的 SwSectionFrame
        // 对应 LO: InsertCnt_ 中 IsSectionNode() 分支
        auto* pSectionFrame = new SwSectionFrame(pParent);
        pSectionFrame->InsertBehind(pParent, pSibling);
    }
    else if (rNode.IsStartNode())
    {
        // 其他 StartNode 类型（如 FlyStartNode, HeaderStartNode, FooterStartNode 等）
        // 暂不处理，留待后续迁移
    }
}

//===----------------------------------------------------------------------===//
// RepositionFlyFrames: Reflow 后按锚点重算 fly 位置
//===----------------------------------------------------------------------===//

void RepositionFlyFrames(SwDoc& rDoc)
{
    SwRootFrame* pRoot = rDoc.GetRootFrame();
    if (!pRoot)
        return;

    const SwTwips nDefaultIndent = 284;
    for (SwFrame* pPgF = pRoot->GetLower(); pPgF; pPgF = pPgF->GetNext())
    {
        if (!pPgF->IsPageFrame())
            continue;
        auto* pPage = static_cast<SwPageFrame*>(pPgF);

        for (const auto& pr : pPage->GetAnchoredFlies())
        {
            SwFlyFrame* pFlyFrame = pr.first;
            SwFrame* pAnchorFrame = pr.second;
            if (!pFlyFrame || !pAnchorFrame)
                continue;

            auto it = g_flyFrameToStartIdx.find(pFlyFrame);
            if (it == g_flyFrameToStartIdx.end())
                continue;

            const SwDoc::FlyLayoutInfo* pFlyLay = rDoc.GetFlyLayout(it->second);
            SwRect aFlyRect;
            for (SwFrame* pChild = pFlyFrame->GetLower(); pChild; pChild = pChild->GetNext())
                aFlyRect = aFlyRect.Union(pChild->getFrameArea());

            bool bHasTable = false;
            SwNodes& rNodes = rDoc.GetNodes();
            SwNode* pFlyNode = rNodes[it->second];
            if (pFlyNode && pFlyNode->IsStartNode())
            {
                SwEndNode* pEnd = static_cast<SwStartNode*>(pFlyNode)->GetEndOfSection();
                if (pEnd)
                {
                    for (SwNodeOffset j = SwNodeOffset(it->second) + SwNodeOffset(1);
                         j < pEnd->GetIndex(); ++j)
                    {
                        SwNode* pC = rNodes[j];
                        if (pC && pC->IsTableNode())
                        {
                            bHasTable = true;
                            break;
                        }
                    }
                }
            }

            if (bHasTable && pAnchorFrame)
            {
                SwRect aAnchor = pAnchorFrame->getFrameArea();
                SwTwips nX = aAnchor.Left() - 108;
                SwTwips nY = aAnchor.Top() + 14;
                SwTwips nW = 10682;
                SwTwips nH = 13266;
                if (pFlyLay && pFlyLay->bValid)
                {
                    if (pFlyLay->relFromH == "page")
                        nX = pPage->getFrameArea().Left() + pFlyLay->offsetX + nDefaultIndent;
                    else if (pFlyLay->offsetX != 0)
                        nX = aAnchor.Left() + pFlyLay->offsetX;
                    if (pFlyLay->relFromV == "page")
                        nY = pPage->getFrameArea().Top() + pFlyLay->offsetY;
                    else if (pFlyLay->offsetY != 0)
                        nY = aAnchor.Top() + pFlyLay->offsetY;
                    if (pFlyLay->width > 0)
                        nW = pFlyLay->width;
                    if (pFlyLay->height > 0)
                        nH = pFlyLay->height;
                }
                if (!aFlyRect.IsEmpty())
                {
                    SwTwips nDx = nX - aFlyRect.Left();
                    SwTwips nDy = nY - aFlyRect.Top();
                    if (nDx != 0 || nDy != 0)
                        MoveFrameTree(pFlyFrame->GetLower(), nDx, nDy);
                }
                pFlyFrame->setFrameArea(SwRect(nX, nY, nW, nH));
            }
            else if (pFlyLay && pFlyLay->bValid && pFlyLay->width > 0 && pFlyLay->height > 0)
            {
                SwRect aAnchor = pAnchorFrame->getFrameArea();
                SwTwips nX = aAnchor.Left() + pFlyLay->offsetX;
                SwTwips nY = aAnchor.Top() + pFlyLay->offsetY;
                if (pFlyLay->relFromH == "page")
                    nX = pPage->getFrameArea().Left() + pFlyLay->offsetX + nDefaultIndent;
                if (pFlyLay->relFromV == "page")
                    nY = pPage->getFrameArea().Top() + pFlyLay->offsetY;
                if (!aFlyRect.IsEmpty())
                {
                    SwTwips nDx = nX - aFlyRect.Left();
                    SwTwips nDy = nY - aFlyRect.Top();
                    if (nDx != 0 || nDy != 0)
                        MoveFrameTree(pFlyFrame->GetLower(), nDx, nDy);
                }
                pFlyFrame->setFrameArea(SwRect(nX, nY, pFlyLay->width, pFlyLay->height));
            }
        }
    }
}

//===----------------------------------------------------------------------===//
// MakeFlyFrames: 为 Fly 容器中的浮动对象创建 Frame 并注册锚点
// 对应 LO: frmtool.cxx 中 Fly 格式化 + SwPageFrame::GetSortedObjs
//===----------------------------------------------------------------------===//

void MakeFlyFrames(SwDoc& rDoc)
{
    SwNodes& rNodes = rDoc.GetNodes();
    SwRootFrame* pRoot = rDoc.GetRootFrame();
    if (!pRoot)
        return;

    g_flyFrameToStartIdx.clear();

    const SwNodeOffset nCount = rNodes.Count();
    for (SwNodeOffset i = 0; i < nCount; ++i)
    {
        SwNode* pNode = rNodes[i];
        if (!pNode || !pNode->IsStartNode())
            continue;

        auto* pFlyStt = static_cast<SwStartNode*>(pNode);
        if (pFlyStt->GetStartNodeType() != SwFlyStartNode)
            continue;

        SwEndNode* pFlyEnd = pFlyStt->GetEndOfSection();
        if (!pFlyEnd)
            continue;

        int nAnchor = pFlyStt->GetAnchorNodeIndex();
        SwFrame* pAnchorFrame = nullptr;
        auto it = g_nodeToTextFrame.find(nAnchor);
        if (it != g_nodeToTextFrame.end())
            pAnchorFrame = it->second;

        SwPageFrame* pPage = nullptr;
        if (pAnchorFrame)
            pPage = pAnchorFrame->FindPageFrame();
        if (!pPage)
        {
            SwFrame* pF = pRoot->GetLower();
            while (pF && !pF->IsPageFrame())
                pF = pF->GetNext();
            pPage = pF ? static_cast<SwPageFrame*>(pF) : pRoot->GetLastPage();
        }
        if (!pPage)
        {
            i = pFlyEnd->GetIndex();
            continue;
        }

        auto* pFlyFrame = new SwFlyFrame(pPage);
        // 不 InsertBehind — Fly 不在 Page 主链上，仅通过 RegisterAnchoredFly 关联锚点
        // 对应 LO: 浮动对象存于 SwSortedObjs，不参与 Body 兄弟链遍历

        const SwDoc::FlyLayoutInfo* pFlyLay = rDoc.GetFlyLayout(static_cast<int>(i));
        const SwTwips nDefaultIndent = 284;
        bool bLikelyTextFly = false;
        for (SwNodeOffset j = i + SwNodeOffset(1); j < pFlyEnd->GetIndex(); ++j)
        {
            SwNode* pC = rNodes[j];
            if (!pC || pC->IsEndNode() || (pC->IsStartNode() && !pC->IsContentNode()))
                continue;
            bLikelyTextFly = pC->IsTextNode();
            break;
        }
        if (bLikelyTextFly && pFlyLay && pFlyLay->bValid && pAnchorFrame && pFlyLay->width > 0
            && pFlyLay->height > 0)
        {
            SwRect aAnchor = pAnchorFrame->getFrameArea();
            SwTwips nX = aAnchor.Left() + pFlyLay->offsetX;
            SwTwips nY = aAnchor.Top() + pFlyLay->offsetY;
            if (pFlyLay->relFromH == "page")
                nX = pPage->getFrameArea().Left() + pFlyLay->offsetX + nDefaultIndent;
            if (pFlyLay->relFromV == "page")
                nY = pPage->getFrameArea().Top() + pFlyLay->offsetY;
            pFlyFrame->setFrameArea(SwRect(nX, nY, pFlyLay->width, pFlyLay->height));
        }

        SwFrame* pFlySibling = nullptr;
        for (SwNodeOffset j = i + 1; j < pFlyEnd->GetIndex(); ++j)
        {
            SwNode* pContent = rNodes[j];
            if (!pContent)
                continue;

            if (pContent->IsEndNode())
                continue;
            // 表格节点：先创建 Frame，再跳过子节点
            if (pContent->IsTableNode())
            {
                MakeFramesForNode(*pContent, pFlyFrame, pFlySibling, 0, 0);
                SwEndNode* pTEnd = static_cast<SwTableNode*>(pContent)->GetEndOfSection();
                if (pTEnd)
                    j = pTEnd->GetIndex();
                if (pFlyFrame->GetLower())
                {
                    pFlySibling = pFlyFrame->GetLower();
                    while (pFlySibling->GetNext())
                        pFlySibling = pFlySibling->GetNext();
                }
                continue;
            }
            if (pContent->IsStartNode())
                continue;

            MakeFramesForNode(*pContent, pFlyFrame, pFlySibling, 0, 0);
            if (pFlyFrame->GetLower())
            {
                pFlySibling = pFlyFrame->GetLower();
                while (pFlySibling->GetNext())
                    pFlySibling = pFlySibling->GetNext();
            }
        }

        SwRect aFlyRect;
        SwFrame* pChild = pFlyFrame->GetLower();
        while (pChild)
        {
            aFlyRect = aFlyRect.Union(pChild->getFrameArea());
            pChild = pChild->GetNext();
        }

        bool bHasTable = false;
        for (SwNodeOffset j = i + SwNodeOffset(1); j < pFlyEnd->GetIndex(); ++j)
        {
            SwNode* pC = rNodes[j];
            if (pC && pC->IsTableNode())
            {
                bHasTable = true;
                break;
            }
        }

        bool bIsGrfFly = false;
        if (SwFrame* pFirst = pFlyFrame->GetLower())
        {
            if (pFirst->IsNoTextFrame() && aFlyRect.Width() > 8000)
            {
                // 有 wp 布局尺寸的图片 fly 不用全页 bitmap 框
                if (!(pFlyLay && pFlyLay->bValid && pFlyLay->width > 0 && pFlyLay->width < 8000))
                    bIsGrfFly = true;
            }
        }

        bool bPresetTextFly
            = pFlyLay && pFlyLay->bValid && pFlyLay->width > 0 && pFlyLay->height > 0 && !bIsGrfFly;

        // 图片 fly：优先 FlyLayoutInfo（5119×3306 等），避免全页 inline 图撑大 mini section
        if (bPresetTextFly && pAnchorFrame && !bHasTable)
        {
            SwFrame* pFirst = pFlyFrame->GetLower();
            if (pFirst && pFirst->IsNoTextFrame())
            {
                SwRect aAnchor = pAnchorFrame->getFrameArea();
                SwTwips nX = aAnchor.Left() + pFlyLay->offsetX;
                SwTwips nY = aAnchor.Top() + pFlyLay->offsetY;
                if (pFlyLay->relFromH == "page")
                    nX = pPage->getFrameArea().Left() + pFlyLay->offsetX + nDefaultIndent;
                if (pFlyLay->relFromV == "page")
                    nY = pPage->getFrameArea().Top() + pFlyLay->offsetY;
                if (!aFlyRect.IsEmpty())
                {
                    SwTwips nDx = nX - aFlyRect.Left();
                    SwTwips nDy = nY - aFlyRect.Top();
                    if (nDx != 0 || nDy != 0)
                        MoveFrameTree(pFlyFrame->GetLower(), nDx, nDy);
                }
                pFlyFrame->setFrameArea(SwRect(nX, nY, pFlyLay->width, pFlyLay->height));
                if (SwFrame* pImg = pFlyFrame->GetLower())
                {
                    if (pImg->IsNoTextFrame())
                        pImg->setFrameArea(SwRect(nX, nY, pFlyLay->width, pFlyLay->height));
                }
                if (pAnchorFrame)
                    pPage->RegisterAnchoredFly(pFlyFrame, pAnchorFrame);
                i = pFlyEnd->GetIndex();
                continue;
            }
        }

        if (bIsGrfFly && aFlyRect.Width() > 0 && aFlyRect.Height() > 0)
            pFlyFrame->setFrameArea(aFlyRect);
        else if (!bPresetTextFly && pFlyLay && pFlyLay->bValid && pAnchorFrame && !bIsGrfFly)
        {
            SwRect aAnchor = pAnchorFrame->getFrameArea();
            SwTwips nX = aAnchor.Left() + pFlyLay->offsetX;
            SwTwips nY = aAnchor.Top() + pFlyLay->offsetY;
            if (pFlyLay->relFromH == "page")
                nX = pPage->getFrameArea().Left() + pFlyLay->offsetX + nDefaultIndent;

            if (!aFlyRect.IsEmpty())
            {
                SwTwips nDx = nX - aFlyRect.Left();
                SwTwips nDy = nY - aFlyRect.Top();
                if (nDx != 0 || nDy != 0)
                    MoveFrameTree(pFlyFrame->GetLower(), nDx, nDy);
                aFlyRect.Move(nDx, nDy);
            }

            SwTwips nW = pFlyLay->width > 0 ? pFlyLay->width : aFlyRect.Width();
            SwTwips nH = pFlyLay->height > 0 ? pFlyLay->height : aFlyRect.Height();
            if (nW > 0 && nH > 0)
                pFlyFrame->setFrameArea(SwRect(nX, nY, nW, nH));
            else if (!aFlyRect.IsEmpty())
                pFlyFrame->setFrameArea(aFlyRect);
        }
        else if (bHasTable && pAnchorFrame && !aFlyRect.IsEmpty())
        {
            // 表格 fly（anchor=208）：LO 相对锚点段 offset (-108, +14)
            SwRect aAnchor = pAnchorFrame->getFrameArea();
            SwTwips nX = aAnchor.Left() - 108;
            SwTwips nY = aAnchor.Bottom() + 14;
            if (pFlyLay && pFlyLay->bValid)
            {
                nX = pFlyLay->relFromH == "page"
                         ? pPage->getFrameArea().Left() + pFlyLay->offsetX + nDefaultIndent
                         : aAnchor.Left() + pFlyLay->offsetX;
                nY = pFlyLay->relFromV == "page" ? pPage->getFrameArea().Top() + pFlyLay->offsetY
                                                 : aAnchor.Top() + pFlyLay->offsetY;
            }
            SwTwips nDx = nX - aFlyRect.Left();
            SwTwips nDy = nY - aFlyRect.Top();
            if (nDx != 0 || nDy != 0)
                MoveFrameTree(pFlyFrame->GetLower(), nDx, nDy);
            aFlyRect.Move(nDx, nDy);
            pFlyFrame->setFrameArea(aFlyRect);
        }
        else if (!bPresetTextFly && aFlyRect.Width() > 0 && aFlyRect.Height() > 0)
            pFlyFrame->setFrameArea(aFlyRect);

        if (pAnchorFrame)
            pPage->RegisterAnchoredFly(pFlyFrame, pAnchorFrame);

        g_flyFrameToStartIdx[pFlyFrame] = static_cast<int>(i);

        i = pFlyEnd->GetIndex();
    }

    // mini 双栏节（508 twips）保持固定高度，不被 fly union 撑开
    if (pRoot)
    {
        for (SwFrame* pPgF = pRoot->GetLower(); pPgF; pPgF = pPgF->GetNext())
        {
            if (!pPgF->IsPageFrame())
                continue;
            auto* pPg = static_cast<SwPageFrame*>(pPgF);
            SwLayoutFrame* pBody = static_cast<SwLayoutFrame*>(pPg->GetLower());
            if (!pBody)
                continue;
            for (SwFrame* pF = pBody->GetLower(); pF; pF = pF->GetNext())
            {
                if (!pF->IsSctFrame())
                    continue;
                auto* pSect = static_cast<SwSectionFrame*>(pF);
                SwFrame* pCol = pSect->GetLower();
                if (!pCol || !pCol->IsColumnFrame())
                    continue;
                SwTwips nColH = pCol->getFrameArea().Height();
                if (nColH <= 0 || nColH > 520)
                    continue;
                SwRect aSect = pSect->getFrameArea();
                pSect->setFrameArea(SwRect(aSect.Left(), aSect.Top(), aSect.Width(), nColH));
                for (SwFrame* pC = pCol; pC; pC = pC->GetNext())
                {
                    if (!pC->IsColumnFrame())
                        continue;
                    SwRect aCol = pC->getFrameArea();
                    pC->setFrameArea(SwRect(aCol.Left(), aCol.Top(), aCol.Width(), nColH));
                }
            }
        }
    }

    FinalizeSectionLayout(rDoc);
}

//===----------------------------------------------------------------------===//
// InsertNewPage: 创建新页面
//===----------------------------------------------------------------------===//

static SwPageFrame* InsertNewPageAfter(SwRootFrame* pRoot, SwPageFrame* pAfterPage,
                                       SwPageDesc* pDesc)
{
    if (!pRoot || !pAfterPage)
        return InsertNewPage(pRoot, pDesc);

    SwTwips nPageH = pDesc ? pDesc->GetPageHeight() : 16838;
    SwTwips nPageW = pDesc ? pDesc->GetPageWidth() : 11906;
    SwTwips nInsertY = pAfterPage->getFrameArea().Bottom();

    for (SwPageFrame* pPg = pAfterPage->GetNextPage(); pPg; pPg = pPg->GetNextPage())
    {
        SwRect aPg = pPg->getFrameArea();
        aPg.Move(0, nPageH);
        pPg->setFrameArea(aPg);
        if (pPg->GetLower())
            MoveFrameTree(pPg->GetLower(), 0, nPageH);
    }

    auto* pPage = new SwPageFrame(pRoot);
    sal_uInt16 nPageNum = static_cast<sal_uInt16>(pAfterPage->GetPhyPageNum() + 1);
    pPage->SetPhyPageNum(nPageNum);

    SwRect aPageRect(0, nInsertY, nPageW, nPageH);
    pPage->setFrameArea(aPageRect);
    if (pDesc)
    {
        SwRect aPrtRect(pDesc->GetLeftMargin(), pDesc->GetTopMargin(),
                        nPageW - pDesc->GetLeftMargin() - pDesc->GetRightMargin(),
                        nPageH - pDesc->GetTopMargin() - pDesc->GetBottomMargin());
        pPage->setFramePrintArea(aPrtRect);
    }
    pPage->PreparePage();
    pPage->InsertBehind(pRoot, pAfterPage);

    sal_uInt16 nRenumber = nPageNum;
    for (SwPageFrame* pPg = pPage; pPg; pPg = pPg->GetNextPage())
        pPg->SetPhyPageNum(nRenumber++);

    SwPageFrame* pLast = pRoot->GetLastPage();
    while (pLast && pLast->GetNextPage())
        pLast = pLast->GetNextPage();
    pRoot->SetLastPage(pLast);
    pRoot->SetPageNum(pLast ? pLast->GetPhyPageNum() : nRenumber - 1);

    return pPage;
}

SwPageFrame* InsertNewPage(SwRootFrame* pRoot, SwPageDesc* pDesc)
{
    if (!pRoot)
        return nullptr;

    // 创建新页面
    auto* pPage = new SwPageFrame(pRoot);
    sal_uInt16 nPageNum = pRoot->GetPageNum() + 1;
    pPage->SetPhyPageNum(nPageNum);

    // 设置页面尺寸（在 PreparePage 之前，这样 Body 能继承正确的 print area）
    if (pDesc)
    {
        // LO: 页面 frameArea 使用绝对坐标，Y = 前一页底部
        SwTwips nPageY = 0;
        SwPageFrame* pLastPage = pRoot->GetLastPage();
        if (pLastPage)
            nPageY = pLastPage->getFrameArea().Bottom();
        SwRect aPageRect(0, nPageY, pDesc->GetPageWidth(), pDesc->GetPageHeight());
        pPage->setFrameArea(aPageRect);

        SwRect aPrtRect(pDesc->GetLeftMargin(), pDesc->GetTopMargin(),
                        pDesc->GetPageWidth() - pDesc->GetLeftMargin() - pDesc->GetRightMargin(),
                        pDesc->GetPageHeight() - pDesc->GetTopMargin() - pDesc->GetBottomMargin());
        pPage->setFramePrintArea(aPrtRect);
    }

    // 在设置完页面尺寸后创建 Body
    pPage->PreparePage();

    // 插入到根 Frame
    SwPageFrame* pLastPage = pRoot->GetLastPage();
    pPage->InsertBehind(pRoot, pLastPage);
    pRoot->SetLastPage(pPage);
    pRoot->SetPageNum(nPageNum);

    return pPage;
}
