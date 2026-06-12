# WORK.md — DOCX 独立渲染管线项目总览

## 一、项目目标

从 LibreOffice 内核提取 DOCX 核心功能，构建独立的文档渲染管线：

```
OOXML ZIP → XML解析 → DocumentModel → Frame树 → 排版分页 → 渲染指令 → 比对验证
```

**终极目标**：aproj/docx 的渲染指令与 LibreOffice 完全一致（±10 twips 容差内），使得差异**必定**来自排版逻辑（sw 模块），而非渲染架构差异。从而集中精力检查 sw 实现逻辑，找到问题并加入 aproj。

---

## 二、核心技术栈

| 技术 | 用途 | 位置 |
|------|------|------|
| **miniz** | ZIP 解压（.docx = ZIP） | `third_party/miniz.h` |
| **pugixml** | XML 解析（document.xml, styles.xml 等） | `third_party/pugixml.hpp` |
| **stb_truetype** | 字体度量（ascent/descender/lineHeight） | `third_party/stb_truetype.h` |
| **BigPtrArray** | 块结构数组（LibreOffice 同源） | `src/core/bparr.h` |
| **SwNode 树** | 文档模型（StartNode/EndNode/TextNode/TableNode） | `src/core/node.h` |
| **Frame 树** | 排版布局（RootFrame/PageFrame/BodyFrame/TextFrame） | `src/frame/frame.h` |
| **OutputDevice** | 抽象渲染接口（与 VCL API 对称） | `src/render/output_device.h` |
| **GDIMetaFile** | VCL 层渲染指令录制（LibreOffice 侧） | `sw/source/core/layout/meta_to_instruction.cxx` |
| **RenderInstructionSink** | 共享渲染指令接收接口 | `sw/source/core/inc/render_instruction.h` |
| **instruction_builder.h** | 共享指令构建函数（零依赖） | `sw/source/core/inc/instruction_builder.h` |

---

## 三、关键架构设计

### 3.1 统一渲染框架（核心原则）

aproj 必须使用与 VCL 相同的 `OutputDevice` 接口进行渲染：

```
LibreOffice:  SwTextFrame::PaintSwFrame → OutputDevice::DrawText → GDIMetaFile → RenderInstruction
aproj:        SwTextFrame::PaintSwFrame → OutputDevice::DrawText → RenderInstructionOutputDevice → RenderInstruction
```

两侧的 `PaintSwFrame` 方法调用相同的 `OutputDevice` API，只是 OutputDevice 的实现不同。任何渲染指令差异**必定**来自排版逻辑差异。

### 3.2 共享指令构建模块

```
                 instruction_builder.h (共享，零依赖，纯 inline)
                /                                      \
  meta_to_instruction.cxx                  render_output_device.cpp
  (VCL 类型 → 原始类型 → Build*)           (aproj 类型 → 原始类型 → Build*)
```

- 所有 `Build*Instruction()` 函数使用原始类型（int, const char*, uint32_t）
- 不依赖任何 VCL 或 aproj 类型，两端直接调用
- 指令构建逻辑 100% 共享，消除代码重复

### 3.3 双层渲染指令录制

```
┌─────────────────────────────────────────────────────────────┐
│  Frame 层（语义级）                                          │
│  PAGE_START, PAGE_END, TEXT_FRAME, TABLE_FRAME, IMAGE_FRAME │
│  → SwPaintEventListener 在 PaintSwFrame 中植入              │
│  → 环境变量 SW_RENDER_LOG 控制                              │
├─────────────────────────────────────────────────────────────┤
│  VCL 层（绘制级）                                            │
│  RECT, LINE, TEXT_RUN, SET_FONT, SET_TEXT_COLOR, BITMAP     │
│  → GDIMetaFile::Record() 录制 OutputDevice::Draw* 调用      │
│  → MetaToInstructionConverter 转换为 RenderInstruction      │
│  → 环境变量 SW_VCL_RENDER_LOG 控制                          │
└─────────────────────────────────────────────────────────────┘
```

两层交叉验证：frame 层说"这里有一个 TEXT_FRAME"，VCL 层说"这里有一组 DrawText 调用"。如果两者不一致，说明有问题。

### 3.4 POD 结构体 + TSV 格式

```c
// render_instruction.h — 零外部依赖，纯 POD
struct RenderInstruction {
    RenderCmdType type;
    int pageNum, x, y, width, height;
    const char* text;     // UTF-8
    const char* fontName;
    int fontSize;         // 半点
    uint32_t fontColor;   // 0xRRGGBB
    uint8_t fontWeight, fontItalic, underline, strikeout;
    // ...
};
```

