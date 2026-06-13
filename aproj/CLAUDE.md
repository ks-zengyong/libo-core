# CLAUDE.md — aproj/docx 项目硬性标准与指导

## 项目主旨

将 LibreOffice 关于 DOCX 组件的核心代码迁移重构到 `aproj/docx` 下。目标是实现一个独立的 DOCX 解析器和排版引擎，最终渲染输出与 LibreOffice 做到 **0 差异**。

## 技术约束（硬性）

1. **LibreOffice 源码位置**: `libo-core` 下，主要在 `sw` 模块（解析文档和排版）
2. **最小实现但功能完整**: 迁移 LibreOffice 关于 DOCX 文档核心最小实现，但功能要完整
3. **0 差异目标**: 最终文档排版效果和 LibreOffice 做到 0 差异
4. **三方库复用**: `libo-core` 依赖的三方库，如有必要 `aproj/docx` 也需要引进，而不是自行实现。切记只迁移 DOCX 文档相关的核心实现
5. **禁止硬编码**: 不能通过写死逻辑或者为了解决问题不考虑通用逻辑自行进行硬编码，需要参考 `libo-core` 源代码来迁移适配
6. **参考源而非猜测**: 如果测试发现差异，一定是 LibreOffice 部分实现逻辑没有迁移适配到 `aproj/docx` 下。请参考 LibreOffice 源代码（位置在 `libo-core` 下）寻找代码位置然后迁移过来，而不要自行决策寻找解决方案

## 架构概览

```
docx_parser.cpp → SwDoc → SwNodes (解析 DOCX XML)
    ↓
frmtree.cpp → MakeFrames (SwTextFrame/SwPageFrame, 使用原始字体布局)
    ↓
render_log.cpp → RenderLogger → render 指令 (含字体替换)
    ↓
render_diff.exe → 与 LO 参考对比 (lo_frame.txt / lo_vcl.txt)
```

### 模块职责

| 模块 | 路径 | 职责 |
|------|------|------|
| **core** | `src/core/` | 文档模型：SwDoc, SwNodes, SwNode 层次结构, 样式系统 (SwFormat/SwTextFormatColl/SwPageDesc), 类型定义 |
| **filter** | `src/filter/` | DOCX 解析：ZIP 解压 (miniz) → XML 解析 (pugixml) → 文档模型构建。处理 styles, numbering, body, tables, sections, theme fonts |
| **font** | `src/font/` | 字体引擎：FontEngine 单例 + FontInstance 缓存。字体路径解析, 文本宽度/高度测量, 断行计算 |
| **frame** | `src/frame/` | 布局 Frame 树：SwFrame 层次结构, MakeFrames 构建页面/文本/表格 Frame, 分页/分节/多栏处理 |
| **layout** | `src/layout/` | 布局动作编排：SwLayAction 遍历页面 Frame 并格式化子元素 |
| **render** | `src/render/` | 渲染指令输出：OutputDevice 抽象接口, RenderLogger 记录指令并写 TSV, 字体替换规则 |

### 关键数据流

1. **解析阶段**: `DocxParser::Read()` → ZIP 解压 → XML 解析 → `SwDoc` (含 `SwNodes`, 样式, 页描述符)
2. **布局阶段**: `InitLayout()` → `MakeFrames()` → 构建 SwPageFrame/SwBodyFrame/SwTextFrame 树（使用原始字体度量）
3. **渲染阶段**: `SwLayAction::Action()` → `RenderLogger::LogFrameTree()` → 遍历 Frame 树输出 render 指令（含字体替换）
4. **比对阶段**: `render_diff.exe` 对比 `aproj_frame.txt` 与 `lo_frame.txt`

## 构建与测试

### 构建

```bash
cd aproj/docx
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Debug
```

- C++17, MSVC, `/W3 /utf-8`
- 三方库缺失时运行 `download_deps.ps1`

### 测试命令

```bash
cd aproj/docx/build
./Debug/docx_e2e_test.exe
./Debug/render_diff.exe ../tests/lo_frame.txt ../tests/aproj_frame.txt
```

### 测试标准

- **使用现有 `sample.docx`**（`aproj/docx/sample.docx`），**不要自己生成 docx 测试文件**
- 测试差异数目标为 **0**
- `known_diffs.txt` 中列出的已知差异（IMAGE_FRAME, Header, Footer, FlyFrame, Footnote, Endnote, Column）可暂时忽略

## 代码规范

### 命名规则

本项目命名与 LibreOffice 保持一致（这是有意为之——项目重新实现 LO 内部结构）：

- **类型名**: 与 LO 一致 — `SwDoc`, `SwNodes`, `SwTextNode`, `SwPageFrame`, `SwTwips`, `sal_uInt16`, `SwNodeOffset`
- **成员变量**: `m_` 前缀 — `m_pNodes`, `m_aAttrs`, `m_nSize`
  - 指针: `mp` — `mpRoot`, `mpNext`
  - 数值: `mn` — `mnFrameId`
  - 布尔: `mb` — `mbVertical`
  - 容器: `ma` — `m_aTextFormatColls`
- **参数**: `r` 前缀引用 (`rDoc`, `rNode`)，`p` 前缀指针 (`pRoot`, `pPage`)
- **枚举**: PascalCase — `SwNodeType::Start`, `SwFrameType::Page`, `RenderCmdType::TEXT_FRAME`
- **公有方法**: PascalCase — `MakeFrames`, `GetTextWidth`, `ParseParagraph`
- **内部/LO 镜像方法**: camelCase — `getFrameArea`, `setFrameArea`

### 注释规则

