# LibreOffice DOCX 组件架构与核心逻辑

## 1. 总体架构

LibreOffice 的 DOCX 处理分布在三个主要代码区域：

| 模块 | 路径 | 职责 |
|------|------|------|
| **oox** | `oox/source/` | 通用 OOXML 工具包（Writer/Calc/Impress 共享）：ZIP 存储、SAX 快速解析、Token 管理、命名空间 |
| **writerfilter** | `sw/source/writerFilter/` | Writer 专用 OOXML **导入**管线：OOXML Token → DomainMapper → SwDoc |
| **ww8 (export)** | `sw/source/filter/ww8/` | DOCX **导出**路径：`DocxExport` / `DocxExportFilter` |

导入与导出是**完全独立的代码路径**，仅共享目标文档模型 `SwDoc`。

### 1.1 三层架构总览

```
┌─────────────────────────────────────────────────────┐
│              渲染层 (Paint / View)                    │
│   SwViewShell → SwRootFrame::Paint → VCL Output     │
├─────────────────────────────────────────────────────┤
│              布局层 (Layout / Format)                 │
│   SwRootFrame → SwPageFrame → SwBodyFrame → ...     │
│   Frame 树 + 排版引擎 (MakeAll / Follow / 分页)      │
├─────────────────────────────────────────────────────┤
│              模型层 (Document Model)                  │
│   SwDoc → SwNodes → SwNode 树 + SwFormat 样式系统    │
│   文档内容 + 样式 + 属性                              │
└─────────────────────────────────────────────────────┘
```

**核心设计原则**：
- **模型-布局分离**：SwNodes 存储逻辑内容，Frame 树存储物理布局，两者通过 `SwClient`/`SwModify` 观察者模式同步
- **惰性计算**：Frame 在需要时才创建和格式化（`MakeFrames` 按需触发）
- **增量更新**：通过 `Invalidate`/`Validate` 标志位，只重新格式化变化的部分
- **观察者模式**：`SwClient`/`SwModify` 实现模型变化→布局失效→重新格式化的闭环

---

## 2. DOCX 导入管线

### 2.1 数据流总览

```
.docx ZIP 文件
  └─ OOXMLStream (打开 ZIP，按 relationships 定位各 part)
      └─ OOXMLDocumentImpl (驱动各 part 的 SAX 解析)
          └─ OOXMLFastContextHandler 层级 (将 XML 元素转为 Token 事件)
              └─ OOXMLFactory (按 model.xml 分发到对应 handler)
                  └─ DomainMapper (接收 property/stream/table/binary 事件)
                      └─ DomainMapper_Impl (通过 UNO API 写入 SwDoc)
                          └─ SwDoc / SwNodes / SwTextNode
```

### 2.2 入口点

`WriterFilter::filter()` (`sw/source/writerfilter/filter/WriterFilter.cxx`)

1. `oox::core::FilterDetect` 解密 ZIP 包
2. `DomainMapperFactory::createMapper()` — 创建 Stream 消费者
3. `OOXMLDocumentFactory::createStream()` — 创建 ZIP/OOXML 流读取器
4. `OOXMLDocumentFactory::createDocument()` — 创建文档编排器
5. `pDocument->resolve(*pStream)` — 启动解析

### 2.3 三层架构

**Layer 1: OOXMLStream** (`OOXMLStreamImpl.cxx`)
- 封装 ZIP 包，通过 `XRelationshipAccess` 导航 OOXML relationship 图
- 映射关系类型到流目标：`officeDocument→DOCUMENT`, `styles→STYLES`, `numbering→NUMBERING`, `fontTable→FONTTABLE`, `footnotes/endnotes→FOOTNOTES/ENDNOTES`, `comments→COMMENTS`, `theme→THEME`, `settings→SETTINGS`, `header/footer→HEADER/FOOTER`

**Layer 2: OOXML Tokenizer** (`sw/source/writerfilter/ooxml/`)
- `OOXMLFastContextHandler` — 实现 `XFastContextHandler`（SAX 快速解析接口），是层级化 handler 的根
  - `OOXMLFastContextHandlerStream` — 文本内容上下文
  - `OOXMLFastContextHandlerProperties` — 属性集合上下文
  - `OOXMLFastContextHandlerTable` — 表格上下文
  - `OOXMLFastContextHandlerXNote` — 脚注/尾注
  - `OOXMLFastContextHandlerShape` — DrawingML/VML 形状
  - `OOXMLFastContextHandlerMath` — OMML 数学公式
- `OOXMLFactory` — 由 `model.xml` 通过 Python 脚本自动生成，将 XML 元素分发到正确的 context handler
- `OOXMLPropertySet` — 累积 XML 属性为 key-value 对，再转发给 domain mapper
- `OOXMLDocumentImpl` — 编排整体文档解析，按 relationship 依次解析各 part

**Layer 3: DomainMapper** (`sw/source/writerfilter/dmapper/`)
- `DomainMapper` 实现四个接口：`LoggedProperties`, `LoggedTable`, `BinaryObj`, `LoggedStream`
- `DomainMapper_Impl` 持有所有内部状态：
  - 5 个属性栈（SECTION, PARAGRAPH, CHARACTER, STYLESHEET, LIST）
  - `StyleSheetTable` — Word 样式 → Writer 样式映射
  - `NumberingManager` — 列表/编号导入
  - `SettingsTable` — 文档设置
  - `FontTable` — 字体映射
  - `ThemeHandler` — Office 主题处理
  - `DomainMapperTableHandler` — 表格导入
  - `GraphicImport` / `OLEHandler` — 图片和 OLE
  - `SdtHelper` — 结构化文档标签（内容控件）

### 2.4 关键子流解析顺序

`OOXMLDocumentImpl::resolve()` 按以下顺序解析：
1. SETTINGS（文档设置）
2. THEME（主题）
3. GLOSSARY（自动图文集）
4. EMBEDDINGS（嵌入对象）
5. CUSTOMXML
6. FONTTABLE, STYLES, NUMBERING
7. 主文档 document.xml（最后解析）
8. 脚注/尾注/页眉/页脚/批注 — 按需延迟解析

### 2.5 导入后布局触发

导入完成后，布局通过以下路径触发：

```
SwDoc::InitLayout()
  → new SwRootFrame(pFormat, pViewShell)
  → SwRootFrame::Init(pFormat)
      → 创建初始 SwPageFrame
      → 触发 SwPageFrame::MakeAll()
          → 创建 SwBodyFrame / SwHeaderFrame / SwFooterFrame
          → 遍历子节点创建对应的 Frame
```

`SwNode2Layout` 类（`sw/source/core/layout/node2lay.cxx`）负责将 SwNode 树映射到 Frame 树：
- 遍历 SwNodes 区间
- 为每个 `SwContentNode` 调用 `MakeFrame()` 创建对应的 `SwContentFrame`
- 为 `SwTableNode` 创建 `SwTabFrame` + `SwRowFrame` + `SwCellFrame`
- 为 `SwSectionNode` 创建 `SwSectionFrame`

---

## 3. DOCX 导出管线

### 3.1 类层次

```
MSWordExportBase (wrtww8.hxx)  ── 抽象基类，所有 Word 格式导出共享
  ├── WW8Export                 ── 二进制 .DOC 导出
  └── DocxExport                ── DOCX 导出
```

### 3.2 策略模式

`MSWordExportBase::WriteText()` 遍历 `SwNodes`，调用 `OutputTextNode()`, `OutputGrfNode()` 等，将格式化输出委托给 `AttributeOutputBase` 子类：
- `WW8AttributeOutput` → 二进制 .DOC
- `DocxAttributeOutput` → OOXML XML
- `RtfAttributeOutput` → RTF

### 3.3 导出入口

`DocxExportFilter::exportDocument()` (`sw/source/filter/ww8/docxexportfilter.cxx`)：
1. 从 UNO 模型获取 `SwDoc*`
2. `SwViewShell::CalcLayout()` 确保布局最新
3. 创建覆盖整个文档的 `SwPaM`
4. 构造 `DocxExport` 并调用 `ExportDocument(true)`

### 3.4 导出的 OOXML Part

`DocxExport::ExportDocument_Impl()` 依次写入：
- `styles.xml` — `InitStyles()`
- `fontTable.xml` — `WriteFonts()`
- `numbering.xml` — `WriteNumbering()`
- `footnotes.xml` / `endnotes.xml` — `WriteFootnotesEndnotes()`
- `comments.xml` — `WritePostitFields()`
- `document.xml` — `WriteMainText()`（主文档体）
- `settings.xml` — `WriteSettings()`
- `theme1.xml` — `WriteTheme()`
- `docProps/core.xml` — `WriteProperties()`

