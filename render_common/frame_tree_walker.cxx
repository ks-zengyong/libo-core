/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * 共享的 Frame 树遍历器 — 实现
 *
 * 公共模块: render_common/ — sw 和 aproj/docx 共用
 */

#include "frame_tree_walker.h"

// 前序遍历：先处理当前节点，再递归子节点
void WalkFrameTreeAndLog(IFrameNode* pNode, RenderInstructionSink& rSink)
{
    if (!pNode)
        return;

    int x, y, w, h;
    int pageNum = pNode->GetPageNum();

    switch (pNode->GetNodeType())
    {
        case FrameNodeType::Page:
            pNode->GetRect(x, y, w, h);
            BuildPageStartInstruction(rSink, pageNum, w, h);
            break;

        case FrameNodeType::Text:
            pNode->GetRect(x, y, w, h);
            BuildTextFrameInstruction(rSink, pageNum, x, y, w, h, pNode->GetText(),
                                      pNode->GetTextLen(), pNode->GetFontName(),
                                      pNode->GetFontSize(), pNode->GetFontColor(),
                                      pNode->GetFontWeight(), pNode->GetFontItalic(),
                                      pNode->GetStyleName());
            break;

        case FrameNodeType::NoText:
            pNode->GetRect(x, y, w, h);
            BuildImageFrameInstruction(rSink, pageNum, x, y, w, h);
            break;

        case FrameNodeType::Table:
            pNode->GetRect(x, y, w, h);
            BuildTableFrameInstruction(rSink, pageNum, x, y, w, h);
            break;

        case FrameNodeType::TabRow:
            pNode->GetRect(x, y, w, h);
            BuildTableRowInstruction(rSink, pageNum, x, y, w, h);
            break;

        case FrameNodeType::TabCell:
            pNode->GetRect(x, y, w, h);
            BuildTableCellInstruction(rSink, pageNum, x, y, w, h);
            break;

        case FrameNodeType::Section:
            pNode->GetRect(x, y, w, h);
            BuildSectionFrameInstruction(rSink, pageNum, x, y, w, h);
            break;

        case FrameNodeType::Column:
            pNode->GetRect(x, y, w, h);
            BuildColumnFrameInstruction(rSink, pageNum, x, y, w, h);
            break;

        case FrameNodeType::Header:
            pNode->GetRect(x, y, w, h);
            BuildHeaderFrameInstruction(rSink, pageNum, x, y, w, h);
            break;

        case FrameNodeType::Footer:
            pNode->GetRect(x, y, w, h);
            BuildFooterFrameInstruction(rSink, pageNum, x, y, w, h);
            break;

        case FrameNodeType::FootnoteCont:
            pNode->GetRect(x, y, w, h);
            BuildFootnoteContFrameInstruction(rSink, pageNum, x, y, w, h);
            break;

        case FrameNodeType::Footnote:
            pNode->GetRect(x, y, w, h);
            BuildFootnoteFrameInstruction(rSink, pageNum, x, y, w, h);
            break;

        case FrameNodeType::Fly:
            pNode->GetRect(x, y, w, h);
            BuildFlyFrameInstruction(rSink, pageNum, x, y, w, h);
            break;

        case FrameNodeType::Body:
        case FrameNodeType::Unknown:
        default:
            // Body 和未知类型：仅递归子节点，不生成指令
            break;
    }

    // 递归子节点
    for (IFrameNode* pChild = pNode->GetFirstChild(); pChild;
         pChild = pChild->GetNextSibling())
    {
        WalkFrameTreeAndLog(pChild, rSink);
    }

    // Page 结束
    if (pNode->GetNodeType() == FrameNodeType::Page)
    {
        BuildPageEndInstruction(rSink, pageNum);
    }
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */