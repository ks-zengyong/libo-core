# 测试差异对比与迭代修复工作流

## 1. 概述

aproj/docx 的目标是产出与 LibreOffice **0 差异**的排版结果。为了实现这个目标，我们采用**两级差异对比**的方式量化 aproj/docx 与 LibreOffice 的输出差距，然后按差异项逐一定位 LO 源码中未迁移的逻辑，迭代修复。

两级对比分别为：
- **Step 1 — Frame 树对比**：对比解析排版后的 Frame 布局树结构
- **Step 2 — VCL 渲染指令对比**：对比最终渲染层的绘制指令

## 2. 总体架构

```
samples/*.docx
  │
  ├── aproj/docx  ──┬──→ aproj_frame.txt ──┐
  │                 └──→ aproj_vcl.txt  ───┤
  │                                        ├──→ render_diff → 差异列表 → 定位 LO 源码 → 迁移修复
  └── LibreOffice ──┬──→ lo_frame.txt ─────┤
                    └──→ lo_vcl.txt ───────┘
```

**核心原则**：
- 两边的生成逻辑和数据结构必须一致，确保 `render_diff` 对比的是逻辑差异而非格式差异
- 数据格式统一为 TSV（Tab-Separated Values），每条指令一行，字段通过 `\t` 分隔
- 指令格式定义在共享头文件 `sw/source/core/inc/render_instruction.h` 中

## 3. Step 1: Frame 树差异对比（已完成）

### 3.1 Frame 层的数据结构

Frame 层记录的是**语义级布局信息**，即 Frame 树的拓扑结构和几何属性。当前支持的指令类型：

| 指令类型 | 字段 | 说明 |
|---------|------|------|
| `PAGE_START` | pageNum, width, height | 页面开始，附带页面尺寸 |
| `PAGE_END` | pageNum | 页面结束 |
| `TEXT_FRAME` | pageNum, x, y, w, h, text, fontName, fontSize, fontColor, fontWeight, fontItalic, underline, strikeout, styleName | 文本段落 Frame |
| `TEXT_LINE` | 同 TEXT_FRAME | 文本行 Frame |
| `TABLE_FRAME` | pageNum, x, y, w, h | 表格 Frame |
| `TABLE_ROW` | pageNum, x, y, w, h | 表格行 Frame |
| `TABLE_CELL` | pageNum, x, y, w, h | 表格单元格 Frame |
| `IMAGE_FRAME` | pageNum, x, y, w, h | 图片 Frame |
| `SECTION_FRAME` | pageNum, x, y, w, h | 节 Frame |

格式一致性由 `sw/source/core/inc/instruction_builder.h` 中的 `Build*Instruction()` 函数族保证——aproj/docx 和 LO 两侧共享同一个头文件。

### 3.2 aproj/docx 生成方式

**入口**：`docx_e2e_test` 可执行文件（编译自 `test/test_end_to_end.cpp`）

**数据流**：
```
samples/*.docx
  → DocxParser::Read()             (解析 OOXML)
  → SwDoc                          (构建文档模型)
  → InitLayout() + MakeFrames()    (创建 Frame 树)
  → SwLayAction::Action()          (执行排版)
  → RenderLogger::LogFrameTree()   (遍历 Frame 树，生成指令)
  → WriteFrameLayerToFile()        (写入 aproj_frame.txt)
```

**关键实现文件**：
- `src/render/render_log.cpp` — `LogFrameTree()` 遍历 Frame 树，`WriteFrameLayerToFile()` 写入 TSV
- `src/render/render_output_device.cpp` — `RenderInstructionOutputDevice` 将 Draw 调用转为 RenderInstruction

**当前生成产物**（在 `test/` 目录下）：
| 产物 | 内容 |
|------|------|
| `aproj_frame.txt` | Frame 层语义指令 |
| `aproj_vcl.txt` | VCL 层绘制指令 |
| `aproj_all.log` | 全量指令日志 |

### 3.3 LibreOffice 生成方式

LibreOffice 侧在 `sw` 模块中内置了 `SwPaintEventListener`（位于 `sw/source/core/inc/`），通过环境变量 `SW_RENDER_LOG` 触发记录。