- 注释语言：中文为主，LO 源文件引用用英文
- 每个 `.cpp` 文件开头注释对应的 LO 源文件路径（如 "corresponds to LibreOffice's `sw/source/core/docnode/node.cxx`"）
- 段落分隔符使用 LLVM 风格：`//===---...---===//`
- 内联注释解释 LO 匹配逻辑、像素/twip 转换、设计决策

### Include 规范

- 本地头文件用引号：`#include "doc.h"`, `#include "../core/types.h"`
- 三方库用引号：`#include "pugixml.hpp"`, `#include "miniz.h"`
- 系统头文件用尖括号：`#include <cstdint>`, `#include <algorithm>`
- 使用相对路径（如 `../core/types.h`, `../../third_party/stb_truetype.h`）

### 通用模式

- `#pragma once` 作为所有头文件的 include guard
- `std::unique_ptr` 管理所有权（节点数组、页描述符、样式集合）
- 头文件中优先使用前向声明而非 include
- 多态基类使用虚析构函数；平凡析构函数用 `= default`
- 未使用参数用 `(void)param;` 标记
- 全局定义 `_CRT_SECURE_NO_WARNINGS` 以兼容 MSVC 的 `fopen`/`fread`

## 设计原则

1. **架构一致**: 虽然是迁移最小实现，但基本架构要与 LibreOffice 一致，模块划分清晰，数据结构设计合理
2. **单一职责**: 不要把所有逻辑写在一个文件。每个模块有明确的职责边界
3. **布局与渲染分离**: 布局阶段（`frmtree.cpp`）使用原始字体进行换行计算；字体替换仅在渲染阶段（`render_log.cpp`）进行——与 LO 行为一致
4. **共享接口**: 与 LO 侧共享 `render_instruction.h` 和 `instruction_builder.h`，确保指令构造完全一致

## 决策过滤器（遇到问题时的处理顺序）

1. **测试发现差异** → 一定是 LibreOffice 部分实现逻辑没有迁移适配。去 `libo-core` 源码中寻找对应实现
2. **不确定如何实现** → 先查看 LO 源码中对应的实现方式，迁移适配而非自行设计
3. **字体度量不匹配** → 参考 `vcl/source/font/fontmetric.cxx` 中 LO 的 `FontMetricData::ImplCalcLineSpacing` 逻辑
4. **行高计算不匹配** → 参考 `sw/source/core/txtnode/fntcache.cxx` 中 `SwFntObj::GetFontHeight` 逻辑
5. **Section 模型不匹配** → 参考 OOXML 规范：`body/sectPr` 定义最后一节，`paragraph/sectPr` 定义前面各节
6. **字体替换不匹配** → LO 在渲染阶段（OutputDevice）才进行字体替换，布局阶段使用原始字体

## 关键 LO 源码参考位置

| 功能 | LO 源文件 |
|------|-----------|
| 字体度量计算 | `vcl/source/font/fontmetric.cxx` |
| 行高计算 | `sw/source/core/txtnode/fntcache.cxx` |
| 节点层次结构 | `sw/source/core/docnode/node.cxx` |
| Frame 树构建 | `sw/source/core/layout/frmtool.cxx` |
| 文本排版 | `sw/source/core/text/txtfrm.cxx` |
| 属性系统 | `sw/source/core/attr/` |
| BigPtrArray | `sw/source/core/bastyp/bparr.cxx` |
| SwNodes | `sw/source/core/docnode/ndarr.cxx` |

## 三方库依赖

| 库 | 用途 | 位置 |
|----|------|------|
| **miniz** | ZIP 解压（DOCX 是 ZIP） | `third_party/miniz*` |
| **pugixml** | XML DOM 解析 | `third_party/pugixml.*` |
| **stb_truetype** | TrueType 字体加载和字形度量（回退方案） | `third_party/stb_truetype.h` |
| **Windows GDI** | 文本宽度/高度精确测量（首选方案，`#ifdef _WIN32`） | 系统库 `gdi32.lib` |

### 共享头文件（来自 LibreOffice）

- `sw/source/core/inc/render_instruction.h` — 共享 POD `RenderInstruction` 结构和 `RenderCmdType` 枚举
- `sw/source/core/inc/instruction_builder.h` — 共享内联 builder 函数，确保 LO 和 aproj 供建构指令完全一致

## 当前状态与已知问题

### 字体度量差异（核心阻塞项）

LO 使用 HarfBuzz 获取字体度量，aproj 使用 stb_truetype（hhea 表）。两者返回值不同：

| 字体 | stbtt | LO | 差异 |
|------|-------|-----|------|
| Segoe UI Semibold/36 | 478 | 508 | 30 |
| Poppins/24 | 256 | 258 | 2 |
| fony family/24 | 292 | 359 | 67 |

**已尝试但失败的方案**: 校准表、OS/2 sTypo 全局使用、usWinAscent/usWinDescent、GDI GetTextMetrics

**下一步**: 集成 HarfBuzz 替换 stb_truetype 进行字体度量计算

### LO 字体度量逻辑（`fontmetric.cxx`）

1. 先用 hhea 表 (ascent, descent, lineGap) via HarfBuzz
2. 如果 OS/2 存在且 hhea 为空或字体在 `FontsUseWinMetrics` 列表，用 usWinAscent/usWinDescent
3. 如果 `fsSelection` bit 7 (USE_TYPO_METRICS) 设置，用 sTypoAscender/sTypoDescender/sTypoLineGap

### LO 行高计算（`fntcache.cxx`）

- `nRet = GetTextHeight() + GetFontLeading()`
- `GetTextHeight = mnLineHeight + mnEmphasisAscent + mnEmphasisDescent`
- `mnLineHeight = FontMetric.GetAscent() + FontMetric.GetDescent()`
- `GetFontLeading` = external leading（如果 ADD_EXT_LEADING 文档设置启用）
