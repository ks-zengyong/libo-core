// Frame 树构建实现，对应 LibreOffice 的 sw/source/core/layout/frmtool.cxx

#include "frmtree.h"
#include "../core/node.h"
#include "../core/ndarr.h"
#include "../core/doc.h"
#include "../core/format.h"
#include <cassert>
#include <iostream>

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
                if (*pBreak == "page" || *pBreak == "section")
                {
                    // 节分隔 = 分页
                    if (*pBreak == "section")
                    {
                        nCurrentSection++;
                        const SwDoc::SectionMargins* pMargins
                            = rDoc.GetSectionMargins(nCurrentSection);
                        if (pMargins)
                        {
                            SwPageDesc* pDesc = rDoc.GetDefaultPageDesc();
                            pDesc->SetTopMargin(pMargins->top);
                            pDesc->SetBottomMargin(pMargins->bottom);
                            pDesc->SetLeftMargin(pMargins->left);
                            pDesc->SetRightMargin(pMargins->right);
                        }
                    }
                    // 创建新页面
                    SwPageDesc* pDesc = rDoc.GetDefaultPageDesc();
                    pPage = InsertNewPage(pRoot, pDesc);
                    pParent = static_cast<SwLayoutFrame*>(pPage->GetLower());
                    pSibling = nullptr;
                }
                else if (*pBreak == "continuous")
                {
                    // 连续节分隔：不分页，但更新节索引（列布局等会变化）
                    nCurrentSection++;
                    const SwDoc::SectionMargins* pMargins = rDoc.GetSectionMargins(nCurrentSection);
                    if (pMargins)
                    {
                        SwPageDesc* pDesc = rDoc.GetDefaultPageDesc();
                        pDesc->SetTopMargin(pMargins->top);
                        pDesc->SetBottomMargin(pMargins->bottom);
                        pDesc->SetLeftMargin(pMargins->left);
                        pDesc->SetRightMargin(pMargins->right);
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
        // LibreOffice 使用实际字体度量 (ascent + descent + leading)
        // 这里使用字体特定的比率进行估算
        const std::string* pSize = pTextNode->GetAttr(RES_CHRATR_FONTSIZE);
        const std::string* pFont = pTextNode->GetAttr(RES_CHRATR_FONT);
        int nFontSize = pSize ? std::stoi(*pSize) : 20; // 半点

        // 字体特定的行高比率（从 LibreOffice 输出校准）
        double nHeightRatio = 14.1; // 默认
        if (pFont)
        {
            if (pFont->find("Emoji") != std::string::npos)
                nHeightRatio = 18.39;
            else if (pFont->find("Poppins Medium") != std::string::npos)
                nHeightRatio = 14.11;
            else if (pFont->find("Poppins SemiBold") != std::string::npos)
                nHeightRatio = 14.10;
            else if (pFont->find("Poppins") != std::string::npos)
                nHeightRatio = 13.75;
            else if (pFont->find("Segoe UI Semibold") != std::string::npos)
                nHeightRatio = 14.11;
            else if (pFont->find("fony family") != std::string::npos)
                nHeightRatio = 14.54;
            else if (pFont->find("Calibri") != std::string::npos)
                nHeightRatio = 14.11;
        }

        // 应用行距规则
        // OOXML w:line=240 表示单倍行距，w:line=204 表示约 0.85 倍行距
        SwTwips nLineHeight = static_cast<SwTwips>(nFontSize * nHeightRatio);
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
        // 最小行高：确保至少有基本高度
        if (nLineHeight < 200)
            nLineHeight = 200;

        // 计算文本行数（考虑自动换行）
        // LibreOffice 使用实际字体度量计算换行，这里使用字符宽度估算
        int nLineCount = 1;
        const std::string& rText = pTextNode->GetText();
        if (!rText.empty() && nPageWidth > 0)
        {
            // 估算字符宽度：字体大小 × 比率（从 LO 输出校准）
            // 大多数字体的平均字符宽度约为字体大小的 0.45-0.55 倍
            double nCharWidth = nFontSize * 0.50 * 10; // 半点→twips × 比率
            if (nCharWidth > 0)
            {
                // 计算每行可容纳的字符数
                int nCharsPerLine = static_cast<int>(nPageWidth / nCharWidth);
                if (nCharsPerLine < 1)
                    nCharsPerLine = 1;

                // 计算文本行数（考虑换行符）
                int nTextLines = 1;
                int nCurrentLineLen = 0;
                for (char c : rText)
                {
                    if (c == '\n' || c == '\f' || c == '\v')
                    {
                        nTextLines++;
                        nCurrentLineLen = 0;
                    }
                    else
                    {
                        nCurrentLineLen++;
                        if (nCurrentLineLen >= nCharsPerLine)
                        {
                            nTextLines++;
                            nCurrentLineLen = 0;
                        }
                    }
                }
                nLineCount = nTextLines;
            }
        }

        // 应用行数到高度
        SwTwips nTotalHeight = nLineHeight * nLineCount;

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
            // LibreOffice 的 TextFrame y = 页面frameArea.Top + 打印区域.Top + CalcUpperSpace
            // CalcUpperSpace 在页面顶部添加段落 space-before 偏移
            nY = pPage ? pPage->getFrameArea().Top() + pPage->getFramePrintArea().Top() : 0;
            // 添加默认段落间距偏移（对应 LibreOffice 的 CalcUpperSpace）
            // 这个值来自段落的 space-before 属性，由 LO 的 SwFlowFrame::CalcUpperSpace 计算
            const std::string* pSpaceBefore = pTextNode->GetAttr(RES_UL_SPACE);
            SwTwips nSpaceBefore = 284; // 默认值（0.5cm）
            if (pSpaceBefore)
            {
                try
                {
                    nSpaceBefore = std::stoi(*pSpaceBefore);
                }
                catch (...)
                {
                    nSpaceBefore = 284;
                }
            }
            nY += nSpaceBefore;
        }

        // TextFrame: x = 默认段落缩进(284)
        // LibreOffice 的 TextFrame frameArea 的 x 只包含默认缩进
        // 段落级缩进 (w:ind w:left) 在 LO 中作为文本内部边距处理，不影响 frame 位置
        // TextFrame x = 默认缩进(284) + 节左边距 + 列偏移
        // LO 在帧位置中包含节的左边距和列偏移
        const SwTwips nDefaultIndent = 284;
        SwTwips nSectLeftMargin = 0;
        SwTwips nColOffset = 0;
        const SwDoc::SectionMargins* pSectM = pTextNode->GetDoc().GetSectionMargins(nSection);
        if (pSectM)
        {
            nSectLeftMargin = pSectM->left;
            // 多列布局：列偏移 = 列索引 × (列宽 + 列间距)
            if (pSectM->numCols > 1 && pSectM->colWidth > 0 && nCol > 0)
            {
                nColOffset = static_cast<SwTwips>(nCol) * (pSectM->colWidth + pSectM->colSpace);
            }
        }
        SwRect aFrameArea(nDefaultIndent + nSectLeftMargin + nColOffset, nY, nPageWidth,
                          nTotalHeight);
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
                    nCellWidth = gridCols[c];

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

                    SwRect aTextRect(0, 0, nCellWidth, nRowHeight);
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
