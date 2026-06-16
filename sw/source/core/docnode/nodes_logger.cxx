/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * 节点结构日志记录器实现 — 使用共享 node_instruction.h 格式
 */

#include "nodes_logger.hxx"
#include <node.hxx>
#include <ndarr.hxx>
#include <ndtxt.hxx>
#include <ndnotxt.hxx>
#include <swtable.hxx>
#include <section.hxx>
#include <frmfmt.hxx>
#include <fmtanchr.hxx>
#include <fmtcntnt.hxx>
#include <doc.hxx>
#include <osl/process.h>
#include <rtl/strbuf.hxx>

#include "../../../../render_common/node_instruction.h"
#include "../../../../render_common/node_builder.h"
#include "../../../../render_common/node_walker.h"
#include "../../../../render_common/node_format.h"

#include <iostream>
#include <sstream>
#include <deque>

// ── INode 包装器：将 LO 的 SwNode 适配为共享遍历接口 ──

namespace
{
class LoNode : public INode
{
public:
    LoNode(const SwNode* pNode)
        : m_pNode(pNode)
    {
        if (pNode && pNode->IsTextNode())
            ExtractTextInfo();
    }

    bool IsStartNode() const override
    {
        // SwTableNode继承自SwStartNode，需要排除，让它走到IsTableNode()分支
        return m_pNode && m_pNode->IsStartNode() && !m_pNode->IsTableNode();
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
        return m_pNode ? static_cast<int>(m_pNode->GetIndex().get()) : -1;
    }

    int GetStartNodeType() const override
    {
        if (!m_pNode || !m_pNode->IsStartNode())
            return 0;
        const SwStartNode* pStartNode = static_cast<const SwStartNode*>(m_pNode);
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
        const SwTableNode* pTableNode = static_cast<const SwTableNode*>(m_pNode);
        const SwTable& rTable = pTableNode->GetTable();
        return static_cast<int>(rTable.GetTabLines().size());
    }

    int GetTableCols() const override
    {
        if (!m_pNode || !m_pNode->IsTableNode())
            return 0;
        const SwTableNode* pTableNode = static_cast<const SwTableNode*>(m_pNode);
        const SwTable& rTable = pTableNode->GetTable();
        const SwTableLines& rLines = rTable.GetTabLines();
        if (rLines.empty())
            return 0;
        // 返回第一行的列数
        return static_cast<int>(rLines[0]->GetTabBoxes().size());
    }

    int GetEndNodeIndex() const override
    {
        if (!m_pNode)
            return -1;
        if (m_pNode->IsStartNode())
        {
            const SwStartNode* pStartNode = static_cast<const SwStartNode*>(m_pNode);
            const SwEndNode* pEndNode = pStartNode->EndOfSectionNode();
            return pEndNode ? static_cast<int>(pEndNode->GetIndex().get()) : -1;
        }
        if (m_pNode->IsTableNode())
        {
            const SwTableNode* pTableNode = static_cast<const SwTableNode*>(m_pNode);
            const SwEndNode* pEndNode = pTableNode->EndOfSectionNode();
            return pEndNode ? static_cast<int>(pEndNode->GetIndex().get()) : -1;
        }
        if (m_pNode->IsSectionNode())
        {
            const SwSectionNode* pSectionNode = static_cast<const SwSectionNode*>(m_pNode);
            const SwEndNode* pEndNode = pSectionNode->EndOfSectionNode();
            return pEndNode ? static_cast<int>(pEndNode->GetIndex().get()) : -1;
        }
        return -1;
    }

