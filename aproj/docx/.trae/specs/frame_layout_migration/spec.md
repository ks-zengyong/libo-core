# LO Frame 构建与排版逻辑迁移 - Product Requirement Document

## Overview
- **Summary**: 分析 LibreOffice (LO) 的 frame 构建和排版调整逻辑，与当前 aproj/docx 的实现进行对比，识别差异并制定迁移适配计划。
- **Purpose**: 确保 aproj/docx 的 frame 构建和排版算法与 LO 完全一致，实现 0 差异目标。
- **Target Users**: aproj/docx 开发团队，用于指导代码迁移工作。

## Goals
- 全面分析 LO frame 构建和排版的核心逻辑
- 识别 aproj 与 LO 的关键差异点
- 制定系统化的迁移适配计划
- 确保最终输出与 LO 实现 0 差异

## Non-Goals (Out of Scope)
- 不涉及渲染层（VCL）的迁移
- 不涉及文档解析（filter）的迁移
- 不涉及字体引擎的迁移
- 不涉及 UI 交互逻辑的迁移

## Background & Context
根据项目规则，aproj/docx 是 LO DOCX 功能的忠实迁移，要求架构、数据结构、算法逻辑必须与 LO 一致。当前 aproj 的 frame 构建和排版逻辑存在多处简化，需要进行系统性的迁移适配。

## Functional Requirements
- **FR-1**: 完整迁移 LO 的 `MakeFrames` 逻辑
- **FR-2**: 完整迁移 LO 的 `SwLayAction` 排版动作逻辑
- **FR-3**: 完整迁移 LO 的 `SwFlowFrame` 分页流动逻辑
- **FR-4**: 完整迁移 LO 的浮动对象（Fly）管理和定位逻辑
- **FR-5**: 完整迁移表格、节、列、脚注等布局逻辑

## Non-Functional Requirements
- **NFR-1**: 迁移后的代码结构与 LO 保持一致
- **NFR-2**: 算法复杂度与 LO 保持一致
- **NFR-3**: 支持 LO 相同的边界情况和特殊场景
- **NFR-4**: 保持代码可追溯性，便于与 LO 源码对照

## Constraints
- **Technical**: C++ 语言，需要适配 LO 的数据结构和类型定义
- **Dependencies**: 依赖 LO 的核心数据结构定义（swrect.h, types.h 等）
- **Timeline**: 需要分阶段逐步迁移，避免一次性大改动

## Assumptions
- LO 源码位于 `libo-core/sw/source/core/layout/` 目录
- aproj 已具备基本的 frame 数据结构定义
- 需要迁移的核心文件已在 LO 中存在且可访问

## Acceptance Criteria

### AC-1: MakeFrames 逻辑完整迁移
- **Given**: LO 的 `frmtool.cxx` 中的 MakeFrames 逻辑
- **When**: 迁移到 aproj 的 `frmtree.cpp`
- **Then**: aproj 的 frame 构建结果与 LO 完全一致
- **Verification**: `programmatic` - 对比 frame 树输出

### AC-2: SwLayAction 逻辑完整迁移
- **Given**: LO 的 `layact.cxx` 中的排版动作逻辑
- **When**: 迁移到 aproj 的 `layact.cpp`
- **Then**: aproj 的排版结果与 LO 完全一致
- **Verification**: `programmatic` - 对比渲染输出

### AC-3: SwFlowFrame 逻辑完整迁移
- **Given**: LO 的 `flowfrm.cxx` 中的分页流动逻辑
- **When**: 迁移到 aproj 的 `frame.cpp`
- **Then**: aproj 的分页行为与 LO 完全一致
- **Verification**: `programmatic` - 对比分页结果

### AC-4: 浮动对象管理逻辑完整迁移
- **Given**: LO 的 `flyfrms.cxx`, `sortedobjs.cxx`, `objectformatter.cxx` 中的浮动对象逻辑
- **When**: 迁移到 aproj
- **Then**: aproj 的浮动对象定位与 LO 完全一致
- **Verification**: `programmatic` - 对比浮动对象位置

## Open Questions
- [ ] LO 的 Layouter 机制是否需要迁移？
- [ ] LO 的 FlyInCnt（文本内浮动对象）机制是否需要迁移？
- [ ] LO 的 ObjectFormatter 是否需要完整迁移？
