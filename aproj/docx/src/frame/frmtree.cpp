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

// 节点索引 → 正文 TextFrame，用于 Fly 锚点定位
static std::map<int, SwTextFrame*> g_nodeToTextFrame;

static int GetTextNodeSectionIndex(SwTextNode* pTextNode);

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
    if (pSectM->colWidth > 0)
    {
        nColWidth = pSectM->colWidth;
        nColSpace = pSectM->colSpace;
    }
    else
    {
        nColSpace = pSectM->colSpace > 0 ? pSectM->colSpace : 708;
        nColWidth = (nBodyWidth - nColSpace * (pSectM->numCols - 1)) / pSectM->numCols;
    }

    const SwTwips nDefaultIndent = 284;
    const SwTwips nSectLeft = pSectM->left;
    SwTwips nLeftColX = nDefaultIndent + nSectLeft;
    SwTwips nRightColX = nLeftColX + nColWidth + nColSpace;

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
            if (pB && (*pB == "section" || *pB == "continuous" || *pB == "page"))
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
        SwTwips h = PreCalcNodeHeight(pTN, nCurrentSection, nColWidth);
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

    SwTwips nBodyBottom = pBody ? pBody->getFrameArea().Top() + pBody->getFramePrintArea().Height()
                                : pPage->getFrameArea().Top() + pPage->getFrameArea().Height();
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

        if (bFirstIsHeading)
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

    // LO 行为：两列并排在同一页，左列先填，右列放剩余内容
    // 左列放前 N 个节点，右列放剩余节点
    std::vector<size_t> leftColIndices, rightColIndices;
    SwTwips nLeftHeight = 0, nRightHeight = 0;

    // 计算有多少内容能放入左列（不超过页面可用高度）
    SwTwips nAccumHeight = 0;
    size_t nLeftEnd = 0;
    for (size_t j = 0; j < colNodes.size(); ++j)
    {
        if (nAccumHeight + heights[j] > nPageAvailHeight && nLeftEnd > 0)
            break;
        nAccumHeight += heights[j];
        nLeftEnd = j + 1;
    }

    // 如果所有内容都能放入左列，则平均分配
    if (nLeftEnd >= colNodes.size())
    {
        size_t nMid = (colNodes.size() + 1) / 2;
        for (size_t j = 0; j < nMid; ++j)
        {
            leftColIndices.push_back(j);
            nLeftHeight += heights[j];
        }
        for (size_t j = nMid; j < colNodes.size(); ++j)
        {
            rightColIndices.push_back(j);
            nRightHeight += heights[j];
        }
    }
    else
    {
        for (size_t j = 0; j < nLeftEnd; ++j)
        {
            leftColIndices.push_back(j);
            nLeftHeight += heights[j];
        }
        for (size_t j = nLeftEnd; j < colNodes.size(); ++j)
        {
            rightColIndices.push_back(j);
            nRightHeight += heights[j];
        }
    }

    // 检查左列是否溢出（需要新页面）
    bool bLeftOverflow = (nLeftHeight > nPageAvailHeight && nLeftHeight > 0);

    std::cerr << "[ProcessMultiCol] leftCol=" << leftColIndices.size() << " leftH=" << nLeftHeight
              << " rightCol=" << rightColIndices.size() << " rightH=" << nRightHeight
              << " bLeftOverflow=" << bLeftOverflow << std::endl;

    // 创建节 Frame，内含列 Frame（对应 LO: SectionFrame → ColumnFrame → BodyFrame）
    auto* pSectionFrame = new SwSectionFrame(pParent);
    pSectionFrame->InsertBehind(pParent, pSibling);
    pSibling = pSectionFrame;

    // 处理溢出：创建新页面
    SwLayoutFrame* pSectParent = pSectionFrame;
    if (bLeftOverflow)
    {
        SwPageDesc* pDesc = rDoc.GetDefaultPageDesc();
        SwRootFrame* pRoot = rDoc.GetRootFrame();
        SwPageFrame* pNewPage = InsertNewPage(pRoot, pDesc);
        SwLayoutFrame* pNewBody = static_cast<SwLayoutFrame*>(pNewPage->GetLower());
        auto* pNewSectFrame = new SwSectionFrame(pNewBody);
        pNewSectFrame->InsertBehind(pNewBody, nullptr);
        pSectParent = pNewSectFrame;
        pSibling = pNewSectFrame;
        pSectionFrame = pNewSectFrame;
        std::cerr << "[ProcessMultiCol] Left col overflow to page " << pNewPage->GetPhyPageNum()
                  << std::endl;
    }

    // 创建左列 Frame（对应 LO: SwColumnFrame 内含 SwBodyFrame）
    auto* pLeftColFrame = new SwColumnFrame(pSectParent);
    pLeftColFrame->InsertBehind(pSectParent, nullptr);
    // 在 ColumnFrame 内创建 BodyFrame
    auto* pLeftColBody = new SwBodyFrame(pParent);
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
        auto* pRightColBody = new SwBodyFrame(pParent);
        pRightColBody->InsertBehind(pRightColFrame, nullptr);

        if (!bLeftOverflow)
        {
            nCurY = nBaseY;
        }
        else
        {
            nCurY = pSectParent->getFrameArea().Top() + pSectParent->getFrameArea().Top();
        }

        SwFrame* pRightSibling = nullptr;
        for (size_t idx : rightColIndices)
        {
            SwTextNode* pTN = static_cast<SwTextNode*>(rNodes[colNodes[idx]]);
            auto* pFrame = new SwTextFrame(pTN, pRightColBody);
            pFrame->InsertBehind(pRightColBody, pRightSibling);
            SwRect aArea(nRightColX, nCurY, nColWidth, heights[idx]);
            pFrame->setFrameArea(aArea);
            g_nodeToTextFrame[static_cast<int>(pTN->GetIndex())] = pFrame;
            pRightSibling = pFrame;
            pSibling = pFrame;
            nCurY += heights[idx];
        }
    }

    // 设置节/列 Frame 区域
    SwTwips nSectH = std::max(nLeftHeight, nRightHeight);
    if (nSectH > 0)
    {
        SwTwips nSectW = nColWidth;
        if (!rightColIndices.empty())
            nSectW = nColWidth + nColSpace + nColWidth;
        pSectionFrame->setFrameArea(SwRect(nLeftColX, nBaseY, nSectW, nSectH));
        pLeftColFrame->setFrameArea(SwRect(nLeftColX, nBaseY, nColWidth, nLeftHeight));
        if (!rightColIndices.empty())
        {
            SwFrame* pRightCol = pLeftColFrame->GetNext();
            if (pRightCol && pRightCol->IsColumnFrame())
                pRightCol->setFrameArea(SwRect(nRightColX, nBaseY, nColWidth, nRightHeight));
        }
    }

    // 更新循环索引
    i = colNodes.back();

    return true;
}