**数据流**：
```
样本文档
  → soffice --headless                (无头模式打开文档)
  → SwDoc → 排版 → SwRootFrame
  → PaintSwFrame() → GDIMetaFile     (绘制 → 元文件记录)
  → SwPaintEventListener             (将绘制调用转为 RenderInstruction)
  → 写入 SW_RENDER_LOG 指定的文件
```

**生成命令**（Linux/WSL 环境）：
```bash
SW_RENDER_LOG=lo_frame.txt instdir/program/soffice --headless path/to/sample.docx
```

**注意**：LO 的 `SW_RENDER_LOG` 输出包含 frame 层和 vcl 层全部指令。与 aproj 侧一样，可通过 `IsFrameLayerInstruction()` / `IsVclLayerInstruction()` 筛选分层。

### 3.4 Python 脚本约定

Python 脚本统一放在 `test/` 目录，职责是调用两侧的编译产物生成对应的产物文件：

| 脚本 | 职责 | 输入 | 输出 |
|------|------|------|------|
| `test/gen_aproj_frame.py` | 调用 aproj e2e test 生成 frame 记录 | `samples/*.docx` | `test/aproj_frame.txt` |
| `test/gen_lo_frame.py` | 启动 soffice 生成 LO frame 记录 | `samples/*.docx` | `test/lo_frame.txt` |

### 3.5 对比方式

```bash
# 对比 frame 层
render_diff test/lo_frame.txt test/aproj_frame.txt

# 对比 VCL 层
render_diff test/lo_vcl.txt test/aproj_vcl.txt
```

对比工具 `tools/render_diff.cpp` 支持：
- `--tolerance N`：位置/尺寸容差（默认 10 twips）
- `--known-diffs F`：已知差异文件，标记为 [KNOWN] 不视为失败
- `--verbose`：同时输出匹配的行

## 4. Step 2: VCL 渲染指令差异对比（VCL 接入待完成）

### 4.1 VCL 层的数据结构

VCL 层记录的是**绘制级指令**——即最终渲染时对 `OutputDevice` 的实际调用。当前支持的指令类型：

| 指令类型 | 字段 | 说明 |
|---------|------|------|
| `PAGE_START` | pageNum, width, height | 页面开始 |
| `PAGE_END` | pageNum | 页面结束 |
| `SET_FONT` | pageNum, fontName, fontSize, fontWeight, fontItalic | 设置当前字体 |
| `SET_TEXT_COLOR` | pageNum, color | 设置文字颜色 |
| `SET_FILL_COLOR` | pageNum, color | 设置填充色 |
| `SET_LINE_COLOR` | pageNum, color | 设置线条色 |
| `SET_CLIP_REGION` | pageNum | 设置裁剪区域 |
| `TEXT_RUN` | pageNum, x, y, w, h, text, fontName, fontSize, fontColor, fontWeight, fontItalic | 文本绘制 |
| `RECT` | pageNum, x, y, w, h | 矩形绘制 |
| `LINE` | pageNum, x1, y1, x2, y2 | 线段绘制 |
| `ELLIPSE` | pageNum, x, y, w, h | 椭圆绘制 |
| `BITMAP` | pageNum, x, y, w, h | 位图绘制 |
| `PUSH` / `POP` | pageNum | 状态保存/恢复 |

### 4.2 当前状态：简化 OutputDevice

aproj/docx 当前在 `src/render/render_output_device.cpp` 中实现了一个最小 `RenderInstructionOutputDevice`——它提供了 `DrawText()`、`DrawRect()`、`SetFont()` 等基础方法，并在每个方法中直接调用 `Build*Instruction()`。

**限制**：
- 不经过 LO 的 VCL 渲染管线（字体替代、颜色管理、坐标变换等）
- VCL 自身对输出的影响（如 `ImplFontSubstitute`、`Push/Pop` 状态管理、裁剪区域等）无法体现

### 4.3 目标状态：接入 libo-core VCL 模块

**目标**：aproj/docx 直接链接 `libo-core/vcl/` 模块，在 Frame 树上的 `PaintSwFrame()` 调用使用真实的 `OutputDevice`（而非简化的 `RenderInstructionOutputDevice`）。

