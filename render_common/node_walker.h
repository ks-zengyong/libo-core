/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * 共享的 Node 树遍历器
 *
 * 定义 INode 抽象接口，将 Node 类型和属性信息从具体实现中解耦。
 * WalkNodesAndLog 是唯一的遍历 + 指令生成实现，LO 和 aproj 共用。
 *
 * 遍历规则：
 *   对 SwNodes 数组做线性遍历，根据节点类型生成对应指令：
 *   - SwStartNode: 输出 START_NODE (容器 START)
 *   - SwEndNode:   输出 END_NODE (容器 END)
 *   - SwTextNode:  输出 TEXT_NODE (非容器)
 *   - SwGrfNode:   输出 GRF_NODE (非容器)
 *   - SwOLENode:   输出 OLE_NODE (非容器)
 *   - SwTableNode: 输出 TABLE_START + 递归子节点 + TABLE_END
 *   - SwSectionNode: 输出 SECTION_START + 递归子节点 + SECTION_END
 *
 * 公共模块: render_common/ — sw 和 aproj/docx 都引用此文件
 */

#pragma once

#include "node_instruction.h"

// ── 抽象 Node 节点（LO 和 aproj 各自实现） ──

class INode
{
public:
    virtual ~INode() = default;

    // ── 类型查询 ──
    virtual bool IsStartNode() const = 0;
    virtual bool IsEndNode() const = 0;
    virtual bool IsTextNode() const = 0;
    virtual bool IsGrfNode() const = 0;
    virtual bool IsOLENode() const = 0;
    virtual bool IsTableNode() const = 0;
    virtual bool IsSectionNode() const = 0;

    // ── 索引 ──
    virtual int GetIndex() const = 0;

    // ── StartNode 子类型 (仅 IsStartNode 时有效) ──
    // 0=Normal, 1=TableBox, 2=Fly, 3=Footnote, 4=Header, 5=Footer
    virtual int GetStartNodeType() const { return 0; }

    // ── 文本内容 (仅 IsTextNode 时有效) ──
    virtual const char* GetText() const { return nullptr; }
    virtual int GetTextLen() const { return 0; }
    virtual const char* GetStyleName() const { return nullptr; }

    // ── 表格信息 (仅 IsTableNode 时有效) ──
    virtual int GetTableRows() const { return 0; }
    virtual int GetTableCols() const { return 0; }

    // ── 容器导航 (仅 IsTableNode / IsSectionNode 时有效) ──
    // 返回容器内第一个子节点的索引，-1 表示无子节点
    virtual int GetFirstChildIndex() const { return -1; }
    // 返回容器对应的 EndNode 索引 (StartNode 用)
    virtual int GetEndNodeIndex() const { return -1; }

    // ── Fly 节区锚点引用 (仅 IsStartNode 且 GetStartNodeType()==2(Fly) 时有效) ──
    // 返回 Fly 节区锚点所在的文本节点索引，-1 表示无锚点或非 Fly
    virtual int GetAnchorNodeIndex() const { return -1; }
};

// ── 抽象 Nodes 数组（LO 和 aproj 各自实现） ──

class INodesArray
{
public:
    virtual ~INodesArray() = default;

    // 节点总数
    virtual int Count() const = 0;

    // 按索引获取节点
    virtual INode* GetNode(int index) const = 0;

    // 获取 Body 区域的第一个节点索引 (Content 区起始)
    virtual int GetBodyStartIndex() const = 0;

    // 获取 Body 区域的 EndOfContent 节点索引
    virtual int GetBodyEndIndex() const = 0;
};

// ── 公共遍历入口 ──
//
// 对 Nodes 数组执行遍历，为每个节点生成对应的 NodeInstruction。
// LO 和 aproj 共用此函数，确保节点层输出完全一致。
//
// 参数:
//   pNodes - Nodes 数组
//   rSink  - 指令接收器
//
void WalkNodesAndLog(const INodesArray* pNodes, NodeInstructionSink& rSink);