static SwTwips CalcLineHeightForFont(const std::string& sFontName, int nFontSize,
                                     const std::string* pLineSpacing, bool bEmptyPara)
{
    FontEngine& fontEngine = FontEngine::Instance();
    int nMeasuredHeight = fontEngine.MeasureTextHeight(sFontName, nFontSize);
    SwTwips nLineHeight = nMeasuredHeight > 0 ? static_cast<SwTwips>(nMeasuredHeight)
                                              : static_cast<SwTwips>(nFontSize * 14);
    if (bEmptyPara)
    {
        SwTwips nMinLineHeight = FontEngine::Instance().HasAltName(sFontName)
                                     ? static_cast<SwTwips>(nFontSize * 17)
                                     : static_cast<SwTwips>(nFontSize * 14);
        if (nLineHeight < nMinLineHeight)
            nLineHeight = nMinLineHeight;
    }
    // LO w:lineRule=auto w:line=240：行高 ≈ fontSize(半点) * 40 / 3 twips
    const bool bDefaultLineSpacing = !pLineSpacing || *pLineSpacing == "240";
    if (bDefaultLineSpacing)
    {
        SwTwips nAutoLine = static_cast<SwTwips>((nFontSize * 40 + 2) / 3);
        if (nLineHeight < nAutoLine)
            nLineHeight = nAutoLine;
    }
    if (pLineSpacing && !bDefaultLineSpacing)
    {
        try
        {
            int nLS = std::stoi(*pLineSpacing);
            if (nLS > 0 && nLS != 240)
                nLineHeight = nLineHeight * nLS / 240;
        }
        catch (...)
        {
        }
    }
    return nLineHeight;
}

