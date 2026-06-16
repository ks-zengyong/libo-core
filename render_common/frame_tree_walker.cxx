/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * 共享的 Frame 树遍历器 — 实现
 *
 * 遍历规则：
 *   1) 对每个节点：
 *      a) 如果是容器型 (Page/Section/Column/Table/TabRow/TabCell/
 *         Header/Footer/FootnoteCont/Fly)，输出 <TYPE>_START；
 *         否则输出该类型本身的单条指令 (TEXT_FRAME / IMAGE_FRAME 等)。
 *      b) 递归主链 GetFirstChild() -> ... -> GetNextSibling()。
 *      c) 递归浮动对象链 GetFirstFly() -> ... -> GetNextSiblingFly()，
 *         每条浮动对象也走同样规则（FLY_START + 递归内容 + FLY_END）。
 *      d) 如果是容器型节点，输出 <TYPE>_END，与 START 对应。
 *      e) Body 类型是特殊容器：它不产生 START/END 指令，只用于
 *         把内部的 content 推进一级。
 *   2) 递归子节点时使用 nestLevel + 1，保证 START/END 对齐缩进。
 *   3) 根节点 (WalkFrameTreeAndLog 入口) 一般是 Page，nestLevel 为 0。
 *
 * 公共模块: render_common/ — sw 和 aproj/docx 共用
 */

#include "frame_tree_walker.h"

namespace
{
// 输出某个容器型节点的 START 指令
void EmitContainerStart(FrameNodeType type, RenderInstructionSink& rSink, int pageNum,
                        int nestLevel, int x, int y, int w, int h)
{
    switch (type)
    {
        case FrameNodeType::Page:
            BuildPageStartInstruction(rSink, pageNum, nestLevel, w, h);
            break;
        case FrameNodeType::Body:
            // Body 不产生 START/END，避免文档里多一层无意义块
            break;
        case FrameNodeType::Header:
            BuildHeaderStartInstruction(rSink, pageNum, nestLevel, x, y, w, h);
            break;
        case FrameNodeType::Footer:
            BuildFooterStartInstruction(rSink, pageNum, nestLevel, x, y, w, h);
            break;
        case FrameNodeType::Section:
            BuildSectionStartInstruction(rSink, pageNum, nestLevel, x, y, w, h);
            break;
        case FrameNodeType::Column:
            BuildColumnStartInstruction(rSink, pageNum, nestLevel, x, y, w, h);
            break;
        case FrameNodeType::Table:
            BuildTableStartInstruction(rSink, pageNum, nestLevel, x, y, w, h);
            break;
        case FrameNodeType::TabRow:
            BuildTableRowStartInstruction(rSink, pageNum, nestLevel, x, y, w, h);
            break;
        case FrameNodeType::TabCell:
            BuildTableCellStartInstruction(rSink, pageNum, nestLevel, x, y, w, h);
            break;
        case FrameNodeType::FootnoteCont:
            BuildFootnoteContStartInstruction(rSink, pageNum, nestLevel, x, y, w, h);
            break;
        case FrameNodeType::Fly:
            BuildFlyStartInstruction(rSink, pageNum, nestLevel, x, y, w, h);
            break;
        default:
            break;
    }
}

// 输出某个容器型节点的 END 指令
void EmitContainerEnd(FrameNodeType type, RenderInstructionSink& rSink, int pageNum, int nestLevel)
{
    switch (type)
    {
        case FrameNodeType::Page:
            BuildPageEndInstruction(rSink, pageNum, nestLevel);
            break;
        case FrameNodeType::Body:
            // Body 不输出 END
            break;
        case FrameNodeType::Header:
            BuildHeaderEndInstruction(rSink, pageNum, nestLevel);
            break;
        case FrameNodeType::Footer:
            BuildFooterEndInstruction(rSink, pageNum, nestLevel);
            break;
        case FrameNodeType::Section:
            BuildSectionEndInstruction(rSink, pageNum, nestLevel);
            break;
        case FrameNodeType::Column:
            BuildColumnEndInstruction(rSink, pageNum, nestLevel);
            break;
        case FrameNodeType::Table:
            BuildTableEndInstruction(rSink, pageNum, nestLevel);
            break;
        case FrameNodeType::TabRow:
            BuildTableRowEndInstruction(rSink, pageNum, nestLevel);
            break;
        case FrameNodeType::TabCell:
            BuildTableCellEndInstruction(rSink, pageNum, nestLevel);
            break;
        case FrameNodeType::FootnoteCont:
            BuildFootnoteContEndInstruction(rSink, pageNum, nestLevel);
            break;
        case FrameNodeType::Fly:
            BuildFlyEndInstruction(rSink, pageNum, nestLevel);
            break;
        default:
            break;
    }
}

// 输出一个非容器节点的指令
void EmitLeaf(IFrameNode* pNode, RenderInstructionSink& rSink, int nestLevel)
{
    const FrameNodeType type = pNode->GetNodeType();
    int x = 0, y = 0, w = 0, h = 0;
    pNode->GetRect(x, y, w, h);
    const int pageNum = pNode->GetPageNum();

    switch (type)
    {
        case FrameNodeType::Text:
            BuildTextFrameInstruction(
                rSink, pageNum, nestLevel, x, y, w, h, pNode->GetText(), pNode->GetTextLen(),
                pNode->GetFontName(), pNode->GetFontSize(), pNode->GetFontColor(),
                pNode->GetFontWeight(), pNode->GetFontItalic(), pNode->GetStyleName());
            break;
        case FrameNodeType::NoText:
            BuildImageFrameInstruction(rSink, pageNum, nestLevel, x, y, w, h);
            break;
        case FrameNodeType::Footnote:
            BuildFootnoteFrameInstruction(rSink, pageNum, nestLevel, x, y, w, h);
            break;
        case FrameNodeType::Unknown:
        default:
            // 未知类型：不输出任何指令，只递归 (若有 child / fly)
            break;
    }
}

} // namespace

