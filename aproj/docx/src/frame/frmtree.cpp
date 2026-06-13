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
// 像 LO 一样：先填满左列到页面底部，然后溢出到右列
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
              << " pPage=" << (pPage ? pPage->GetPhyPageNum() : 0)
              << " pSibling=" << (pSibling ? "yes" : "no")
              << " pPageTop=" << (pPage ? pPage->getFrameArea().Top() : 0)
              << " pPagePrtTop=" << (pPage ? pPage->getFramePrintArea().Top() : 0) << std::endl;

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

    // 计算起始 Y 位置
    SwTwips nBaseY = 0;
    if (pSibling)
        nBaseY = pSibling->getFrameArea().Top() + pSibling->getFrameArea().Height();
    else
        nBaseY = pPage->getFrameArea().Top() + pPage->getFramePrintArea().Top();

    // 列分配：前半部分到左列，后半部分到右列
    // LO 大致按节点数量平分，不是按高度填充
    SwFrame* pColSibling = pSibling;
    std::vector<size_t> leftColIndices, rightColIndices;
    size_t nSplitIdx = (heights.size() + 1) / 2; // 向上取整，左列多一点

    // 分配到左右列
    for (size_t j = 0; j < nSplitIdx; ++j)
        leftColIndices.push_back(j);
    for (size_t j = nSplitIdx; j < heights.size(); ++j)
        rightColIndices.push_back(j);

    // 创建左列 Frame
    SwTwips nCurY = nBaseY;
    for (size_t idx : leftColIndices)
    {
        SwTextNode* pTN = static_cast<SwTextNode*>(rNodes[colNodes[idx]]);
        auto* pFrame = new SwTextFrame(pTN, pParent);
        pFrame->InsertBehind(pParent, pColSibling);
        SwRect aArea(nLeftColX, nCurY, nColWidth, heights[idx]);
        pFrame->setFrameArea(aArea);
        pColSibling = pFrame;
        nCurY += heights[idx];
    }

    // 创建右列 Frame (从同一基 Y 开始)
    nCurY = nBaseY;
    for (size_t idx : rightColIndices)
    {
        SwTextNode* pTN = static_cast<SwTextNode*>(rNodes[colNodes[idx]]);
        auto* pFrame = new SwTextFrame(pTN, pParent);
        pFrame->InsertBehind(pParent, pColSibling);
        SwRect aArea(nRightColX, nCurY, nColWidth, heights[idx]);
        pFrame->setFrameArea(aArea);
        pColSibling = pFrame;
        nCurY += heights[idx];
    }

    // 更新 pSibling 为最后创建的 Frame
    pSibling = pColSibling;

    // 跳过多列节中的所有节点
    i = colNodes.back();
    return true;
}

