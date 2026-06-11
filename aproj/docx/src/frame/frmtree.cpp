// Frame 树构建实现，对应 LibreOffice 的 sw/source/core/layout/frmtool.cxx

#include "frmtree.h"
#include "../core/node.h"
#include "../core/ndarr.h"
#include "../core/doc.h"
#include "../core/format.h"
#include <cassert>

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
        auto* pBody = new SwBodyFrame(pPage);
        pBody->InsertBehind(pPage, nullptr);
        pParent = pBody;
    }

    SwFrame* pSibling = nullptr;

    // 遍历节点范围
    SwNodeOffset nStt = rSttIdx.GetIndex();
    SwNodeOffset nEnd = rEndIdx.GetIndex();

    for (SwNodeOffset i = nStt; i <= nEnd; ++i)
    {
        SwNode* pNode = rNodes[i];
        if (!pNode)
            continue;

        MakeFramesForNode(*pNode, pParent, pSibling);

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
    pPage->PreparePage();

    // 设置页面尺寸
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

void MakeFramesForNode(SwNode& rNode, SwLayoutFrame* pParent, SwFrame* pSibling)
{
    if (rNode.IsTextNode())
    {
        // 创建文本 Frame
        SwTextNode* pTextNode = static_cast<SwTextNode*>(&rNode);
        auto* pFrame = new SwTextFrame(pTextNode, pParent);
        pFrame->InsertBehind(pParent, pSibling);
    }
    else if (rNode.IsTableNode())
    {
        // 创建表格 Frame（简化版）
        // 实际实现会更复杂
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
    pPage->PreparePage();

    // 设置页面尺寸
    if (pDesc)
    {
        SwRect aPageRect(0, 0, pDesc->GetPageWidth(), pDesc->GetPageHeight());
        pPage->setFrameArea(aPageRect);

        SwRect aPrtRect(pDesc->GetLeftMargin(), pDesc->GetTopMargin(),
                        pDesc->GetPageWidth() - pDesc->GetLeftMargin() - pDesc->GetRightMargin(),
                        pDesc->GetPageHeight() - pDesc->GetTopMargin() - pDesc->GetBottomMargin());
        pPage->setFramePrintArea(aPrtRect);
    }

    // 插入到根 Frame
    SwPageFrame* pLastPage = pRoot->GetLastPage();
    pPage->InsertBehind(pRoot, pLastPage);
    pRoot->SetLastPage(pPage);
    pRoot->SetPageNum(nPageNum);

    return pPage;
}
