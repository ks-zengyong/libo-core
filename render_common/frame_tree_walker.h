/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * 共享的 Frame 树遍历器
 *
 * 定义 IFrameNode 抽象接口，将 Frame 树节点类型和几何信息从具体实现中解耦。
 * WalkFrameTreeAndLog 是唯一的遍历 + 指令生成实现，LO 和 aproj 共用。
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
    Page,         // 页面
    Body,         // 正文区
    Header,       // 页眉
    Footer,       // 页脚
    Section,      // 节
    Column,       // 分栏
    Text,         // 文本段落
    NoText,       // 图片/OLE（非文本内容）
    Table,        // 表格
    TabRow,       // 表格行
    TabCell,      // 表格单元格
    FootnoteCont, // 脚注容器
    Footnote,     // 脚注
    Fly,          // 浮动框
};

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
    // 返回第一个子节点，无子节点返回 nullptr
    virtual IFrameNode* GetFirstChild() const = 0;
    // 返回下一个兄弟节点，无下一个返回 nullptr
    virtual IFrameNode* GetNextSibling() const = 0;
};

// ── 公共遍历入口 ──
//
// 对 Frame 树执行前序遍历，为每个节点生成对应的 RenderInstruction。
// LO 和 aproj 共用此函数，确保 Frame 层输出完全一致。
//
// 参数:
//   pRoot  - Frame 树根节点（IFrameNode 包装）
//   rSink  - 指令接收器
//
void WalkFrameTreeAndLog(IFrameNode* pRoot, RenderInstructionSink& rSink);