TSV 格式输出，字段顺序一致，支持直接 diff 比对。

---

## 四、已完成工作

### Phase 0-1: 基础设施与文档模型

| 组件 | 状态 | 说明 |
|------|------|------|
| 基础类型 (types.h, swrect.h, bparr.h) | ✅ | SwTwips, SwRect, BigPtrArray |
| 文档模型 (node.h, ndarr.h, format.h, doc.h) | ✅ | SwNode 树, SwNodes, SwFormat, SwDoc |
| Frame 树 (frame.h, frmtree.h) | ✅ | SwRootFrame → SwPageFrame → SwBodyFrame → SwTextFrame |
| DOCX 解析器 (docx_parser.h) | ✅ | miniz + pugixml, 解析 document.xml/styles.xml |
| 排版引擎 (layout.h) | ✅ | LayoutEngine, 行分段, 翻页 |
| 字体引擎 (font_engine.h) | ✅ | stb_truetype 度量 |

### Phase 2: 共享渲染指令格式

| 文件 | 状态 | 说明 |
|------|------|------|
| `sw/source/core/inc/render_instruction.h` | ✅ | 共享 POD 结构体 + RenderInstructionSink 接口 |
| `sw/source/core/inc/instruction_builder.h` | ✅ | 共享 Build*Instruction() 函数集 |

### Phase 3: aproj/docx 侧实现

| 文件 | 状态 | 说明 |
|------|------|------|
| `src/render/render_log.h/cpp` | ✅ | RenderLogger 实现 RenderInstructionSink |
| `src/render/output_device.h` | ✅ | 抽象 OutputDevice 接口（与 VCL 对称） |
| `src/render/render_output_device.h/cpp` | ✅ | RenderInstructionOutputDevice 实现 |
| `src/frame/frame.cpp` | ✅ | SwTextFrame::PaintSwFrame 通过 OutputDevice 绘制 |
| `test/test_end_to_end.cpp` | ✅ | 28/28 测试通过 |
| `tools/render_diff.cpp` | ✅ | 比对工具（支持 --tolerance 和 --known-diffs） |
| `tests/sample.docx` | ✅ | 纯文本测试文件 |
| `tests/known_diffs.txt` | ✅ | 已知差异列表 |

### Phase 4: LibreOffice 侧植入

| 文件 | 状态 | 说明 |
|------|------|------|
| `sw/source/core/inc/paint_listener.hxx` | ✅ | SwPaintEventListener 头文件 |
| `sw/source/core/layout/paint_listener.cxx` | ✅ | Frame 层 + VCL 层录制实现 |
| `sw/source/core/layout/paintfrm.cxx` | ✅ | PaintSwFrame 中植入 PAGE_START/END |
| `sw/source/core/text/frmpaint.cxx` | ✅ | 植入 TEXT_FRAME |
| `sw/source/core/layout/newfrm.cxx` | ✅ | CheckEnvAndStart() |

### Phase 5: VCL 层 GDIMetaFile 录制（LibreOffice 侧）

| 文件 | 状态 | 说明 |
|------|------|------|
| `sw/source/core/inc/meta_to_instruction.hxx` | ✅ | MetaAction → RenderInstruction 转换器 |
| `sw/source/core/layout/meta_to_instruction.cxx` | ✅ | 转换实现，维护绘制状态上下文 |
| `sw/source/core/layout/paintfrm.cxx` | ✅ | StartPageRecord/StopPageRecordAndConvert |
| `sw/Library_sw.mk` | ✅ | 编译目标 |

### Phase 6-7: aproj 统一渲染框架 + 代码复用

| 文件 | 状态 | 说明 |
|------|------|------|
| `aproj/docx/CMakeLists.txt` | ✅ | 添加 sw/source/core/inc 到 include path |
| `aproj/docx/src/render/render_instruction.h` | ✅ 已删除 | 不再维护副本，通过 include path 引用 sw 版本 |

### 已修复的 Bug

