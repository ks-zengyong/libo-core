/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * 节点结构日志记录器 — 遍历 SwNodes 数组，输出节点层语义信息
 * 与 aproj/docx 的 NodesLogger 使用同一套 node_instruction.h 定义
 */

#ifndef SW_SOURCE_CORE_DOCNODE_NODES_LOGGER_HXX
#define SW_SOURCE_CORE_DOCNODE_NODES_LOGGER_HXX

#include <rtl/string.hxx>
#include <osl/file.hxx>
#include <fstream>
#include <vector>
#include <memory>

class SwNodes;
class SwNode;
class SwStartNode;
class SwEndNode;
class SwTextNode;
class SwTableNode;
class SwSectionNode;

// 前向声明 render_common 接口
class INode;
class INodesArray;
class NodeInstructionSink;

/**
 * SwNodesLogger — 节点结构日志记录器
 *
 * 遍历 SwNodes 数组，为每个节点生成 NodeInstruction 并输出到文件。
 * 通过环境变量 SW_NODES_LOG 控制输出路径。
 *
 * 输出格式与 aproj/docx 的 NodesLogger 完全一致 (TSV)。
 */
class SwNodesLogger
{
public:
    static SwNodesLogger& Get();

    // 控制
    void StartLog(const OString& filePath);
    void EndLog();
    bool IsLogging() const { return m_bLogging; }

    // 遍历并输出节点结构
    void LogNodes(const SwNodes& rNodes);

    // 检查环境变量 SW_NODES_LOG，如有则自动启动
    void CheckEnvAndStart();

private:
    SwNodesLogger() = default;
    ~SwNodesLogger() = default;

    // 禁止拷贝
    SwNodesLogger(const SwNodesLogger&) = delete;
    SwNodesLogger& operator=(const SwNodesLogger&) = delete;

    std::ofstream m_File;
    OString m_sLogPath;
    bool m_bLogging = false;
    bool m_bLogged = false; // 防止重复记录
};

#endif // SW_SOURCE_CORE_DOCNODE_NODES_LOGGER_HXX

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
