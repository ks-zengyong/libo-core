# 目标

将 LibreOffice 的 DOCX 核心功能（解析 → SwNodes 结构 → Frame 树 → 排版分页）提取到 `aproj/docx` 工程下，达到与 LibreOffice 完全一致的排版输出效果。渲染层可以复用 LibreOffice 现有渲染框架，不在渲染层做差异。

# 约束条件

1. 尽可能模仿 LibreOffice 的现有代码架构迁移，只剔除不必要的其他逻辑
2. 不必参考 `aproj/docx/src/` 下的已有代码，它们是上一次的失败方案

---

# 一、LibreOffice DOCX 处理全流程概览

```
.docx ZIP 文件
  │
  ├─ [阶段1] ZIP 解包 + XML 解析 (OOXML SAX)
  │   OOXMLStreamImpl → OOXMLFastDocumentHandler → OOXMLFastContextHandler
  │   将 XML 元素转换为 Token 事件流
  │
  ├─ [阶段2] Domain Mapper (Token → UNO API 调用)
  │   DomainMapper → DomainMapper_Impl
  │   通过 SwXTextAppend / SwXTextCursor 操作文档
  │
  ├─ [阶段3] 文档模型 (SwNodes 构建)
  │   SwDoc → SwNodes → SwTextNode / SwTableNode / SwSectionNode ...
  │
  ├─ [阶段4] Frame 树构建
  │   MakeFrames() → SwTextFrame / SwTabFrame / SwSectionFrame ...
  │
  ├─ [阶段5] 排版分页 (Layout)
  │   SwLayAction → SwFrame::MakeAll() → SwFlowFrame::CheckMoveFwd()
  │
  └─ [阶段6] 渲染
      SwViewShell::Paint → SwRootFrame::PaintSwFrame → 各级 Frame::PaintSwFrame
```

---

# 二、各阶段详细代码索引

## 阶段1: DOCX 解包与 XML 解析

### 入口点
- `sw/source/filter/docx/swdocxreader.cxx` — `ImportDOCX()` 入口，创建 `SwDOCXReader`，委托给 UNO 服务

### WriterFilter 服务 (编排层)
- `sw/source/writerfilter/filter/WriterFilter.cxx` — `WriterFilter::filter()` 方法:
  1. 用 `oox::core::FilterDetect` 解包 ZIP
  2. 用 `DomainMapperFactory::createMapper()` 创建 DomainMapper
  3. 创建 `OOXMLDocument`，调用 `pDocument->resolve(*pStream)` 启动解析

### OOXML Tokenizer (XML → 事件流)
- `sw/source/writerfilter/ooxml/OOXMLDocumentImpl.cxx` — `OOXMLDocument::resolve()` / `resolveFastSubStream()`
- `sw/source/writerfilter/ooxml/OOXMLFastDocumentHandler.hxx` — SAX 文档处理器
- `sw/source/writerfilter/ooxml/OOXMLFastContextHandler.hxx` — 核心上下文处理器层级:
  - `OOXMLFastContextHandler` — 基类，处理 MCE、BiDi、SDT、字段等
  - `OOXMLFastContextHandlerStream` — 流级别上下文
  - `OOXMLFastContextHandlerProperties` — 属性累积
  - `OOXMLFastContextHandlerTextTable/Row/Cell` — 表格结构
  - `OOXMLFastContextHandlerXNote` — 脚注/尾注
  - `OOXMLFastContextHandlerShape` — DrawingML/VML 形状
- `sw/source/writerfilter/ooxml/OOXMLFactory.hxx` — 从 `model.xml` 代码生成的派发表
- `sw/source/writerfilter/ooxml/model.xml` — OOXML 元素/属性的 schema 定义
- `sw/source/writerfilter/ooxml/OOXMLPropertySet.hxx` — 中间数据结构:
  - `OOXMLValue` — variant 类型 (bool/int/string/PropertySet/...)
  - `OOXMLProperty` — 单个属性 (extends `Sprm`)
  - `OOXMLPropertySet` — 属性集合 (implements `Reference<Properties>`)
