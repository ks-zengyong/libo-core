# AGENTS — aproj/docx 项目上下文说明

> **用途**：本文件为 TRAE Agent 提供 `aproj/docx` 项目的整体上下文说明、硬性约束索引、技能（Skill）索引、参考资料索引、当前任务状态及典型工作流程。Agent 在对话开始时应读取本文件，以自动加载项目规则和技能，避免反复向用户索取相同信息。

---

## 1. 项目概览

`aproj/docx` 是一个**独立的 DOCX 解析与排版引擎**，从 `libo-core`（LibreOffice 源码）中迁移精炼而来。项目目标是：**使 aproj/docx 的解析结果、Frame 布局树、VCL 渲染指令与 LibreOffice 做到 0 差异**。

- **根目录**：`aproj/docx/`（相对于 `libo-core/` 即 `./aproj/docx/`）
- **参考源码**：`libo-core/sw/`（Writer 核心）、`libo-core/vcl/`（渲染层）、`libo-core/oox/`（OOXML 解析）
- **语言**：C++17（主体）+ Python（测试脚本）+ CMake（构建）
- **构建产物输出目录**：`aproj/docx/output/`
- **三方库**：`third_party/` 下包含 harfbuzz、pugixml、miniz、stb_truetype

### 核心定位：迁移，不是重新设计

`aproj/docx` 是 LibreOffice DOCX 功能的**忠实迁移**。这意味着：
- **架构照搬**：模块划分、类层次、调用链路必须与 LO 一致
- **数据结构照搬**：SwDoc、SwNodes、SwNode、SwFrame 等核心数据结构的定义、字段、关系必须与 LO 一致
- **算法逻辑照搬**：排版计算、分页决策、断行逻辑、浮动对象定位等算法必须从 LO 源码迁移
- **命名照搬**：类名、方法名、变量名保持与 LO 一致

> **⚠️ 硬性约束**：任何"我觉得这样更好"的改动都是违反项目原则的。LO 怎么写，aproj/docx 就怎么写。

---

## 2. 目录结构速览

```
libo-core/                          # LibreOffice 源码根目录
├── sw/                             # Writer 模块（DOCX 解析与排版核心代码）
├── vcl/                            # VCL 渲染层模块
├── oox/                            # OOXML 解析模块
├── sax/                            # XML 解析模块
├── ...                             # 其他 LO 模块
│
└── aproj/                          # 本项目工作区根目录
    └── docx/                       # DOCX 独立解析排版引擎
        ├── src/                    # 核心源码
        │   ├── core/               # 核心数据结构（doc, node, format, types 等）
        │   ├── filter/             # DOCX 文件解析器（docx_parser）
        │   ├── font/               # 字体引擎
        │   ├── frame/              # Frame 排版树
        │   ├── layout/             # 排版动作（layact）
        │   └── render/             # 渲染层（接入 LO 的 VCL）
        ├── test/                   # 测试用例与差异对比脚本
        ├── samples/                # 测试用 .docx 样本文件
        ├── third_party/            # 三方库（harfbuzz, pugixml, miniz, stb_truetype）
        ├── tools/                  # 独立工具（render_diff, node_diff, frame_diff）
        │   └── debug/              # 调试/分析脚本（Python, C++）
        ├── output/                 # 编译产物输出目录
        ├── docs/                   # 项目级配置
        │   ├── rules/              # 规则文件
        │   ├── skills/             # 技能文件（操作流程）
        │   └── reference/          # 参考文档（架构手册）
        ├── CMakeLists.txt          # 主构建脚本
        ├── build.bat               # 编译脚本（Windows）
        └── AGENTS.md               # 本文件
```

### 核心源文件对应关系

