// Frame 树构建实现，对应 LibreOffice 的 sw/source/core/layout/frmtool.cxx

#include "frmtree.h"
#include "../core/node.h"
#include "../core/ndarr.h"
#include "../core/doc.h"
#include "../core/format.h"
#include "../font/font_engine.h"
#include <cassert>
#include <iostream>

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
            pRightSibling = pFrame;
            pSibling = pFrame;
            nCurY += heights[idx];
        }
    }

    // 更新循环索引
    i = colNodes.back();

    return true;
}

static SwTwips PreCalcNodeHeight(SwTextNode* pTextNode, int nSection, SwTwips nColWidth)
{
    // 行高应使用段落标记字体（w:pPr/w:rPr），而非内容字体
    const std::string* pSize = pTextNode->GetAttr(RES_CHRATR_FONTSIZE_PARA_MARK);
    const std::string* pFont = pTextNode->GetAttr(RES_CHRATR_FONT_PARA_MARK);
    if (!pSize)
        pSize = pTextNode->GetAttr(RES_CHRATR_FONTSIZE);
    if (!pFont)
        pFont = pTextNode->GetAttr(RES_CHRATR_FONT);
    int nFontSize = pSize ? std::stoi(*pSize) : 20;
    std::string sFontName = pFont ? *pFont : "Calibri";

    FontEngine& fontEngine = FontEngine::Instance();
    int nMeasuredHeight = fontEngine.MeasureTextHeight(sFontName, nFontSize);
    SwTwips nLineHeight = nMeasuredHeight > 0 ? static_cast<SwTwips>(nMeasuredHeight)
                                              : static_cast<SwTwips>(nFontSize * 14.1);

    const std::string* pLineSpacing = pTextNode->GetAttr(RES_PARATR_LINESPACING);
    if (pLineSpacing)
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

    // 计算行数（使用内容字体进行宽度测量）
    const std::string* pContentFont = pTextNode->GetAttr(RES_CHRATR_FONT);
    const std::string* pContentSize = pTextNode->GetAttr(RES_CHRATR_FONTSIZE);
    std::string sContentFontName = pContentFont ? *pContentFont : "Calibri";
    int nContentFontSize = pContentSize ? std::stoi(*pContentSize) : 20;

    int nLineCount = 1;
    const std::string& rText = pTextNode->GetText();
    if (!rText.empty() && nColWidth > 0)
    {
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
        nLineCount = nTextLines;
    }

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

    SwTwips nTotal = nSpaceBefore + nLineHeight * nLineCount + nSpaceAfter;
    fprintf(stderr,
            "[PreCalcNodeHeight] font=%s size=%d lineH=%d lineSpacing=%s lines=%d spaceBefore=%d "
            "spaceAfter=%d total=%d text=\"%.30s\"\n",
            sFontName.c_str(), nFontSize, nLineHeight,
            pLineSpacing ? pLineSpacing->c_str() : "none", nLineCount, nSpaceBefore, nSpaceAfter,
            nTotal, rText.c_str());
    return nTotal;
}