- `sw/source/writerfilter/ooxml/OOXMLParserState.hxx` — 解析器全局状态
- `sw/source/writerfilter/ooxml/OOXMLStreamImpl.hxx` — OPC ZIP 流导航

### 关键接口 (资源模型)
- `sw/source/writerfilter/inc/dmapper/resourcemodel.hxx`:
  - `Reference<T>` — 可解析的引用模板
  - `Properties` — `attribute(Id, Value)` / `sprm(Sprm&)`
  - `Table` — `entry(int, Reference<Properties>)`
  - `Stream` — `startParagraphGroup()` / `endParagraphGroup()` / `text()` / `utext()` / `props()` 等

## 阶段2: Domain Mapper (Token 事件 → 文档操作)

### 核心类
- `sw/source/writerfilter/dmapper/DomainMapper.hxx` / `.cxx` — 实现 `LoggedProperties`, `LoggedTable`, `BinaryObj`, `LoggedStream` 四个接口
  - `lcl_startParagraphGroup()` (行 4074) — 开始段落
  - `lcl_endParagraphGroup()` (行 4128) — 结束段落，调用 `finishParagraph()`
  - `lcl_text()` (行 4299) — 接收文本字节
  - `lcl_utext()` (行 4554) — 接收 Unicode 文本
  - `lcl_props()` (行 4974) — 接收属性集
  - `lcl_startSectionGroup()` / `lcl_endSectionGroup()` — 节边界

- `sw/source/writerfilter/dmapper/DomainMapper_Impl.hxx` / `.cxx` — 重量级实现类:
  - `appendTextPortion()` (行 3511) — 插入文本+字符属性
  - `finishParagraph()` (行 2233) — 段落属性定稿（编号、样式、分页）
  - `appendTextContent()` (行 3640) — 插入嵌入内容（OLE、公式、脚注）
  - `appendTextSectionAfter()` (行 3934) — 创建节

### 子处理器
- `sw/source/writerfilter/dmapper/StyleSheetTable.cxx` — 导入 `word/styles.xml`
- `sw/source/writerfilter/dmapper/SettingsTable.cxx` — 导入 `word/settings.xml`
- `sw/source/writerfilter/dmapper/NumberingManager.cxx` — 导入 `word/numbering.xml`
- `sw/source/writerfilter/dmapper/FontTable.cxx` — 导入 `word/fontTable.xml`
- `sw/source/writerfilter/dmapper/ThemeHandler.cxx` — 主题数据
- `sw/source/writerfilter/dmapper/GraphicImport.cxx` — 图片导入
- `sw/source/writerfilter/dmapper/OLEHandler.cxx` — OLE 对象导入
- `sw/source/writerfilter/dmapper/DomainMapperTableManager.hxx` — 表格管理
- `sw/source/writerfilter/dmapper/PropertyMap.hxx` — 属性映射:
  - `PropertyMap` — PropertyId → PropValue 映射
  - `SectionPropertyMap` — 页面布局属性（边距、分栏、边框、页眉页脚）
  - `ParagraphPropertiesPropertyMap` — 段落属性（框架模式、首字下沉、定位）
  - `StyleSheetPropertyMap` — 样式属性（列表级别、大纲级别）

## 阶段3: 文档模型 (SwNodes)

### 节点数组
- `sw/inc/ndarr.hxx` (行 105) — `SwNodes` 类，继承 `BigPtrArray`（B-tree 数组）
  - 包含固定哨兵节点: `m_pEndOfPostIts`, `m_pEndOfInserts`, `m_pEndOfAutotext`, `m_pEndOfRedlines`, `m_pEndOfContent`
  - 工厂方法: `MakeTextNode()`, `MakeGrfNode()`, `InsertTable()`, `InsertTextSection()`
  - 实现: `sw/source/core/docnode/nodes.cxx`

