// 节点结构日志记录器实现 — 使用共享 node_instruction.h 格式
// 与 LibreOffice 侧 SwNodesLogger 输出完全相同的 TSV 格式

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
        return m_pNode && m_pNode->IsStartNode();
    }

    bool IsEndNode() const override
    {
        return m_pNode && m_pNode->IsEndNode();
    }

    bool IsTextNode() const override
    {
        return m_pNode && m_pNode->IsTextNode();
    }

    bool IsGrfNode() const override
    {
        return m_pNode && m_pNode->IsGrfNode();
    }

    bool IsOLENode() const override
    {
        return m_pNode && m_pNode->IsOLENode();
    }

    bool IsTableNode() const override
    {
        return m_pNode && m_pNode->IsTableNode();
    }

    bool IsSectionNode() const override
    {
        return m_pNode && m_pNode->IsSectionNode();
    }

    int GetIndex() const override
    {
        return m_pNode ? static_cast<int>(m_pNode->GetIndex()) : -1;
    }

    int GetStartNodeType() const override
    {
        if (!m_pNode || !m_pNode->IsStartNode())
            return 0;
        SwStartNode* pStartNode = static_cast<SwStartNode*>(m_pNode);
        return static_cast<int>(pStartNode->GetStartNodeType());
    }

    const char* GetText() const override
    {
        return m_textBuf.empty() ? nullptr : m_textBuf.c_str();
    }

    int GetTextLen() const override
    {
        return static_cast<int>(m_textBuf.size());
    }

    const char* GetStyleName() const override
    {
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

    int Count() const override
    {
        return static_cast<int>(m_rNodes.Count());
    }

    INode* GetNode(int index) const override
    {
        if (index < 0 || index >= Count())
            return nullptr;
        SwNode* pNode = m_rNodes[SwNodeOffset(index)];
        return pNode ? new AprojNode(pNode) : nullptr;
    }

    int GetBodyStartIndex() const override
    {
        // Body 区域从 EndOfAutotext 之后开始
        SwNode& rEndOfAutotext = m_rNodes.GetEndOfAutotext();
        return static_cast<int>(rEndOfAutotext.GetIndex()) + 1;
    }

    int GetBodyEndIndex() const override
    {
        // Body 区域到 EndOfContent 结束
        SwNode& rEndOfContent = m_rNodes.GetEndOfContent();
        return static_cast<int>(rEndOfContent.GetIndex());
    }

private:
    SwNodes& m_rNodes;
};

} // namespace

//===----------------------------------------------------------------------===//
// NodesLogger 实现
//===----------------------------------------------------------------------===//

NodesLogger::NodesLogger() {}

NodesLogger::~NodesLogger() {}

const char* NodesLogger::StoreString(const char* s)
{
    if (!s)
        return "";
    try
    {
        std::string copy(s);
        m_aStrings.push_back(copy);
        return m_aStrings.back().c_str();
    }
    catch (...)
    {
        return "";
    }
}

void NodesLogger::OnInstruction(const NodeInstruction& inst)
{
    // 存储字符串副本，避免指针悬空
    NodeInstruction copy = inst;
    if (copy.text)
        copy.text = StoreString(copy.text);
    if (copy.styleName)
        copy.styleName = StoreString(copy.styleName);

    m_aInstructions.push_back(copy);
}

void NodesLogger::LogNodes(SwNodes& rNodes)
{
    // 创建适配器
    AprojNodesArray nodesArray(rNodes);

    // 使用共享遍历器
    WalkNodesAndLog(&nodesArray, *this);
}

void NodesLogger::WriteToFile(const std::string& filePath)
{
    std::cerr << "[NodesLogger] Writing to: " << filePath << std::endl;
    std::cerr << "[NodesLogger] Instructions count: " << m_aInstructions.size() << std::endl;
    
    std::ofstream file(filePath);
    if (!file.is_open()) {
        std::cerr << "[NodesLogger] ERROR: Failed to open file: " << filePath << std::endl;
        return;
    }

    for (const auto& inst : m_aInstructions)
    {
        WriteNodeInstructionToStream(file, inst);
    }

    file.close();
    std::cerr << "[NodesLogger] File written successfully" << std::endl;
}
