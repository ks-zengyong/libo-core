# 测试差异对比与迭代修复工作流

## 0. 调用约定（重要）

> **本技能已封装完整的生成与对比流程，大模型应直接调用已有脚本，不要自行拼接命令或手动生成文件。**

### 生成产物

调用 `aproj/docx/test/` 下的 Python 脚本来生成 nodes、frame、vcl 文件，产物自动写入 `aproj/docx/test/` 目录：

| 脚本 | 作用 | 产物 |
|------|------|------|
| `python test/gen_aproj.py` | 编译 aproj/docx 项目并运行端到端测试（docx_e2e_test），生成 Nodes 结构、Frame 树和 VCL 渲染指令记录 | `test/aproj_nodes.txt`, `test/aproj_frame.txt`, `test/aproj_vcl.txt` |
| `python test/gen_lo.py` | 启动 LibreOffice soffice 无界面模式转换文档，生成 LO 侧的 Nodes 结构、Frame 树和 VCL 渲染指令参考记录 | `test/lo_nodes.txt`, `test/lo_frame.txt`, `test/lo_vcl.txt` |

**不要**自己调用 `docx_e2e_test.exe` 或 LibreOffice `soffice` 来生成这些文件，**不要**自己编写生成脚本。

### 差异对比

调用 `aproj/docx/test/` 下的 bat 脚本来执行对比，脚本自动引用同目录下的上述产物文件：

| 脚本 | 对比内容 |
|------|----------|
| `.\test\diff_node.bat` | 对比 aproj 与 LO 的 Nodes 文档节点树结构差异 |
| `.\test\diff_frame.bat` | 对比 aproj 与 LO 的 Frame 布局树结构差异 |
| `.\test\diff_vcl.bat` | 对比 aproj 与 LO 的 VCL 渲染层绘制指令差异 |

**不要**自己调用 `render_diff.exe`（渲染指令差异对比工具）并手动拼接路径参数，**不要**自己编写对比脚本。

### 标准操作序列

```powershell
# 1. 编译 aproj/docx 项目（Debug 配置）
.\build.bat

# 2. 生成 LibreOffice 侧参考输出（Nodes/Frame/VCL）
python test\gen_lo.py

# 3. 生成 aproj/docx 侧输出（Nodes/Frame/VCL）
python test\gen_aproj.py

# 4. 逐级对比差异
.\test\diff_node.bat    # Nodes 文档节点树结构对比
.\test\diff_frame.bat   # Frame 布局树结构对比
.\test\diff_vcl.bat     # VCL 渲染指令对比
```

按此顺序执行即可，无需额外操作。

---

## 1. 概述

aproj/docx 的目标是产出与 LibreOffice **0 差异**的排版结果。为了实现这个目标，我们采用**三级差异对比**的方式量化 aproj/docx 与 LibreOffice 的输出差距，然后按差异项逐一定位 LO 源码中未迁移的逻辑，迭代修复。

三级对比分别为：
- **Step 1 — Nodes 结构对比**：对比解析后的文档节点树结构
- **Step 2 — Frame 树对比**：对比解析排版后的 Frame 布局树结构
- **Step 3 — VCL 渲染指令对比**：对比最终渲染层的绘制指令

## 2. 总体架构

```
samples/*.docx
  │
  ├── aproj/docx  ──┬──→ aproj_nodes.txt ──┐
  │                 ├──→ aproj_frame.txt ──┤
  │                 └──→ aproj_vcl.txt  ───┤
  │                                        ├──→ render_diff → 差异列表 → 定位 LO 源码 → 迁移修复
  └── LibreOffice ──┬──→ lo_nodes.txt ────┤
                    ├──→ lo_frame.txt ─────┤
                    └──→ lo_vcl.txt ───────┘
```

**核心原则**：
- 两边的生成逻辑和数据结构必须一致，确保 `render_diff` 对比的是逻辑差异而非格式差异
- Nodes 层、Frame 层和 VCL 层**独立输出**，不合并
- 数据格式统一为 TSV（Tab-Separated Values），每条指令一行，字段通过 `\t` 分隔
- 指令格式定义在共享头文件 `sw/source/core/inc/render_instruction.h` 中

## 3. Step 1: Nodes 结构差异对比

### 3.1 Nodes 层的数据结构

Nodes 层记录的是**文档解析后的节点树结构**，即 SwNodes 中所有节点的层级关系和属性信息。当前支持的节点类型：

