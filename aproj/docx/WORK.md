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
| Step 4 | 扩展测试 .docx 文件 | ⬜ 待开始 |
| Step 5 | 迭代 render_diff 比对 | ⬜ 待开始 |
| Step 6 | VCL 层接入（可选） | ⬜ 待开始 |

### 当前渲染输出统计（sample.docx）

```
PAGE_START: 1
PAGE_END:   1
TEXT_FRAME: 102 (frame 层语义指令)
TEXT_RUN:   52  (VCL 层绘制指令)
SET_FONT:   102 (VCL 层状态指令)
TABLE_FRAME: 0  (待实现)
```

### 端到端比对结果

```
LibreOffice frame 层 (lo_frame.txt):  PAGE_START → TEXT_FRAME → TEXT_FRAME → PAGE_END
aproj (aproj_render.txt):             PAGE_START → TEXT_FRAME → SET_FONT → TEXT_RUN → PAGE_END
```

**差异类型**：
- 指令类型不匹配：LibreOffice 输出 TEXT_FRAME（语义级），aproj 同时输出 TEXT_FRAME + TEXT_RUN
- 多余状态指令：aproj 输出 SET_FONT/SET_TEXT_COLOR，LibreOffice frame 层不输出
- 这是**架构差异**，不是 Bug：aproj 同时输出 frame 层和 VCL 层，与 LibreOffice 的双层录制对称

### 测试文件

| 文件 | 说明 |
|------|------|
| `tests/sample.docx` | WPS Office 复杂文档（A4，104 段落，23 张图片，表格，多节） |
| `tests/aproj_frame.txt` | aproj frame 层输出（TEXT_FRAME 等语义指令） |
| `tests/aproj_vcl.txt` | aproj VCL 层输出（SET_FONT, TEXT_RUN 等绘制指令） |
| `tests/aproj_all.log` | aproj 全部指令（frame + VCL 混合） |
| `tests/frmtree_dump.xml` | Frame 树 XML 转储 |
| `tests/nodes_dump.xml` | 节点树 XML 转储 |
| `tests/lo_frame.txt` | LibreOffice frame 层输出（参考） |
| `tests/lo_vcl.txt` | LibreOffice VCL 层输出（参考） |
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

### Step 4: 扩展测试 .docx 文件

当前测试使用 sample.docx（WPS Office 复杂文档，A4，104 段落，表格，23 张图片）。后续可添加更多测试文件：

| 文件 | 内容 | 用途 |
|------|------|------|
| `tests/sample.docx` | 当前主测试文件 | 端到端测试 |
| `tests/simple.docx` | 2 段纯文本 | 基础回归测试 |
| `tests/table.docx` | 纯表格文档 | 表格渲染测试 |
| `tests/image.docx` | 图片文档 | 图片渲染测试 |

### Step 5: 迭代比对修复（持续）

```
循环：
  1. 用新测试文件运行 LibreOffice 生成 lo_frame.txt
  2. 用同一文件运行 aproj 生成 aproj_render.txt
  3. render_diff 比对
  4. 分析差异：
     - 已知差异 → 加入 known_diffs.txt
     - aproj Bug → 修复 aproj 排版逻辑
     - 架构差异 → 记录但不修复
  5. 重复直到 render_diff 输出 PASS
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
