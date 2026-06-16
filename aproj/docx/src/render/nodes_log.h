#pragma once
// 节点结构日志记录器 — 遍历 SwNodes 数组，输出节点层语义信息
// 与 LibreOffice 侧 SwNodesLogger 使用同一套 node_instruction.h 定义

#include "node_instruction.h"
#include "../core/types.h"
#include <string>
#include <vector>
#include <deque>
#include <fstream>

// 前向声明
class SwNodes;
class SwDoc;

// NodesLogger: 节点结构日志记录器
class NodesLogger : public NodeInstructionSink
{
public:
    NodesLogger();
    ~NodesLogger() override;

    // ── NodeInstructionSink 接口 ──
    void OnInstruction(const NodeInstruction& inst) override;

    // ── 记录控制 ──
    bool IsLogging() const { return !m_aInstructions.empty(); }

    // ── 节点遍历 ──
    // 遍历整个 SwNodes 数组，通过 render_common 共享的 WalkNodesAndLog 生成节点指令
    void LogNodes(SwNodes& rNodes);

    // ── 输出 ──
    // 获取所有记录的指令
    const std::vector<NodeInstruction>& GetInstructions() const { return m_aInstructions; }
    // 将指令写入文件 (与 LibreOffice 相同的 TSV 格式，使用 render_common 共享实现)
    void WriteToFile(const std::string& filePath);

private:
    // 存储字符串副本，确保指针在指令生命周期内有效
    const char* StoreString(const char* s);

    std::vector<NodeInstruction> m_aInstructions;
    std::deque<std::string> m_aStrings; // 字符串存储池 (deque 不会使已有指针失效)
};
