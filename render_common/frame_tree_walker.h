/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * 共享的 Frame 树遍历器
 *
 * 定义 IFrameNode 抽象接口，将 Frame 树节点类型和几何信息从具体实现中解耦。
 * WalkFrameTreeAndLog 是唯一的遍历 + 指令生成实现，LO 和 aproj 共用。
 *
 * 相比旧版本的变化：
 *   1. 容器型节点 (Page / Section / Column / Table / TabRow / TabCell /
 *      Header / Footer / FootnoteCont / Fly) 会输出一对 *_START / *_END，
 *      二者之间是递归处理的子节点。
 *   2. IFrameNode 新增 GetFirstFly() / GetNextSiblingFly()，用来遍历
 *      "浮动对象链" (即 LO 的 SwSortedObjs)。典型结构是：
 *        SwPageFrame → GetLower() → body / section / column / text / table ...
 *                   → GetSortedObjs() → SwFlyFrame → SwNoTextFrame / SwTextFrame
 *      浮动对象本身也是容器型节点，可以嵌套更多浮动对象。
 *   3. 每条指令携带 nestLevel，TSV 输出时行首会有对应层级的缩进空格。
 *
 * 公共模块: render_common/ — sw 和 aproj/docx 都引用此文件
 */

#pragma once

#include "render_instruction.h"
#include "instruction_builder.h"

// ── Frame 节点类型（遍历用，不依赖任何具体 Frame 类型） ──

enum class FrameNodeType : uint8_t
{
    Unknown = 0,
    Page, // 页面 (容器)
    Body, // 正文区 (容器 - 但通常不生成指令，仅用于递归)
    Header, // 页眉 (容器)
    Footer, // 页脚 (容器)
    Section, // 节 (容器)
    Column, // 分栏 (容器)
    Text, // 文本段落 (非容器)
    NoText, // 图片/OLE (非容器)
    Table, // 表格 (容器)
    TabRow, // 表格行 (容器)
    TabCell, // 表格单元格 (容器)
    FootnoteCont, // 脚注容器 (容器)
    Footnote, // 脚注 (非容器)
    Fly, // 浮动框 (容器)
};

// 判断某节点类型是否需要以 START/END 对包裹
inline bool IsContainerNodeType(FrameNodeType t)
{
    switch (t)
    {
        case FrameNodeType::Page:
        case FrameNodeType::Body:
        case FrameNodeType::Header:
        case FrameNodeType::Footer:
        case FrameNodeType::Section:
        case FrameNodeType::Column:
        case FrameNodeType::Table:
        case FrameNodeType::TabRow:
        case FrameNodeType::TabCell:
        case FrameNodeType::FootnoteCont:
        case FrameNodeType::Fly:
            return true;
        default:
            return false;
    }
}

// ── 抽象 Frame 节点（LO 和 aproj 各自实现） ──

class IFrameNode
{
public:
    virtual ~IFrameNode() = default;

    // ── 类型 ──
    virtual FrameNodeType GetNodeType() const = 0;

    // ── 几何 ──
    virtual int GetPageNum() const = 0;
    virtual void GetRect(int& x, int& y, int& w, int& h) const = 0;

    // ── 文本内容（仅 Text 类型有效，其他类型返回默认值） ──
    virtual const char* GetText() const { return nullptr; }
    virtual int GetTextLen() const { return 0; }
    virtual const char* GetFontName() const { return nullptr; }
    virtual int GetFontSize() const { return 0; }
    virtual uint32_t GetFontColor() const { return 0; }
    virtual uint8_t GetFontWeight() const { return 0; }
    virtual uint8_t GetFontItalic() const { return 0; }
    virtual const char* GetStyleName() const { return nullptr; }

    // ── 树导航 ──
    // 主链：第一个子节点 (通常对应 LO 的 GetLower())
    virtual IFrameNode* GetFirstChild() const = 0;
    // 下一个兄弟 (通常对应 LO 的 GetNext())
    virtual IFrameNode* GetNextSibling() const = 0;

    // 浮动对象链：在主链之外挂在当前节点的浮动对象列表
    // (通常对应 LO 的 SwPageFrame::GetSortedObjs()，或 SwTextFrame 的
    //  GetFollow()/字符级锚定)。没有则返回 nullptr。
    virtual IFrameNode* GetFirstFly() const { return nullptr; }
    virtual IFrameNode* GetNextSiblingFly() const { return nullptr; }
};

// ── 公共遍历入口 ──
//
// 对 Frame 树执行前序遍历，为每个节点生成对应的 RenderInstruction。
// LO 和 aproj 共用此函数，确保 Frame 层输出完全一致。
//
// 参数:
//   pRoot  - Frame 树根节点（通常是一个 Page 节点）
//   rSink  - 指令接收器
//
// 嵌套层级约定:
//   Page 的 START 指令位于 nestLevel = 0;
//   Page 内第一个子元素位于 nestLevel = 1;
//   每进入一层容器，nestLevel 加一；退出时回到上一级。
//
void WalkFrameTreeAndLog(IFrameNode* pNode, RenderInstructionSink& rSink);

// 内部递归实现 (声明放在头文件便于单元测试等外部用途)
namespace detail
{
void WalkFrameNodeRecursive(IFrameNode* pNode, RenderInstructionSink& rSink, int nestLevel);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