**涉及模块**：
| 模块 | 路径 | 职责 |
|------|------|------|
| outdev | `vcl/source/outdev/` | OutputDevice 核心：文字、图形、位图绘制 |
| gdi | `vcl/source/gdi/` | GDIMetaFile、打印、区域管理 |
| font | `vcl/source/font/` | 字体管理、替换 |
| text | `vcl/source/text/` | 文字布局（HarBuzz 集成） |

**VCL 的记录回放机制**：
- LO 的 VCL 原生支持通过 `GDIMetaFile` 记录所有绘制调用
- `SwPaintEventListener` 遍历 `GDIMetaFile` 中的 action 序列，转为 `RenderInstruction`
- aproj/docx 接入 VCL 后，`PaintSwFrame()` 将产生真实的 `GDIMetaFile`，再由共享的 `SwPaintEventListener` 转为指令

**CMake 接入要点**：
- `CMakeLists.txt` 需要新增 vcl 相关源文件和 include 路径
- 需要处理 vcl 依赖的平台后端（Windows: GDI/sal, Linux: X11/Cairo）

### 4.4 Python 脚本约定

| 脚本 | 职责 | 输入 | 输出 |
|------|------|------|------|
| `test/gen_aproj_vcl.py` | 调用 aproj e2e test 生成 vcl 记录 | `samples/*.docx` | `test/aproj_vcl.txt` |
| `test/gen_lo_vcl.py` | 启动 soffice 生成 LO vcl 记录 | `samples/*.docx` | `test/lo_vcl.txt` |

## 5. 差异对比与修复工作流

### 5.1 执行顺序

```
              ┌─ Step 1: Frame 树对比 ──→ Frame 差异为 0？ ──No──→ 修复 Frame 层差异
              │                              │
samples/*.docx┤                         Yes │
              │                              ▼
              └─ Step 2: VCL 指令对比 ──→ VCL 差异为 0？ ───No──→ 修复 VCL 层差异
                                              │
                                         Yes  │
                                              ▼
                                         目标达成：0 差异
```

Frame 树差异基本消除后再进入 VCL 层对比，因为 VCL 层差异可能源自 Frame 树的布局差异。

### 5.2 差异定位策略

每一条差异记录包含：
- `refLine` / `testLine`：LO 侧和 aproj 侧的行号
- `description`：差异描述（哪个字段、ref 值、test 值）

**定位流程**：
1. 根据差异类型（如 `TEXT_FRAME` 的 `x` 坐标偏差）确定涉及的排版逻辑
2. 在 `libo-core/sw/source/core/layout/` 中搜索对应的排版计算代码
3. 将缺失或有差异的逻辑迁移到 `aproj/docx/src/layout/` 或相关模块
4. 重新运行测试验证

### 5.3 迭代策略

- **不追求差异总数递减**：差异总数可能受指令数量变化影响，不必强约束
- **按序逐个解决**：按差异出现顺序（行号顺序），逐个分析、定位、修复
- **每次修复后验证**：修复后重新运行 `render_diff`，确认目标差异消除且未引入新差异
- **已知差异管理**：暂时无法解决的差异写入 `test/known_diffs.txt`，标记为 `[KNOWN]`

### 5.4 标准修复步骤

```
1. 查看差异: render_diff lo_frame.txt aproj_frame.txt
     ↓
2. 定位 LO 源码: 在 libo-core/sw/ 中搜索差异相关的排版/渲染逻辑
     ↓
3. 迁移适配: 将 LO 实现移植到 aproj/docx/src/ 对应模块
     ↓
4. 编译验证: cmake --build build && ./build/Debug/docx_e2e_test.exe
     ↓
5. 差异复检: render_diff lo_frame.txt aproj_frame.txt
     ↓
6. 循环: 如差异未消除，回到步骤 2
```

## 6. 脚本与产物约定

### 6.1 目录结构