### 节点类型
| 类 | 头文件 | 职责 |
|---|---|---|
| `SwNode` | `sw/inc/node.hxx:96` | 抽象基类，`m_nNodeType` 区分类型 |
| `SwStartNode` | `sw/inc/node.hxx:352` | 节区开始标记，`m_eStartNodeType` 区分子类型 |
| `SwEndNode` | `sw/inc/node.hxx:382` | 节区结束标记 |
| `SwContentNode` | `sw/inc/node.hxx:399` | 内容节点抽象基类，持有 `SwAttrSet` |
| `SwTextNode` | `sw/inc/ndtxt.hxx:115` | 段落节点，`OUString m_Text` + `SwpHints` |
| `SwTableNode` | `sw/inc/node.hxx:539` | 表格节点，持有 `SwTable` |
| `SwSectionNode` | `sw/inc/node.hxx:582` | 节节点，持有 `SwSection` |
| `SwGrfNode` | `sw/inc/ndgrf.hxx` | 图片节点 |
| `SwOLENode` | `sw/inc/ndole.hxx` | OLE 对象节点 |

### 节点实现
- `sw/source/core/docnode/node.cxx` — `SwNode` 基类
- `sw/source/core/docnode/ndtxt.cxx` — `SwTextNode`
- `sw/source/core/docnode/ndtbl.cxx` / `ndtbl1.cxx` — `SwTableNode`
- `sw/source/core/docnode/ndsect.cxx` — `SwSectionNode`

### 样式系统
- `sw/inc/format.hxx:48` — `SwFormat` 基类，持有 `SwAttrSet`
- `sw/inc/frmfmt.hxx:68` — `SwFrameFormat`，用于布局元素
- `sw/inc/fmtcol.hxx:55` — `SwTextFormatColl`（段落样式）
- `sw/inc/pagedesc.hxx:137` — `SwPageDesc`（页面样式描述符，定义页面尺寸、边距、页眉页脚等）

## 阶段4: Frame 树构建

### Frame 类层级
```
SwFrameAreaDefinition (几何)    sw/source/core/inc/frame.hxx:133
  └─ SwFrame (基础)             sw/source/core/inc/frame.hxx:324
       ├─ SwLayoutFrame (容器, m_pLower 链表)
       │    ├─ SwRootFrame         sw/source/core/inc/rootfrm.hxx:83
       │    ├─ SwFootnoteBossFrame sw/source/core/inc/ftnboss.hxx:49
       │    │    ├─ SwPageFrame    sw/source/core/inc/pagefrm.hxx:61
       │    │    └─ SwColumnFrame  sw/source/core/inc/colfrm.hxx:25
       │    ├─ SwBodyFrame         sw/source/core/inc/bodyfrm.hxx:26
       │    ├─ SwHeaderFrame / SwFooterFrame
       │    ├─ SwFootnoteContFrame / SwFootnoteFrame
       │    ├─ SwFlyFrame          sw/source/core/inc/flyfrm.hxx:78
       │    │    ├─ SwFlyFreeFrame
       │    │    │    └─ SwFlyAtContentFrame (也继承 SwFlowFrame)
       │    │    └─ SwFlyInContentFrame
       │    ├─ SwTabFrame (也继承 SwFlowFrame)  sw/source/core/inc/tabfrm.hxx:47
       │    ├─ SwRowFrame          sw/source/core/inc/rowfrm.hxx
       │    ├─ SwCellFrame
       │    └─ SwSectionFrame (也继承 SwFlowFrame) sw/source/core/inc/sectfrm.hxx:49
       └─ SwContentFrame (也继承 SwFlowFrame)    sw/source/core/inc/cntfrm.hxx:58
            ├─ SwTextFrame         sw/source/core/inc/txtfrm.hxx:174
            └─ SwNoTextFrame       sw/source/core/inc/notxtfrm.hxx:31
```

### SwFlowFrame (分页混入类)
- `sw/source/core/inc/flowfrm.hxx:58` — 不是真正的 Frame 类，持有 `SwFrame& m_rThis` 引用
- 被 `SwContentFrame`, `SwTabFrame`, `SwSectionFrame`, `SwFlyAtContentFrame` 继承
- Follow 链: `m_pFollow` / `m_pPrecede` — 连接主 Frame 和续接 Frame