---

## 4. 文档模型 (SwDoc)

### 4.1 SwDoc — 中央文档类

`SwDoc` (`sw/inc/doc.hxx`) 是 `final` 类，拥有文档的一切。功能拆分为 ~20 个 Manager 类，各实现一个 `IDocument*` 接口：

| 接口 | 职责 |
|------|------|
| `IDocumentContentOperations` | 插入/删除内容 |
| `IDocumentLayoutAccess` | 布局引擎访问 |
| `IDocumentFieldsAccess` | 字段管理 |
| `IDocumentSettingAccess` | 文档设置 |
| `IDocumentRedlineAccess` | 修订追踪 |
| `IDocumentListsAccess` | 列表/编号 |
| `IDocumentMarkAccess` | 书签 |
| `IDocumentUndoRedo` | 撤销/重做 |
| `IDocumentStylePoolAccess` | 样式池 |
| `IDocumentDrawModelAccess` | 绘图模型访问 |
| `IDocumentLinksAdministration` | 链接管理 |
| `IDocumentDeviceAccess` | 打印设备 |

### 4.2 SwNodes — 节点数组

`SwNodes` (`sw/inc/ndarr.hxx`) 继承自 `BigPtrArray`（块状数组，每块 1000 指针）。构造时创建固定的顶层段：

```
[0] SwStartNode ─┐
[1] SwEndNode   ─┘ PostIts 区
[2] SwStartNode ─┐
[3] SwEndNode   ─┘ Inserts 区（脚注内容）
[4] SwStartNode ─┐
[5] SwEndNode   ─┘ AutoText 区（浮动框架/页眉页脚）
[6] SwStartNode ─┐
[7] SwEndNode   ─┘ Redlines 区（修订内容）
[8] SwStartNode ─┐
[9] SwEndNode   ─┘ Content 区（文档正文）
```

**关键特性**：
- 基于 `BigPtrArray` 实现，支持 O(log n) 的插入/删除操作
- 使用分段数组结构（每块约 100 个指针），避免大文档的内存拷贝
- 维护 `SwOutlineNodes`（排序向量）索引，加速大纲/目录操作
- 固定区域（PostIts/Inserts/AutoText/Redlines）在构造时创建，正文区动态增长

### 4.3 SwNode 类型层次

```
SwNode (抽象基类, 继承 sw::BorderCacheOwner + BigPtrEntry)
  │
  │  核心成员:
  │    m_nNodeType (SwNodeType)      — 节点类型标识
  │    m_pStartOfSection (SwStartNode*) — 所属区间的起始节点
  │    m_aAnchoredFlys (vector<SwFrameFormat*>) — 锚定在此节点上的浮动对象
  │    m_vIndices (SwNodeIndex*)     — 指向此节点的索引环
  │    m_eMerge (Merge)              — Redline 合并状态
  │
  ├── SwStartNode           ── 开始一个"段落区间"
  │     │  m_nStartNodeType (SwStartNodeType)
  │     ├── SwTableNode     ── 表格（继承 SwStartNode）
  │     │     持有 SwTable 逻辑结构
  │     └── SwSectionNode   ── 节（继承 SwStartNode）
  │           持有 SwSection 数据
  │
  ├── SwEndNode             ── 结束一个"段落区间"
  │
  └── SwContentNode         ── 抽象内容节点（继承 BroadcastingModify + SwContentIndexReg）
        │  核心成员:
        │    m_pSwFrameFormat (SwFrameFormat*)  — 关联的框架格式
        │    MakeFrame() / DelFrames()          — Frame 创建/删除
        │
        ├── SwTextNode      ── 文本段落（最核心的节点类型）
        │     核心成员:
        │       m_Text (OUString)              — 文本内容
        │       m_pSwpHints (SwpHints*)        — 内联文本属性（粗体、斜体、字段等）
        │       mpNodeNum (SwNodeNum*)         — 编号状态
        │     关键方法:
        │       InsertText(), SplitContentNode(), JoinNext()
        │
        └── SwNoTextNode    ── 非文本内容（抽象）
              ├── SwGrfNode ── 图片
              └── SwOLENode ── OLE 对象
```

**SwNodeType 枚举** (`sw/inc/ndtyp.hxx`)：

```cpp
enum class SwNodeType : sal_uInt8 {
    NONE         = 0x00,
    End          = 0x01,
    Start        = 0x02,
    Table        = 0x04 | Start,  // 继承自 StartNode
    Text         = 0x08,
    Grf          = 0x10,
    Ole          = 0x20,
    Section      = 0x40 | Start,  // 继承自 StartNode
    PlaceHolder  = 0x80,
    // 组合掩码:
    NoTextMask   = Grf | Ole,     // 非文本节点
    ContentMask  = Text | NoTextMask, // 所有内容节点
};
```

**SwStartNodeType 枚举**：

```cpp
enum SwStartNodeType {
    SwNormalStartNode = 0,    // 普通节
    SwTableBoxStartNode,      // 表格单元格
    SwFlyStartNode,           // 浮动框架内容
    SwFootnoteStartNode,      // 脚注
    SwHeaderStartNode,        // 页眉
    SwFooterStartNode         // 页脚
};
```

### 4.4 SwTextNode — 段落

`SwTextNode` (`sw/inc/ndtxt.hxx`) 是最重要的节点类型：
- `m_Text` (`OUString`) — 实际文本内容
- `m_pSwpHints` (`SwpHints`) — 内联文本属性（粗体、斜体、字段、脚注等），按位置排序
- `mpNodeNum` — 编号状态
- `m_aAnchoredFlys` — 锚定在此段落上的浮动框架列表
- 关键方法：`InsertText()`, `SplitContentNode()`（回车分段）, `JoinNext()`（合并段落）, `MakeFrame()` → 创建 `SwTextFrame`

### 4.5 表格节点结构

```
SwTableNode (StartNode, type=TableBoxStartNode)
  SwStartNode (TableBoxStartNode) ── 行1列1
    [内容节点]
  SwEndNode
  SwStartNode (TableBoxStartNode) ── 行1列2
    [内容节点]
  SwEndNode
  ...
SwEndNode
```

`SwTable` 持有逻辑结构：`SwTableLine`（行）→ `SwTableBox`（单元格，引用 `SwStartNode`）

### 4.6 位置与选择系统

- **`SwPosition`** — `SwNodeIndex`（哪个节点）+ `SwContentIndex`（节点内字符偏移）
- **`SwNodeIndex`** — 引用计数的节点数组指针，注册在目标节点的环中，节点插入/删除时自动更新
- **`SwContentIndex`** — 节点内字符偏移，维护双向链表
- **`SwPaM`** (Point and Mark) — 两个 `SwPosition` 端点表示选区，多个 SwPaM 形成环（多选区）

---

## 5. 样式/格式系统

### 5.1 类层次

```
SwFormat (基类, 继承 BroadcastingModify)
  │  核心成员:
  │    m_aAttrSet (SwAttrSet)      — 属性集合
  │    m_aFormatName (OUString)    — 格式名称
  │    m_nWhichId (sal_uInt16)     — 格式类型标识
  │
  ├── SwFrameFormat         ── 布局元素样式（段落、表格、浮动框）
  │     关键属性: 锚点类型、大小、位置、环绕、边框
  │     子类用途:
  │       - 段落格式 (RES_FRMFMT_TEXT)
  │       - 表格格式 (RES_FRMFMT_TABLE)
  │       - 浮动框架格式 (RES_FRMFMT_FLY*)
  │       - 页面格式 (RES_FRMFMT_PAGE)
  │
  ├── SwCharFormat          ── 字符样式
  ├── SwTextFormatColl      ── 段落样式集合
  ├── SwGrfFormatColl       ── 图片样式集合
  ├── SwSectionFormat       ── 节样式
  └── SwTableFrameFormat    ── 表格样式
```

### 5.2 属性系统