//===----------------------------------------------------------------------===//
// MakeFrames: 为节点范围创建 Frame 树
//===----------------------------------------------------------------------===//

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
                        nCurrentSection++;
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
                    pParent = static_cast<SwLayoutFrame*>(pPage->GetLower());
                    pSibling = nullptr;
                    std::cerr << "[MakeFrames] SECTION BREAK: new page="
                              << (pPage ? pPage->GetPhyPageNum() : 0)
                              << " pParent=" << (pParent ? "yes" : "no")
                              << " bMultiColumn=" << bMultiColumn << std::endl;

                    // 节分隔节点本身不创建 Frame（匹配 LO 行为）
                    // 跳过 MakeFramesForNode，直接处理多列布局或继续循环
                    if (bMultiColumn)
                    {
                        std::cerr << "[MakeFrames] Detected multi-column section "
                                  << nCurrentSection
                                  << " pPage=" << (pPage ? pPage->GetPhyPageNum() : 0) << std::endl;
                        bool bHandled = ProcessMultiColumnSection(
                            rDoc, rNodes, pPage, pParent, pSibling, i, nEnd, nCurrentSection);
                        pPage = pRoot->GetLastPage();
                        pParent = static_cast<SwLayoutFrame*>(pPage->GetLower());
                        if (bHandled)
                            continue;
                    }
                    continue; // 节分隔节点不创建 Frame
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
                        pParent = static_cast<SwLayoutFrame*>(pPage->GetLower());
                        bool bHandled = ProcessMultiColumnSection(
                            rDoc, rNodes, pPage, pParent, pSibling, i, nEnd, nCurrentSection);
                        pPage = pRoot->GetLastPage();
                        pParent = static_cast<SwLayoutFrame*>(pPage->GetLower());
                        // 更新 pSibling 为左列溢出页 Body 的最后一个 Frame
                        pSibling = pParent->GetLower();
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
                            pParent = static_cast<SwLayoutFrame*>(pPage->GetLower());
                            pSibling = nullptr;
                        }
                        else
                        {
                            // 单列→单列：在同一页继续
                            pPage = pRoot->GetLastPage();
                            pParent = static_cast<SwLayoutFrame*>(pPage->GetLower());
                            // 设置 pSibling 为当前页 Body 的最后一个子 Frame
                            pSibling = pParent->GetLower();
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

        // 溢出预检测：在创建 Frame 前检查是否需要新页面
        // 使用 Body frame 的打印区域高度（考虑页面边距），而不是整个页面高度
        if (pNode->IsTextNode() && pPage && pSibling)
        {
            SwTwips nFrameBottom
                = pSibling->getFrameArea().Top() + pSibling->getFrameArea().Height();
            // Body frame 的底部 = Body 顶部 + Body 打印区域高度
            SwLayoutFrame* pBody = static_cast<SwLayoutFrame*>(pPage->GetLower());
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
                          << " pPage=" << pPage->GetPhyPageNum() << std::endl;
                SwPageDesc* pDesc = rDoc.GetDefaultPageDesc();
                pPage = InsertNewPage(pRoot, pDesc);
                pParent = static_cast<SwLayoutFrame*>(pPage->GetLower());
                pSibling = nullptr;
            }
        }

        std::cerr << "[MakeFrames] Calling MakeFramesForNode i=" << i
                  << " pPage=" << (pPage ? pPage->GetPhyPageNum() : 0)
                  << " pParent=" << (pParent ? "yes" : "no")
                  << " pSibling=" << (pSibling ? "yes" : "no") << std::endl;
        MakeFramesForNode(*pNode, pParent, pSibling, nCurrentSection, nCurrentCol);

        // 如果是表格节点，跳过其所有子节点（行、单元格、文本等）
        if (pNode->IsTableNode())
        {
            SwTableNode* pTable = static_cast<SwTableNode*>(pNode);
            SwEndNode* pEnd = pTable->GetEndOfSection();
            if (pEnd)
            {
                i = pEnd->GetIndex();
                // MakeFramesForNode 创建了单元格文本节点，导致索引偏移
                // 需要更新 nEnd 以包含位移后的后续节点
                SwNodeOffset nNewEnd = rNodes.Count() - SwNodeOffset(1);
                if (nNewEnd > nEnd)
                {
                    std::cerr << "[MakeFrames] nEnd updated: " << nEnd << " -> " << nNewEnd
                              << std::endl;
                    nEnd = nNewEnd;
                }
            }
        }

        // 如果是节节点，跳过其所有子节点
        // 对应 LO: InsertCnt_ 中 IsSectionNode() 分支递归处理节内容
        if (pNode->IsSectionNode())
        {
            SwSectionNode* pSection = static_cast<SwSectionNode*>(pNode);
            SwEndNode* pEnd = pSection->GetEndOfSection();
            if (pEnd)
            {
                i = pEnd->GetIndex();
            }
        }

        // 更新 pSibling 为最后一个创建的 Frame
        if (pParent->GetLower())
        {
            pSibling = pParent->GetLower();
            while (pSibling->GetNext())
            {
                pSibling = pSibling->GetNext();
            }
        }

        // DEBUG: trace loop iterations
        if (i >= 145)
            std::cerr << "[MakeFrames] LOOP_END i=" << i << " nEnd=" << nEnd
                      << " isText=" << pNode->IsTextNode() << " isTable=" << pNode->IsTableNode()
                      << " isStart=" << pNode->IsStartNode() << std::endl;
    }
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

        // 获取页面尺寸和 Body 宽度
        SwTwips nPageWidth = 11906; // 默认 A4
        {
            SwFrame* pF = pParent;
            while (pF && !pF->IsPageFrame())
                pF = pF->GetUpper();
            if (pF)
            {
                pPage = static_cast<SwPageFrame*>(pF);
                // 使用 Body 的打印区域宽度（考虑页面边距），而不是整个页面宽度
                // LibreOffice 的 TextFrame 宽度 = Body 打印区域宽度
                SwLayoutFrame* pBody = static_cast<SwLayoutFrame*>(pPage->GetLower());
                if (pBody)
                    nPageWidth = pBody->getFramePrintArea().Width();
                else
                    nPageWidth = pPage->getFrameArea().Width();
            }
        }

        // 多列布局：查找当前节的列设置
        {
            const SwDoc::SectionMargins* pSectMargins
                = pTextNode->GetDoc().GetSectionMargins(nSection);
            if (pSectMargins && pSectMargins->numCols > 1)
            {
                if (pSectMargins->colWidth > 0)
                {
                    // 使用显式列宽定义
                    nPageWidth = pSectMargins->colWidth;
                }
                else
                {
                    // 计算列宽：(Body宽 - 列间距 × (列数-1)) / 列数
                    SwTwips nTotalSpace = pSectMargins->colSpace * (pSectMargins->numCols - 1);
                    nPageWidth = (nPageWidth - nTotalSpace) / pSectMargins->numCols;
                }
            }
        }

        // 估算文本高度：基于字体度量的行高计算
        // 使用 FontEngine 获取精确字体度量（对应 LO 的 SwFntObj::GetFontHeight）
        // 行高应使用段落标记字体（w:pPr/w:rPr），而非内容字体
        // 对应 LO 中 APPLY_PARAGRAPH_MARK_FORMAT_TO_EMPTY_LINE_AT_END_OF_PARAGRAPH
        const std::string* pSize = pTextNode->GetAttr(RES_CHRATR_FONTSIZE_PARA_MARK);
        const std::string* pFont = pTextNode->GetAttr(RES_CHRATR_FONT_PARA_MARK);
        if (!pSize)
            pSize = pTextNode->GetAttr(RES_CHRATR_FONTSIZE);
        if (!pFont)
            pFont = pTextNode->GetAttr(RES_CHRATR_FONT);
        int nFontSize = pSize ? std::stoi(*pSize) : 20; // 半点
        std::string sFontName = pFont ? *pFont : "Calibri";

        // 使用 FontEngine 获取精确行高（已返回 twips）
        FontEngine& fontEngine = FontEngine::Instance();
        int nMeasuredHeight = fontEngine.MeasureTextHeight(sFontName, nFontSize);

        SwTwips nLineHeight = nMeasuredHeight > 0
                                  ? static_cast<SwTwips>(nMeasuredHeight)
                                  : static_cast<SwTwips>(nFontSize * 14.1); // 后备估算
        const std::string* pLineSpacing = pTextNode->GetAttr(RES_PARATR_LINESPACING);
        if (pLineSpacing)
        {
            try
            {
                int nLineSpacing = std::stoi(*pLineSpacing);
                if (nLineSpacing > 0 && nLineSpacing != 240)
                {
                    // 调整行高：lineSpacing/240 * baseHeight
                    nLineHeight = nLineHeight * nLineSpacing / 240;
                }
            }
            catch (...)
            {
            }
        }

        // 计算文本行数（考虑自动换行）
        // 使用 FontEngine 进行精确字形宽度测量（对应 LO 的 VCL GetTextBreak）
        // 宽度测量使用内容字体（RES_CHRATR_FONT），而非段落标记字体
        const std::string* pContentFont = pTextNode->GetAttr(RES_CHRATR_FONT);
        const std::string* pContentSize = pTextNode->GetAttr(RES_CHRATR_FONTSIZE);
        std::string sContentFontName = pContentFont ? *pContentFont : "Calibri";
        int nContentFontSize = pContentSize ? std::stoi(*pContentSize) : 20;

        int nLineCount = 1;
        const std::string& rText = pTextNode->GetText();
        if (!rText.empty() && nPageWidth > 0)
        {
            FontEngine& engine = FontEngine::Instance();

            // 逐行计算：找到每行的断点，累加行数
            int nTextLines = 1;
            size_t nStart = 0;
            while (nStart < rText.size())
            {
                // 查找下一个换行符
                size_t nNewline = rText.find('\n', nStart);
                std::string sLine;
                if (nNewline != std::string::npos)
                {
                    sLine = rText.substr(nStart, nNewline - nStart);
                }
                else
                {
                    sLine = rText.substr(nStart);
                }

                if (!sLine.empty())
                {
                    // 测量整行宽度（使用内容字体）
                    SwTwips nLineWidth
                        = engine.MeasureTextWidth(sContentFontName, nContentFontSize, sLine);
                    if (nLineWidth > nPageWidth)
                    {
                        // 需要换行：使用 GetTextBreak 逐段切分
                        size_t nPos = 0;
                        while (nPos < sLine.size())
                        {
                            std::string sRemain = sLine.substr(nPos);
                            int nBreak = engine.FindLineBreak(sContentFontName, nContentFontSize,
                                                              sRemain, nPageWidth);
                            if (nBreak < 0 || nBreak >= static_cast<int>(sRemain.size()))
                            {
                                // 剩余文本都能放下
                                break;
                            }
                            if (nBreak == 0)
                                nBreak = 1; // 至少前进一个字符
                            nPos += nBreak;
                            nTextLines++;
                        }
                    }
                }

                if (nNewline != std::string::npos)
                {
                    nTextLines++; // 换行符本身占一行
                    nStart = nNewline + 1;
                }
                else
                {
                    break;
                }
            }
            nLineCount = nTextLines;
        }

        // 获取段落间距（space-before 和 space-after）
        // 对应 LibreOffice 的 SwTextFrame::Format() 中的 CalcUpperSpace / CalcLowerSpace
        const std::string* pSpaceBefore = pTextNode->GetAttr(RES_UL_SPACE);
        const std::string* pSpaceAfter = pTextNode->GetAttr(RES_UL_SPACE_AFTER);
        SwTwips nSpaceBefore = 0;
        SwTwips nSpaceAfter = 0;
        if (pSpaceBefore)
        {
            try
            {
                nSpaceBefore = std::stoi(*pSpaceBefore);
            }
            catch (...)
            {
                nSpaceBefore = 0;
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
                nSpaceAfter = 0;
            }
        }

        // 应用行数到高度（包含段落间距）
        // 对应 LO 的 SwTextFrame frame 高度 = CalcUpperSpace + 文本行高 + CalcLowerSpace
        SwTwips nTotalHeight = nSpaceBefore + nLineHeight * nLineCount + nSpaceAfter;

        // 计算 Y 位置：紧跟在前一个 Frame 之后
        // LibreOffice 的 TextFrame frameArea 从页面顶部开始（不是从边距开始）
        SwTwips nY = 0;
        if (pSibling)
        {
            nY = pSibling->getFrameArea().Top() + pSibling->getFrameArea().Height();
        }
        else
        {
            // 第一个子 Frame，从页面打印区域顶部开始
            // LibreOffice 的 TextFrame y = 页面frameArea.Top + 打印区域.Top
            // 段落间距已包含在 frame 高度中，不加到 Y 位置
            nY = pPage ? pPage->getFrameArea().Top() + pPage->getFramePrintArea().Top() : 0;
            std::cerr << "[MakeFramesForNode] FIRST_FRAME: pPage=" << (pPage ? "yes" : "no")
                      << " frameTop=" << (pPage ? pPage->getFrameArea().Top() : -1)
                      << " prtTop=" << (pPage ? pPage->getFramePrintArea().Top() : -1)
                      << " nY=" << nY << std::endl;
        }

        // TextFrame: x = 默认段落缩进(284)
        // LibreOffice 的 TextFrame frameArea 的 x 只包含默认缩进
        // 段落级缩进 (w:ind w:left) 在 LO 中作为文本内部边距处理，不影响 frame 位置
        // TextFrame x = 默认缩进(284) + 节左边距
        // LO 在帧位置中包含节的左边距
        const SwTwips nDefaultIndent = 284;
        SwTwips nSectLeftMargin = 0;
        const SwDoc::SectionMargins* pSectM = pTextNode->GetDoc().GetSectionMargins(nSection);
        if (pSectM)
            nSectLeftMargin = pSectM->left;

        // 当节左边距等于 nDefaultIndent（section 0 特殊情况，最小边距为 284），
        // 只使用节左边距作为 x 位置，避免 double-counting
        // 当节左边距大于 nDefaultIndent 时，x = nDefaultIndent + 节左边距
        // 对应 LO：section 0 的帧 x = 页面边距(284)，其他 section 在默认缩进上叠加节边距
        SwTwips nFrameX = (nSectLeftMargin <= nDefaultIndent) ? nSectLeftMargin
                                                              : (nDefaultIndent + nSectLeftMargin);

        // 帧宽度：section 0 使用页面全宽，其他 section 使用可打印区域宽度
        // 对应 LO：section 0 帧宽度 = 页面宽度(11906)，其他 section 帧宽度 = 可打印宽度
        SwTwips nFrameWidth
            = (nSectLeftMargin <= nDefaultIndent) ? pPage->getFrameArea().Width() : nPageWidth;

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

            // 计算 Y 位置
            SwTwips nY = 0;
            if (pSibling)
                nY = pSibling->getFrameArea().Top() + pSibling->getFrameArea().Height();
            else
                nY = pPage ? pPage->getFrameArea().Top() + pPage->getFramePrintArea().Top() : 0;

            // 获取 Body 宽度
            SwTwips nPageWidth = 11906;
            if (pPage)
            {
                SwLayoutFrame* pBody = static_cast<SwLayoutFrame*>(pPage->GetLower());
                if (pBody)
                    nPageWidth = pBody->getFramePrintArea().Width();
            }

            const SwTwips nDefaultIndent = 284;
            // 默认图片/OLE 高度（在无实际图片数据时使用）
            SwTwips nDefaultHeight = 1440; // 1 inch

            SwRect aFrameArea(nDefaultIndent, nY, nPageWidth - nDefaultIndent, nDefaultHeight);
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