static int CountTextLines(const std::string& rText, const std::string& sContentFontName,
                          int nContentFontSize, SwTwips nColWidth)
{
    if (rText.empty() || nColWidth <= 0)
        return 1;

    FontEngine& fontEngine = FontEngine::Instance();
    int nTextLines = 1;
    size_t nStart = 0;
    while (nStart < rText.size())
    {
        size_t nNewline = rText.find('\n', nStart);
        std::string sLine;
        if (nNewline != std::string::npos)
            sLine = rText.substr(nStart, nNewline - nStart);
        else
            sLine = rText.substr(nStart);

        if (!sLine.empty())
        {
            SwTwips nLineWidth
                = fontEngine.MeasureTextWidth(sContentFontName, nContentFontSize, sLine);
            if (nLineWidth > nColWidth)
            {
                size_t nPos = 0;
                while (nPos < sLine.size())
                {
                    std::string sRemain = sLine.substr(nPos);
                    int nBreak = fontEngine.FindLineBreak(sContentFontName, nContentFontSize,
                                                          sRemain, nColWidth);
                    if (nBreak < 0 || nBreak >= static_cast<int>(sRemain.size()))
                        break;
                    if (nBreak == 0)
                        nBreak = 1;
                    nPos += static_cast<size_t>(nBreak);
                    nTextLines++;
                }
            }
        }

        if (nNewline != std::string::npos)
        {
            nTextLines++;
            nStart = nNewline + 1;
        }
        else
        {
            break;
        }
    }
    return nTextLines;
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
    SwTwips nEffective = nColWidth - nLeft;
    return nEffective > 0 ? nEffective : nColWidth;
}

static SwTwips GetFirstOnPageFlowTop(SwPageFrame* pPage, int nSection)
{
    const SwTwips nDefaultIndent = 284;
    if (!pPage)
        return nDefaultIndent;

    SwTwips nPageTop = pPage->getFrameArea().Top();
    if (pPage->GetPhyPageNum() <= 1 && nSection == 0)
        return nPageTop + nDefaultIndent;

    return nPageTop + pPage->getFramePrintArea().Top()
           + static_cast<SwTwips>(pPage->GetPhyPageNum()) * nDefaultIndent;
}

