# TODO3.md — 渲染指令记录重构进展

## 一、核心理念

**aproj 必须使用与 VCL 相同的 `OutputDevice` 接口进行渲染。**

两侧使用同一套渲染代码路径，任何渲染指令差异**必定**来自排版逻辑（sw 模块），而非渲染架构差异。这使得我们可以集中精力检查 sw 实现逻辑，找到问题并加入 aproj。

```
LibreOffice:  SwTextFrame::PaintSwFrame → OutputDevice::DrawText → GDIMetaFile → RenderInstruction
aproj:        SwTextFrame::PaintSwFrame → OutputDevice::DrawText → RenderInstructionOutputDevice → RenderInstruction
```

---

## 二、已完成工作

### Phase 1-4: 基础设施（参考 TODO2.md）

| 阶段 | 状态 | 说明 |
|------|------|------|
| Phase 1: SwDoc 管线修复 | ✅ 完成 | InitLayout/MakeFrames 正确存储 RootFrame |
| Phase 2: 共享渲染指令格式 | ✅ 完成 | `render_instruction.h` POD 结构体 + TSV 格式 |
| Phase 3: aproj 侧实现 | ✅ 完成 | RenderLogger + 端到端测试 + 比对工具 |
| Phase 4: LibreOffice 侧植入 | ✅ 完成 | SwPaintEventListener 在 PaintSwFrame 中植入 |

### Phase 5: VCL 层渲染指令录制（LibreOffice 侧）

利用 VCL 已有的 `GDIMetaFile::Record()` 机制，在 PaintSwFrame 流程中透明录制所有 `OutputDevice::Draw*` 调用，转换为 `RenderInstruction`。

| 文件 | 修改类型 | 说明 |
|------|----------|------|
| `sw/source/core/inc/render_instruction.h` | 修改 | 新增 POLYGON/BITMAP/ELLIPSE/POLYLINE/SET_FONT/SET_LINE_COLOR/SET_FILL_COLOR/SET_TEXT_COLOR/SET_CLIP_REGION/PUSH/POP 指令类型 |
| `sw/source/core/inc/meta_to_instruction.hxx` | 新建 | MetaAction → RenderInstruction 转换器头文件 |
| `sw/source/core/layout/meta_to_instruction.cxx` | 新建 | 转换器实现，维护绘制状态上下文（字体、颜色等） |
| `sw/source/core/inc/paint_listener.hxx` | 修改 | 新增 VCL 层录制成员（GDIMetaFile、MetaToInstructionConverter）和方法 |
| `sw/source/core/layout/paint_listener.cxx` | 修改 | 实现 StartVclLog/StartPageRecord/StopPageRecordAndConvert，CheckEnvAndStart 检查 SW_VCL_RENDER_LOG |
| `sw/source/core/layout/paintfrm.cxx` | 修改 | 在页面循环中插入 StartPageRecord/StopPageRecordAndConvert |
| `sw/Library_sw.mk` | 修改 | 添加 meta_to_instruction 编译目标 |

**使用方式**：
```bash
# 同时启用 frame 层和 VCL 层日志
SW_RENDER_LOG=frame_render.txt SW_VCL_RENDER_LOG=vcl_render.txt \
  instdir/program/soffice.exe --headless tests/sample.docx
```

### Phase 6: aproj 统一渲染框架为 VCL 接口

让 aproj 的 `PaintSwFrame()` 通过与 VCL 对称的 `OutputDevice` 接口绘制，实现代码路径对称。

| 文件 | 修改类型 | 说明 |
|------|----------|------|
| `aproj/docx/src/render/output_device.h` | 新建 | 抽象 OutputDevice 接口，API 与 VCL 对称（DrawText/DrawRect/DrawLine/SetFont/SetTextColor 等） |
| `aproj/docx/src/render/render_output_device.h` | 新建 | RenderInstructionOutputDevice 头文件 |
| `aproj/docx/src/render/render_output_device.cpp` | 新建 | 实现：将 Draw* 调用转换为 RenderInstruction，输出到 RenderInstructionSink |
| `aproj/docx/src/frame/frame.h` | 修改 | PaintSwFrame() 改为接受 OutputDevice* 参数 |
| `aproj/docx/src/frame/frame.cpp` | 修改 | SwTextFrame::PaintSwFrame 实现：通过 OutputDevice 设置字体、绘制文本 |
| `aproj/docx/src/render/render_log.h` | 修改 | 移除不再需要的 LogFrameRecursive |
| `aproj/docx/src/render/render_log.cpp` | 修改 | LogFrameTree 改为创建 RenderInstructionOutputDevice，通过 PaintSwFrame(&aOutDev) 绘制 |
| `aproj/docx/CMakeLists.txt` | 修改 | 添加 render_output_device.cpp 编译目标 |

