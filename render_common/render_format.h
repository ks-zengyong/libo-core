/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * 共享的渲染指令 TSV 格式化输出
 *
 * 将 RenderInstruction 序列化为与 LibreOffice 完全一致的 TSV 格式。
 * sw 和 aproj/docx 共用的唯一实现，确保输出格式绝对一致。
 *
 * 公共模块: render_common/ — sw 和 aproj/docx 都引用此文件
 */

#pragma once

#include "render_instruction.h"
#include <ostream>

/**
 * 将单条渲染指令格式化为 TSV 行并写入流。
 *
 * 与 LibreOffice SwPaintEventListener 和 aproj RenderLogger 的输出格式完全一致。
 * 无需依赖任何平台特定类型（不依赖 VCL、OString、OUString 等）。
 */
void WriteInstructionToStream(std::ostream& out, const RenderInstruction& inst);