- `SwAttrPool` — 文档中所有属性项的主池（继承 `SfxItemPool`）
- `SwAttrSet` — `SfxPoolItem` 对象集合，通过 `WhichId` 标识
- 属性类别：
  - `RES_CHRATR_*` — 字符属性（字体、粗细、大小、颜色等）
  - `RES_PARATR_*` — 段落属性（对齐、间距、缩进、编号等）
  - `RES_FRMATR_*` — 框架属性（大小、锚点、环绕、边框等）
  - `RES_GRFATR_*` — 图片属性
  - `RES_BREAK_*` — 分页/分栏控制
  - `RES_KEEP_*` — Keep 属性（与下段同页）

---

## 6. 布局引擎 (Frame Tree) — 核心详解

### 6.1 Frame 类层次

```
SwFrame (基类, 继承 SwFrameAreaDefinition + SwClient + SfxBroadcaster)
  │
  │  核心成员:
  │    mpRoot (SwRootFrame*)       — 所属根框架
  │    mpUpper (SwLayoutFrame*)    — 父框架
  │    mpNext (SwFrame*)           — 下一个兄弟框架
  │    mpPrev (SwFrame*)           — 上一个兄弟框架
  │    m_pDrawObjs (SwSortedObjs*) — 锚定在此 Frame 上的绘图对象
  │    mnFrameType (SwFrameType)   — 框架类型标识
  │
  │  关键虚方法:
  │    MakeAll(RenderContext*)     — 核心格式化入口（纯虚）
  │    ShrinkFrame()               — 缩小框架
  │    GrowFrame()                 — 增大框架
  │    MakePos()                   — 计算位置
  │
  │  状态标志:
  │    mbInvalidR2L    — 从右到左标志无效
  │    mbVertical      — 垂直方向
  │    mbValidLineNum  — 行号有效
  │    mbFixSize       — 固定大小
  │    mbCompletePaint — 需要完整重绘
  │    mbRetouche      — 需要修饰
  │    mbColLocked     — 锁定 Grow/Shrink
  │
  ├── SwLayoutFrame (容器 Frame, 有 m_pLower 子链)
  │     │  核心成员:
  │     │    m_pLower (SwFrame*)  — 第一个子框架
  │     │
  │     ├── SwRootFrame       ── 布局树根，Lower() 链包含 SwPageFrame
  │     │     核心成员:
  │     │       maPageRects (vector<SwRect>)  — 所有页面矩形
  │     │       mnViewWidth, mnColumns        — 视图宽度和列数
  │     │       mbBookMode                    — 书籍视图模式
  │     │       mpLastPage (SwPageFrame*)     — 最后一个页面
  │     │       mpCurrShell (SwViewShell*)    — 当前视图 Shell
  │     │       mpDrawPage (SdrPage*)         — 绘图页面
  │     │       mbCheckSuperfluous            — 检查空页面
  │     │       mbAssertFlyPages              — 为浮动对象插入页面
  │     │
  │     ├── SwPageFrame       ── 页面（继承 SwFootnoteBossFrame）
  │     │     核心成员:
  │     │       m_pDesc (SwPageDesc*)         — 页面描述（大小、边距等）
  │     │       m_nPhyPageNum (sal_uInt16)    — 物理页码
  │     │       m_pSortedObjs (SwSortedObjs*) — 页面上的浮动对象
  │     │       m_bInvalidContent             — 内容无效
  │     │       m_bInvalidLayout              — 布局无效
  │     │       m_bFootnotePage               — 脚注页
  │     │       m_bEmptyPage                  — 空页
  │     │
  │     ├── SwBodyFrame       ── 正文区域（页面中页眉页脚之间的区域）
  │     ├── SwHeaderFrame     ── 页眉
  │     ├── SwFooterFrame     ── 页脚
  │     ├── SwColumnFrame     ── 栏（多栏布局中的单栏）
  │     ├── SwFootnoteContFrame ── 脚注容器（页面底部的脚注区域）
  │     ├── SwFootnoteFrame   ── 单个脚注
  │     │
  │     ├── SwTabFrame        ── 表格（+ SwFlowFrame 支持跨页）
  │     │     ├── SwRowFrame  ── 表格行
  │     │     └── SwCellFrame ── 表格单元格
  │     │
  │     ├── SwSectionFrame    ── 节（+ SwFlowFrame 支持跨页）
  │     │
  │     └── SwFlyFrame        ── 浮动框架（文本框、图片框等）
  │           同时继承 SwAnchoredObject
  │           ├── SwFlyAtContentFrame  — 段落锚定 (FLY_AT_PARA)
  │           ├── SwFlyAtCharFrame     — 字符锚定 (FLY_AT_CHAR)
  │           ├── SwFlyLayFrame        — 页面/框架锚定 (FLY_AT_PAGE/FLY_AT_FLY)
  │           └── SwFlyInContentFrame  — 作为字符嵌入 (FLY_AS_CHAR)
  │
  └── SwContentFrame (内容 Frame, + SwFlowFrame 混入)
        │  同时继承 SwFrame 和 SwFlowFrame
        │  SwFlowFrame 提供 follow 链和分页能力
        │
        │  核心方法:
        │    MakeAll()         — 内容格式化入口
        │    MakePrtArea()     — 计算打印区域
        │    WouldFit_()       — 检查是否能放入给定空间
        │
        ├── SwTextFrame       ── 文本段落布局（最复杂）
        │     核心成员:
        │       m_pPara (SwParaPortion*)  — 排版后的段落结构（行链表）
        │       m_nOfst (TextFrameIndex)  — 文本偏移（follow 时非零）
        │     关键方法:
        │       Format()        — 文本格式化
        │       FormatLine()    — 行格式化
        │       SplitFrame()    — 拆分框架（创建 follow）
        │       JoinFrame()     — 合并 follow 框架
        │
        └── SwNoTextFrame     ── 图片/OLE 布局
              ├── SwGrfFrame  — 图片框架
              └── SwOLEFrame  — OLE 对象框架
```

### 6.2 SwFlowFrame — 跨页流核心机制

`SwFlowFrame` (`sw/source/core/inc/flowfrm.hxx`) 是一个**混入类**（mixin），不是独立的 Frame，而是通过组合嵌入到 `SwContentFrame`、`SwTabFrame`、`SwSectionFrame` 中，赋予它们跨页流动能力。

```cpp
class SwFlowFrame {
    SwFrame& m_rThis;  // 关联的宿主 Frame（友元关系）

    // follow 链 — 核心数据结构
    SwFlowFrame *m_pFollow;   // 下一个 follow 框架（内容溢出部分）
    SwFlowFrame *m_pPrecede;  // 前一个框架（master 方向）

    // 状态标志
    bool m_bLockJoin   : 1;   // 锁定，禁止合并（Join）
    bool m_bUndersized : 1;   // 尺寸不足（小于理想大小）
    bool m_bFlyLock    : 1;   // 锁定字符锚定浮动对象的位置

    // 静态标志
    static bool s_bMoveBwdJump;  // 标记后移是否跨多页跳转
};
```

**Follow 链示意图**：

```
一个长段落跨三页的情况：

Page 1:
  SwBodyFrame
    └─ SwTextFrame [master]
         ├─ 文本内容 "第一段开头..."
         ├─ m_pFollow ──────────────────┐
         └─ m_pPrecede = nullptr        │
                                        │
Page 2:                                 │
  SwBodyFrame                           │
    └─ SwTextFrame [follow #1] ←────────┘
         ├─ 文本内容 "...中间部分..."
         ├─ m_pFollow ──────────────────┐
         └─ m_pPrecede → master         │
                                        │
Page 3:                                 │
  SwBodyFrame                           │
    └─ SwTextFrame [follow #2] ←────────┘
         ├─ 文本内容 "...结尾部分"
         ├─ m_pFollow = nullptr
         └─ m_pPrecede → follow #1
```

**关键方法**：

```cpp
// follow 链管理
bool HasFollow() const;                    // 是否有 follow
bool IsFollow() const;                     // 自己是否是 follow
const SwFlowFrame* GetFollow() const;      // 获取 follow
void SetFollow(SwFlowFrame* pFollow);      // 设置 follow
const SwFlowFrame* GetPrecede() const;     // 获取前驱

// 分页决策
bool MoveFwd(bool bMakePage, bool bPageBreak, bool bMoveAlways);  // 前移到下一页
bool MoveBwd(bool &rbReformat);                                  // 后移到上一页
virtual bool ShouldBwdMoved(SwLayoutFrame*, bool&) = 0;          // 判断是否应后移

// 空间检查
bool WouldFit(SwTwips nSpace, SwLayoutFrame *pNewUpper,
              bool bTstMove, const bool bObjsInNewUpper);         // 检查空间是否足够

// Keep 和 Break 属性
bool IsKeep(SvxFormatKeepItem const&, SvxFormatBreakItem const&, bool);
bool IsPageBreak(bool bAct) const;   // 是否有分页符
bool IsColBreak(bool bAct) const;    // 是否有分栏符

// 树操作
void MoveSubTree(SwLayoutFrame* pParent, SwFrame* pSibling);  // 移动子树到新父节点
```