### Frame 创建
- `sw/source/core/layout/frmtool.cxx:2073` — `MakeFrames(SwDoc&, SwNode&, SwNode&)` 批量创建
  1. `FindPrvNxtFrameNode()` 找到已有 Frame 的相邻节点
  2. `SwNode2Layout` 迭代器定位插入点
  3. 对每个节点调用 `MakeFrame()` 或 `sw::MakeTextFrame()`
  4. `pFrame->InsertBehind(pLay, pPrv)` 插入树中
- `sw/source/core/inc/txtfrm.hxx:118` — `MakeTextFrame(SwTextNode&, SwFrame*, FrameMode)`
- `sw/source/core/layout/newfrm.cxx` — Frame 构造函数 (`SwRootFrame`, `SwPageFrame` 等)
- `sw/source/core/docnode/node.cxx:1389` — `MakeFramesForAdjacentContentNode()` 单节点创建
- `sw/source/core/inc/node2lay.hxx:55` / `sw/source/core/docnode/node2lay.cxx` — 节点到布局的桥接

### Frame 树内部结构
- `SwFrame` 成员: `mpRoot`(SwRootFrame*), `mpUpper`(SwLayoutFrame* 父), `mpNext`/`mpPrev`(兄弟)
- `SwLayoutFrame` 成员: `m_pLower`(第一个子节点)，子节点通过 `GetNext()` 链接
- `SwFrameType` 枚举: Root, Page, Column, Header, Footer, FootnoteContainer, Footnote, Body, Fly, Section, Tab, Row, Cell, Txt, NoTxt

## 阶段5: 排版分页

### SwFlowFrame 分页逻辑 (sw/source/core/layout/flowfrm.cxx)
- `IsPageBreak(bool bAct)` (行 1304) — 检查硬分页 (`SvxFormatBreakItem` PageBefore/PageAfter/PageBoth + `SwFormatPageDesc`)
- `IsColBreak(bool bAct)` (行 ~1360) — 检查分栏符
- `CheckMoveFwd(bool& rbMakePage, bool bKeep, bool bIgnoreMyOwnKeepValue)` (行 1994) — 主向前移动决策:
  1. `bKeep` 且下一 Frame 已定位 → `MoveFwd()` 保持在一起
  2. `IsPrevObjMove()` 重叠对象 → 强制前移
  3. `IsPageBreak(false)` → 循环 `MoveFwd()` 直到新页
  4. `IsColBreak(false)` → 循环 `MoveFwd()` 直到新栏
- `MoveFwd(bool bMakePage, bool bPageBreak, bool bMoveAlways)` (行 2101) — 实际移到下一叶节点
- `MoveBwd(bool& rbReformat)` — 空间释放时后移
- `IsKeep()` — 评估 "与下段同页" 属性

### SwTextFrame 排版 (sw/source/core/text/txtfrm.cxx)
- `FormatImpl()` — 核心: 迭代行，调用 `SwTextFormatter`
- `FormatAdjust()` — 处理 widow/orphan，决定断行点
- `AdjustFollow_()` — 分离 master，创建/调整 follow Frame
- `FormatLine()` — 格式化单行

### SwLayAction (排版动作编排)
- `sw/source/core/inc/layact.hxx:59` / `sw/source/core/layout/layact.cxx`
- `Action()` (行 373) — 入口:
  1. 尝试 turbo 模式（单内容 Frame 优化）
  2. 调用 `InternalAction()`
  3. 如有页面删除则重复 (`IsAgain()`)
- `InternalAction()` (行 489):
  1. `m_pRoot->Calc()` 格式化根
  2. 找第一个无效页
  3. 循环: `FormatLayout()` → `FormatContent()` → `PaintContent()`
  4. `SwLayouter` 循环检测防无限循环