| aproj/docx 文件 | 对应 LO 模块 | 职责 |
|-----------------|-------------|------|
| `src/core/doc.cpp/h` | `sw/source/core/doc/` | SwDoc 文档模型 |
| `src/core/node.cpp/h` | `sw/source/core/docnode/` | SwNode 节点 |
| `src/core/ndarr.cpp/h` | `sw/source/core/docnode/` | SwNodes 节点数组 |
| `src/core/bparr.cpp/h` | `sw/source/core/bparr/` | 大端数组 |
| `src/core/format.cpp/h` | `sw/source/core/attr/` | 格式属性 |
| `src/core/types.h` | `sw/inc/` | 类型定义 |
| `src/core/swrect.h` | `sw/inc/` | 矩形区域 |
| `src/filter/docx_parser.cpp/h` | `sw/source/filter/ww8/docxml/` | DOCX OOXML 解析 |
| `src/font/font_engine.cpp/h` | `vcl/` | 字体/字形测量 |
| `src/frame/frame.cpp/h` | `sw/source/core/layout/` | SwFrame 基类 |
| `src/frame/frmtree.cpp/h` | `sw/source/core/layout/` | Frame 树构建 |
| `src/frame/layhelper.cpp/h` | `sw/source/core/layout/` | SwLayHelper 辅助 |
| `src/frame/sortedobjs.cpp/h` | `sw/source/core/layout/` | 浮动对象管理 |
| `src/frame/objectformatter.cpp/h` | `sw/source/core/layout/` | 浮动对象格式化 |
| `src/layout/layact.cpp/h` | `sw/source/core/layout/layact.cxx` | 排版动作 |
| `src/render/render_log.cpp/h` | - | 渲染日志（frame/vcl 打印） |
| `src/render/render_output_device.cpp/h` | `vcl/` | 输出设备 |
| `src/render/nodes_log.cpp/h` | - | Nodes 结构打印 |
| `src/render/instruction_builder.h` | - | 渲染指令构建 |
| `src/render/output_device.h` | - | 输出设备接口 |

### 测试脚本

| 脚本 | 用途 |
|------|------|
| `build.bat` | 编译 aproj/docx 项目 |
| `test/gen_lo.py` | 调用 LibreOffice 生成 LO 侧产物（nodes/frame/vcl） |
| `test/gen_aproj.py` | 调用 `docx_e2e_test.exe` 生成 aproj 侧产物 |
| `test/diff_node.bat` | 对比 Nodes 差异 |
| `test/diff_frame.bat` | 对比 Frame 差异 |
| `test/diff_vcl.bat` | 对比 VCL 渲染指令差异 |

---

## 3. 规则（Rules）索引

### 3.1 项目硬性标准

