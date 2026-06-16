/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * 共享的节点指令 TSV 格式化输出
 *
 * 将 NodeInstruction 序列化为 TSV 格式。
 * sw 和 aproj/docx 共用的唯一实现，确保输出格式绝对一致。
 *
 * 公共模块: render_common/ — sw 和 aproj/docx 都引用此文件
 */

#pragma once

#include "node_instruction.h"
#include <ostream>

/**
 * 将单条节点指令格式化为 TSV 行并写入流。
 *
 * 每条指令独占一行，行首按照 nestLevel 输出对应数量的空格
 * （每级 2 个空格），之后才是 "TYPE\t字段1\t字段2\t..."。
 */
void WriteNodeInstructionToStream(std::ostream& out, const NodeInstruction& inst);
