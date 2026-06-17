// 节点结构日志记录器实现 — 使用共享 node_instruction.h 格式
// 与 LibreOffice 侧 SwNodesLogger 输出完全相同的 TSV 格式
//
// 输出结构：
//   # SwNodes Structure Overview
//   # Total nodes: N
//   # BodyStart: N
//   # BodyEnd: N
//   (空行)
//   # All Nodes (including non-Content areas):
//   # [0] TYPE ...
//   # [1] TYPE ...
//   (空行)
//   # Body Area Nodes (structured):
//   START_NODE	0	Normal
//   ... (缩进结构)
//
// 锚点引用：如果节点是 Fly anchor（有 Fly 节区引用它），则在该节点下方
//   缩进展示所有引用的 Fly 节区内容。

#include "nodes_log.h"
#include "../core/node.h"
#include "../core/ndarr.h"
#include "../core/doc.h"
#include "../../../../render_common/node_walker.h"
#include "../../../../render_common/node_builder.h"
#include "../../../../render_common/node_format.h"
#include <functional>
#include <iostream>
#include <sstream>
#include <fstream>
#include <cstring>

//===----------------------------------------------------------------------===//
// INode 包装器：将 aproj 的 SwNode 适配为共享遍历接口
//===----------------------------------------------------------------------===//

namespace
{
class AprojNode : public INode
{
public:
    AprojNode(SwNode* pNode)
        : m_pNode(pNode)
    {
        if (pNode && pNode->IsTextNode())
            ExtractTextInfo();
    }

    bool IsStartNode() const override
    {
        // SwTableNode继承自SwStartNode，需要排除，让它走到IsTableNode()分支
        // 与 LO 保持一致
        bool bIsStart = m_pNode && m_pNode->IsStartNode();
        bool bIsTable = m_pNode && m_pNode->IsTableNode();
        if (bIsTable && bIsStart)
        {
            return false;
        }
        return bIsStart;
    }

    bool IsEndNode() const override { return m_pNode && m_pNode->IsEndNode(); }

    bool IsTextNode() const override { return m_pNode && m_pNode->IsTextNode(); }

    bool IsGrfNode() const override { return m_pNode && m_pNode->IsGrfNode(); }

    bool IsOLENode() const override { return m_pNode && m_pNode->IsOLENode(); }

    bool IsTableNode() const override { return m_pNode && m_pNode->IsTableNode(); }

    bool IsSectionNode() const override { return m_pNode && m_pNode->IsSectionNode(); }

    int GetIndex() const override { return m_pNode ? static_cast<int>(m_pNode->GetIndex()) : -1; }

    int GetStartNodeType() const override
    {
        if (!m_pNode || !m_pNode->IsStartNode())
            return 0;
        SwStartNode* pStartNode = static_cast<SwStartNode*>(m_pNode);
        return static_cast<int>(pStartNode->GetStartNodeType());
    }

    const char* GetText() const override { return m_textBuf.empty() ? nullptr : m_textBuf.c_str(); }

    int GetTextLen() const override { return static_cast<int>(m_textBuf.size()); }

    const char* GetStyleName() const override
    {
        // 以 LO 为标准：将 "Normal" 映射为 "Default Paragraph Style"
        if (m_styleBuf == "Normal")
            return "Default Paragraph Style";
        return m_styleBuf.empty() ? nullptr : m_styleBuf.c_str();
    }

    int GetTableRows() const override
    {
        if (!m_pNode || !m_pNode->IsTableNode())
            return 0;
        SwTableNode* pTableNode = static_cast<SwTableNode*>(m_pNode);
        const auto& tableData = pTableNode->GetTableData();
        return static_cast<int>(tableData.size());
    }

    int GetTableCols() const override
    {
        if (!m_pNode || !m_pNode->IsTableNode())
            return 0;
        SwTableNode* pTableNode = static_cast<SwTableNode*>(m_pNode);
        const auto& tableData = pTableNode->GetTableData();
        if (tableData.empty())
            return 0;
        return static_cast<int>(tableData[0].cells.size());
    }

    int GetEndNodeIndex() const override
    {
        if (!m_pNode)
            return -1;
        if (m_pNode->IsStartNode())
        {
            SwStartNode* pStartNode = static_cast<SwStartNode*>(m_pNode);
            SwEndNode* pEndNode = pStartNode->GetEndOfSection();
            return pEndNode ? static_cast<int>(pEndNode->GetIndex()) : -1;
        }
        if (m_pNode->IsTableNode())
        {
            SwTableNode* pTableNode = static_cast<SwTableNode*>(m_pNode);
            SwEndNode* pEndNode = pTableNode->GetEndOfSection();
            return pEndNode ? static_cast<int>(pEndNode->GetIndex()) : -1;
        }
        if (m_pNode->IsSectionNode())
        {
            SwSectionNode* pSectionNode = static_cast<SwSectionNode*>(m_pNode);
            SwEndNode* pEndNode = pSectionNode->GetEndOfSection();
            return pEndNode ? static_cast<int>(pEndNode->GetIndex()) : -1;
        }
        return -1;
    }