### 页面管理
- `sw/source/core/layout/pagechg.cxx` — `SwPageFrame::PreparePage()`, `PrepareHeader()`, `PrepareFooter()`
- `sw/source/core/layout/layouter.cxx` — `SwLayouter` 循环控制、尾注放置

## 阶段6: 渲染接口

### 渲染链
```
SwViewShell::Paint()                    sw/source/core/view/viewsh.cxx:2026
  → SwRootFrame::PaintSwFrame()         sw/source/core/layout/newfrm.cxx
    → SwPageFrame::PaintSwFrame()       (页面背景、边距、阴影、边框)
      → SwLayoutFrame::PaintSwFrame()   sw/source/core/inc/layfrm.hxx:102 (遍历 m_pLower)
        → SwTextFrame::PaintSwFrame()   (用 SwParaPortion 行数据绘制文本)
        → SwFrame::PaintSwFrame()       sw/source/core/layout/paintfrm.cxx (背景、边框)
```

### 关键渲染类
- `sw/inc/viewsh.hxx:117` — `SwViewShell`，持有 `SwRootFramePtr mpLayout`, `OutputDevice* mpOut`
- `sw/source/core/inc/viewimp.hxx` — `SwViewShellImp`，管理绘制队列、`SwLayAction`、`SwLayIdle`
- `sw/source/core/layout/paintfrm.cxx` — `PaintBaBo()`(背景), `PaintSwFrameShadowAndBorder()`(边框), `PaintBorderLine()`

---

# 三、建议的迁移策略

## 阶段划分

### Phase 0: 基础设施
- 建立 CMake 工程框架（已有）
- 引入必要的基础类型: `tools::Rectangle`, `Point`, `Size`, `Color` 等
- 引入 `BigPtrArray` / `BigPtrEntry`（SwNodes 的底层容器）
- 引入 `SwAttrSet` / `SfxItemSet` 属性系统（或设计简化替代）

### Phase 1: DOCX 解析 (ZIP → XML → 结构化数据)
- 选择方案: 直接使用 pugixml 解析 OOXML XML（已在 third_party 中），不引入 LibreOffice 的 OOXML Tokenizer 体系
- 实现: 解压 ZIP (miniz) → 解析 `word/document.xml` + 子流 → 构建中间表示
- 参考 `OOXMLFastContextHandler` 的逻辑理解每个 XML 元素的含义
- 需要处理的子流: `word/styles.xml`, `word/numbering.xml`, `word/settings.xml`, `word/fontTable.xml`, `word/header*.xml`, `word/footer*.xml`, `word/footnotes.xml`, `word/endnotes.xml`

### Phase 2: 文档模型 (SwNodes 构建)
- 迁移 `SwNodes` / `SwNode` / `SwStartNode` / `SwEndNode` / `SwTextNode` / `SwTableNode` / `SwSectionNode`
- 迁移 `SwTextFormatColl` / `SwFormat` / `SwFrameFormat` / `SwPageDesc` 样式系统
- 从 Phase 1 的解析结果构建 SwNodes
- 关键: 保持与 LibreOffice 相同的节点结构和属性值

### Phase 3: Frame 树构建
- 迁移 `SwFrame` / `SwLayoutFrame` / `SwRootFrame` / `SwPageFrame` / `SwBodyFrame`
- 迁移 `SwContentFrame` / `SwTextFrame` / `SwFlowFrame`
- 迁移 `MakeFrames()` / `MakeTextFrame()` / `SwNode2Layout`
- 表格相关: `SwTabFrame` / `SwRowFrame` / `SwCellFrame`

### Phase 4: 排版分页
- 迁移 `SwTextFrame::FormatImpl()` / `FormatAdjust()` / `AdjustFollow_()`
- 迁移 `SwFlowFrame::CheckMoveFwd()` / `MoveFwd()` / `MoveBwd()`
- 迁移 `SwLayAction::Action()` / `InternalAction()`
- 迁移页面创建逻辑 `SwPageFrame::PreparePage()`