| 指令类型 | 字段 | 说明 |
|---------|------|------|
| `START_NODE` | nodeId, nodeType | 节点开始，附带节点类型（如 Normal, TableBox 等） |
| `END_NODE` | nodeId | 节点结束 |
| `TEXT_NODE` | nodeId, text, styleName | 文本节点，附带文本内容和样式名 |

格式一致性由 `NodesLogger` 类保证——aproj/docx 和 LO 两侧使用相同的输出格式。

### 3.2 aproj/docx 生成方式

**入口**：`docx_e2e_test` 可执行文件（编译自 `test/test_end_to_end.cpp`）

**数据流**：
```
samples/*.docx
  → DocxParser::Read()             (解析 OOXML)
  → SwDoc                          (构建文档模型)
  → NodesLogger::LogNodes()        (遍历 SwNodes，生成节点记录)
  → WriteToFile()                  (写入 aproj_nodes.txt)
```

**关键实现文件**：
- `test/test_end_to_end.cpp` — 端到端测试中调用 NodesLogger
- `src/render/nodes_log.cpp` — `NodesLogger` 实现节点树遍历和输出

### 3.3 LibreOffice 生成方式

LibreOffice 侧通过环境变量 `SW_NODES_LOG` 控制输出：

| 环境变量 | 输出内容 |
|----------|----------|
| `SW_NODES_LOG` | Nodes 结构记录 |

**数据流**：
```
样本文档
  → soffice --headless --convert-to pdf  (强制完全排版)
  → SwDoc → SwNodes
  → NodesLogger                          (遍历节点树，生成记录)
  → 写入 SW_NODES_LOG 指定的文件
```

### 3.4 对比方式

```powershell
# 对比 Nodes 层
.\test\diff_node.bat
```

或直接使用 `render_diff`：
```powershell
.\output\render_diff_debug.exe node      # 对比 Nodes 层（Debug 产物）
```

对比工具 `tools/render_diff.cpp` 进行严格逐字段比对，不允许容差（tolerance）。

## 4. Step 2: Frame 树差异对比

### 4.1 Frame 层的数据结构

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

### 4.2 aproj/docx 生成方式

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
| `aproj_nodes.txt` | Nodes 结构记录 |

### 4.3 LibreOffice 生成方式

LibreOffice 侧在 `sw` 模块中内置了 `SwPaintEventListener`（位于 `sw/source/core/inc/`），通过环境变量控制输出：

| 环境变量 | 输出内容 |
|----------|----------|
| `SW_RENDER_LOG` | Frame 层指令 |
| `SW_VCL_RENDER_LOG` | VCL 层绘制指令 |
| `SW_NODES_LOG` | Nodes 结构记录 |

**数据流**：
```
样本文档
  → soffice --headless --convert-to pdf  (强制完全排版)
  → SwDoc → 排版 → SwRootFrame
  → PaintSwFrame() → GDIMetaFile         (绘制 → 元文件记录)
  → SwPaintEventListener                 (将绘制调用转为 RenderInstruction)
  → 分别写入 SW_RENDER_LOG 和 SW_VCL_RENDER_LOG 指定的文件
```

### 4.4 Python 脚本约定

所有生成脚本统一放在 `test/` 目录：

| 脚本 | 职责 | 输入 | 输出 |
|------|------|------|------|
| `test/gen_aproj.py` | 编译并运行 aproj e2e test，收集产物 | `samples/*.docx` | `test/aproj_frame.txt`, `test/aproj_vcl.txt`, `test/aproj_nodes.txt` |
| `test/gen_lo.py` | 启动 soffice 生成 LO 参考输出 | `samples/sample0.docx` | `test/lo_frame.txt`, `test/lo_vcl.txt`, `test/lo_nodes.txt` |

### 4.5 对比方式

```powershell
# 对比 Nodes 层
.\test\diff_node.bat

# 对比 Frame 层
.\test\diff_frame.bat

# 对比 VCL 层
.\test\diff_vcl.bat
```

或直接使用 `render_diff`：
```powershell
.\output\render_diff_debug.exe node      # 对比 Nodes 层（Debug 产物）
.\output\render_diff_debug.exe frame     # 对比 Frame 层（Debug 产物）
.\output\render_diff_debug.exe vcl       # 对比 VCL 层（Debug 产物）
```

对比工具 `tools/render_diff.cpp` 进行严格逐字段比对，不允许容差（tolerance）。支持 `--known-diffs` 跳过已知差异，`--verbose` 显示匹配行。

> **注意**：`render_diff_debug.exe` 的默认 `--test-dir` 为 `../test`（相对于 exe 所在目录 `output/`），即自动定位到 `aproj/docx/test/`。使用快捷模式 `frame`/`vcl` 时无需额外指定路径。