    int GetAnchorNodeIndex() const override
    {
        // 仅对 Fly 节区有效 (startNodeType == 2)
        if (!m_pNode || !m_pNode->IsStartNode())
            return -1;
        const SwStartNode* pStartNode = static_cast<const SwStartNode*>(m_pNode);
        if (pStartNode->GetStartNodeType() != SwFlyStartNode)
            return -1;

        // 获取 Fly 的 FrameFormat
        SwFrameFormat* pFlyFormat = pStartNode->GetFlyFormat();
        if (!pFlyFormat)
            return -1;

        // 获取锚点信息
        const SwFormatAnchor& rAnchor = pFlyFormat->GetAnchor();
        if (rAnchor.GetAnchorId() == RndStdIds::FLY_AT_PARA
            || rAnchor.GetAnchorId() == RndStdIds::FLY_AT_CHAR)
        {
            // 锚点在段落或字符位置
            const SwPosition* pPos = rAnchor.GetContentAnchor();
            if (pPos)
            {
                const SwNode* pAnchorNode = &pPos->GetNode();
                return static_cast<int>(pAnchorNode->GetIndex().get());
            }
        }
        return -1;
    }

private:
    void ExtractTextInfo()
    {
        if (!m_pNode || !m_pNode->IsTextNode())
            return;

        const SwTextNode* pTextNode = static_cast<const SwTextNode*>(m_pNode);
        const OUString& rText = pTextNode->GetText();
        OString utf8 = OUStringToOString(rText, RTL_TEXTENCODING_UTF8);
        m_textBuf = utf8.getStr();

        const SwFormatColl* pColl = pTextNode->GetFormatColl();
        if (pColl)
        {
            OString sn = OUStringToOString(pColl->GetName().toString(), RTL_TEXTENCODING_UTF8);
            m_styleBuf = sn.getStr();
        }
    }

    const SwNode* m_pNode;
    std::string m_textBuf;
    std::string m_styleBuf;
};

// ── INodesArray 包装器：将 LO 的 SwNodes 适配为共享遍历接口 ──

class LoNodesArray : public INodesArray
{
public:
    LoNodesArray(const SwNodes& rNodes)
        : m_rNodes(rNodes)
    {
    }

    int Count() const override
    {
        return static_cast<int>(m_rNodes.Count().get());
    }

    INode* GetNode(int index) const override
    {
        if (index < 0 || index >= Count())
            return nullptr;
        const SwNode* pNode = m_rNodes[SwNodeOffset(index)];
        return pNode ? new LoNode(pNode) : nullptr;
    }

    int GetBodyStartIndex() const override
    {
        // 输出完整节点结构：从索引 0 开始
        // 这样可以包含 AutoText 区域的浮动框架和表格
        return 0;
    }

    int GetBodyEndIndex() const override
    {
        // 输出到 EndOfContent 结束
        const SwNode& rEndOfContent = m_rNodes.GetEndOfContent();
        return static_cast<int>(rEndOfContent.GetIndex().get());
    }

private:
    const SwNodes& m_rNodes;
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

// ── 单例 ──

SwNodesLogger& SwNodesLogger::Get()
{
    static SwNodesLogger s_Instance;
    return s_Instance;
}

// ── 控制 ──

void SwNodesLogger::StartLog(const OString& filePath)
{
    m_sLogPath = filePath;
    m_File.open(filePath.getStr(), std::ios::out | std::ios::binary);
    m_bLogging = true;
    std::cerr << "SwNodesLogger: logging to " << filePath.getStr() << std::endl;
}

void SwNodesLogger::EndLog()
{
    if (m_File.is_open())
    {
        m_File.flush();
        m_File.close();
    }
    m_bLogging = false;
}

void SwNodesLogger::CheckEnvAndStart()
{
    // 检查环境变量 SW_NODES_LOG
    OUString logPath;
    if (osl_getEnvironment(OUString("SW_NODES_LOG").pData, &logPath.pData) == osl_Process_E_None)
    {
        if (!logPath.isEmpty())
        {
            OString path = OUStringToOString(logPath, RTL_TEXTENCODING_UTF8);
            StartLog(path);
        }
    }
}

void SwNodesLogger::LogNodes(const SwNodes& rNodes)
{
    if (!m_bLogging)
        return;

    // 创建适配器
    LoNodesArray nodesArray(rNodes);

    // 遍历并序列化
    std::string content;

    // 输出节点数组概览
    content += "# SwNodes Structure Overview\n";
    content += "# Total nodes: " + std::to_string(nodesArray.Count()) + "\n";
    content += "# BodyStart: " + std::to_string(nodesArray.GetBodyStartIndex()) + "\n";
    content += "# BodyEnd: " + std::to_string(nodesArray.GetBodyEndIndex()) + "\n";
    content += "\n";

    // 输出所有区域的节点（用于诊断）
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

    // 输出 Body 区域的节点结构
    content += "# Body Area Nodes (structured):\n";
    SerializingNodeSink sink(content);
    WalkNodesAndLog(&nodesArray, sink);

    // 写入文件（每次覆盖，确保记录最终状态）
    if (m_File.is_open())
    {
        m_File.seekp(0);
        m_File << content;
        m_File.flush();
    }
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