### Phase 5: 渲染指令记录
- 实现轻量级渲染层: 不做真正的像素绘制，而是记录渲染指令
- 拦截 `PaintSwFrame()` 调用，记录: Frame 类型、位置、尺寸、文本内容、字体、颜色等
- 输出为结构化文本/XML 用于比对

---

# 四、测试策略

## 测试框架

### 基准数据生成
1. 使用 LibreOffice 打开 `docx/sample.docx`
2. 通过 `SW_DEBUG` 模式或 UNO API dump 出 LibreOffice 的:
   - SwNodes 结构 (nodes.xml)
   - Layout Frame 树 (layout.xml)
   - 渲染指令序列

### 比对测试流程
```
输入: docx/sample.docx
  │
  ├─ LibreOffice 路径:
  │   soffice → SwNodes → Frame树 → Layout → 渲染指令 → reference_output.txt
  │
  └─ aproj/docx 路径:
      docx_reader → SwNodes → Frame树 → Layout → 渲染指令 → test_output.txt
  │
  └─ diff reference_output.txt test_output.txt → 发现差异 → 修复
```

### 渲染指令记录格式建议
每条指令一行，格式:
```
<页码> <Frame类型> <x> <y> <宽> <高> <内容摘要> [<额外属性>]
```
示例:
```
PAGE 1 595 842
TEXTFRAME 7200 1440 45000 400 "Hello World" font=Arial size=1200 bold=0
TEXTFRAME 7200 2880 45000 400 "Second paragraph" font=Arial size=1200 bold=0
TABLE 7200 4320 45000 2000
ROW 7200 4320 45000 600
CELL 7200 4320 15000 600
TEXTFRAME 7300 4320 14800 400 "Cell 1" font=Arial size=1000
```

### 逐步验证
- **Phase 2 验证**: dump 出 SwNodes，与 LibreOffice 的 nodes.xml 比对
- **Phase 3 验证**: dump 出 Frame 树，与 LibreOffice 的 layout.xml 比对
- **Phase 4 验证**: dump 出分页结果（每页的 Frame 列表），比对分页点
- **Phase 5 验证**: 比对完整渲染指令序列

---

# 五、关键设计决策

## 1. 是否引入 UNO API 层？
LibreOffice 的 Domain Mapper 通过 UNO API (XTextAppend, XTextCursor) 操作文档。在提取版本中:
- **方案A**: 直接调用 SwDoc 内部 API 构建 SwNodes（推荐，更直接，减少抽象层）
- **方案B**: 模拟 UNO API 层（更贴近原代码，但增加复杂度）

## 2. 属性系统简化
LibreOffice 的 `SfxItemSet` / `SwAttrSet` 非常复杂（含继承、条件格式等）。可以:
- 保留完整的属性系统（精确但工作量大）
- 设计简化的属性映射（快速但可能有差异）

## 3. 字体度量
排版需要精确的字体度量（字宽、行高、上升/下降）。LibreOffice 使用 `vcl` 模块的字体引擎:
- 可以使用系统的字体 API (DirectWrite/CoreText)
- 或使用 stb_truetype（已在 third_party 中，但精度有限）

## 4. 文本换行算法
`SwTextFrame::FormatImpl()` 中的换行逻辑非常复杂（断字、连字、双向文本等）。建议:
- 先实现基本换行（空格断行）
- 逐步加入断字、Kashida 等高级特性

---

# 六、工具使用

## LibreOffice 调试 dump
LibreOffice 内置调试功能 (需编译时启用 `SW_DEBUG`):
- `SW_DEBUG=1` 环境变量启用调试模式
- F12: dump layout.xml (Frame 树)
- Shift+F12: dump nodes.xml (SwNodes)

相关工具脚本:
- `aproj/docx/tools/dump_lo_debug.ps1` — 自动化 SW_DEBUG dump 流程
- `aproj/docx/tools/dump_lo_nodes.py` — 通过 UNO bridge dump (需要运行中的 soffice)

