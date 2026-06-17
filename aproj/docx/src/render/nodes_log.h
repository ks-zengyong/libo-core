#pragma once
// 节点结构日志记录器 — 遍历 SwNodes 数组，输出与 LibreOffice SwNodesLogger 完全一致的格式
//
// 输出结构：
//   # SwNodes Structure Overview
//   # Total nodes: N
//   # BodyStart: N
//   # BodyEnd: N
//   (空行)
//   # All Nodes (including non-Content areas):
//   # [0] TYPE ...
//   (空行)
//   # Body Area Nodes (structured):
//   START_NODE	0	Normal
//   ... (缩进结构，含锚点引用展开)

#include "node_instruction.h"
#include "../core/types.h"
#include <string>
#include <fstream>

// 前向声明
class SwNodes;
class SwDoc;

// NodesLogger: 节点结构日志记录器
class NodesLogger
{
public:
    NodesLogger();
    ~NodesLogger();

    // ── 节点遍历 ──
    // 遍历整个 SwNodes 数组，通过 render_common 共享的 WalkNodesAndLog 生成节点指令
    void LogNodes(SwNodes& rNodes);

    // ── 输出 ──
    // 将完整格式化的节点结构写入文件 (与 LibreOffice 相同的格式)
    void WriteToFile(const std::string& filePath);

private:
    std::string m_aOutput; // 完整格式化的输出内容
};