### 6.3 Frame 排版核心流程 — MakeAll()

每个 Frame 类都实现自己的 `MakeAll()` 方法，这是排版引擎的核心入口。

#### 6.3.1 SwContentFrame::MakeAll() 流程

```
SwContentFrame::MakeAll(RenderContext*)
  │
  ├─ 1. 检查锁定状态
  │     if (IsJoinLocked() || IsColLocked()) return;
  │
  ├─ 2. 计算位置 — MakePos()
  │     根据父框架和上一个兄弟的位置确定自身位置
  │     考虑上边距 (UpperSpace)、缩进、对齐等
  │
  ├─ 3. 计算打印区域 — MakePrtArea()
  │     根据边框、内边距计算 Frame 内部的打印区域
  │     使用 SwBorderAttrs 获取边框宽度
  │
  ├─ 4. 计算大小 — Format()
  │     SwTextFrame::Format() → 文本断行、计算总高度
  │     SwNoTextFrame::Format() → 使用固有大小
  │
  ├─ 5. 检查是否需要后移 — MoveBwd()
  │     if (ShouldBwdMoved(pNewUpper, reformat))
  │       MoveBwd(reformat);
  │
  ├─ 6. 检查是否需要前移 — MoveFwd()
  │     if (!WouldFit(nSpace, pNewUpper, false, false))
  │       MoveFwd(true, false, false);
  │
  └─ 7. 处理 follow 链
        if (内容溢出)
          SplitFrame() → 创建 follow
        if (内容不足 && HasFollow())
          JoinFrame() → 合并 follow
```

#### 6.3.2 SwLayoutFrame::MakeAll() 流程

```
SwLayoutFrame::MakeAll(RenderContext*)
  │
  ├─ 1. 计算位置 — MakePos()
  │
  ├─ 2. 计算打印区域 — Format()
  │     计算边框、内边距
  │     如果有列，调用 FormatWidthCols()
  │
  ├─ 3. 格式化所有子框架
  │     for (SwFrame* pLower = m_pLower; pLower; pLower = pLower->GetNext())
  │       pLower->MakeAll(RenderContext);
  │
  ├─ 4. 根据子框架总大小调整自身大小
  │     GrowFrame() / ShrinkFrame()
  │
  └─ 5. 检查是否需要分页（对 SwSectionFrame 等）
```

#### 6.3.3 SwPageFrame::MakeAll() 流程

```
SwPageFrame::MakeAll(RenderContext*)
  │
  ├─ 1. 设置页面大小（根据 SwPageDesc）
  │
  ├─ 2. 格式化页眉 — HeaderFrame::MakeAll()
  │
  ├─ 3. 格式化正文区域 — BodyFrame::MakeAll()
  │     BodyFrame 内部遍历所有正文内容
  │
  ├─ 4. 格式化脚注区域 — FootnoteContFrame
  │
  ├─ 5. 格式化页脚 — FooterFrame::MakeAll()
  │
  └─ 6. 处理页面上的浮动对象
        遍历 m_pSortedObjs，调用每个 SwFlyFrame::MakeAll()
```

### 6.4 分页决策详解

#### 6.4.1 MoveFwd() — 向前移动（到下一页）

```
SwFlowFrame::MoveFwd(bMakePage, bPageBreak, bMoveAlways)
  │
  ├─ 1. 检查是否允许前移
  │     if (!IsFwdMoveAllowed()) return false;
  │
  ├─ 2. 检查 Keep 属性
  │     Keep=true 表示"与上一段保持在同一页"
  │     if (IsKeep(rKeep, rBreak) && !bMoveAlways)
  │       return false;  // 不能前移
  │
  ├─ 3. 查找下一个合适的容器 — GetNextLeaf()
  │     ├─ 首先尝试当前页面的下一个栏/列
  │     ├─ 如果没有更多列，尝试获取下一个页面
  │     │     GetNextLeaf() → GetNextPage()
  │     └─ 如果不存在下一页，创建新页面
  │           InsertPage(pPage, bFootnote)
  │
  ├─ 4. 执行移动 — MoveSubTree()
  │     将自身从当前父框架中 Cut()
  │     粘贴到新父框架中 Paste()
  │     更新 Follow 链
  │
  ├─ 5. 处理 follow
  │     如果移动后仍有溢出内容：
  │       SplitFrame() → 创建新的 follow Frame
  │       SetFollow(pFollow); pFollow->SetPrecede(this);
  │
  └─ 6. 通知相关对象
        更新浮动对象位置
        通知脚注重新分配
```

#### 6.4.2 MoveBwd() — 向后移动（到上一页）

```
SwFlowFrame::MoveBwd(rbReformat)
  │
  ├─ 1. 查找上一个容器 — GetPrevLeaf()
  │     返回前一个页面/栏的最后一个容器
  │
  ├─ 2. 检查是否应该后移 — ShouldBwdMoved()
  │     虚方法，由子类实现具体逻辑
  │     考虑：浮动对象重叠、空间充足性等
  │
  ├─ 3. 执行移动 — MoveSubTree()
  │
  ├─ 4. 尝试合并 follow — JoinFrame()
  │     如果后移后空间足够容纳 follow 的内容
  │     则将 follow 合并回来
  │
  └─ 5. 设置 rbReformat 标志
        通知后续 Frame 需要重新格式化
```

#### 6.4.3 WouldFit() — 空间适配检查

```
SwFlowFrame::WouldFit(nSpace, pNewUpper, bTstMove, bObjsInNewUpper)
  │
  ├─ 1. 计算所需空间
  │     nNeeded = 内容高度 + 下边距 + 段后间距
  │
  ├─ 2. 考虑浮动对象占用空间
  │     检查 pNewUpper 中浮动对象的环绕区域
  │
  ├─ 3. 考虑 Widow/Orphan 规则
  │     Widow: 段落末尾至少 N 行在下一页
  │     Orphan: 段落开头至少 N 行在上一页
  │
  └─ 4. 返回 nNeeded <= nSpace
```

#### 6.4.4 分页符与分栏符

```cpp
// 分页符属性 (SvxFormatBreakItem)
enum class SvxBreak {
    NONE,        // 无分页符
    COLUMN,      // 分栏符
    COLUMN_BEFORE, // 分栏 + 从前一栏开始
    PAGE,        // 分页符
    PAGE_BEFORE, // 分页 + 从前一页开始
    PAGE_LEFT,   // 分页到左页
    PAGE_RIGHT   // 分页到右页
};

// 分页符检查
bool SwFlowFrame::IsPageBreak(bool bAct) const {
    const SwFormatBreak& rBreak = GetFormat()->GetBreak();
    return rBreak.GetBreak() != SvxBreak::NONE;
}

// Keep 属性检查 — "与下段同页"
bool SwFlowFrame::IsKeep(SvxFormatKeepItem const& rKeep,
                         SvxFormatBreakItem const& rBreak,
                         bool bBreakCheck) const {
    // Keep=true 且没有显式分页符时生效
    return rKeep.GetValue() && !bBreakCheck;
}
```

### 6.5 Follow 链创建与合并

#### 6.5.1 SplitFrame() — 拆分框架

```
SwTextFrame::SplitFrame(nSplitPos)
  │
  ├─ 1. 在 nSplitPos 位置拆分文本
  │     创建新的 SwTextFrame (follow)
  │     follow 的 m_nOfst = nSplitPos
  │
  ├─ 2. 建立 follow 链
  │     this->SetFollow(pNew);
  │     pNew->SetPrecede(this);
  │
  ├─ 3. 将 follow 插入到布局树
  │     找到当前页面的下一个合适位置
  │     如果需要新页面，创建 SwPageFrame
  │
  └─ 4. 触发 follow 的格式化
        pNew->MakeAll(RenderContext);
```

#### 6.5.2 JoinFrame() — 合并框架