### Phase 7: 共享指令构建模块（代码复用）

将 RenderInstruction 的构建逻辑提取为共享模块 `instruction_builder.h`，消除两侧的代码重复。所有 Build* 函数使用原始类型（int, const char*, uint32_t），不依赖任何 VCL 或 aproj 类型，两端直接调用。

| 文件 | 修改类型 | 说明 |
|------|----------|------|
| `sw/source/core/inc/instruction_builder.h` | 新建 | 共享的 Build*Instruction() 内联函数集，依赖 render_instruction.h |
| `sw/source/core/layout/meta_to_instruction.cxx` | 修改 | Emit* 函数改为调用 Build*Instruction()，仅保留 VCL 类型转换 |
| `aproj/docx/src/render/render_output_device.cpp` | 修改 | Draw*/Set* 方法改为调用 Build*Instruction()，仅保留 aproj 类型适配 |
| `sw/source/core/inc/render_instruction.h` | 修改 | 添加 `#include <cstring>`（memset 依赖） |
| `aproj/docx/src/render/render_instruction.h` | 删除 | 不再维护副本，aproj 通过 include path 引用 sw 版本 |
| `aproj/docx/CMakeLists.txt` | 修改 | 添加 sw/source/core/inc 到 include path |

**架构原则**：
```
                 instruction_builder.h (共享，零依赖)
                /                                      \
  meta_to_instruction.cxx                  render_output_device.cpp
  (VCL 类型 → 原始类型 → Build*)           (aproj 类型 → 原始类型 → Build*)
```

两侧各自负责类型转换（VCL Point → int 或 aproj Point → int），但指令构建逻辑 100% 共享。

---

## 三、当前架构

```
┌──────────────────────────────────────────────────────────────────┐
│  共享模块                                                        │
│  render_instruction.h  — POD 结构体定义                           │
│  instruction_builder.h — Build*Instruction() 指令构建函数         │
├──────────────────────────────────────────────────────────────────┤
│  LibreOffice (sw)                                                │
│  SwRootFrame::PaintSwFrame(rRenderContext)                       │
│    → SwTextFrame::PaintSwFrame → rRenderContext.DrawText()       │
│    → GDIMetaFile 录制 MetaAction                                  │
│    → SwPaintEventListener (frame 层: PAGE_START/TEXT_FRAME)      │
│    → MetaToInstructionConverter (VCL 层: RECT/LINE/TEXT_RUN/...) │
│      → VCL 类型转换 + Build*Instruction() ← 共享                │
│    → 输出: frame_render.txt + vcl_render.txt                     │
├──────────────────────────────────────────────────────────────────┤
│  aproj/docx                                                      │
│  RenderLogger::LogFrameTree(pRoot)                               │
│    → 创建 RenderInstructionOutputDevice(&sink, pageNum)          │
│    → SwTextFrame::PaintSwFrame(&outDev)                          │
│    → outDev->SetFont() + outDev->DrawText()                      │
│      → aproj 类型适配 + Build*Instruction() ← 共享              │
│    → 输出: render_output.txt                                     │
├──────────────────────────────────────────────────────────────────┤
│  比对                                                            │
│  render_diff frame_render.txt render_output.txt                  │
│  → 差异 = 排版逻辑差异（sw 模块），非渲染架构差异               │
└──────────────────────────────────────────────────────────────────┘
```

---

## 四、编译状态

| 项目 | 状态 |
|------|------|
| LibreOffice `make sw` | ✅ 编译通过 |
| aproj `docx_core` | ✅ 编译通过 |
| aproj `docx_e2e_test` | ✅ 28/28 通过 |
| aproj `docx_test` | ✅ 19/19 通过 |
| aproj `render_diff` | ✅ 编译通过 |

---

## 五、后续待完成

### 1. 端到端比对测试（已执行）

```bash
# LibreOffice 侧生成渲染指令 (PDF 触发完整渲染管线)
SW_RENDER_LOG=lo_frame.txt SW_VCL_RENDER_LOG=lo_vcl.txt \
  instdir/program/soffice.exe --headless --convert-to pdf aproj/docx/tests/sample.docx

# aproj 侧生成渲染指令
cd aproj/docx/build/Debug
./docx_e2e_test ../../tests/sample.docx

# 比对（frame 层 vs frame 层）
./render_diff lo_frame.txt render_output.txt --tolerance 10
```

**首次比对结果**: FAIL — 7 个差异