**路径**：[aproj/docx/docs/rules/project_rules.md](file:///e:/lo/libo-core/aproj/docx/docs/rules/project_rules.md)

**核心内容摘要**：

| 类别 | 要点 |
|------|------|
| **项目主旨** | 将 LibreOffice DOCX 核心代码迁移到 aproj/docx，做到 0 差异 |
| **迁移原则** | 架构 / 数据结构 / 算法 / 命名 **四照搬**，禁止自行设计 |
| **技术约束** | 最小实现但功能完整；禁止硬编码；参考 LO 源码而非猜测 |
| **代码规范** | CamelCase 类名、`m_` 前缀、`p` 指针前缀；英文注释 |
| **测试标准** | 三级差异对比（Nodes → Frame → VCL），目标均为 0；使用 `gen_*.py` 和 `diff_*.bat`，不自行拼接命令 |
| **工具规范** | 调试脚本放 `tools/debug/`；配置信息缓存在 `aproj/cache/`；DOCX 解压缓存至 `samples/*_extracted/` |

---

## 4. 技能（Skills）索引

所有技能均位于 `aproj/docx/docs/skills/` 下，每个技能包含一个 `SKILL.md` 文件，描述详细操作流程。

### 4.1 build_aproj_docx — 编译 aproj/docx 项目

**路径**：[aproj/docx/docs/skills/build_aproj_docx/SKILL.md](file:///e:/lo/libo-core/aproj/docx/docs/skills/build_aproj_docx/SKILL.md)

**核心流程**：
1. `cd aproj/docx`
2. 运行 `build.bat`（调用 CMake + MSBuild）
3. 产物输出到 `output/`：`docx_core.lib`、`docx_e2e_test.exe`、`node_diff.exe`、`frame_diff.exe`、`render_diff.exe` 等

**常用命令**：
```bat
cd aproj/docx
build.bat
```

### 4.2 build_lo — 编译 LibreOffice (libo-core)

**路径**：[aproj/docx/docs/skills/build_lo/SKILL.md](file:///e:/lo/libo-core/aproj/docx/docs/skills/build_lo/SKILL.md)

**核心流程**：
1. 增量编译（禁止 clean）
2. 模块级编译：`make sw` 等
3. 三条件构建成功判定
4. 故障排除

**关键约束**：
- 不做全量 clean，走增量编译
- `build_lo.bat` 位于 `libo-core/` 根目录

### 4.3 git_ops — Git 操作规范

**路径**：[aproj/docx/docs/skills/git_ops/SKILL.md](file:///e:/lo/libo-core/aproj/docx/docs/skills/git_ops/SKILL.md)

**核心要点**：
- 提交文件范围：`src/`、`test/`、`docs/`、`sw/` 等源码/文档目录
- **排除**：`build/`、`tools/debug/`、`output/`、临时文件
- 标准命令：commit / pull / push

### 4.4 lo_docx_structure — LibreOffice DOCX 架构索引

**路径**：[aproj/docx/docs/skills/lo_docx_structure/SKILL.md](file:///e:/lo/libo-core/aproj/docx/docs/skills/lo_docx_structure/SKILL.md)

**完整参考手册**：[aproj/docx/docs/reference/lo_docx_structure.md](file:///e:/lo/libo-core/aproj/docx/docs/reference/lo_docx_structure.md)

**核心内容**：
- 三层架构（解析 → 排版 → 渲染）
- 导入/导出管线
- SwDoc 模型（Nodes 节点树）
- Frame 树排版引擎
- 浮动对象体系
- 关键文件索引（sw/、vcl/、oox/ 中的核心文件）

### 4.5 test_diff_workflow — 测试差异对比与迭代修复工作流

**路径**：[aproj/docx/docs/skills/test_diff_workflow/SKILL.md](file:///e:/lo/libo-core/aproj/docx/docs/skills/test_diff_workflow/SKILL.md)

**核心流程**：
```
build.bat → gen_lo.py → gen_aproj.py → diff_node.bat → diff_frame.bat → diff_vcl.bat
```

**三级对比**：
1. **Nodes 结构**：解析后的文档节点树
2. **Frame 树**：排版后的 Frame 布局树
3. **VCL 渲染指令**：最终绘制指令

**迭代策略**：按差异项顺序逐个解决，每轮修改后验证 node diff 不劣化（必须保持 0）。

---

## 5. 当前任务状态

**任务文档**：[aproj/docx/TASK.md](file:///e:/lo/libo-core/aproj/docx/TASK.md)

### 阶段状态

- **阶段一：Nodes 差异归零** — ✅ 已完成，diff = 0
- **阶段二：Frame 差异归零** — 🔄 **当前阶段**
- **阶段三：VCL 渲染指令差异归零** — ⏳ 待进行

### 当前状态（截至 2026-06-21）

| 指标 | LO | aproj | 状态 |
|------|----|-------|------|
| Node diff | — | 0 | ✅ PASS |
| Frame diff | — | 197 | 🔄 进行中 |
| 页数 | 7 | 4 | ⚠️ 缺 20 个 TEXT_FRAME |
| SECTION/COLUMN 指令 | 5/4 | 2/2 | ⚠️ 不匹配 |

### 已修复的关键项

- Section 1 页边距：`pgMar=0` 时固定为 720 twips
- Page 2 首帧位置：`CalcBodyTextFrameHorz` 改用 `GetSectionMargins()`
- Run 直接格式：新增 `ApplyFirstTextRunFromXml()`，调整属性应用顺序
- 行高公式：默认行距 240 时使用 LO auto 规则
- Frame 日志字体：非空段优先记录样式链字体

### 剩余主要问题

- 页数：LO 7 页 vs aproj 4 页（缺 20 个 TEXT_FRAME）
- SECTION/COLUMN 指令不匹配（5/4 vs 2/2）
- Page 1 行高/换行：Share、Poppins 段落等仍有偏差，Y 坐标级联偏移
- 1–2 twip 舍入：479/958/639 等边界值

### 下一步方向

1. 迁移 LO `SwTextFormatter::CalcRealHeight` 完整行距逻辑（含 spaceBefore、多行换行）
2. 修复分页/分栏，补齐 page 5–7 的 frame
3. 每轮迭代执行：`build.bat → gen_lo.py / gen_aproj.py → diff_node.bat → diff_frame.bat`

---

## 6. 典型工作流程（Agent 执行步骤）

### 6.1 差异分析 → 修复 → 验证 的标准循环

```
┌───────────────────────────────────────────────────────────┐
│  1. 先读取硬性约束                                         │
│     → [project_rules.md](aproj/docx/docs/rules/project_rules.md) │
│     → [test_diff_workflow SKILL](aproj/docx/docs/skills/test_diff_workflow/SKILL.md) │
├───────────────────────────────────────────────────────────┤
│  2. 编译项目                                               │
│     cd aproj/docx                                          │
│     build.bat                                              │
├───────────────────────────────────────────────────────────┤
│  3. 生成产物                                               │
│     python test\gen_lo.py                                  │
│     python test\gen_aproj.py                               │
├───────────────────────────────────────────────────────────┤
│  4. 查看差异                                               │
│     .\test\diff_node.bat   (确认 = 0，无劣化)               │
│     .\test\diff_frame.bat  (当前关注项)                     │
│     .\test\diff_vcl.bat    (阶段三关注项)                   │
├───────────────────────────────────────────────────────────┤
│  5. 差异分析                                               │
│     - 逐条分析 diff 输出                                   │
│     - 判断是打印逻辑问题还是业务逻辑问题                    │
│     - 在 sw/ 中定位 LO 对应实现                            │
│     - 如需临时分析脚本，放在 tools/debug/ 下                │
├───────────────────────────────────────────────────────────┤
│  6. 代码迁移/修复                                          │
│     - 从 sw/ 迁移逻辑到 aproj/docx/src/                    │
│     - 保持架构/数据结构/算法/命名与 LO 一致                 │
│     - 英文注释，CamelCase 风格                             │
├───────────────────────────────────────────────────────────┤
│  7. 回归验证                                               │
│     回到步骤 2，确认 node diff = 0（不可劣化）               │
│     确认 frame diff 改进（减少或精度提升）                   │
└───────────────────────────────────────────────────────────┘
```

### 6.2 首次对话时的加载清单

Agent 启动时按以下顺序加载上下文：

| 优先级 | 文件 | 作用 |
|--------|------|------|
| 1（必读） | [aproj/docx/docs/rules/project_rules.md](file:///e:/lo/libo-core/aproj/docx/docs/rules/project_rules.md) | 项目硬性约束 |
| 2（必读） | [aproj/docx/docs/skills/test_diff_workflow/SKILL.md](file:///e:/lo/libo-core/aproj/docx/docs/skills/test_diff_workflow/SKILL.md) | 测试差异工作流 |
| 3（必读） | [aproj/docx/TASK.md](file:///e:/lo/libo-core/aproj/docx/TASK.md) | 当前任务状态 |
| 4（按需） | [aproj/docx/docs/skills/build_aproj_docx/SKILL.md](file:///e:/lo/libo-core/aproj/docx/docs/skills/build_aproj_docx/SKILL.md) | 编译 aproj/docx |
| 5（按需） | [aproj/docx/docs/skills/build_lo/SKILL.md](file:///e:/lo/libo-core/aproj/docx/docs/skills/build_lo/SKILL.md) | 编译 LO |
| 6（按需） | [aproj/docx/docs/reference/lo_docx_structure.md](file:///e:/lo/libo-core/aproj/docx/docs/reference/lo_docx_structure.md) | LO 架构手册 |
| 7（按需） | [aproj/docx/docs/skills/lo_docx_structure/SKILL.md](file:///e:/lo/libo-core/aproj/docx/docs/skills/lo_docx_structure/SKILL.md) | LO 架构索引 |
| 8（按需） | [aproj/docx/docs/skills/git_ops/SKILL.md](file:///e:/lo/libo-core/aproj/docx/docs/skills/git_ops/SKILL.md) | Git 操作规范 |

---

## 7. 关键文件快速索引

### 7.1 aproj/docx 核心源码

| 文件 | 职责 |
|------|------|
| [CMakeLists.txt](file:///e:/lo/libo-core/aproj/docx/CMakeLists.txt) | 主构建脚本（CMake 配置） |
| [src/core/doc.h](file:///e:/lo/libo-core/aproj/docx/src/core/doc.h) / [doc.cpp](file:///e:/lo/libo-core/aproj/docx/src/core/doc.cpp) | SwDoc 文档模型 |
| [src/core/node.h](file:///e:/lo/libo-core/aproj/docx/src/core/node.h) / [node.cpp](file:///e:/lo/libo-core/aproj/docx/src/core/node.cpp) | SwNode 节点 |
| [src/core/ndarr.h](file:///e:/lo/libo-core/aproj/docx/src/core/ndarr.h) / [ndarr.cpp](file:///e:/lo/libo-core/aproj/docx/src/core/ndarr.cpp) | SwNodes 节点数组 |
| [src/core/format.h](file:///e:/lo/libo-core/aproj/docx/src/core/format.h) / [format.cpp](file:///e:/lo/libo-core/aproj/docx/src/core/format.cpp) | 格式属性 |
| [src/core/types.h](file:///e:/lo/libo-core/aproj/docx/src/core/types.h) | 类型定义 |
| [src/core/swrect.h](file:///e:/lo/libo-core/aproj/docx/src/core/swrect.h) | 矩形区域 |
| [src/core/bparr.h](file:///e:/lo/libo-core/aproj/docx/src/core/bparr.h) / [bparr.cpp](file:///e:/lo/libo-core/aproj/docx/src/core/bparr.cpp) | 大端数组 |
| [src/filter/docx_parser.h](file:///e:/lo/libo-core/aproj/docx/src/filter/docx_parser.h) / [docx_parser.cpp](file:///e:/lo/libo-core/aproj/docx/src/filter/docx_parser.cpp) | DOCX OOXML 解析器 |
| [src/font/font_engine.h](file:///e:/lo/libo-core/aproj/docx/src/font/font_engine.h) / [font_engine.cpp](file:///e:/lo/libo-core/aproj/docx/src/font/font_engine.cpp) | 字体引擎 |
| [src/frame/frame.h](file:///e:/lo/libo-core/aproj/docx/src/frame/frame.h) / [frame.cpp](file:///e:/lo/libo-core/aproj/docx/src/frame/frame.cpp) | SwFrame 基类 |
| [src/frame/frmtree.h](file:///e:/lo/libo-core/aproj/docx/src/frame/frmtree.h) / [frmtree.cpp](file:///e:/lo/libo-core/aproj/docx/src/frame/frmtree.cpp) | Frame 树构建 |
| [src/frame/layhelper.h](file:///e:/lo/libo-core/aproj/docx/src/frame/layhelper.h) / [layhelper.cpp](file:///e:/lo/libo-core/aproj/docx/src/frame/layhelper.cpp) | SwLayHelper 辅助类 |
| [src/frame/sortedobjs.h](file:///e:/lo/libo-core/aproj/docx/src/frame/sortedobjs.h) / [sortedobjs.cpp](file:///e:/lo/libo-core/aproj/docx/src/frame/sortedobjs.cpp) | 浮动对象管理 |
| [src/frame/objectformatter.h](file:///e:/lo/libo-core/aproj/docx/src/frame/objectformatter.h) / [objectformatter.cpp](file:///e:/lo/libo-core/aproj/docx/src/frame/objectformatter.cpp) | 浮动对象格式化 |
| [src/layout/layact.h](file:///e:/lo/libo-core/aproj/docx/src/layout/layact.h) / [layact.cpp](file:///e:/lo/libo-core/aproj/docx/src/layout/layact.cpp) | 排版动作 |
| [src/render/render_log.h](file:///e:/lo/libo-core/aproj/docx/src/render/render_log.h) / [render_log.cpp](file:///e:/lo/libo-core/aproj/docx/src/render/render_log.cpp) | Frame/VCL 日志打印 |
| [src/render/nodes_log.h](file:///e:/lo/libo-core/aproj/docx/src/render/nodes_log.h) / [nodes_log.cpp](file:///e:/lo/libo-core/aproj/docx/src/render/nodes_log.cpp) | Nodes 日志打印 |
| [src/render/render_output_device.h](file:///e:/lo/libo-core/aproj/docx/src/render/render_output_device.h) / [render_output_device.cpp](file:///e:/lo/libo-core/aproj/docx/src/render/render_output_device.cpp) | 渲染输出设备 |
| [src/render/instruction_builder.h](file:///e:/lo/libo-core/aproj/docx/src/render/instruction_builder.h) | 渲染指令构建器 |
| [src/render/output_device.h](file:///e:/lo/libo-core/aproj/docx/src/render/output_device.h) | 输出设备接口 |

### 7.2 测试与工具

| 文件 | 职责 |
|------|------|
| [build.bat](file:///e:/lo/libo-core/aproj/docx/build.bat) | Windows 编译脚本 |
| [test/gen_lo.py](file:///e:/lo/libo-core/aproj/docx/test/gen_lo.py) | 生成 LO 侧产物 |
| [test/gen_aproj.py](file:///e:/lo/libo-core/aproj/docx/test/gen_aproj.py) | 生成 aproj 侧产物 |
| [test/diff_node.bat](file:///e:/lo/libo-core/aproj/docx/test/diff_node.bat) | Nodes 差异对比 |
| [test/diff_frame.bat](file:///e:/lo/libo-core/aproj/docx/test/diff_frame.bat) | Frame 差异对比 |
| [test/diff_vcl.bat](file:///e:/lo/libo-core/aproj/docx/test/diff_vcl.bat) | VCL 差异对比 |
| [test/test_end_to_end.cpp](file:///e:/lo/libo-core/aproj/docx/test/test_end_to_end.cpp) | 端到端测试主程序 |
| [tools/render_diff.cpp](file:///e:/lo/libo-core/aproj/docx/tools/render_diff.cpp) | 通用差异对比工具 |
| [tools/node_diff.cpp](file:///e:/lo/libo-core/aproj/docx/tools/node_diff.cpp) | Nodes 专用对比工具 |
| [tools/frame_diff.cpp](file:///e:/lo/libo-core/aproj/docx/tools/frame_diff.cpp) | Frame 专用对比工具 |

### 7.3 LO 参考源码路径

| LO 路径 | 对应 aproj 模块 |
|---------|----------------|
| `sw/source/core/doc/` | `src/core/doc.*` |
| `sw/source/core/docnode/` | `src/core/node.*`, `ndarr.*` |
| `sw/source/core/bparr/` | `src/core/bparr.*` |
| `sw/source/core/attr/` | `src/core/format.*` |
| `sw/source/core/layout/` | `src/frame/*`, `src/layout/*` |
| `sw/source/filter/ww8/docxml/` | `src/filter/docx_parser.*` |
| `sw/inc/` | 头文件对照 |
| `vcl/` | 渲染层参考 |
| `oox/source/` | OOXML 解析参考 |

---

## 8. 不可违反的核心约束（检查清单）

在任何代码修改前，请确认以下事项：

- [ ] **已阅读** `project_rules.md`
- [ ] **已确认** 修改涉及的 LO 源码位置（`sw/`、`vcl/` 等）
- [ ] **已确认** 修改方向是"迁移 LO 逻辑"而非"自行设计"
- [ ] **已确认** 类名、方法名、字段名、命名风格与 LO 一致
- [ ] **已确认** 调试/临时脚本放在 `tools/debug/` 下
- [ ] **已确认** 不修改 `gen_*.py` / `diff_*.bat`（除非 LO 侧变化）
- [ ] **已确认** 修改后 node diff = 0（不可劣化）
- [ ] **已确认** 修改后 frame diff 有改进（减少或精度提升）

---

## 9. Agent 角色说明

根据当前任务（Frame 差异归零阶段），Agent 可能扮演以下角色：

### 差异分析 Agent
- 解析 `diff_frame.bat` 输出
- 逐条识别差异类型（字段缺失/值不同/结构不同/顺序不同）
- 判断根因（打印逻辑问题 vs 业务逻辑问题）
- 在 `sw/` 中定位 LO 对应实现

### 打印逻辑同步 Agent
- 对比 aproj 和 lo 的 frame 打印函数
- 识别字段列表、顺序、数值精度、格式化方式等不一致
- 将 aproj 的打印逻辑修改为与 lo 逐字节一致

### 业务逻辑迁移 Agent
- 在 lo 源码中定位差异对应的解析/构建逻辑
- 将 lo 的逻辑迁移到 aproj，确保架构/数据结构/算法一致
- 保持 node diff = 0

### 回归验证 Agent
- 编译 → 生成产物 → 验证三级差异
- 报告修复项、新增差异（劣化）、剩余差异

---

*本文档由 Agent 使用/维护，随项目进展同步更新。最后更新：2026-06-21*