```
SwTextFrame::JoinFrame()
  │
  ├─ 1. 检查是否可以合并
  │     if (IsJoinLocked()) return;
  │
  ├─ 2. 将 follow 的内容追加到自身
  │     合并文本范围
  │
  ├─ 3. 从布局树中移除 follow
  │     pFollow->Cut();
  │
  ├─ 4. 更新 follow 链
  │     SetFollow(pFollow->GetFollow());
  │
  └─ 5. 删除 follow
        delete pFollow;
```

### 6.6 表格跨页分割

```
SwTabFrame::MakeAll(RenderContext*)
  │
  ├─ 1. 遍历所有行
  │     for (SwRowFrame* pRow = FirstRow(); pRow; pRow = pRow->GetNext())
  │
  ├─ 2. 格式化每一行
  │     pRow->MakeAll(RenderContext);
  │
  ├─ 3. 检查行是否可以分割（行内段落跨页）
  │     if (pRow->IsSplitAllowed() && pRow->IsOverflow()) {
  │       // 将行拆分为两部分
  │       SwRowFrame* pFollowRow = pRow->SplitRow();
  │
  │       // 创建 follow 表格
  │       SwTabFrame* pFollowTab = SplitTabFrame();
  │       pFollowTab->AppendRow(pFollowRow);
  │
  │       // 设置 follow 链
  │       SetFollow(pFollowTab);
  │     }
  │
  ├─ 4. 检查表格是否需要整体前移
  │     if (!WouldFit(nSpace, pNewUpper, false, false))
  │       MoveFwd(true, false);
  │
  └─ 5. 处理 "Keep with next" 行
        如果行设置了 Keep，确保不与上一行分离
```

**表格跨页示意**：

```
Page 1:
  SwTabFrame [master]
    ├─ SwRowFrame (行1)
    ├─ SwRowFrame (行2)
    ├─ SwRowFrame (行3) — 部分溢出
    │   └─ [拆分点]
    └─ m_pFollow → SwTabFrame [follow]

Page 2:
  SwTabFrame [follow]
    ├─ SwRowFrame (行3 剩余部分)
    ├─ SwRowFrame (行4)
    └─ m_pPrecede → SwTabFrame [master]
```

### 6.7 典型布局树（完整示例）

```
SwRootFrame
  │
  ├─ SwPageFrame [Page 1]
  │   │  m_pDesc → SwPageDesc (A4, 上下边距2.54cm, 左右3.18cm)
  │   │  m_nPhyPageNum = 1
  │   │  m_pSortedObjs: [SwFlyAtContentFrame#1, SwFlyAtCharFrame#2]
  │   │
  │   ├─ SwHeaderFrame
  │   │   └─ SwTextFrame (页眉内容)
  │   │
  │   ├─ SwBodyFrame
  │   │   ├─ SwTextFrame [master] (段落1)
  │   │   │   └─ m_pFollow → SwTextFrame [follow] (在 Page 2)
  │   │   │
  │   │   ├─ SwTabFrame [master] (表格)
  │   │   │   ├─ SwRowFrame
  │   │   │   │   ├─ SwCellFrame
  │   │   │   │   │   └─ SwTextFrame
  │   │   │   │   └─ SwCellFrame
  │   │   │   │       └─ SwTextFrame
  │   │   │   └─ SwRowFrame
  │   │   │       └─ ...
  │   │   │   └─ m_pFollow → SwTabFrame [follow] (在 Page 2)
  │   │   │
  │   │   ├─ SwSectionFrame (分节)
  │   │   │   ├─ SwTextFrame
  │   │   │   └─ SwTextFrame
  │   │   │
  │   │   └─ SwFlyAtContentFrame (浮动文本框)
  │   │       ├─ SwTextFrame (框内文本)
  │   │       └─ [锚定在段落1]
  │   │
  │   ├─ SwFootnoteContFrame
  │   │   └─ SwFootnoteFrame
  │   │       └─ SwTextFrame (脚注内容)
  │   │
  │   └─ SwFooterFrame
  │       └─ SwTextFrame (页脚内容)
  │
  ├─ SwPageFrame [Page 2]
  │   ├─ SwHeaderFrame
  │   ├─ SwBodyFrame
  │   │   ├─ SwTextFrame [follow] (段落1 续)
  │   │   │   └─ m_pPrecede → SwTextFrame [master] (在 Page 1)
  │   │   ├─ SwTabFrame [follow] (表格续)
  │   │   └─ SwTextFrame (新段落)
  │   └─ SwFooterFrame
  │
  └─ SwPageFrame [Page 3]
      └─ ...
```

---

## 7. 浮动对象体系

### 7.1 SwAnchoredObject — 锚定对象基类

```cpp
// sw/inc/anchoredobject.hxx
class SwAnchoredObject {
    // 锚定类型
    virtual RndStdIds GetAnchorType() const = 0;

    // 位置管理
    virtual void MakeObjPos() = 0;           // 计算位置
    virtual void InvalidateObjPos() = 0;     // 使位置失效
    virtual bool IsPositionLocked() const;   // 位置是否锁定

    // 页面注册
    virtual void RegisterAtPage(SwPageFrame&) = 0;
    virtual void RegisterAtCorrectPage() = 0;

    // 边界矩形
    virtual SwRect GetObjBoundRect() const = 0;  // 对象边界（用于环绕计算）
    virtual SwRect GetObjRect() const = 0;        // 对象完整矩形

    // 格式化控制
    virtual bool IsFormatPossible() const;        // 是否可以格式化

    // 关联
    SwFrameFormat* GetFrameFormat();              // 获取格式
    SwPageFrame* GetPageFrame();                  // 获取所在页面
};
```

### 7.2 SwFlyFrame — 浮动框架

```cpp
// sw/source/core/inc/flyfrm.hxx
class SwFlyFrame : public SwLayoutFrame, public SwAnchoredObject {
    SwFlyFrame *m_pPrevLink;  // 链接的前一个浮动框架（文本流链接）
    SwFlyFrame *m_pNextLink;  // 链接的下一个浮动框架

    // 锚定类型标志（互斥）
    bool m_bInCnt  : 1;  // FLY_AS_CHAR — 作为字符嵌入文本流
    bool m_bAtCnt  : 1;  // FLY_AT_PARA 或 FLY_AT_CHAR — 段落/字符锚定
    bool m_bLayout : 1;  // FLY_AT_PAGE 或 FLY_AT_FLY — 页面/框架锚定
    bool m_bAutoPosition : 1;  // FLY_AT_CHAR — 自动定位

    // 状态标志
    bool m_bLocked : 1;         // 锁定（格式化期间）
    bool m_bInvalid : 1;        // 位置/大小无效
    bool m_bMinHeight : 1;      // 最小高度（可以增长）
    bool m_bHeightClipped : 1;  // 高度被裁剪
    bool m_bWidthClipped : 1;   // 宽度被裁剪
    bool m_bFormatHeightOnly : 1; // 仅重新格式化高度

    Point m_aContentPos;        // 内容区域相对位置

    // 关键方法
    virtual void MakeObjPos() override;     // 计算浮动对象位置
    virtual void MakeAll(RenderContext*) override;  // 完整格式化

    // 框架链接（文本框文本流）
    static void ChainFrames(SwFlyFrame &rMaster, SwFlyFrame &rFollow);
    static void UnchainFrames(SwFlyFrame &rMaster, SwFlyFrame &rFollow);
    SwFlyFrame* FindChainNeighbour(SwFrameFormat const&, SwFrame*);

    // 类型判断
    bool IsFlyInContentFrame() const;   // 作为字符嵌入
    bool IsFlyFreeFrame() const;        // 自由浮动 (AtPara/AtChar/Lay)
    bool IsFlyLayFrame() const;         // 页面/框架锚定
    bool IsFlyAtContentFrame() const;   // 段落锚定
};
```

### 7.3 锚定类型详解

```cpp
enum class RndStdIds {
    FLY_AS_CHAR,    // 作为字符嵌入 — 在文本流中占据一个字符位置
                    // 对应 SwFlyInContentFrame
                    // 随文本流动，不能自由定位

    FLY_AT_PARA,    // 锚定到段落 — 位置相对于段落左上角
                    // 对应 SwFlyAtContentFrame
                    // 可设置水平/垂直偏移和环绕方式

    FLY_AT_CHAR,    // 锚定到字符 — 位置相对于特定字符
                    // 对应 SwFlyAtCharFrame
                    // 可设置相对于字符的偏移

    FLY_AT_PAGE,    // 锚定到页面 — 位置相对于页面左上角
                    // 对应 SwFlyLayFrame
                    // 绝对定位

    FLY_AT_FLY      // 锚定到另一个浮动对象 — 位置相对于父浮动对象
                    // 对应 SwFlyLayFrame
                    // 嵌套浮动
};
```