    int GetAnchorNodeIndex() const override
    {
        // 仅对 Fly 节区有效 (startNodeType == 2)
        if (!m_pNode || !m_pNode->IsStartNode())
            return -1;
        SwStartNode* pStartNode = static_cast<SwStartNode*>(m_pNode);
        if (pStartNode->GetStartNodeType() != SwFlyStartNode)
            return -1;
        return pStartNode->GetAnchorNodeIndex();
    }

private:
    void ExtractTextInfo()
    {
        if (!m_pNode || !m_pNode->IsTextNode())
            return;

        SwTextNode* pTextNode = static_cast<SwTextNode*>(m_pNode);
        m_textBuf = pTextNode->GetText();
        m_styleBuf = pTextNode->GetStyleName();
    }

    SwNode* m_pNode;
    std::string m_textBuf;
    std::string m_styleBuf;
};

// ── INodesArray 包装器：将 aproj 的 SwNodes 适配为共享遍历接口 ──

class AprojNodesArray : public INodesArray
{
public:
    AprojNodesArray(SwNodes& rNodes)
        : m_rNodes(rNodes)
    {
    }

    int Count() const override { return static_cast<int>(m_rNodes.Count()); }

    INode* GetNode(int index) const override
    {
        if (index < 0 || index >= Count())
            return nullptr;
        SwNode* pNode = m_rNodes[SwNodeOffset(index)];
        return pNode ? new AprojNode(pNode) : nullptr;
    }

    int GetBodyStartIndex() const override
    {
        // 输出完整节点结构：从索引 0 开始
        // 这样可以包含 AutoText 区域的浮动框架和表格
        // 与 LO 保持一致
        return 0;
    }

    int GetBodyEndIndex() const override
    {
        SwNode& rEnd = m_rNodes.GetEndOfContent();
        return static_cast<int>(rEnd.GetIndex()) + 1;
    }

private:
    SwNodes& m_rNodes;
};

// ── NodeInstructionSink 实现：直接序列化到字符串 ──

class SerializingNodeSink : public NodeInstructionSink
{
public:
    explicit SerializingNodeSink(std::string& content)
        : m_content(content)
    {
    }

    void OnInstruction(const NodeInstruction& inst) override
    {
        std::ostringstream oss;
        WriteNodeInstructionToStream(oss, inst);
        m_content += oss.str();
    }

private:
    std::string& m_content;
};

} // namespace

//===----------------------------------------------------------------------===//
// NodesLogger 实现
//===----------------------------------------------------------------------===//

NodesLogger::NodesLogger() {}

NodesLogger::~NodesLogger() {}

void NodesLogger::LogNodes(SwNodes& rNodes)
{
    // 创建适配器
    AprojNodesArray nodesArray(rNodes);

    // 遍历并序列化（与 LO SwNodesLogger::LogNodes 格式完全一致）
    std::string content;

    // 1. 输出节点数组概览
    content += "# SwNodes Structure Overview\n";
    content += "# Total nodes: " + std::to_string(nodesArray.Count()) + "\n";
    content += "# BodyStart: " + std::to_string(nodesArray.GetBodyStartIndex()) + "\n";
    content += "# BodyEnd: " + std::to_string(nodesArray.GetBodyEndIndex()) + "\n";
    content += "\n";

    // 2. 输出所有区域的节点（用于诊断）
    //    格式：# [idx] TYPE [extra_info]
    content += "# All Nodes (including non-Content areas):\n";
    for (int i = 0; i < nodesArray.Count(); ++i)
    {
        INode* pNode = nodesArray.GetNode(i);
        if (!pNode)
            continue;

        std::ostringstream oss;
        oss << "# [" << i << "] ";

        if (pNode->IsTableNode())
            oss << "TABLE_NODE rows=" << pNode->GetTableRows() << " cols=" << pNode->GetTableCols();
        else if (pNode->IsStartNode())
            oss << "START_NODE type=" << pNode->GetStartNodeType();
        else if (pNode->IsEndNode())
            oss << "END_NODE";
        else if (pNode->IsTextNode())
            oss << "TEXT_NODE";
        else if (pNode->IsGrfNode())
            oss << "GRF_NODE";
        else if (pNode->IsOLENode())
            oss << "OLE_NODE";
        else if (pNode->IsSectionNode())
            oss << "SECTION_NODE";
        else
            oss << "UNKNOWN";

        oss << "\n";
        content += oss.str();
        delete pNode;
    }
    content += "\n";

    // 3. 输出 Body 区域的节点结构（缩进形式，支持锚点引用展开）
    content += "# Body Area Nodes (structured):\n";
    SerializingNodeSink sink(content);
    WalkNodesAndLog(&nodesArray, sink);

    m_aOutput = content;
}

void NodesLogger::WriteToFile(const std::string& filePath)
{
    std::ofstream file(filePath, std::ios::out | std::ios::binary);
    if (!file.is_open())
    {
        std::cerr << "[NodesLogger] ERROR: Failed to open file: " << filePath << std::endl;
        return;
    }

    file << m_aOutput;
    file.flush();
    file.close();
}