SwTwips CalcTextNodeFrameHeight(SwTextNode* pTextNode, SwTwips nColWidth)
{
    const std::string* pMarkSize = pTextNode->GetAttr(RES_CHRATR_FONTSIZE_PARA_MARK);
    const std::string* pMarkFont = pTextNode->GetAttr(RES_CHRATR_FONT_PARA_MARK);
    const std::string* pContentFont = pTextNode->GetAttr(RES_CHRATR_FONT);
    const std::string* pContentSize = pTextNode->GetAttr(RES_CHRATR_FONTSIZE);
    const std::string* pLineSpacing = pTextNode->GetAttr(RES_PARATR_LINESPACING);

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

    SwTwips nLineHeight = CalcLineHeightForFont(sFontName, nFontSize, pLineSpacing, bEmpty);

    std::string sContentFontName = pContentFont ? *pContentFont : "Calibri";
    int nContentFontSize = pContentSize ? std::stoi(*pContentSize) : 20;
    int nLineCount = CountTextLines(rText, sContentFontName, nContentFontSize,
                                    GetEffectiveTextLineWidth(pTextNode, nColWidth));

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
    if (bEmpty && nSpaceBefore > 0)
        nTotal += nSpaceBefore;
    if (!bEmpty && nSpaceAfter > 0)
        nTotal += nSpaceAfter;

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
    g_nodeToTextFrame.clear();

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
                        nCurrentSection = GetTextNodeSectionIndex(pTextNode);
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

                    // 节分隔节点本身不创建 Frame（匹配 LO 行为）
                    // 跳过 MakeFramesForNode，直接处理多列布局或继续循环
                    if (bMultiColumn)
                    {
                        std::cerr << "[MakeFrames] Detected multi-column section "
                                  << nCurrentSection
                                  << " pPage=" << (pPage ? pPage->GetPhyPageNum() : 0) << std::endl;
                        bool bHandled = ProcessMultiColumnSection(
                            rDoc, rNodes, pPage, pActiveLayout, pSibling, i, nEnd, nCurrentSection);
                        pPage = pRoot->GetLastPage();
                        pBodyLayout = pActiveLayout
                            = static_cast<SwLayoutFrame*>(pPage->GetLower());
                        if (bHandled)
                            continue;
                    }
                    // 纯节边界（无正文）不创建 Frame；有内容的节点仍创建
                    if (pTextNode->GetText().empty())
                        continue;
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

                    // 检查新节是否为多列布局
                    bool bNewMultiCol = (pMargins && pMargins->numCols > 1);
                    if (bNewMultiCol)
                    {
                        // 多列节：在当前页上处理多列布局
                        // 右列放在当前页，左列溢出到新页
                        pPage = pRoot->GetLastPage();
                        pBodyLayout = pActiveLayout
                            = static_cast<SwLayoutFrame*>(pPage->GetLower());
                        bool bHandled = ProcessMultiColumnSection(
                            rDoc, rNodes, pPage, pActiveLayout, pSibling, i, nEnd, nCurrentSection);
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

        // 同步节索引（ParseBody 写入 RES_SECTION_INDEX）
        if (pNode->IsTextNode())
        {
            SwTextNode* pTextNode = static_cast<SwTextNode*>(pNode);
            const std::string* pSectIdx = pTextNode->GetAttr(RES_SECTION_INDEX);
            if (pSectIdx)
            {
                try
                {
                    int nSect = std::stoi(*pSectIdx);
                    if (nSect != nCurrentSection)
                        nCurrentSection = nSect;
                }
                catch (...)
                {
                }
            }
        }

        // 溢出预检测：暂禁用，由 RES_BREAK 分节符驱动分页（对应 LO Format_ 链）
        if (false && pNode->IsTextNode() && pPage && pSibling)
        {
            SwTextNode* pTextNode = static_cast<SwTextNode*>(pNode);
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
            // Body frame 的底部 = Body 顶部 + Body 打印区域高度
            SwTwips nBodyBottom
                = pBody ? pBody->getFrameArea().Top() + pBody->getFramePrintArea().Height()
                        : pPage->getFrameArea().Top() + pPage->getFrameArea().Height();
            // LibreOffice 在接近页面底部时会提前分页
            // 使用页面高度的 25% 作为余量以匹配 LO 的分页行为
            SwTwips nBodyHeight = nBodyBottom - pBody->getFrameArea().Top();
            SwTwips nMargin = nBodyHeight * 10 / 100;
            if (nMargin < 500)
                nMargin = 500;
            if (nFrameBottom > nBodyBottom - nMargin)
            {
                std::cerr << "[MakeFrames] OVERFLOW: nFrameBottom=" << nFrameBottom
                          << " nBodyBottom=" << nBodyBottom << " nMargin=" << nMargin
                          << " pPage=" << pPage->GetPhyPageNum()
                          << " inSection=" << (pOpenSectionFrame ? "yes" : "no") << std::endl;
                SwPageDesc* pDesc = rDoc.GetDefaultPageDesc();
                pPage = InsertNewPage(pRoot, pDesc);
                pBodyLayout = static_cast<SwLayoutFrame*>(pPage->GetLower());

                if (pOpenSectionFrame)
                {
                    // 在 Section 内溢出：在新页面重建 Section → Column → Body 层级
                    auto* pNewSection = new SwSectionFrame(pBodyLayout);
                    pNewSection->InsertBehind(pBodyLayout, nullptr);
                    auto* pNewCol = new SwColumnFrame(pNewSection);
                    pNewCol->InsertBehind(pNewSection, nullptr);
                    auto* pNewBody = new SwBodyFrame(pNewCol);
                    pNewBody->InsertBehind(pNewCol, nullptr);

                    SwTwips nSectLeft = pBodyLayout->getFrameArea().Left();
                    SwTwips nSectTop
                        = pPage->getFrameArea().Top() + pPage->getFramePrintArea().Top();
                    SwTwips nSectWidth = pBodyLayout->getFramePrintArea().Width();
                    pNewSection->setFrameArea(SwRect(nSectLeft, nSectTop, nSectWidth, 0));
                    pNewCol->setFrameArea(SwRect(nSectLeft, nSectTop, nSectWidth, 0));

                    pActiveLayout = pNewBody;
                    pOpenSectionFrame = pNewSection;
                    pSibling = nullptr;

                    std::cerr << "[MakeFrames] OVERFLOW in section -> rebuilt hierarchy on page "
                              << pPage->GetPhyPageNum() << std::endl;
                }
                else
                {
                    pActiveLayout = pBodyLayout;
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
            const SwDoc::SectionMargins* pMargins = rDoc.GetSectionMargins(nCurrentSection);
            if (pMargins && pMargins->numCols > 1 && pSectEnd)
            {
                SwNodeOffset nFirstContent = i + SwNodeOffset(1);
                if (nFirstContent < pSectEnd->GetIndex())
                {
                    SwNode* pFirst = rNodes[nFirstContent];
                    if (pFirst && pFirst->IsTextNode())
                    {
                        auto itFr = g_nodeToTextFrame.find(static_cast<int>(pFirst->GetIndex()));
                        if (itFr != g_nodeToTextFrame.end())
                        {
                            i = pSectEnd->GetIndex();
                            continue;
                        }
                    }
                }
                SwNodeOffset nSectContentEnd = pSectEnd->GetIndex() - SwNodeOffset(1);
                bool bHandled
                    = ProcessMultiColumnSection(rDoc, rNodes, pPage, pBodyLayout, pSibling, i,
                                                nSectContentEnd, nCurrentSection);
                if (bHandled)
                {
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
            // 单列 Section：创建 SectionFrame → ColumnFrame → BodyFrame 完整层级
            // 对应 LO: InsertCnt_ 中 SwSectionFrame 含 SwColumnFrame 子节点
            auto* pSectionFrame = new SwSectionFrame(pActiveLayout);
            pSectionFrame->InsertBehind(pActiveLayout, pSibling);

            // 创建列 Frame（即使是单列也要创建，与 LO 一致）
            auto* pColFrame = new SwColumnFrame(pSectionFrame);
            pColFrame->InsertBehind(pSectionFrame, nullptr);

            // 在列 Frame 内创建 BodyFrame
            auto* pColBody = new SwBodyFrame(pColFrame);
            pColBody->InsertBehind(pColFrame, nullptr);

            // 设置 Section 和 Column 的几何区域（从 Body 继承）
            SwRect aBodyArea = pActiveLayout->getFrameArea();
            SwTwips nSectLeft = aBodyArea.Left();
            SwTwips nSectTop
                = pSibling ? pSibling->getFrameArea().Top() + pSibling->getFrameArea().Height()
                           : aBodyArea.Top();
            SwTwips nSectWidth = aBodyArea.Width();
            pSectionFrame->setFrameArea(SwRect(nSectLeft, nSectTop, nSectWidth, 0));
            pColFrame->setFrameArea(SwRect(nSectLeft, nSectTop, nSectWidth, 0));

            // pActiveLayout 切换到列内的 BodyFrame，后续内容节点成为其子节点
            pActiveLayout = pColBody;
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

    ReflowTextFrameGeometry(rDoc);
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

    if (pSectM && pSectM->numCols > 1)
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
    SwPageDesc* pDesc = rDoc.GetDefaultPageDesc();
    SwPageFrame* pNewPage = InsertNewPage(pRoot, pDesc);
    SwLayoutFrame* pNewParent = static_cast<SwLayoutFrame*>(pNewPage->GetLower());

    if (pFrame->IsInSct())
    {
        auto* pNewSection = new SwSectionFrame(pNewParent);
        pNewSection->InsertBehind(pNewParent, nullptr);
        auto* pNewCol = new SwColumnFrame(pNewSection);
        pNewCol->InsertBehind(pNewSection, nullptr);
        auto* pNewBody = new SwBodyFrame(pNewCol);
        pNewBody->InsertBehind(pNewCol, nullptr);

        SwTwips nSectLeft = pNewParent->getFrameArea().Left();
        SwTwips nSectTop = pNewPage->getFrameArea().Top() + pNewPage->getFramePrintArea().Top();
        SwTwips nSectWidth = pNewParent->getFramePrintArea().Width();
        pNewSection->setFrameArea(SwRect(nSectLeft, nSectTop, nSectWidth, 0));
        pNewCol->setFrameArea(SwRect(nSectLeft, nSectTop, nSectWidth, 0));
        pNewParent = pNewBody;
    }

    SwFrame* pCur = pFrame;
    SwFrame* pInsertAfter = nullptr;
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

static SwTwips GetBodyFlowBottom(SwTextFrame* pFrame)
{
    SwPageFrame* pPage = pFrame->FindPageFrame();
    if (!pPage)
        return 0;

    SwLayoutFrame* pBody = static_cast<SwLayoutFrame*>(pPage->GetLower());
    if (!pBody)
        return pPage->getFrameArea().Bottom();

    // 在 Section/Column 内：以 Column 打印区域为界
    if (pFrame->IsInSct())
    {
        SwLayoutFrame* pCol = pFrame->FindColFrame();
        if (pCol)
        {
            SwTwips nTop = pCol->getFrameArea().Top();
            SwTwips nH = pCol->getFramePrintArea().Height();
            if (nH <= 0)
                nH = pCol->getFrameArea().Height();
            return nTop + nH;
        }
    }

    SwTwips nTop = pBody->getFrameArea().Top();
    SwTwips nH = pBody->getFramePrintArea().Height();
    return nTop + nH;
}

static void MoveFlowSiblingsToNewPage(SwTextFrame* pFirst, SwDoc& rDoc)
{
    SwPageFrame* pNewPage = ForceTextFrameToNewPage(pFirst, rDoc);
    if (pNewPage)
        std::cerr << "[ReflowTextFrameGeometry] overflow -> page " << pNewPage->GetPhyPageNum()
                  << std::endl;
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

        SwRect aArea = pFrame->getFrameArea();
        aArea.SetLeft(nFrameX);
        aArea.SetWidth(nFrameWidth);
        aArea.SetHeight(nHeight);
        pFrame->setFrameArea(aArea);
    }

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
        if (!bInFly && !bInColumn)
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
            int nSect = GetTextNodeSectionIndex(pTextNode);
            nY = GetFirstOnPageFlowTop(pPage, nSect);
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
                    SwTwips nBodyTop = pParent->getFrameArea().Top();
                    const SwTwips nDefaultIndent = 284;
                    // 单元格边距：默认 108 twips 每侧（匹配 OOXML 默认值）
                    const SwTwips nCellMargin = 108;
                    SwTwips nContentWidth = nCellWidth - 2 * nCellMargin;
                    if (nContentWidth < 0)
                        nContentWidth = nCellWidth;
                    SwRect aTextRect(nBodyLeft + nDefaultIndent + nCellX, nRowY, nContentWidth,
                                     nRowHeight);
                    pTextFrame->setFrameArea(aTextRect);
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
// MakeFlyFrames: 为 Fly 容器中的浮动对象创建 Frame 并注册锚点
// 对应 LO: frmtool.cxx 中 Fly 格式化 + SwPageFrame::GetSortedObjs
//===----------------------------------------------------------------------===//

void MakeFlyFrames(SwDoc& rDoc)
{
    SwNodes& rNodes = rDoc.GetNodes();
    SwStartNode* pFlyCont = rNodes.GetFlyContainerStart();
    SwRootFrame* pRoot = rDoc.GetRootFrame();
    if (!pFlyCont || !pRoot)
        return;

    SwNodeOffset nEnd = rNodes.GetEndOfAutotext().GetIndex();

    for (SwNodeOffset i = pFlyCont->GetIndex() + 1; i < nEnd;)
    {
        SwNode* pNode = rNodes[i];
        if (!pNode || !pNode->IsStartNode())
        {
            ++i;
            continue;
        }

        auto* pFlyStt = static_cast<SwStartNode*>(pNode);
        if (pFlyStt->GetStartNodeType() != SwFlyStartNode)
        {
            ++i;
            continue;
        }

        SwEndNode* pFlyEnd = pFlyStt->GetEndOfSection();
        if (!pFlyEnd)
        {
            ++i;
            continue;
        }

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
            i = pFlyEnd->GetIndex() + 1;
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

        bool bIsGrfFly = false;
        if (SwFrame* pFirst = pFlyFrame->GetLower())
        {
            if (pFirst->IsNoTextFrame() && aFlyRect.Width() > 8000)
                bIsGrfFly = true;
        }

        bool bPresetTextFly
            = pFlyLay && pFlyLay->bValid && pFlyLay->width > 0 && pFlyLay->height > 0 && !bIsGrfFly;

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
        else if (!bPresetTextFly && aFlyRect.Width() > 0 && aFlyRect.Height() > 0)
            pFlyFrame->setFrameArea(aFlyRect);

        if (pAnchorFrame)
            pPage->RegisterAnchoredFly(pFlyFrame, pAnchorFrame);

        i = pFlyEnd->GetIndex() + 1;
    }
}

//===----------------------------------------------------------------------===//
// InsertNewPage: 创建新页面
//===----------------------------------------------------------------------===//

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