## 5. Step 3: VCL 渲染指令差异对比（VCL 接入待完成）

### 5.1 VCL 层的数据结构

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
| `POLYGON` | pageNum, x, y, w, h | 多边形绘制 |
| `POLYLINE` | pageNum, x1, y1, x2, y2 | 折线绘制 |
| `BITMAP` | pageNum, x, y, w, h | 位图绘制 |
| `PUSH` / `POP` | pageNum | 状态保存/恢复 |

### 5.2 当前状态：简化 OutputDevice

aproj/docx 当前在 `src/render/render_output_device.cpp` 中实现了一个最小 `RenderInstructionOutputDevice`——它提供了 `DrawText()`、`DrawRect()`、`SetFont()` 等基础方法，并在每个方法中直接调用 `Build*Instruction()`。

**限制**：
- 不经过 LO 的 VCL 渲染管线（字体替代、颜色管理、坐标变换等）
- VCL 自身对输出的影响（如 `ImplFontSubstitute`、`Push/Pop` 状态管理、裁剪区域等）无法体现

### 5.3 目标状态：接入 libo-core VCL 模块

**目标**：aproj/docx 直接链接 `libo-core/vcl/` 模块，在 Frame 树上的 `PaintSwFrame()` 调用使用真实的 `OutputDevice`（而非简化的 `RenderInstructionOutputDevice`）。

**涉及模块**：
| 模块 | 路径 | 职责 |
|------|------|------|
| outdev | `vcl/source/outdev/` | OutputDevice 核心：文字、图形、位图绘制 |
| gdi | `vcl/source/gdi/` | GDIMetaFile、打印、区域管理 |
| font | `vcl/source/font/` | 字体管理、替换 |
| text | `vcl/source/text/` | 文字布局（HarBuzz 集成） |

## 6. 差异对比与修复工作流

### 6.1 执行顺序

```
              ┌─ Step 1: Nodes 结构对比 ──→ Nodes 差异为 0？ ──No──→ 修复 Nodes 层差异
              │                                    │
              │                                   Yes │
              │                                    ▼
samples/*.docx┤                         Step 2: Frame 树对比 ──→ Frame 差异为 0？ ──No──→ 修复 Frame 层差异
              │                                              │
              │                                             Yes │
              │                                              ▼
              └─ Step 3: VCL 指令对比 ──→ VCL 差异为 0？ ───No──→ 修复 VCL 层差异
                                              │
                                         Yes  │
                                              ▼
                                         目标达成：0 差异
```

Nodes 结构差异最先对比，因为解析阶段的节点树差异会影响后续排版；Frame 树差异基本消除后再进入 VCL 层对比，因为 VCL 层差异可能源自 Frame 树的布局差异。

### 6.2 差异定位策略

每一条差异记录包含：
- `refLine` / `testLine`：LO 侧和 aproj 侧的行号
- `description`：差异描述（哪个字段、ref 值、test 值）

**定位流程**：
1. 根据差异类型（如 `TEXT_FRAME` 的 `x` 坐标偏差）确定涉及的排版逻辑
2. 在 `libo-core/sw/source/core/layout/` 中搜索对应的排版计算代码
3. 将缺失或有差异的逻辑迁移到 `aproj/docx/src/layout/` 或相关模块
4. 重新运行测试验证

### 6.3 迭代策略

- **不追求差异总数递减**：差异总数可能受指令数量变化影响，不必强约束
- **按序逐个解决**：按差异出现顺序（行号顺序），逐个分析、定位、修复
- **每次修复后验证**：修复后重新运行 `render_diff`，确认目标差异消除且未引入新差异
- **已知差异管理**：暂时无法解决的差异写入 `test/known_diffs.txt`，标记为 `[KNOWN]`

### 6.4 标准修复步骤

```
1. 查看差异: .\test\diff_node.bat (Nodes 层)
              .\test\diff_frame.bat (Frame 层)
              .\test\diff_vcl.bat (VCL 层)
     ↓
2. 定位 LO 源码: 在 libo-core/sw/ 中搜索差异相关的解析/排版/渲染逻辑
     ↓
3. 迁移适配: 将 LO 实现移植到 aproj/docx/src/ 对应模块
     ↓
4. 编译验证: cmake --build build --config Debug && build.bat
     ↓
5. 差异复检: .\test\diff_node.bat
              .\test\diff_frame.bat
              .\test\diff_vcl.bat
     ↓
6. 循环: 如差异未消除，回到步骤 2
```

## 7. 脚本与产物约定

### 7.1 目录结构