static SwTwips PreCalcNodeHeight(SwTextNode* pTextNode, int nSection, SwTwips nColWidth)
{
    const std::string* pSize = pTextNode->GetAttr(RES_CHRATR_FONTSIZE);
    const std::string* pFont = pTextNode->GetAttr(RES_CHRATR_FONT);
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

    // 计算行数
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
                SwTwips nLineWidth = fontEngine.MeasureTextWidth(sFontName, nFontSize, sLine);
                if (nLineWidth > nColWidth)
                {
                    size_t nPos = 0;
                    while (nPos < sLine.size())
                    {
                        std::string sRemain = sLine.substr(nPos);
                        int nBreak
                            = fontEngine.FindLineBreak(sFontName, nFontSize, sRemain, nColWidth);
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

    return nSpaceBefore + nLineHeight * nLineCount + nSpaceAfter;
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

    for (SwNodeOffset i = nStt; i <= nEnd; ++i)
    {
        SwNode* pNode = rNodes[i];
        if (!pNode)
            continue;

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

                    // 创建新页面（多列节不分页，在当前页上处理，与 LO 行为一致）
                    if (!bMultiColumn)
                    {
                        SwPageDesc* pDesc = rDoc.GetDefaultPageDesc();
                        pPage = InsertNewPage(pRoot, pDesc);
                        pParent = static_cast<SwLayoutFrame*>(pPage->GetLower());
                        pSibling = nullptr;
                    }

                    // 处理多列布局（如果新节是多列）
                    if (bMultiColumn)
                    {
                        std::cerr << "[MakeFrames] Detected multi-column section "
                                  << nCurrentSection
                                  << " pPage=" << (pPage ? pPage->GetPhyPageNum() : 0) << std::endl;
                        if (ProcessMultiColumnSection(rDoc, rNodes, pPage, pParent, pSibling, i,
                                                      nEnd, nCurrentSection))
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

                    // LO 行为：多列节之后的连续分节符会在新页开始（匹配 LO 输出）
                    if (bPrevMultiCol)
                    {
                        std::cerr << "[MakeFrames] Creating new page after multi-column section"
                                  << std::endl;
                        SwPageDesc* pDesc = rDoc.GetDefaultPageDesc();
                        pPage = InsertNewPage(pRoot, pDesc);
                        pParent = static_cast<SwLayoutFrame*>(pPage->GetLower());
                        pSibling = nullptr;
                    }
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

        MakeFramesForNode(*pNode, pParent, pSibling, nCurrentSection, nCurrentCol);

        // 如果是表格节点，跳过其所有子节点（行、单元格、文本等）
        if (pNode->IsTableNode())
        {
            SwTableNode* pTable = static_cast<SwTableNode*>(pNode);
            SwEndNode* pEnd = pTable->GetEndOfSection();
            if (pEnd)
                i = pEnd->GetIndex();
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
        SwRect aPageRect(0, 0, pDesc->GetPageWidth(), pDesc->GetPageHeight());
        pPage->setFrameArea(aPageRect);

        // 设置打印区域（减去边距）
        SwRect aPrtRect(pDesc->GetLeftMargin(), pDesc->GetTopMargin(),
                        pDesc->GetPageWidth() - pDesc->GetLeftMargin() - pDesc->GetRightMargin(),
                        pDesc->GetPageHeight() - pDesc->GetTopMargin() - pDesc->GetBottomMargin());
        pPage->setFramePrintArea(aPrtRect);
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
    if (rNode.IsTextNode())
    {
        // 创建文本 Frame
        // 对应 LibreOffice：TextFrame 使用页面绝对坐标，宽度 = 页面宽度
        SwTextNode* pTextNode = static_cast<SwTextNode*>(&rNode);
        auto* pFrame = new SwTextFrame(pTextNode, pParent);
        pFrame->InsertBehind(pParent, pSibling);

        // 获取页面尺寸和 Body 宽度（向上查找 PageFrame）
        SwTwips nPageWidth = 11906; // 默认 A4
        SwPageFrame* pPage = nullptr;
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
        const std::string* pSize = pTextNode->GetAttr(RES_CHRATR_FONTSIZE);
        const std::string* pFont = pTextNode->GetAttr(RES_CHRATR_FONT);
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
                    // 测量整行宽度
                    SwTwips nLineWidth = engine.MeasureTextWidth(sFontName, nFontSize, sLine);
                    if (nLineWidth > nPageWidth)
                    {
                        // 需要换行：使用 GetTextBreak 逐段切分
                        size_t nPos = 0;
                        while (nPos < sLine.size())
                        {
                            std::string sRemain = sLine.substr(nPos);
                            int nBreak
                                = engine.FindLineBreak(sFontName, nFontSize, sRemain, nPageWidth);
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
        // DEBUG: trace all frames
        std::cerr << "[MakeFramesForNode] section=" << nSection << " nY=" << nY
                  << " nPageWidth=" << nPageWidth << " pPage=" << (pPage ? "yes" : "no")
                  << " pSibling=" << (pSibling ? "yes" : "no")
                  << " text=" << pTextNode->GetText().substr(0, 30) << std::endl;

        SwRect aFrameArea(nDefaultIndent + nSectLeftMargin, nY, nPageWidth, nTotalHeight);
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
            const auto& rowData = tableData[r];

            // 创建 RowFrame
            auto* pRowFrame = new SwRowFrame(pTabFrame);
            pRowFrame->InsertBehind(pTabFrame, pRowSibling);

            // 估算行高
            SwTwips nRowHeight = rowData.height > 0 ? rowData.height : 276;
            SwRect aRowRect(0, nRowY - nTabY, nTableWidth, nRowHeight);
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

                // 计算单元格宽度
                SwTwips nCellWidth = nTableWidth / rowData.cells.size();
                if (!gridCols.empty() && c < gridCols.size())
                {
                    // 按比例缩放 gridCols 到表格宽度
                    SwTwips nTotalGrid = 0;
                    for (auto gw : gridCols)
                        nTotalGrid += gw;
                    if (nTotalGrid > 0)
                        nCellWidth = nTableWidth * gridCols[c] / nTotalGrid;
                    else
                        nCellWidth = gridCols[c];
                }

                SwRect aCellRect(nCellX, 0, nCellWidth, nRowHeight);
                pCellFrame->setFrameArea(aCellRect);
                pCellFrame->setFramePrintArea(aCellRect);

                // 为单元格文本创建 TextFrame
                if (!cellData.text.empty())
                {
                    // 创建临时文本节点存储单元格内容
                    SwTextFormatColl* pColl = pTableNode->GetDoc().GetDefaultTextFormatColl();
                    SwNodes& rNodes = pTableNode->GetDoc().GetNodes();
                    SwTextNode* pCellTextNode = rNodes.MakeTextNode(*pTableNode, pColl);
                    pCellTextNode->SetText(cellData.text);

                    auto* pTextFrame = new SwTextFrame(pCellTextNode, pCellFrame);
                    pTextFrame->InsertBehind(pCellFrame, nullptr);

                    // 表格单元格文本 Frame 使用 Body 的绝对位置
                    // body.printArea 是 (0,0)，需要用 body.frameArea.Left/Top 获取绝对坐标
                    SwTwips nBodyLeft = pParent->getFrameArea().Left();
                    SwTwips nBodyTop = pParent->getFrameArea().Top();
                    const SwTwips nDefaultIndent = 284;
                    SwRect aTextRect(nBodyLeft + nDefaultIndent + nCellX, nRowY, nCellWidth,
                                     nRowHeight);
                    pTextFrame->setFrameArea(aTextRect);
                }

                nCellX += nCellWidth;
                pCellSibling = pCellFrame;
            }

            nRowY += nRowHeight;
            pRowSibling = pRowFrame;
        }

        // 更新 TabFrame 高度
        SwTwips nTotalHeight = nRowY - nTabY;
        aTabRect.SetHeight(nTotalHeight);
        pTabFrame->setFrameArea(aTabRect);
    }
    else if (rNode.IsStartNode())
    {
        // 节区开始，可能需要创建子布局
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
        SwRect aPageRect(0, 0, pDesc->GetPageWidth(), pDesc->GetPageHeight());
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