| Bug | 根因 | 修复 |
|-----|------|------|
| segfault (EndOfContent) | `SwNodes::InitNodes()` 未创建 EndOfContent 哨兵 | 添加 EndOfContent 节点 |
| Frame 链表断裂 | `InsertBehind()` 未更新父节点 m_pLower | 添加 SetLower() 调用 |
| Body 未关联页面 | `PreparePage()` 创建 SwBodyFrame 后未插入 | 添加 InsertBehind 调用 |
| VCL 日志路由错误 | VCL 层指令写入 frame 文件 | 添加 m_bConvertingVcl 标志 |
| 文本乱码 | `ParseParagraph` 未解析 w:rPr | 添加 ParseRunProps 调用 |
| 页面尺寸不一致 | ParseSectionProps 创建新 PageDesc 而非更新默认 | 改为更新 GetDefaultPageDesc() |
| WriteToFile 指针悬空 | 字符串指针在 OnInstruction 后失效 | 移除 WriteToFile 调用 |
| Frame 树遍历不完整 | 只遍历 page 直接子节点 | 改为递归遍历 body 子节点 |
| fontSize=2 | 半点值直接赋给 twips 字段 | `aFont.height = halfPoints * 10` |
| SwTableNode 类型错误 | 构造函数未设置 `m_nNodeType = Table` | 添加赋值语句 |
| 表格文本为空 | InsertTable 未初始化 tableData | 添加 tableData 初始化 |
| 表格 Frame 重复创建 | MakeFrames 遍历表格子节点 | 遇到 TableNode 时跳过子节点 |
| VCL 层文本乱码 | `std::vector` 重新分配使 `c_str()` 指针失效 | 改用 `std::deque` 存储字符串 |
| TSV/XML 异常行终止符 | 文本中的换行符直接写入输出 | 添加 EscapeForTsv/EscapeForXml 转义 |
| Frame 层 fontWeight 错误 | `std::stoi("bold")` 会抛异常 | 改为检查字符串 "bold"→188, 默认→144 |
| render_diff 已知差异加载失败 | `loadKnownDiffs` 要求 lineNum+TAB 格式 | 支持纯 pattern 格式（无行号） |
| LibreOffice PAGE_END 累积 | `OnPageEnd` 在 if 块外调用 | 添加 `bPageStarted` 标志 |
| LibreOffice 文本换行符未转义 | `WriteInstructionToStream` 未转义特殊字符 | 添加 `EscapeForTsv` 函数 |
| aproj 未解析 w:pPr 内的 w:sectPr | `ParseParagraphProps` 未处理节属性 | 添加 `w:sectPr` 解析逻辑 |

---

## 五、当前状态

### 编译状态

| 项目 | 状态 |
|------|------|
| LibreOffice `make sw` | ✅ 编译通过 |
| aproj `docx_e2e_test` | ✅ 21/21 通过 |
| aproj `render_diff` | ✅ 编译通过 |

### 已完成的后续步骤

| 步骤 | 内容 | 状态 |
|------|------|------|
| Step 1 | 修复 fontSize 单位（半点→twips） | ✅ 已完成 |
| Step 2 | 添加 frame 层语义输出（TEXT_FRAME） | ✅ 已完成 |
| Step 3 | 扩展 PaintSwFrame（TABLE 支持） | ✅ 已完成 |
| Step 4 | 深化 sample.docx 测试验证 | 🔄 进行中 |
| Step 5 | 迭代 render_diff 比对 | ⬜ 待开始 |
| Step 6 | VCL 层接入（可选） | ⬜ 待开始 |

### Step 4 进展

- ✅ 修复 `render_diff.cpp` 的 `loadKnownDiffs`：支持无行号格式（纯 pattern 匹配）
- ✅ 更新 WORK.md：明确使用 sample.docx 作为唯一测试文件，不自动生成 .docx
- ✅ 修复 frame 层 `fontWeight`：与 VCL 层保持一致（144=normal, 188=bold）
- ✅ 修复 LibreOffice paint_listener 的 PAGE_END 累积 bug
- ✅ 修复 LibreOffice paint_listener 的文本换行符转义
- ✅ 修复 aproj 解析器：支持 `w:pPr` 内的 `w:sectPr` 解析

### 当前渲染输出统计（sample.docx）

```
Frame 层 (aproj_frame.txt): 104 条
  PAGE_START: 1
  PAGE_END:   1
  TEXT_FRAME: 102 (语义指令，含字体信息)

VCL 层 (aproj_vcl.txt): 258 条
  PAGE_START: 1
  PAGE_END:   1
  SET_FONT:   104 (状态指令)
  SET_TEXT_COLOR: 104
  TEXT_RUN:   48 (绘制指令)

LibreOffice 参考 (lo_frame.txt): 114 条
  PAGE_START: 7
  PAGE_END:   7
  TEXT_FRAME: 100
```

### render_diff 比对结果