```
aproj/docx/
  ├── test/
  │   ├── test_end_to_end.cpp        # 端到端测试（C++）
  │   ├── gen_aproj_frame.py         # 生成 aproj_frame.txt
  │   ├── gen_aproj_vcl.py           # 生成 aproj_vcl.txt
  │   ├── gen_lo_frame.py            # 生成 lo_frame.txt
  │   ├── gen_lo_vcl.py              # 生成 lo_vcl.txt
  │   ├── aproj_frame.txt            # aproj Frame 层产物
  │   ├── aproj_vcl.txt              # aproj VCL 层产物
  │   ├── lo_frame.txt               # LO Frame 层产物
  │   ├── lo_vcl.txt                 # LO VCL 层产物
  │   ├── known_diffs.txt            # 已知差异清单
  │   ├── nodes_dump.xml             # 节点转储（调试用）
  │   └── frmtree_dump.xml           # Frame 树转储（调试用）
  ├── tools/
  │   ├── render_diff.cpp            # 差异对比工具
  │   └── run_comparison_tests.ps1   # 自动化对比测试脚本
  └── samples/
      └── *.docx                     # 测试样本文档
```

### 6.2 命名约定

- **产物文件**：`{prefix}_{type}.txt`
  - `prefix`：`aproj` 或 `lo`
  - `type`：`frame` 或 `vcl`
- **Python 脚本**：`gen_{prefix}_{type}.py`，与产物命名对应

### 6.3 产物生成的一体化命令（建议）

```bash
# 一键生成所有产物并对比
python test/gen_lo_frame.py samples/sample0.docx
python test/gen_aproj_frame.py
render_diff test/lo_frame.txt test/aproj_frame.txt

python test/gen_lo_vcl.py samples/sample0.docx
python test/gen_aproj_vcl.py
render_diff test/lo_vcl.txt test/aproj_vcl.txt
```

## 7. 当前进度

| 步骤 | 组件 | 状态 |
|------|------|------|
| Step 1 | Frame 树记录（aproj 侧） | 已完成 — `RenderLogger::LogFrameTree()` + `WriteFrameLayerToFile()` |
| Step 1 | Frame 树记录（LO 侧） | 已完成 — `SW_RENDER_LOG` 环境变量触发 |
| Step 1 | render_diff 对比工具 | 已完成 — `tools/render_diff.cpp` |
| Step 1 | Python 生成脚本 | 待实现 — `test/gen_aproj_frame.py`、`test/gen_lo_frame.py` |
| Step 2 | VCL 模块接入 aproj/docx | **待完成** — 当前为简化 `RenderOutputDevice` |
| Step 2 | VCL 记录（aproj 侧） | 待 VCL 接入后完成 |
| Step 2 | VCL 记录（LO 侧） | 已完成 — LO 内置支持 |
| Step 2 | Python 生成脚本 | 待 VCL 接入后实现 — `test/gen_aproj_vcl.py`、`test/gen_lo_vcl.py` |
| 迭代修复 | 差异驱动修复工作流 | 进行中 — 已有 `render_diff` + `run_comparison_tests.ps1` |

## 8. 相关文件索引

| 文件 | 说明 |
|------|------|
| `test/test_end_to_end.cpp` | 端到端测试入口，自动扫描 samples/*.docx |
| `src/render/render_log.cpp` | Frame 树遍历 + TSV 指令记录 |
| `src/render/render_output_device.cpp` | 简化的 OutputDevice，Draw → RenderInstruction |
| `src/render/render_log.h` | RenderLogger 接口 |
| `src/render/output_device.h` | OutputDevice 抽象接口 |
| `tools/render_diff.cpp` | 渲染指令逐行比对工具 |
| `tools/run_comparison_tests.ps1` | 自动化对比测试脚本 |
| `tools/dump_lo.py` | LO 节点/布局导出（UNO 方式） |
| `tools/dump_lo_nodes.py` | LO 文档节点导出（UNO 方式） |
| `CMakeLists.txt` | 构建配置 |
| `libo-core/sw/source/core/inc/instruction_builder.h` | 共享指令构建器 |
| `libo-core/sw/source/core/inc/render_instruction.h` | 共享指令数据结构 |
| `libo-core/vcl/` | LO VCL 模块（待接入） |