### 7.4 浮动对象位置计算

```
SwFlyFrame::MakeObjPos()
  │
  ├─ 1. 确定锚点位置
  │     ├─ FLY_AS_CHAR: 在文本行中的字符位置
  │     ├─ FLY_AT_PARA: 锚定段落的左上角
  │     ├─ FLY_AT_CHAR: 锚定字符的位置
  │     ├─ FLY_AT_PAGE: 页面左上角
  │     └─ FLY_AT_FLY: 父浮动对象的位置
  │
  ├─ 2. 计算水平位置
  │     对齐方式: LEFT / CENTER / RIGHT / FROM_LEFT / FROM_RIGHT
  │     关系: PAGE_PRINT_AREA / PAGE_FRAME / FRAME / CHAR / TEXT_LINE
  │     偏移: nHoriOffset (相对于关系对象)
  │
  ├─ 3. 计算垂直位置
  │     对齐方式: TOP / CENTER / BOTTOM / FROM_TOP / FROM_BOTTOM
  │     关系: PAGE_PRINT_AREA / PAGE_FRAME / FRAME / CHAR / TEXT_LINE / LINE
  │     偏移: nVertOffset (相对于关系对象)
  │
  └─ 4. 边界检查
        确保不超出页面边界（考虑边距）
        如果超出，裁剪或调整位置
```

### 7.5 浮动对象与布局的交互

```
SwSortedObjs — 页面/框架上的浮动对象有序列表

每个 SwPageFrame 和 SwFrame 可以持有 SwSortedObjs:
  ├─ SwPageFrame::m_pSortedObjs  — 页面上的所有浮动对象
  └─ SwFrame::m_pDrawObjs        — 锚定在此框架上的绘图对象

浮动对象影响文本排版的方式:
  1. 文本环绕 (Wrap):
     ├─ WrapNone      — 无环绕，文本在对象下方
     ├─ WrapLeft      — 文本在对象左侧
     ├─ WrapRight     — 文本在对象右侧
     ├─ WrapParallel  — 文本在对象两侧
     └─ WrapThrough   — 文本穿过对象

  2. 文本流避让:
     SwTextFrame 在断行时检查当前行是否有浮动对象
     如果有，减少可用宽度以避让

  3. 注册机制:
     SwFlyFrame::RegisterAtPage(SwPageFrame&)
       → 将自身添加到页面的 SwSortedObjs 中
       → 页面格式化时考虑这些对象
```

### 7.6 SwObjectFormatter — 位置格式化器

```
SwObjectFormatter (sw/source/core/inc/objectformatter.hxx)
  │
  ├─ FormatObj(SwAnchoredObject& rObj)
  │     完整格式化一个浮动对象
  │
  ├─ CalcPosition(SwAnchoredObject& rObj)
  │     计算浮动对象的位置
  │     考虑锚点、偏移、对齐、边界
  │
  ├─ CheckWrap(SwAnchoredObject& rObj)
  │     检查环绕方式
  │     计算文本需要避让的区域
  │
  └─ 子类:
      SwObjectFormatterTextFrame  — 为文本框架格式化浮动对象
      SwObjectFormatterPageFrame  — 为页面格式化浮动对象
```

---

## 8. Node-Frame 连接机制

### 8.1 观察者模式

通过 `SwClient`/`SwModify` 回调系统 (`sw/inc/calbck.hxx`)：

```
SwModify (被观察者)
  │  维护 WriterMultiListener 双向链表
  │  当属性/内容变化时通知所有注册的 Client
  │
  └── SwContentNode : BroadcastingModify
        │  当文本/属性变化时，通知关联的 Frame
        │
        └── SwClient (观察者)
              │  SwContentFrame 注册为 SwContentNode 的 Client
              │  接收 SwClientNotify() 回调
              │
              └── 收到通知后:
                    1. 判断变化类型（属性/内容/格式）
                    2. 设置相应的 Invalidate 标志
                    3. 触发 SwLayAction 重新格式化
```

### 8.2 Node→Frame 映射

- **创建**: `SwContentNode::MakeFrame()` 虚方法
  - `SwTextNode::MakeFrame()` → 创建 `SwTextFrame`
  - `SwGrfNode::MakeFrame()` → 创建 `SwGrfFrame`
  - `SwOLENode::MakeFrame()` → 创建 `SwOLEFrame`
- **查找**: `SwContentNode::getLayoutFrame(SwRootFrame*)` — 获取指定布局下的 Frame
- **遍历**: `SwIterator<SwFrame, SwContentNode>` — 遍历注册在某节点上的所有 Frame
- **删除**: `SwContentNode::DelFrames(SwRootFrame*)` — 删除指定布局下的所有 Frame

### 8.3 Frame→Node 映射

- `SwContentFrame` 是其 `SwContentNode` 的 `SwClient`
- 通过 `GetDep()` 访问关联的 `BroadcastingModify`
- `SwTextFrame` 可通过 `GetTextNode()` 直接获取关联的 `SwTextNode`

---

## 9. 渲染流程

### 9.1 Paint 调用链

```
SwViewShell::Paint(const SwRect& rRect)
  │
  ├─ 1. 准备绘制
  │     设置裁剪区域
  │     创建虚拟输出设备（可选）
  │
  ├─ 2. SwRootFrame::Paint(RenderContext, rRect)
  │     遍历所有可见的 SwPageFrame
  │
  ├─ 3. SwPageFrame::Paint(RenderContext, rRect)
  │     ├─ PaintSwFrameShadowAndBorder()  — 绘制页面阴影和边框
  │     ├─ PaintHeader()                   — 绘制页眉区域
  │     ├─ PaintBody()                     — 绘制正文区域
  │     │     遍历所有子 Frame:
  │     │       SwFrame::PaintSwFrame()
  │     │         ├─ PaintBaBo()           — 绘制边框和背景
  │     │         └─ PaintSwFrame()        — 子类特定绘制
  │     ├─ PaintFootnotes()                — 绘制脚注区域
  │     └─ PaintFooter()                   — 绘制页脚区域
  │
  └─ 4. 绘制浮动对象
        遍历 SwSortedObjs，绘制每个 SwFlyFrame
```

### 9.2 文本渲染路径

```
SwTextNode (m_Text + SwpHints)
  │
  → SwTextFrame::MakeAll()
  │   SwTextFormatter 将文本断行为 SwLinePortion 链表
  │   每个 SwLinePortion 包含:
  │     - 文本范围 (start, length)
  │     - 字体信息 (SwFont)
  │     - 宽度、高度
  │     - 类型 (TextPortion, SpacePortion, TabPortion, etc.)
  │
  → SwLayAction::Action()
  │   编排格式化和绘制调度
  │   收集需要重绘的区域
  │
  → SwTextFrame::PaintSwFrame(RenderContext, rRect)
      遍历 SwLinePortion 链表:
        for (SwLinePortion* p = pPara->GetFirstPortion(); p; p = p->GetNext())
          switch (p->GetWhichPorType())
            case POR_TEXT:    DrawText(p)     — 绘制文本
            case POR_TAB:     DrawTab(p)      — 绘制制表符
            case POR_SPACE:   DrawSpace(p)    — 绘制空格
            case POR_FLY:     DrawFly(p)      — 绘制嵌入对象
            case POR_FTN:     DrawFootnote(p) — 绘制脚注标记
```

### 9.3 排版时序总览