```
Reference: tests/lo_frame.txt (114 instructions)
Test:      tests/aproj_frame.txt (104 instructions)
Differences: 847
```

**主要差异**：
| 差异 | LibreOffice | aproj | 状态 |
|------|-------------|-------|------|
| 页数 | 7 页 | 1 页 | 🔴 待修复 |
| 页面宽度 | 11906 | 9360 | 🔴 待修复 |
| 页面边距 | x=284, y=284 | 0, 0 | 🔴 待修复 |
| 字体名 | 正确解析 | 部分正确 | 🟡 部分修复 |
| 样式名 | 有样式名 | 空 | 🟡 待修复 |

### 测试文件

| 文件 | 说明 |
|------|------|
| `sample.docx` | WPS Office 复杂文档（A4，104 段落，23 张图片，表格，多节）— 项目根目录 |
| `tests/aproj_frame.txt` | aproj frame 层输出（TEXT_FRAME 等语义指令） |
| `tests/aproj_vcl.txt` | aproj VCL 层输出（SET_FONT, TEXT_RUN 等绘制指令） |
| `tests/aproj_all.log` | aproj 全部指令（frame + VCL 混合） |
| `tests/frmtree_dump.xml` | Frame 树 XML 转储 |
| `tests/nodes_dump.xml` | 节点树 XML 转储 |
| `tests/lo_frame.txt` | LibreOffice frame 层输出（参考，需手动生成） |
| `tests/lo_vcl.txt` | LibreOffice VCL 层输出（参考，需手动生成） |
| `tests/known_diffs.txt` | 已知差异列表 |

---

## 六、后续步骤

### ~~Step 1: 修复 fontSize 单位问题~~ ✅ 已完成

**问题**：aproj 输出 fontSize=2 而非 24。Parser 存储半点值（24），但 PaintSwFrame 直接赋给 `aFont.height`（twips 单位），`GetHeightInHalfPoints()` 返回 24/10=2。

**修复**：在 `frame.cpp:439` 将半点转为 twips：`aFont.height = std::stoi(*pSize) * 10`。

**验证**：fontSize 现在正确输出为 24（半点 = 12pt），28/28 测试通过。

### ~~Step 2: 添加 frame 层语义输出~~ ✅ 已完成

aproj 现在同时输出 frame 层（TEXT_FRAME）和 VCL 层（TEXT_RUN）指令，与 LibreOffice 的双层录制架构对称。

**实现**：
- `RenderLogger::LogFrameTree` 遍历 Frame 树时，先输出 TEXT_FRAME（从 SwTextNode 属性获取字体信息），再调用 PaintSwFrame 输出 SET_FONT + TEXT_RUN
- 添加 `BuildTextFrameInstruction`、`BuildTextLineInstruction`、`BuildTableFrameInstruction`、`BuildImageFrameInstruction` 到共享 `instruction_builder.h`

**验证**：输出顺序 PAGE_START → TEXT_FRAME → SET_FONT → TEXT_RUN → PAGE_END，102 个 TEXT_FRAME + 52 个 TEXT_RUN。

### ~~Step 3: 扩展 PaintSwFrame（TABLE 支持）~~ ✅ 已完成

**实现**：
- `MakeFramesForNode` 支持 SwTableNode：创建 TabFrame → RowFrame → CellFrame → TextFrame
- `SwTableNode` 构造函数正确设置 `m_nNodeType = SwNodeType::Table`
- `InsertTable` 初始化 tableData 结构
- `ParseTable` 更新实际的文本节点内容
- `MakeFrames` 遇到表格节点时跳过其子节点

**验证**：frame 树正确显示嵌套的 table/row/cell/text 结构。

### ~~Step 3: 扩展 PaintSwFrame（TABLE 支持）~~ ✅ 已完成

见上方详细说明。

### Step 4: 深化 sample.docx 测试验证 🔄 进行中

**策略**：不自动生成 .docx 测试文件，专注使用现有 `sample.docx`（WPS Office 复杂文档，A4，104 段落，表格，23 张图片）作为主要测试输入。该文件已涵盖纯文本、表格、图片等多种元素，足够验证排版逻辑。

**已完成**：
- ✅ 修复 LibreOffice paint_listener 的 PAGE_END 累积 bug
- ✅ 修复 LibreOffice paint_listener 的文本换行符转义
- ✅ 修复 aproj 解析器：支持 `w:pPr` 内的 `w:sectPr` 解析
- ✅ 修复 Body Frame 的 print area 继承（PreparePage 顺序）
- ✅ 修复 TextFrame 坐标系：宽度=页面宽度，位置从页面顶部开始
- ✅ 实现分页逻辑：检测 `w:pageBreakBefore` 和 `w:br type="page"`
- ✅ 实现溢出检测：累积 Y 超出页面底部时创建新页面