```
aproj/docx/
  ├── output/
  │   ├── docx_e2e_test_debug.exe      # Debug 编译产物
  │   ├── render_diff_debug.exe        # Debug 编译产物
  │   ├── docx_e2e_test.exe            # Release 编译产物
  │   └── render_diff.exe              # Release 编译产物
  ├── test/
  │   ├── test_end_to_end.cpp        # 端到端测试（C++）
  │   ├── gen_aproj.py               # 运行 e2e 测试并收集产物
  │   ├── gen_lo.py                  # 生成 LO 参考输出
  │   ├── diff_frame.bat             # Frame 层差异比对脚本
  │   ├── diff_vcl.bat               # VCL 层差异比对脚本
  │   ├── aproj_frame.txt            # aproj Frame 层产物
  │   ├── aproj_vcl.txt              # aproj VCL 层产物
  │   ├── lo_frame.txt               # LO Frame 层产物
  │   ├── lo_vcl.txt                 # LO VCL 层产物
  │   └── known_diffs.txt            # 已知差异清单
  ├── tools/
  │   └── render_diff.cpp            # 差异对比工具
  └── samples/
      └── *.docx                     # 测试样本文档
```

### 7.2 命名约定

- **产物文件**：`{prefix}_{type}.txt`
  - `prefix`：`aproj` 或 `lo`
  - `type`：`frame`、`vcl` 或 `nodes`
- **Python 脚本**：`gen_{prefix}.py`

### 7.3 一体化测试流程

```powershell
# 1. 编译项目（Debug 配置，自动拷贝到 output/）
.\build.bat

# 2. 生成 LO 参考输出
python test\gen_lo.py

# 3. 运行 aproj e2e 测试
python test\gen_aproj.py

# 4. 比对差异（使用 output/ 下的 debug 产物）
.\test\diff_node.bat
.\test\diff_frame.bat
.\test\diff_vcl.bat
```

## 8. 当前进度

| 步骤 | 组件 | 状态 |
|------|------|------|
| Step 1 | Nodes 结构记录（aproj 侧） | 已完成 — `NodesLogger::LogNodes()` + `WriteToFile()` |
| Step 1 | Nodes 结构记录（LO 侧） | 已完成 — `SW_NODES_LOG` 环境变量触发 |
| Step 1 | bat 差异比对脚本 | 已完成 — `test/diff_node.bat`、`test/diff_frame.bat`、`test/diff_vcl.bat` |
| Step 2 | Frame 树记录（aproj 侧） | 已完成 — `RenderLogger::LogFrameTree()` + `WriteFrameLayerToFile()` |
| Step 2 | Frame 树记录（LO 侧） | 已完成 — `SW_RENDER_LOG` 环境变量触发 |
| Step 3 | VCL 模块接入 aproj/docx | **待完成** — 当前为简化 `RenderOutputDevice` |
| Step 3 | VCL 记录（aproj 侧） | 待 VCL 接入后完成 |
| Step 3 | VCL 记录（LO 侧） | 已完成 — `SW_VCL_RENDER_LOG` 环境变量触发 |
| - | render_diff 对比工具 | 已完成 — `tools/render_diff.cpp` |
| - | Python 生成脚本 | 已完成 — `test/gen_aproj.py`、`test/gen_lo.py` |
| 迭代修复 | 差异驱动修复工作流 | 进行中 |

## 9. 相关文件索引

| 文件 | 说明 |
|------|------|
| `test/test_end_to_end.cpp` | 端到端测试入口 |
| `src/render/render_log.cpp` | Frame 树遍历 + TSV 指令记录 |
| `src/render/nodes_log.cpp` | Nodes 结构遍历 + 记录 |
| `src/render/render_output_device.cpp` | 简化的 OutputDevice，Draw → RenderInstruction |
| `src/render/render_log.h` | RenderLogger 接口 |
| `src/render/output_device.h` | OutputDevice 抽象接口 |
| `tools/render_diff.cpp` | 渲染指令逐行比对工具 |
| `test/gen_aproj.py` | 运行 e2e 测试并收集产物 |
| `test/gen_lo.py` | 生成 LO 参考输出 |
| `test/diff_node.bat` | Nodes 层差异比对 |
| `test/diff_frame.bat` | Frame 层差异比对 |
| `test/diff_vcl.bat` | VCL 层差异比对 |
| `CMakeLists.txt` | 构建配置 |
| `libo-core/sw/source/core/inc/instruction_builder.h` | 共享指令构建器 |
| `libo-core/sw/source/core/inc/render_instruction.h` | 共享指令数据结构 |
| `libo-core/vcl/` | LO VCL 模块（待接入） |