```
SwRootFrame::MakeAll()
    │
    ├─ 遍历 SwPageFrame 链表
    │
    ├─ SwPageFrame::MakeAll()
    │   ├─ 设置页面大小
    │   ├─ SwHeaderFrame::MakeAll()
    │   ├─ SwBodyFrame::MakeAll()
    │   │   ├─ 遍历子框架
    │   │   ├─ SwContentFrame::MakeAll()
    │   │   │   ├─ MakePos()
    │   │   │   ├─ MakePrtArea()
    │   │   │   ├─ Format() — 计算内容大小
    │   │   │   ├─ WouldFit() — 检查空间
    │   │   │   │   ├─ 是 → 继续
    │   │   │   │   └─ 否 → MoveFwd()
    │   │   │   │       ├─ GetNextLeaf()
    │   │   │   │       ├─ InsertPage() — 创建新页面
    │   │   │   │       ├─ MoveSubTree() — 移动框架
    │   │   │   │       └─ SplitFrame() — 创建 follow
    │   │   │   └─ 处理 follow 链
    │   │   │       ├─ 内容溢出 → SplitFrame()
    │   │   │       └─ 内容不足 → JoinFrame()
    │   │   │
    │   │   ├─ SwTabFrame::MakeAll()
    │   │   │   ├─ 遍历行
    │   │   │   ├─ 检查行分割
    │   │   │   └─ 创建 follow 表格
    │   │   │
    │   │   └─ SwSectionFrame::MakeAll()
    │   │       ├─ 格式化节内容
    │   │       └─ 处理分栏
    │   │
    │   ├─ SwFootnoteContFrame::MakeAll()
    │   └─ SwFooterFrame::MakeAll()
    │
    └─ 处理浮动对象
        ├─ 遍历所有页面的 SwSortedObjs
        └─ SwFlyFrame::MakeAll()
            ├─ MakeObjPos() — 计算位置
            └─ Format() — 格式化内容
```

---

## 10. 回调/观察者系统

`SwModify`/`SwClient` (`sw/inc/calbck.hxx`) 是核心通知机制：
- `SwModify`（通过 `BroadcastingModify`）维护 `WriterListener` 双向链表
- `SwClient` 注册在一个 `SwModify` 上，接收 `SwClientNotify()` 回调
- `SwDepend` 允许单个 `SwClient` 监听多个 `SwModify`
- `SwIterator<T, Source, Mode>` 遍历注册在某个 `SwModify` 上的所有指定类型 Client

属性变更通过此系统传播：`SwFormat` 属性变化 → 所有注册的 `SwFrame` 收到通知 → 触发重新格式化。

---

## 11. oox 共享模块

`oox/` 是 Writer/Calc/Impress 共享的库：

| 子目录 | 功能 |
|--------|------|
| `source/core/` | XML 过滤基类（`XmlFilterBase`, `FastParser`, `FragmentHandler`, `Relations`, `FilterDetect`） |
| `source/token/` | OOXML Token/命名空间定义（`tokens.txt` → 完美哈希 → `tokenhash.inc`） |
| `source/drawingml/` | DrawingML 导入/导出（所有应用共享） |
| `source/vml/` | VML（矢量标记语言）处理 |
| `source/export/` | 导出辅助（`DrawingML`, `VMLExport`, `ChartExport`） |

`oox::core::XmlFilterBase` 是所有 OOXML XML 过滤器的基类，`DocxExportFilter` 继承它用于导出。

---

## 12. 关键文件索引

### 导入
| 文件 | 说明 |
|------|------|
| `sw/source/writerfilter/filter/WriterFilter.cxx` | 导入入口 |
| `sw/source/writerfilter/dmapper/DomainMapper.hxx/.cxx` | DomainMapper（Stream 消费者） |
| `sw/source/writerfilter/dmapper/DomainMapper_Impl.hxx/.cxx` | DomainMapper 实现 |
| `sw/source/writerfilter/dmapper/StyleSheetTable.cxx` | 样式映射 |
| `sw/source/writerfilter/dmapper/NumberingManager.cxx` | 编号导入 |
| `sw/source/writerfilter/dmapper/DomainMapperTableHandler.cxx` | 表格导入 |
| `sw/source/writerfilter/ooxml/OOXMLFastContextHandler.hxx/.cxx` | SAX 上下文 handler |
| `sw/source/writerfilter/ooxml/OOXMLFactory.hxx` | 元素分发工厂 |
| `sw/source/writerfilter/ooxml/OOXMLDocumentImpl.cxx` | 文档编排器 |
| `sw/source/writerfilter/ooxml/OOXMLPropertySet.hxx/.cxx` | 属性累积 |
| `sw/source/writerfilter/ooxml/model.xml` | 代码生成驱动（OOXML Schema + 动作指令） |

### 导出
| 文件 | 说明 |
|------|------|
| `sw/source/filter/ww8/docxexportfilter.hxx/.cxx` | 导出入口 |
| `sw/source/filter/ww8/docxexport.hxx/.cxx` | 主导出类 |
| `sw/source/filter/ww8/docxattributeoutput.hxx/.cxx` | 属性→XML 输出 |
| `sw/source/filter/ww8/attributeoutputbase.hxx` | 抽象属性接口 |
| `sw/source/filter/ww8/wrtww8.hxx/.cxx` | `MSWordExportBase`，共享导出基础设施 |

### 文档模型
| 文件 | 说明 |
|------|------|
| `sw/inc/doc.hxx` | `SwDoc` |
| `sw/inc/node.hxx` | `SwNode` 基类 |
| `sw/inc/ndtyp.hxx` | `SwNodeType` 枚举 |
| `sw/inc/ndarr.hxx` | `SwNodes` 数组 |
| `sw/inc/pam.hxx` | `SwPosition`, `SwPaM` |
| `sw/inc/ndtxt.hxx` | `SwTextNode` |
| `sw/inc/swtable.hxx` | `SwTable` |
| `sw/inc/format.hxx` | `SwFormat` |
| `sw/inc/calbck.hxx` | `SwModify`/`SwClient` 回调系统 |
| `sw/inc/anchoredobject.hxx` | `SwAnchoredObject` 锚定对象基类 |

### 布局引擎
| 文件 | 说明 |
|------|------|
| `sw/source/core/layout/` | Frame 树、布局计算（所有 layout 实现） |
| `sw/source/core/layout/newfrm.cxx` | SwFrame::MakeAll, MakePos, MakePrtArea 基础实现 |
| `sw/source/core/layout/cntfrm.cxx` | SwContentFrame::MakeAll — 内容框架格式化 |
| `sw/source/core/layout/flowfrm.cxx` | SwFlowFrame — MoveFwd/MoveBwd/WouldFit/分页决策 |
| `sw/source/core/layout/tabfrm.cxx` | SwTabFrame — 表格格式化与跨页分割 |
| `sw/source/core/layout/sectfrm.cxx` | SwSectionFrame — 节格式化与分栏 |
| `sw/source/core/layout/pagechg.cxx` | 页面创建/销毁、SwPageDesc 处理 |
| `sw/source/core/layout/fly.cxx` | SwFlyFrame 基础实现 |
| `sw/source/core/layout/flycnt.cxx` | SwFlyAtContentFrame — 段落锚定浮动对象 |
| `sw/source/core/layout/flylay.cxx` | SwFlyLayFrame — 页面锚定浮动对象 |
| `sw/source/core/layout/anchoredobject.cxx` | SwAnchoredObject 实现 |
| `sw/source/core/layout/objectformatter.cxx` | SwObjectFormatter — 浮动对象位置计算 |
| `sw/source/core/layout/layact.cxx` | SwLayAction — 布局动作调度 |
| `sw/source/core/layout/trvlfrm.cxx` | Frame 树遍历与光标定位 |
| `sw/source/core/layout/ssfrm.cxx` | SwFrame 大小调整 |
| `sw/source/core/layout/wsfrm.cxx` | SwFrame 工作集方法 |
| `sw/source/core/layout/hffrm.cxx` | SwHeaderFrame/SwFooterFrame |
| `sw/source/core/layout/ftnfrm.cxx` | SwFootnoteFrame — 脚注格式化 |
| `sw/source/core/inc/frame.hxx` | `SwFrame` 基类声明 |
| `sw/source/core/inc/txtfrm.hxx` | `SwTextFrame` 声明 |
| `sw/source/core/inc/layfrm.hxx` | `SwLayoutFrame` 声明 |
| `sw/source/core/inc/rootfrm.hxx` | `SwRootFrame` 声明 |
| `sw/source/core/inc/pagefrm.hxx` | `SwPageFrame` 声明 |
| `sw/source/core/inc/flowfrm.hxx` | `SwFlowFrame` 声明 |
| `sw/source/core/inc/cntfrm.hxx` | `SwContentFrame` 声明 |
| `sw/source/core/inc/flyfrm.hxx` | `SwFlyFrame` 声明 |
| `sw/source/core/inc/anchoredobject.hxx` | `SwAnchoredObject` 声明 |
| `sw/source/core/inc/objectformatter.hxx` | `SwObjectFormatter` 声明 |
| `sw/source/core/inc/sortedobjs.hxx` | `SwSortedObjs` 声明 |