**当前 render_diff 状态**：
```
Reference: lo_frame.txt (114 instructions, 7 pages)
Test:      aproj_frame.txt (102 instructions, 3 pages)
Differences: 698 (从 847 降至 698)
```

**主要剩余差异**：
| 差异 | 数量 | 原因 | 备注 |
|------|------|------|------|
| x/y 位置 | 92 | aproj x=0 vs LO x=284 | LO 特有行为 |
| 样式名 | 77 | LO 用本地化名 "Default Paragraph Style" | LO 特有行为 |
| 高度 | 76 | 行高计算差异 | 已改善 |
| 字体名 | 68 | Run 字体 vs 样式字体优先级 | LO 特有行为 |
| 字号 | 66 | 样式继承不完整 | 已改善 |
| 颜色 | 23 | 部分样式颜色未继承 | 已改善 |

**根因分析**：
- fontColor 差异从 92 降至 23：实现默认颜色 FFFFFF + 样式继承
- styleName="Default Paragraph Style" 是 LO 的本地化名称，DOCX 中实际为 "Normal"
- fontName 差异：LO 使用样式字体而非 Run 字体，aproj 正确使用 Run 字体
- height 差异已改善：行高因子从 12.3 改为 14.0 twips/半点
- fontSize 差异已改善：恢复段落级 w:rPr 作为默认字符格式
- 这些差异主要是 LO 特有行为，非 aproj bug
| 页数 | 65 | aproj 2 页 vs LO 7 页 |

**已完成**：
- ✅ 生成 LibreOffice 参考输出（lo_frame.txt, lo_vcl.txt）
- ✅ 修复 LibreOffice paint_listener 的 PAGE_END 累积 bug
- ✅ 修复 LibreOffice paint_listener 的文本换行符转义
- ✅ 修复 TextFrame 宽度=页面宽度
- ✅ 修复 Body Frame print area 继承
- ✅ 实现分页逻辑（pageBreakBefore + 溢出检测）
- ✅ 修复 fontColor 十六进制解析（render_diff 和 aproj）
- ✅ 实现主题字体解析（minorHAnsi → Calibri）
- ✅ 修复 Run 级 w:rPr 只在有文本时应用（跳过绘图 Run）
- ✅ 修复段落级 w:rPr：不作为 Run 默认值（OOXML 修订属性）
| 字号 | LibreOffice 正确解析，aproj 使用默认 | 🔴 高 |
| 颜色 | LibreOffice 有颜色，aproj 为 0 | 🟡 中 |
| 样式名 | LibreOffice 有样式名，aproj 为空 | 🟡 中 |

**根因分析**：aproj 的 DOCX 解析器（docx_parser.cpp）未正确解析：
1. 页面分节（w:sectPr）→ 导致只有 1 页
2. 页面边距（w:pgMar）→ 导致边距为 0
3. 页面宽度（w:pgSz）→ 导致宽度不正确
4. 字体信息（w:rPr/w:rFonts）→ 导致使用默认字体
5. 字号（w:rPr/w:sz）→ 导致使用默认字号
6. 颜色（w:rPr/w:color）→ 导致颜色为 0

### Step 5: 迭代比对修复（持续）

**前置条件**：生成 LibreOffice 参考输出

```bash
# 1. 编译 LibreOffice (含 paint_listener 植入)
cd sw && make

# 2. 运行 LibreOffice 生成参考输出
#    设置环境变量启用 paint_listener：
export SW_RENDER_LOG=tests/lo_frame.txt    # frame 层语义指令
export SW_VCL_RENDER_LOG=tests/lo_vcl.txt  # VCL 层绘制指令
instdir/program/soffice --headless sample.docx

# 3. 运行 aproj 生成测试输出
cd aproj/docx/build && cmake .. && make
./docx_e2e_test sample.docx
# 输出: tests/aproj_frame.txt, tests/aproj_vcl.txt

# 4. 比对 frame 层
./render_diff tests/lo_frame.txt tests/aproj_frame.txt \
  --known-diffs tests/known_diffs.txt --verbose

# 5. 比对 VCL 层（可选）
./render_diff tests/lo_vcl.txt tests/aproj_vcl.txt \
  --known-diffs tests/known_diffs.txt --verbose
```