## 源码阅读建议
阅读顺序（按数据流方向）:
1. `sw/source/writerfilter/ooxml/OOXMLFastContextHandler.hxx` — 理解 XML 元素如何被处理
2. `sw/source/writerfilter/dmapper/DomainMapper.cxx` — 理解 Token 事件如何转为文档操作
3. `sw/source/writerfilter/dmapper/DomainMapper_Impl.cxx` — 理解具体的文本/段落/表格插入
4. `sw/inc/ndarr.hxx` + `sw/inc/node.hxx` — 理解文档模型
5. `sw/source/core/layout/frmtool.cxx` — 理解 Frame 树如何从节点构建
6. `sw/source/core/layout/flowfrm.cxx` — 理解分页逻辑
7. `sw/source/core/layout/layact.cxx` — 理解排版编排
8. `sw/source/core/text/txtfrm.cxx` — 理解文本格式化（最复杂的部分）

---

# 七、实施进度

## 已完成

### Phase 0: 基础设施 ✅
- `src/core/types.h` — 基础类型别名 (sal_Int32, SwTwips 等)
- `src/core/swrect.h` — SwRect 矩形类
- `src/core/bparr.h/cpp` — BigPtrArray / BigPtrEntry 节点容器

### Phase 1: 文档模型 ✅
- `src/core/node.h/cpp` — SwNode 类层级 (SwStartNode, SwEndNode, SwContentNode, SwTextNode, SwTableNode, SwSectionNode)
- `src/core/ndarr.h/cpp` — SwNodes 节点数组 + SwNodeIndex
- `src/core/format.h/cpp` — 样式系统 (SwFormat, SwFrameFormat, SwTextFormatColl, SwPageDesc)
- `src/core/doc.h/cpp` — SwDoc 文档容器

### Phase 2: Frame 树 ✅
- `src/frame/frame.h/cpp` — Frame 类层级:
  - SwFrameAreaDefinition (几何)
  - SwFrame (基类)
  - SwLayoutFrame (容器)
  - SwRootFrame (根)
  - SwPageFrame (页面)
  - SwBodyFrame (正文)
  - SwContentFrame (内容基类)
  - SwTextFrame (段落)
  - SwFlowFrame (分页混入)
  - SwTabFrame, SwRowFrame, SwCellFrame (表格)
  - SwSectionFrame (节)
- `src/frame/frmtree.h/cpp` — MakeFrames() / InitLayout() 入口函数

### Phase 3: DOCX 解析器重写 ✅
- `src/filter/docx_parser.h/cpp` — 新版 DOCX 解析器，输出 SwDoc

### Phase 4: 排版引擎 ✅
- `src/layout/layact.h/cpp` — SwLayAction 排版编排 + TextFormatter 文本格式化

### Phase 5: 渲染指令记录 ✅
- `src/render/render_log.h/cpp` — RenderLogger 渲染指令记录器 + DumpFrameTreeXml/DumpNodesXml 调试工具

# 八、依赖的 LibreOffice 核心模块

| 模块 | 用途 | 是否必须 |
|---|---|---|
| `sw/inc/` | 文档模型头文件 | ✅ 必须 |
| `sw/source/core/docnode/` | 节点实现 | ✅ 必须 |
| `sw/source/core/layout/` | Frame 树 + 排版 | ✅ 必须 |
| `sw/source/core/text/` | 文本格式化 | ✅ 必须 |
| `sw/source/core/inc/` | Frame 类头文件 | ✅ 必须 |
| `sw/source/writerfilter/dmapper/` | 参考逻辑（不直接迁移） | 📖 参考 |
| `sw/source/writerfilter/ooxml/` | 参考逻辑（不直接迁移） | 📖 参考 |
| `tools/` | 基础类型 (Rectangle, Point 等) | ✅ 必须 |
| `vcl/` | 字体度量、渲染 | ⚠️ 部分需要 |
| `sot/`, `comphelper/` | ZIP 存储、工具 | ⚠️ 可用 miniz 替代 |
| `oox/` | OOXML 基础设施 | 📖 参考 |
