# LibreOffice DOCX 组件架构与核心逻辑

## 1. 总体架构

LibreOffice 的 DOCX 处理分布在三个主要代码区域：

| 模块 | 路径 | 职责 |
|------|------|------|
| **oox** | `oox/source/` | 通用 OOXML 工具包（Writer/Calc/Impress 共享）：ZIP 存储、SAX 快速解析、Token 管理、命名空间 |
| **writerfilter** | `sw/source/writerfilter/` | Writer 专用 OOXML **导入**管线：OOXML Token → DomainMapper → SwDoc |
| **ww8 (export)** | `sw/source/filter/ww8/` | DOCX **导出**路径：`DocxExport` / `DocxExportFilter` |

导入与导出是**完全独立的代码路径**，仅共享目标文档模型 `SwDoc`。

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

### 4.3 SwNode 类型层次

```
SwNode (抽象基类, 继承 BigPtrEntry)
  ├── SwStartNode           ── 开始一个"段落区间"
  │     ├── SwTableNode     ── 表格（继承 SwStartNode）
  │     └── SwSectionNode   ── 节（继承 SwStartNode）
  ├── SwEndNode             ── 结束一个"段落区间"
  └── SwContentNode         ── 抽象内容节点（继承 BroadcastingModify + SwContentIndexReg）
        ├── SwTextNode      ── 文本段落（最核心的节点类型）
        └── SwNoTextNode    ── 非文本内容（抽象）
              ├── SwGrfNode ── 图片
              └── SwOLENode ── OLE 对象
```

**SwStartNodeType** 进一步区分：`SwNormalStartNode`, `SwTableBoxStartNode`, `SwFlyStartNode`, `SwFootnoteStartNode`, `SwHeaderStartNode`, `SwFooterStartNode`

### 4.4 SwTextNode — 段落

`SwTextNode` (`sw/inc/ndtxt.hxx`) 是最重要的节点类型：
- `m_Text` (`OUString`) — 实际文本内容
- `m_pSwpHints` (`SwpHints`) — 内联文本属性（粗体、斜体、字段、脚注等）
- `mpNodeNum` — 编号状态
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
  ├── SwFrameFormat         ── 布局元素样式（段落、表格、浮动框）
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

---

## 6. 布局引擎 (Frame Tree)

### 6.1 Frame 类层次

```
SwFrame (基类, 继承 SwFrameAreaDefinition + SwClient + SfxBroadcaster)
  ├── SwLayoutFrame (容器 Frame, 有 m_pLower 子链)
  │     ├── SwRootFrame       ── 布局树根，Lower() 链包含 SwPageFrame
  │     ├── SwPageFrame       ── 页面（继承 SwFootnoteBossFrame）
  │     ├── SwBodyFrame       ── 正文区域
  │     ├── SwHeaderFrame     ── 页眉
  │     ├── SwFooterFrame     ── 页脚
  │     ├── SwFootnoteContFrame ── 脚注容器
  │     ├── SwFootnoteFrame   ── 单个脚注
  │     ├── SwTabFrame        ── 表格（+ SwFlowFrame）
  │     ├── SwRowFrame        ── 表格行
  │     ├── SwCellFrame       ── 表格单元格
  │     ├── SwSectionFrame    ── 节（+ SwFlowFrame）
  │     └── SwFlyFrame        ── 浮动框架（文本框、图片）
  └── SwContentFrame (内容 Frame, + SwFlowFrame)
        ├── SwTextFrame       ── 文本段落布局（最复杂）
        └── SwNoTextFrame     ── 图片/OLE 布局
```

### 6.2 典型布局树

```
SwRootFrame
  SwPageFrame
    SwHeaderFrame
      SwTextFrame
    SwBodyFrame
      SwTextFrame (段落)
      SwTabFrame (表格)
        SwRowFrame
          SwCellFrame
            SwTextFrame
          SwCellFrame
            SwTextFrame
      SwSectionFrame
        SwTextFrame
    SwFootnoteContFrame
      SwFootnoteFrame
        SwTextFrame
    SwFooterFrame
      SwTextFrame
  SwPageFrame
    ...
```

### 6.3 Node-Frame 连接

通过 `SwClient`/`SwModify` 回调系统：
- **Node→Frame**: `SwContentNode` 是 `BroadcastingModify`，其 `SwFrame` 注册为 `SwClient`。用 `SwIterator<SwFrame, SwContentNode>` 或 `getLayoutFrame(SwRootFrame*)` 查找
- **Frame→Node**: `SwContentFrame` 是其 `SwContentNode` 的 `SwClient`，通过 `GetDep()` 访问
- **创建**: `SwContentNode::MakeFrame()` 虚方法，`SwTextNode::MakeFrame()` → `SwTextFrame`
- **删除**: `SwContentNode::DelFrames(SwRootFrame*)`

### 6.4 SwFlowFrame — 跨页流

`SwFlowFrame` 管理内容跨多页/多列的 master/follow 链。一个段落放不下一页时，创建 "follow" Frame 到下一页。

### 6.5 文本渲染路径

```
SwTextNode (m_Text + SwpHints)
  → SwTextFrame::MakeAll() (SwTextFormatter 断行为 SwLinePortion)
    → SwLayAction::Action() (编排格式化和绘制)
      → SwTextFrame::PaintSwFrame() (遍历 SwLinePortion，使用 SwFont 缓存绘制)
```

---

## 7. 回调/观察者系统

`SwModify`/`SwClient` (`sw/inc/calbck.hxx`) 是核心通知机制：
- `SwModify`（通过 `BroadcastingModify`）维护 `WriterListener` 双向链表
- `SwClient` 注册在一个 `SwModify` 上，接收 `SwClientNotify()` 回调
- `SwDepend` 允许单个 `SwClient` 监听多个 `SwModify`
- `SwIterator<T, Source, Mode>` 遍历注册在某个 `SwModify` 上的所有指定类型 Client

属性变更通过此系统传播：`SwFormat` 属性变化 → 所有注册的 `SwFrame` 收到通知 → 触发重新格式化。

---

## 8. oox 共享模块

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

## 9. 关键文件索引

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

### 布局引擎
| 文件 | 说明 |
|------|------|
| `sw/source/core/layout/` | Frame 树、布局计算 |
| `sw/source/core/inc/frame.hxx` | `SwFrame` |
| `sw/source/core/inc/txtfrm.hxx` | `SwTextFrame` |
| `sw/source/core/inc/layfrm.hxx` | `SwLayoutFrame` |
| `sw/source/core/inc/rootfrm.hxx` | `SwRootFrame` |
| `sw/source/core/inc/flowfrm.hxx` | `SwFlowFrame` |