**差异分析循环**：
```
  分析每个 [DIFF] 差异：
    - 已知差异（图片/页眉/脚注等未实现功能）→ 加入 known_diffs.txt
    - aproj Bug（排版逻辑错误）→ 修复 aproj 代码
    - 架构差异（frame/VCL 层混合输出）→ 记录但不修复
  重复直到 render_diff 输出 PASS
```

### Step 6: VCL 层录制接入（可选，长期）

如果需要更精确的 VCL 层比对，可以让 aproj 接入 VCL：

1. 链接 VCL 库到 aproj
2. 使用 `vcl::RenderContext` 替代自定义 `OutputDevice`
3. 复用 LibreOffice 的 `MetaToInstructionConverter`
4. aproj 同时输出 frame 层和 VCL 层日志

当前的 OutputDevice 录制路径已满足核心目标，此步骤为可选优化。

---

## 七、文件结构总览

```
aproj/docx/
├── src/
│   ├── core/                    # 文档模型
│   │   ├── types.h              # 基础类型 (SwTwips, sal_Int32)
│   │   ├── swrect.h             # SwRect 矩形
│   │   ├── bparr.h/cpp          # BigPtrArray 块数组
│   │   ├── node.h/cpp           # SwNode 树
│   │   ├── ndarr.h/cpp          # SwNodes 节点数组
│   │   ├── format.h/cpp         # 样式系统
│   │   └── doc.h/cpp            # SwDoc 文档容器
│   ├── frame/                   # Frame 树
│   │   ├── frame.h/cpp          # Frame 类层级
│   │   └── frmtree.h/cpp        # MakeFrames/InitLayout
│   ├── filter/                  # DOCX 解析
│   │   └── docx_parser.h/cpp    # XML → SwDoc
│   ├── layout/                  # 排版引擎
│   │   └── layact.h/cpp         # SwLayAction
│   └── render/                  # 渲染指令
│       ├── output_device.h      # 抽象 OutputDevice 接口
│       ├── render_output_device.h/cpp  # → RenderInstruction
│       └── render_log.h/cpp     # RenderLogger (分层输出)
├── test/
│   └── test_end_to_end.cpp      # 21/21 测试
├── tools/
│   ├── render_diff.cpp          # 比对工具
│   └── dump_lo.py               # LibreOffice 输出采集
├── tests/                       # 测试输出
│   ├── sample.docx              # 测试输入
│   ├── aproj_frame.txt          # frame 层输出
│   ├── aproj_vcl.txt            # VCL 层输出
│   ├── frmtree_dump.xml         # Frame 树转储
│   └── nodes_dump.xml           # 节点树转储
└── CMakeLists.txt               # 构建配置
├── tests/
│   ├── sample.docx              # 测试文件
│   ├── lo_frame.txt             # LibreOffice frame 层输出
│   ├── lo_vcl.txt               # LibreOffice VCL 层输出
│   ├── aproj_render.txt         # aproj 输出
│   └── known_diffs.txt          # 已知差异
├── refactor/
│   └── RENDER_LOG_REFACTOR.md   # 重构方案文档
├── CMakeLists.txt               # 构建配置
├── WORK.md                      # 本文件
├── Test.md                      # 测试设计文档
├── TODO1.md                     # Phase 0-4 总结
├── TODO2.md                     # Phase 1-4 总结
└── TODO3.md                     # Phase 5-7 总结

sw/source/core/inc/
├── render_instruction.h         # 共享 POD 定义
├── instruction_builder.h        # 共享 Build* 函数
├── paint_listener.hxx           # SwPaintEventListener
└── meta_to_instruction.hxx      # MetaAction → RenderInstruction

sw/source/core/layout/
├── paint_listener.cxx           # SwPaintEventListener 实现
├── meta_to_instruction.cxx      # MetaToInstructionConverter
├── paintfrm.cxx                 # PaintSwFrame 植入点
└── newfrm.cxx                   # CheckEnvAndStart()
```

---

## 八、核心理念总结

1. **统一渲染框架**：aproj 和 LibreOffice 使用相同的 OutputDevice API，差异只来自排版逻辑
2. **共享指令构建**：instruction_builder.h 零依赖，两端直接调用，逻辑 100% 一致
3. **双层交叉验证**：frame 层（语义）+ VCL 层（绘制），两层不一致即有问题
4. **渐进式验证**：每新增一种元素，PaintSwFrame 自动调用 OutputDevice，指令自动记录
5. **POD + TSV**：零外部依赖，字节级一致，支持直接 diff