namespace detail
{
// 实际遍历函数：对每个节点都要处理 "主链 child" 和 "fly chain" 两个子树
void WalkFrameNodeRecursive(IFrameNode* pNode, RenderInstructionSink& rSink, int nestLevel)
{
    if (!pNode)
        return;

    const FrameNodeType type = pNode->GetNodeType();
    const int pageNum = pNode->GetPageNum();
    int x = 0, y = 0, w = 0, h = 0;
    pNode->GetRect(x, y, w, h);

    // 1) 输出本节点：容器 -> START/END 包裹；非容器 -> 单条指令
    const bool isContainer = IsContainerNodeType(type);
    if (isContainer)
    {
        EmitContainerStart(type, rSink, pageNum, nestLevel, x, y, w, h);
    }
    else
    {
        EmitLeaf(pNode, rSink, nestLevel);
    }

    // 2) 先递归主链 (GetLower() 风格的子节点)
    //    内容缩进一级
    for (IFrameNode* pChild = pNode->GetFirstChild(); pChild; pChild = pChild->GetNextSibling())
    {
        WalkFrameNodeRecursive(pChild, rSink, nestLevel + 1);
    }

    // 3) 再递归浮动对象链 (SwSortedObjs 风格的子节点)
    //    浮动对象也挂在当前容器内，但它们在主链之外
    for (IFrameNode* pFly = pNode->GetFirstFly(); pFly; pFly = pFly->GetNextSiblingFly())
    {
        WalkFrameNodeRecursive(pFly, rSink, nestLevel + 1);
    }

    // 4) 若是容器节点，输出 END 对应 START
    if (isContainer)
    {
        EmitContainerEnd(type, rSink, pageNum, nestLevel);
    }
}

} // namespace detail

// 顶层入口：传入 Page 节点
void WalkFrameTreeAndLog(IFrameNode* pNode, RenderInstructionSink& rSink)
{
    if (!pNode)
        return;

    // 顶层节点一般是 Page，nestLevel 从 0 开始
    detail::WalkFrameNodeRecursive(pNode, rSink, /*nestLevel=*/0);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
