/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * 渲染指令监听器 — 拦截 PaintSwFrame 调用，输出结构化渲染指令
 * 与 aproj/docx 的 RenderLogger 使用同一套 render_instruction.h 定义
 */

#pragma once

#include "render_instruction.h"
#include "meta_to_instruction.hxx"
#include <rtl/string.hxx>
#include <osl/file.hxx>
#include <vcl/gdimtf.hxx>
#include <fstream>
#include <vector>
#include <memory>

class SwTextFrame;
class SwTabFrame;
class SwPageFrame;
class OutputDevice;

/**
 * SwPaintEventListener — 渲染指令监听器
 *
 * 在 SwRootFrame::PaintSwFrame / SwTextFrame::PaintSwFrame 中被调用，
 * 记录所有渲染指令到文件。通过环境变量 SW_RENDER_LOG 控制。
 *
 * 输出格式与 aproj/docx 的 RenderLogger 完全一致 (TSV)。
 */
class SwPaintEventListener final : public RenderInstructionSink
{
public:
    static SwPaintEventListener& Get();

    // 控制
    void StartLog(const OString& filePath);
    void EndLog();
    bool IsLogging() const { return m_bLogging; }

    // RenderInstructionSink 接口
    void OnInstruction(const RenderInstruction& inst) override;

    // 高级接口 — 由 PaintSwFrame 调用
    void OnPageStart(int pageNum, int width, int height);
    void OnPageEnd(int pageNum);
    void OnTextFrame(const SwTextFrame* pFrame);

    // ── VCL 层录制 (GDIMetaFile 方式) ──
    void StartVclLog(const OString& filePath);
    void EndVclLog();
    bool IsVclLogging() const { return m_bVclLogging; }
    void StartPageRecord(OutputDevice* pOutDev); // 开始录制当前页的 VCL 绘制操作
    void StopPageRecordAndConvert(int pageNum); // 停止录制并转换为 RenderInstruction

    // 写入文件
    void Flush();

    // 检查环境变量 SW_RENDER_LOG，如有则自动启动
    void CheckEnvAndStart();

private:
    SwPaintEventListener() = default;
    ~SwPaintEventListener() override = default;

    // 禁止拷贝
    SwPaintEventListener(const SwPaintEventListener&) = delete;
    SwPaintEventListener& operator=(const SwPaintEventListener&) = delete;

    static void WriteInstructionToStream(std::ostream& out, const RenderInstruction& inst);

    std::vector<RenderInstruction> m_aInstructions;
    std::ofstream m_File;
    bool m_bLogging = false;

    // VCL 层录制
    GDIMetaFile m_aMetaFile;
    MetaToInstructionConverter m_aConverter;
    std::ofstream m_vclFile;
    bool m_bVclLogging = false;
    bool m_bConvertingVcl = false; // 正在 VCL→RenderInstruction 转换中
};

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