| 差异 | 根因 | 状态 |
|------|------|------|
| 页面尺寸不同 (12240×15840 vs 11906×16838) | aproj 默认页面尺寸与 LibreOffice 不一致 | ✅ 已修复（ParseSectionProps 更新默认 PageDesc） |
| TEXT_FRAME 缺失 | MakeFramesForNode 未设置 Frame 区域 | ✅ 已修复（设置 Frame 位置/尺寸） |
| 文本乱码 | ParseParagraph 未调用 ParseRunProps | ✅ 已修复（解析 w:rPr） |
| 指令类型不匹配 (TEXT_FRAME vs TEXT_RUN) | LibreOffice frame 层输出 TEXT_FRAME，aproj 输出 VCL 层 TEXT_RUN | ⚠️ 架构差异（见下方说明） |
| SET_FONT/SET_TEXT_COLOR 多余指令 | aproj 输出 VCL 层状态指令，LibreOffice frame 层不输出 | ⚠️ 架构差异 |
| WriteToFile 指针失效 | 字符串指针在 OnInstruction 后悬空 | ✅ 已修复（移除 WriteToFile 调用） |

**已修复的预存 Bug**:
- `SwNodes::InitNodes()` 未创建 EndOfContent 哨兵节点 → 解引用空指针导致 segfault
- `SwFrame::InsertBehind()` 未更新父节点的 m_pLower → 子 Frame 链表断裂
- `SwPageFrame::PreparePage()` 创建 SwBodyFrame 后未插入 → Body 未关联到页面
- VCL 层日志写入 frame 文件 → 添加 m_bConvertingVcl 标志分离输出
- DumpFrameTreeXml/测试只遍历 page 直接子节点 → 改为递归遍历 body 子节点

**架构差异说明**：

aproj 当前通过 `OutputDevice::Draw*` 接口录制，输出的是 VCL 层指令（TEXT_RUN、SET_FONT、SET_TEXT_COLOR 等），与 LibreOffice 的 VCL 层日志（lo_vcl.txt）对称。LibreOffice 的 frame 层日志（lo_frame.txt）输出的是语义层指令（TEXT_FRAME、PAGE_START 等）。

aproj 没有接入 VCL 进行渲染，因此没有 VCL 层的 GDIMetaFile 录制。aproj 的 OutputDevice 录制本质上等同于 VCL 层录制。要实现 frame 层录制，需要在 aproj 的 PaintSwFrame 流程中额外输出 TEXT_FRAME 等语义指令。

### 2. 扩展 aproj 的 PaintSwFrame 实现

当前 SwTextFrame::PaintSwFrame 已实现文本绘制。需要扩展：

- **SwTabFrame::PaintSwFrame** — 表格边框（DrawRect/DrawLine）
- **SwRowFrame::PaintSwFrame** — 表格行
- **SwCellFrame::PaintSwFrame** — 表格单元格
- **图片 Frame** — DrawBitmap
- **FlyFrame** — 浮动对象

每新增一种元素，其 PaintSwFrame 实现自然调用 OutputDevice 的 Draw* 方法，渲染指令自动被记录。

### 3. 测试文件准备

- 在 `aproj/docx/tests/` 目录下准备测试 .docx 文件
- 覆盖场景：纯文本、表格、图片、多页、脚注等
- 更新 `known_diffs.txt` 记录已知差异

### 4. 比对结果处理

- 首次运行预期存在差异（IMAGE_FRAME、Header、Footer、FlyFrame 等尚未支持的元素）
- 逐步修复 aproj 侧排版逻辑，直到 render_diff 输出 PASS
- 最终目标：aproj/docx 的渲染指令与 LibreOffice 完全一致（±10 twips 容差内）

### 5. aproj VCL 层渲染指令录制（待实现）

aproj 当前通过 `OutputDevice::Draw*` 接口录制，本质上等同于 VCL 层录制。但 aproj 没有接入 VCL，因此：
- 没有 `GDIMetaFile::Record()` 机制
- 没有 `MetaAction` → `RenderInstruction` 的转换

**两种实现路径**：

1. **当前路径（OutputDevice 录制）**：aproj 的 `RenderInstructionOutputDevice` 直接在 `Draw*` 调用时生成 `RenderInstruction`，等效于 VCL 层录制。已实现且工作正常。

2. **接入 VCL 路径**：让 aproj 使用 VCL 的 `OutputDevice` 进行渲染，然后通过 `GDIMetaFile` 录制。需要：
   - 链接 VCL 库
   - 使用 `vcl::RenderContext` 替代自定义 `OutputDevice`
   - 复用 LibreOffice 的 `MetaToInstructionConverter`

当前路径已满足"统一渲染框架"的核心目标：两侧都通过 `OutputDevice` 接口绘制，共享 `instruction_builder.h` 构建指令。差异仅在于 LibreOffice 额外有 frame 层语义录制（TEXT_FRAME 等）。

### 6. 测试文件扩展

当前测试仅覆盖纯文本场景。需要扩展：
- 表格（TABLE_FRAME/TABLE_ROW/TABLE_CELL）
- 图片（IMAGE_FRAME）
- 混合格式（粗体、斜体、颜色）
- 多页文档
- 列表/编号
