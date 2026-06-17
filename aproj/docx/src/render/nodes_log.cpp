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
        // SwTableNode继承自SwStartNode，需要排除，让它走到IsTableNode()分支
        // 与 LO 保持一致
        bool bIsStart = m_pNode && m_pNode->IsStartNode();
        bool bIsTable = m_pNode && m_pNode->IsTableNode();
        // 调试：如果是 TableNode，输出警告
        if (bIsTable && bIsStart)
        {
            // TableNode 应该被排除，返回 false
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
        // 遍历所有节点，确保不遗漏任何内容（包括 Fly 区和正文区）
        // walker 遍历 [bodyStart, bodyEnd)，使用 Count() 确保包含最后一个节点
        return static_cast<int>(m_rNodes.Count());
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

    // 诊断输出：检查所有节点
    std::cerr << "[NodesLogger] Total nodes: " << nodesArray.Count() << std::endl;
    std::cerr << "[NodesLogger] BodyStart: " << nodesArray.GetBodyStartIndex() << std::endl;
    std::cerr << "[NodesLogger] BodyEnd: " << nodesArray.GetBodyEndIndex() << std::endl;

    // 检查是否有 TableNode
    for (int i = 0; i < nodesArray.Count(); ++i)
    {
        INode* pNode = nodesArray.GetNode(i);
        if (!pNode)
            continue;
        if (pNode->IsTableNode())
        {
            std::cerr << "[NodesLogger] Found TableNode at index " << i
                      << " rows=" << pNode->GetTableRows() << " cols=" << pNode->GetTableCols()
                      << std::endl;
        }
        delete pNode;
    }

    // 使用共享遍历器
    WalkNodesAndLog(&nodesArray, *this);
}

void NodesLogger::WriteToFile(const std::string& filePath)
{
    std::cerr << "[NodesLogger] Writing to: " << filePath << std::endl;
    std::cerr << "[NodesLogger] Instructions count: " << m_aInstructions.size() << std::endl;

    std::ofstream file(filePath);
    if (!file.is_open())
    {
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