### 文本格式化
| 文件 | 说明 |
|------|------|
| `sw/source/core/text/` | 文本排版核心 |
| `sw/source/core/text/txtfrm.cxx` | SwTextFrame 文本格式化 |
| `sw/source/core/text/txtftn.cxx` | 脚注在文本中的处理 |
| `sw/source/core/text/inftxt.hxx` | SwTextFormatInfo — 文本格式化信息 |
| `sw/source/core/text/itrform2.cxx` | SwTextFormatter — 行断行与 Portion 创建 |
| `sw/source/core/text/porlin.hxx` | SwLinePortion — 行元素基类 |
| `sw/source/core/text/portxt.hxx` | SwTextPortion — 文本 Portion |
| `sw/source/core/text/frmform.cxx` | SwTextFrame::Format() 实现 |

### 渲染
| 文件 | 说明 |
|------|------|
| `sw/source/core/layout/paintfrm.cxx` | Frame 绘制实现 |
| `sw/source/core/text/txtpaint.cxx` | 文本绘制辅助 |

---

## 13. 核心数据结构关系总图

```
SwDoc (文档模型)
  │
  ├─ SwNodes (节点数组, BigPtrArray)
  │   ├─ [PostIts区]
  │   ├─ [Inserts区]
  │   ├─ [AutoText区]
  │   ├─ [Redlines区]
  │   └─ [Content区]
  │       ├─ SwTextNode ──────── 关联 → SwTextFrame
  │       ├─ SwTableNode ─────── 关联 → SwTabFrame
  │       │   └─ SwTable (逻辑结构: SwTableLine → SwTableBox)
  │       ├─ SwSectionNode ───── 关联 → SwSectionFrame
  │       │   └─ SwSection (节数据)
  │       ├─ SwGrfNode ───────── 关联 → SwGrfFrame
  │       └─ SwOLENode ───────── 关联 → SwOLEFrame
  │
  ├─ SwFormats (格式集合)
  │   ├─ SwTextFormatColl (段落样式)
  │   ├─ SwCharFormat (字符样式)
  │   ├─ SwFrameFormat (框架格式)
  │   │   ├─ 段落框架格式
  │   │   ├─ 表格框架格式
  │   │   └─ 浮动框架格式 (SwFlyFrameFormat)
  │   ├─ SwSectionFormat (节样式)
  │   └─ SwTableFrameFormat (表格样式)
  │
  ├─ SwPageDescs (页面描述集合)
  │   └─ SwPageDesc (页面大小、边距、页眉页脚格式)
  │
  └─ SwRootFrame (布局根)
      │
      ├─ SwPageFrame [Page 1]
      │   ├─ m_pDesc → SwPageDesc
      │   ├─ m_pSortedObjs → [SwFlyFrame*, ...]  (页面浮动对象)
      │   ├─ SwHeaderFrame
      │   │   └─ SwTextFrame
      │   ├─ SwBodyFrame
      │   │   ├─ SwTextFrame [master]
      │   │   │   ├─ 关联 → SwTextNode
      │   │   │   └─ m_pFollow → SwTextFrame [follow on Page 2]
      │   │   ├─ SwTabFrame [master]
      │   │   │   ├─ SwRowFrame → SwCellFrame → SwTextFrame
      │   │   │   └─ m_pFollow → SwTabFrame [follow on Page 2]
      │   │   ├─ SwSectionFrame
      │   │   │   └─ SwTextFrame(s)
      │   │   └─ SwFlyAtContentFrame (浮动文本框)
      │   │       ├─ 锚定在 SwTextNode
      │   │       ├─ 注册在 SwPageFrame::m_pSortedObjs
      │   │       └─ SwTextFrame (框内文本)
      │   ├─ SwFootnoteContFrame
      │   │   └─ SwFootnoteFrame → SwTextFrame
      │   └─ SwFooterFrame
      │       └─ SwTextFrame
      │
      └─ SwPageFrame [Page 2]
          └─ ...
```

---

## 14. 关键流程时序图

### 14.1 DOCX 导入时序

```
用户 → WriterFilter::filter()
         │
         ├─ FilterDetect → 解密 ZIP
         ├─ DomainMapperFactory::createMapper() → 创建 DomainMapper
         ├─ OOXMLDocumentFactory::createDocument() → 创建文档编排器
         │
         └─ pDocument->resolve(*pStream) → 启动解析
              │
              ├─ 解析 SETTINGS → SettingsTable
              ├─ 解析 THEME → ThemeHandler
              ├─ 解析 STYLES → StyleSheetTable
              ├─ 解析 NUMBERING → NumberingManager
              ├─ 解析 FONTTABLE → FontTable
              │
              └─ 解析 document.xml → DomainMapper
                   │
                   ├─ 段落开始 → DomainMapper_Impl::startParagraphGroup()
                   ├─ 文本内容 → DomainMapper_Impl::appendTextContent()
                   │   → SwDoc::GetNodes().MakeTextNode() → SwTextNode
                   ├─ 表格开始 → DomainMapperTableHandler::startTable()
                   │   → SwDoc::InsertTable() → SwTableNode
                   ├─ 浮动对象 → GraphicImport / OLEHandler
                   │   → SwDoc::Insert() → SwFlyFrameFormat
                   └─ 段落结束 → DomainMapper_Impl::endParagraphGroup()
                        │
                        └─ 导入完成 → SwDoc::InitLayout()
                             → SwRootFrame::Init()
                             → 触发布局格式化
```

### 14.2 排版时序

```
SwLayAction::Action()  (布局动作调度器)
  │
  ├─ 收集需要格式化的 Frame
  │   (通过 Invalidate 标志识别)
  │
  ├─ SwRootFrame::MakeAll()
  │   │
  │   ├─ 遍历 SwPageFrame
  │   │
  │   └─ SwPageFrame::MakeAll()
  │       ├─ SwHeaderFrame::MakeAll()
  │       ├─ SwBodyFrame::MakeAll()
  │       │   └─ 遍历子框架
  │       │       └─ SwContentFrame::MakeAll()
  │       │           ├─ MakePos() — 计算位置
  │       │           ├─ MakePrtArea() — 计算打印区域
  │       │           ├─ Format() — 计算内容大小
  │       │           │   └─ SwTextFrame::Format()
  │       │           │       └─ 断行 → SwLinePortion 链表
  │       │           ├─ ShouldBwdMoved() — 检查是否应后移
  │       │           │   └─ 是 → MoveBwd()
  │       │           ├─ WouldFit() — 检查空间是否足够
  │       │           │   └─ 否 → MoveFwd()
  │       │           │       ├─ GetNextLeaf() — 找下一个容器
  │       │           │       ├─ InsertPage() — 创建新页面
  │       │           │       ├─ MoveSubTree() — 移动框架
  │       │           │       └─ SplitFrame() — 创建 follow
  │       │           └─ 处理 follow 链
  │       │               ├─ 溢出 → SplitFrame()
  │       │               └─ 不足 → JoinFrame()
  │       │
  │       ├─ SwFootnoteContFrame::MakeAll()
  │       └─ SwFooterFrame::MakeAll()
  │
  └─ 处理浮动对象
      └─ SwObjectFormatter::FormatObj()
          ├─ CalcPosition() — 计算位置
          └─ CheckWrap() — 检查环绕
```

---

## 15. 总结

LibreOffice Writer 的 sw 模块是一个复杂而精密排版引擎，其核心特点：

1. **模型-布局分离**：SwNodes 存储逻辑内容，Frame 树存储物理布局，两者通过 `SwClient`/`SwModify` 观察者模式同步
2. **惰性计算**：Frame 在需要时才创建和格式化，通过 `Invalidate`/`Validate` 标志位实现增量更新
3. **流式排版**：通过 `SwFlowFrame` 的 follow 链和 `MoveFwd`/`MoveBwd` 实现跨页流动，支持段落、表格、节的跨页分割
4. **灵活锚定**：支持 5 种锚定类型（AS_CHAR, AT_PARA, AT_CHAR, AT_PAGE, AT_FLY），满足各种浮动对象需求
5. **分页决策**：综合考虑 WouldFit、Keep、Break、Widow/Orphan 等规则，实现智能分页
6. **观察者模式**：属性变更通过 `SwClient`/`SwModify` 系统传播，自动触发受影响的 Frame 重新格式化

整个系统从 DOCX 解析到最终渲染，经历了 **解析 → 建模 → 布局 → 渲染** 四个主要阶段，每个阶段都有清晰的职责划分和高效的数据结构支持。
