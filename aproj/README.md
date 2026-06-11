# LibreOffice DOCX Core Pipeline

> **Goal**: Parse a `.docx` file → build document nodes → construct frame tree → calculate layout/pagination → render to output.
>
> **Sample file**: `WPS Docs Quick Start Guide.docx`

---

## Overview: End-to-End Call Flow

```
.docx file (ZIP)
  │
  ▼
┌─────────────────────────────────────────────────────┐
│  Stage 1: DOCX Import Filter                        │
│  SwDOCXReader → WriterFilter → OOXMLDocument        │
│  SAX parse document.xml → DomainMapper              │
│  → UNO API calls → SwDoc / SwNodes                  │
└──────────────────────┬──────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────┐
│  Stage 2: Document Model (SwNodes)                  │
│  SwDoc.m_pNodes: BigPtrArray<SwNode>                │
│  SwTextNode / SwTableNode / SwSectionNode / ...     │
└──────────────────────┬──────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────┐
│  Stage 3: Frame Tree Construction                   │
│  SwRootFrame::Init() → InsertNewPage() → InsertCnt_ │
│  Node → Frame mapping via SwNode2Layout             │
│  SwPageFrame → SwBodyFrame → SwTextFrame / ...      │
└──────────────────────┬──────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────┐
│  Stage 4: Layout & Pagination                       │
│  SwLayAction::Action() → InternalAction()           │
│  SwContentFrame::MakeAll() → Format() + Flow logic  │
│  MoveFwd() / MoveBwd() → page breaks, widow/orphan  │
└──────────────────────┬──────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────┐
│  Stage 5: Rendering                                 │
│  SwViewShell::Paint() → SwRootFrame::PaintSwFrame() │
│  → SwPageFrame → SwTextFrame::PaintSwFrame()        │
│  → SwTextPaint → OutputDevice                       │
└─────────────────────────────────────────────────────┘
```

---

## Stage 1: DOCX Import — Parse XML into Document Nodes

### Entry Point

```
ImportDOCX()                                          [sw/source/filter/docx/swdocxreader.cxx:44]
  → SwDOCXReader::Read()                              [swdocxreader.cxx:49]
      → UNO service "com.sun.star.comp.Writer.WriterFilter"
      → WriterFilter::filter()                        [sw/source/writerfilter/filter/WriterFilter.cxx:130]
          → OOXMLDocument::resolve()                  [sw/source/writerfilter/ooxml/OOXMLDocumentImpl.cxx:451]
```

### resolve() — Substream Processing Order

```cpp
// OOXMLDocumentImpl.cxx:451
resolve(STYLES)      // 1. styles.xml → create style definitions
resolve(FONTTABLE)   // 2. font table
resolve(NUMBERING)   // 3. numbering.xml → list definitions
resolve(SETTINGS)    // 4. document settings
resolve(THEME)       // 5. theme (colors, effects)
parseStream(document.xml)  // 6. main document content
```

### SAX Parsing Chain

```
document.xml
  → SAX FastParser
    → OOXMLFastDocumentHandler::startFastElement()    [OOXMLFastDocumentHandler.cxx:34]
      → OOXMLFactory::createFastChildContextFromStart()  [OOXMLFactory.cxx]
        → OOXMLFastContextHandler subclass (per element)
          → mpStream->startParagraphGroup() / utext() / ...
            → DomainMapper (implements Stream interface)
```

### Key XML Element → Action Mapping (from model.xml)

| XML Element | Resource Type | Actions |
|---|---|---|
| `<w:p>` (CT_P) | Stream | `startParagraphGroup` → `endOfParagraph` |
| `<w:r>` (CT_R) | Stream | `startCharacterGroup` |
| `<w:pPr>` (CT_PPr) | Properties | paragraph properties |
| `<w:rPr>` (CT_RPr) | Properties | character properties |
| `<w:tbl>` (CT_Tbl) | Table | table creation |
| `<w:sectPr>` (CT_SectPr) | Properties | section properties |

### Paragraph Creation Chain (w:p → SwTextNode)

```
<w:p> startFastElement
  → OOXMLFastContextHandlerStream::startParagraphGroup()
    → DomainMapper::lcl_startParagraphGroup()          [DomainMapper.cxx:4074]
      → PushProperties(CONTEXT_PARAGRAPH)

<w:pPr> → properties stored in ParagraphPropertyMap

<w:r> → startCharacterGroup()
  → DomainMapper::lcl_startCharacterGroup()            [DomainMapper.cxx:4249]
    → PushProperties(CONTEXT_CHARACTER)

<w:t> → text()
  → DomainMapper::lcl_utext()                          [DomainMapper.cxx:4554]
    → DomainMapper_Impl::appendTextPortion()            [DomainMapper_Impl.cxx:3511]
      → xTextAppend->insertTextPortion(rString, aValues)
        → SwXText::insertTextPortion()                 [sw/source/core/unocore/unotext.cxx:1289]
          → SwUnoCursorHelper::DocInsertStringSplitCR()

</w:r> → endCharacterGroup() → PopProperties(CONTEXT_CHARACTER)

</w:p> → endOfParagraph() → sends 0x0d CR
  → DomainMapper::lcl_utext() detects CR
    → DomainMapper::finishParagraph()                  [DomainMapper.cxx:5447]
      → DomainMapper_Impl::finishParagraph()           [DomainMapper_Impl.cxx:2233]
        → xTextAppend->finishParagraph(aProperties)
          → SwXText::finishParagraph()                 [unotext.cxx:1152]
            → DocumentContentOperationsManager::AppendTextNode()  [DocumentContentOperationsManager.cxx:3605]
              → SwNodes::MakeTextNode()                ★ SwTextNode created in node array
```

### Style Application

```
Phase 1: styles.xml
  → StyleSheetTable::ApplyStyleSheets()                [sw/source/writerfilter/dmapper/StyleSheetTable.cxx:1064]
    → SwXStyleFamilies::insertNewByName()              // create style
    → xMultiPropertySet->setPropertyValues()           // apply properties

Phase 2: Direct formatting during import
  → <w:pPr> → CONTEXT_PARAGRAPH property map → applied as direct paragraph formatting
  → <w:rPr> → CONTEXT_CHARACTER property map → applied as direct character formatting
  → <w:pPr><w:pStyle> → PROP_PARA_STYLE_NAME → ParaStyleName property
```

### Key Classes — Stage 1

| Class | File | Role |
|---|---|---|
| `SwDOCXReader` | `sw/source/filter/docx/swdocxreader.cxx` | Thin wrapper, `ImportDOCX()` entry |
| `WriterFilter` | `sw/source/writerfilter/filter/WriterFilter.cxx` | UNO component, creates DomainMapper |
| `OOXMLDocumentImpl` | `sw/source/writerfilter/ooxml/OOXMLDocumentImpl.cxx` | Substream orchestration |
| `OOXMLFastContextHandler` | `sw/source/writerfilter/ooxml/OOXMLFastContextHandler.cxx` | XML→Stream translation |
| `OOXMLFactory` | `sw/source/writerfilter/ooxml/OOXMLFactory.cxx` | Auto-generated from `model.xml` |
| `DomainMapper` | `sw/source/writerfilter/dmapper/DomainMapper.cxx` | Stream impl, receives XML events |
| `DomainMapper_Impl` | `sw/source/writerfilter/dmapper/DomainMapper_Impl.cxx` | Property stacks, UNO API calls |
| `StyleSheetTable` | `sw/source/writerfilter/dmapper/StyleSheetTable.cxx` | Style import/application |

---

## Stage 2: Document Model — SwDoc / SwNodes

### Architecture

```
SwDoc                                                 [sw/inc/doc.hxx:206]
  ├─ m_pNodes: SwNodes                                [sw/inc/ndarr.hxx:105]
  │    └─ BigPtrArray<SwNode>                         // flat array of all nodes
  ├─ mpFrameFormatTable                               // frame formats
  ├─ mpCharFormatTable                                // character formats
  ├─ mpTextFormatCollTable                            // paragraph style collections
  └─ ... (managers for layout, fields, undo, etc.)
```

### SwNodes Structure

```
SwNodes (BigPtrArray)
  ├─ [0]  m_pEndOfPostIts     ← sentinel
  ├─ [1]  m_pEndOfInserts     ← sentinel
  ├─ [2]  m_pEndOfAutotext    ← sentinel
  ├─ [3]  m_pEndOfRedlines    ← sentinel
  ├─ [4]  m_pEndOfExtras      ← sentinel (body starts after this)
  │
  │   ┌─ Body content ─────────────────────────┐
  │   │  SwStartNode (body)                     │
  │   │  SwTextNode "Hello World"               │
  │   │  SwTextNode "Second paragraph"          │
  │   │  SwStartNode (table section)            │
  │   │  SwTableNode                            │
  │   │  SwStartNode (row)                      │
  │   │  SwStartNode (cell)                     │
  │   │  SwTextNode "Cell content"              │
  │   │  SwEndNode (cell)                       │
  │   │  SwEndNode (row)                        │
  │   │  SwEndNode (table section)              │
  │   │  SwTextNode "After table"               │
  │   │  SwEndNode (body)                       │
  │   └─────────────────────────────────────────┘
  │
  └─ [N]  m_pEndOfContent     ← sentinel
```

### Node Types (SwNodeType)

| Type | Value | Class | Description |
|---|---|---|---|
| `End` | 0x01 | `SwEndNode` | Section end marker |
| `Start` | 0x02 | `SwStartNode` | Section start marker |
| `Table` | 0x04\|Start | `SwTableNode` | Table |
| `Text` | 0x08 | `SwTextNode` | Text paragraph |
| `Grf` | 0x10 | `SwGrfNode` | Graphic/image |
| `Ole` | 0x20 | `SwOLENode` | OLE object |
| `Section` | 0x40\|Start | `SwSectionNode` | Section |

### Key Classes — Stage 2

| Class | File | Role |
|---|---|---|
| `SwDoc` | `sw/inc/doc.hxx` | Central document model |
| `SwNodes` | `sw/inc/ndarr.hxx` | Node array (BigPtrArray) |
| `SwNode` | `sw/inc/node.hxx` | Abstract base node |
| `SwTextNode` | `sw/inc/ndtxt.hxx` | Text paragraph node |
| `SwTableNode` | `sw/inc/ndtbl.hxx` | Table node |
| `SwStartNode` / `SwEndNode` | `sw/inc/ndtyp.hxx` | Section delimiters |

---

## Stage 3: Frame Tree Construction

### When Frames Are Created

Frames are created **lazily** — when content needs to be laid out. The entry point is:

```
SwRootFrame::Init()                                   [sw/source/core/layout/newfrm.cxx:440]
  → InsertNewPage()                                   [sw/source/core/layout/frmtool.cxx:3157]
      → SwPageFrame::Paste() + PreparePage()
  → InsertCnt_()                                      [frmtool.cxx:1508]
      → sw::MakeTextFrame() / pNode->MakeFrame()
      → pFrame->InsertBehind(pLay, pPrv)
```

### Frame Class Hierarchy

```
SwFrame                                               [sw/source/core/inc/frame.hxx:324]
  │
  ├─ SwLayoutFrame (containers)                       [sw/source/core/inc/layfrm.hxx:37]
  │    │   └─ m_pLower → first child SwFrame
  │    │
  │    ├─ SwRootFrame                                 [sw/source/core/inc/rootfrm.hxx:83]
  │    │    └─ children: SwPageFrame instances
  │    │
  │    ├─ SwPageFrame                                 [sw/source/core/inc/pagefrm.hxx:61]
  │    │    ├─ SwHeaderFrame (optional)
  │    │    ├─ SwBodyFrame
  │    │    ├─ SwFootnoteContFrame
  │    │    └─ SwFooterFrame (optional)
  │    │
  │    ├─ SwBodyFrame                                 [sw/source/core/inc/bodyfrm.hxx:26]
  │    ├─ SwColumnFrame
  │    ├─ SwSectionFrame
  │    ├─ SwTabFrame                                  [table]
  │    ├─ SwRowFrame                                  [table row]
  │    ├─ SwCellFrame                                 [table cell]
  │    ├─ SwFlyFrame                                  [floating frame]
  │    └─ SwHeadFootFrame                             [header/footer]
  │
  └─ SwContentFrame (leaf content)                    [sw/source/core/inc/cntfrm.hxx:58]
       │   └─ inherits SwFlowFrame (pagination logic)
       │
       ├─ SwTextFrame                                 [sw/source/core/inc/txtfrm.hxx:174]
       │    └─ can split into master/follow frames
       └─ SwNoTextFrame                               [graphic, OLE]
```

### Frame Tree Structure (Example)

```
SwRootFrame
  ├─ SwPageFrame #1
  │    ├─ SwHeaderFrame
  │    │    └─ SwTextFrame "Header text"
  │    ├─ SwBodyFrame
  │    │    ├─ SwTextFrame "Paragraph 1"          ← master
  │    │    ├─ SwTextFrame "Paragraph 2"
  │    │    ├─ SwTabFrame
  │    │    │    └─ SwRowFrame
  │    │    │         ├─ SwCellFrame
  │    │    │         │    └─ SwTextFrame "Cell A1"
  │    │    │         └─ SwCellFrame
  │    │    │              └─ SwTextFrame "Cell B1"
  │    │    └─ SwTextFrame "Paragraph 1 (follow)" ← follow (split across page)
  │    └─ SwFooterFrame
  │         └─ SwTextFrame "Footer text"
  │
  └─ SwPageFrame #2
       └─ SwBodyFrame
            └─ SwTextFrame "Paragraph 3"
```

### Key Frame Creation Functions

| Function | File:Line | Purpose |
|---|---|---|
| `SwRootFrame::Init()` | `newfrm.cxx:440` | Creates initial page + inserts content |
| `InsertNewPage()` | `frmtool.cxx:3157` | Creates SwPageFrame, pastes into tree |
| `InsertCnt_()` | `frmtool.cxx:1508` | Walks nodes, creates frames |
| `MakeFrames()` | `frmtool.cxx:2073` | Creates frames for a node range |
| `MakeTextFrame()` | `txtfrm.hxx:118` | Creates SwTextFrame for SwTextNode |
| `SwFlyFrameFormat::MakeFrames()` | `atrfrm.cxx:3082` | Creates fly frames for anchored objects |

### SwNode2Layout — The Bridge

```
SwNode2Layout                                         [sw/source/core/inc/node2lay.hxx:55]
  └─ SwNode2LayImpl                                   [sw/source/core/docnode/node2lay.cxx]
       ├─ NextFrame()   // find next existing frame for a node
       └─ UpperFrame()  // find parent layout frame for insertion
```

### Key Classes — Stage 3

| Class | File | Role |
|---|---|---|
| `SwFrame` | `sw/source/core/inc/frame.hxx` | Abstract base, position/size |
| `SwLayoutFrame` | `sw/source/core/inc/layfrm.hxx` | Container (has `m_pLower`) |
| `SwContentFrame` | `sw/source/core/inc/cntfrm.hxx` | Leaf content base |
| `SwRootFrame` | `sw/source/core/inc/rootfrm.hxx` | Tree root, children = pages |
| `SwPageFrame` | `sw/source/core/inc/pagefrm.hxx` | One page |
| `SwBodyFrame` | `sw/source/core/inc/bodyfrm.hxx` | Page body area |
| `SwTextFrame` | `sw/source/core/inc/txtfrm.hxx` | Text paragraph visualization |
| `SwFlowFrame` | `sw/source/core/inc/flowfrm.hxx` | Pagination logic mixin |

---

## Stage 4: Layout & Pagination

### Layout Action Driver

```
SwLayAction::Action()                                 [sw/source/core/layout/layact.cxx:373]
  → TurboAction()                                     // fast path: single frame
  → InternalAction()                                  [layact.cxx:489]
      → m_pRoot->Calc()
      → for each invalid SwPageFrame:
          → SwObjectFormatter::FormatObjsAtFrame()    // at-page anchored objects
          → FormatLayout(pPage)                       [layact.cxx:1290]
          │   → pPage->Calc() → recursive on lowers
          → FormatContent(pPage)                      [layact.cxx:1736]
              → pCnt->Calc()
                → SwContentFrame::MakeAll()           ★ core formatting loop
```

### SwContentFrame::MakeAll() — The Pagination Heart

```
SwContentFrame::MakeAll()                             [sw/source/core/layout/calcmove.cxx:1314]
  │
  │  while (!isFrameAreaPositionValid() || !isFrameAreaSizeValid() || !isFrameAreaValid()):
  │
  ├─ CheckMoveFwd()                                   [flowfrm.cxx:1994]
  │    ├─ check "keep" condition (must stay with successor)
  │    ├─ IsPageBreak()                               [flowfrm.cxx:1304]
  │    │    ├─ SvxBreak::PageBefore / PageBoth
  │    │    └─ SwFormatPageDesc change
  │    └─ IsColBreak()
  │    → if needed: MoveFwd()
  │
  ├─ MakePos()                                        // compute position
  ├─ MakePrtArea()                                    // compute print area
  ├─ Format()                                         // → SwTextFrame::Format()
  │    → SwTextFormatter creates SwLinePortion objects
  │
  ├─ if overflows upper:
  │    → MoveFwd()                                    [flowfrm.cxx:2100]
  │        → GetLeaf(MAKEPAGE_INSERT) → may create new page
  │        → MoveSubTree(pNewUpper)
  │
  ├─ if space available before:
  │    → MoveBwd()                                    [flowfrm.cxx:2314]
  │
  └─ handle: widow/orphan, keep-with-next, page/column breaks
```

### Text Frame Formatting

```
SwTextFrame::Format()                                 [sw/source/core/text/frmform.cxx:2282]
  → FormatImpl()                                      [frmform.cxx:2166]
      → SwTextFormatInfo                              // formatting context
      → SwTextFormatter                               // line builder
          → Format_()                                 // iterate text, create portions
              → SwLinePortion hierarchy:
                  ├─ SwTextPortion                     [portxt.hxx] — text run
                  ├─ SwFieldPortion                   [porfld.hxx] — field
                  ├─ SwFlyPortion                     [porfly.hxx] — inline fly
                  ├─ SwGluePortion                    [porglue.hxx] — tab/space glue
                  ├─ SwTabPortion                     [portab.hxx] — tab stop
                  ├─ SwBreakPortion                   [porbrk.hxx] — break
                  └─ SwHolePortion                    [porhyph.hxx] — hyphenation
```

### Pagination Flow Diagram

```
                    ┌──────────────┐
                    │ SwContentFrame│
                    │   MakeAll()  │
                    └──────┬───────┘
                           │
                    ┌──────▼───────┐
                    │CheckMoveFwd()│
                    │  page break? │──── yes ──→ MoveFwd() → new page
                    │  col break?  │
                    │  keep?       │
                    └──────┬───────┘
                           │ no
                    ┌──────▼───────┐
                    │   Format()   │
                    │ text layout  │
                    └──────┬───────┘
                           │
                    ┌──────▼───────┐
                    │  overflow?   │──── yes ──→ MoveFwd() → split frame
                    └──────┬───────┘              (create follow frame)
                           │ no
                    ┌──────▼───────┐
                    │  space left? │──── yes ──→ MoveBwd() → merge back
                    └──────┬───────┘
                           │ no
                    ┌──────▼───────┐
                    │    Done      │
                    └──────────────┘
```

### Key Layout Functions

| Function | File:Line | Purpose |
|---|---|---|
| `SwLayAction::Action()` | `layact.cxx:373` | Main layout entry |
| `SwLayAction::InternalAction()` | `layact.cxx:489` | Per-page layout loop |
| `SwLayAction::FormatLayout()` | `layact.cxx:1290` | Recursive layout formatting |
| `SwLayAction::FormatContent()` | `layact.cxx:1736` | Content frame formatting |
| `SwContentFrame::MakeAll()` | `calcmove.cxx:1314` | Text formatting + pagination |
| `SwLayoutFrame::MakeAll()` | `calcmove.cxx:994` | Layout frame formatting |
| `SwFlowFrame::CheckMoveFwd()` | `flowfrm.cxx:1994` | Check if must move forward |
| `SwFlowFrame::MoveFwd()` | `flowfrm.cxx:2100` | Move to next page/column |
| `SwFlowFrame::MoveBwd()` | `flowfrm.cxx:2314` | Move backward |
| `SwFlowFrame::IsPageBreak()` | `flowfrm.cxx:1304` | Detect hard page breaks |
| `SwTextFrame::Format()` | `frmform.cxx:2282` | Text line layout |

---

## Stage 5: Rendering

### Paint Entry Chain

```
VCL Window Paint Event
  → SwViewShell::Paint()                              [sw/source/core/view/viewsh.cxx:2026]
      → CheckInvalidForPaint()
      │   → if needed: SwLayAction::Action()          // format first!
      │
      → PaintDesktop()                                // background
      │
      → SwRootFrame::PaintSwFrame()                   [sw/source/core/layout/paintfrm.cxx:3225]
          → for each visible SwPageFrame:
              → DLPrePaint2()                          // DrawingLayer overlay
              → pPage->PaintSwFrame()                  // = SwLayoutFrame::PaintSwFrame
              │   → for each lower frame:
              │       → pFrame->Calc()                 // ensure formatted
              │       → pFrame->PaintSwFrame()         // virtual dispatch
              │           ├─ PaintSwFrameBackground()  [paintfrm.cxx:6891]
              │           ├─ [content-specific painting]
              │           └─ PaintSwFrameShadowAndBorder()  [paintfrm.cxx:5537]
              → DLPostPaint2()
```

### SwTextFrame::PaintSwFrame()

```
SwTextFrame::PaintSwFrame()                           [sw/source/core/text/frmpaint.cxx:659]
  → GetFormatted()                                    // ensure SwParaPortion exists
  → SwTextPaint / SwTextIter
      → iterate SwLinePortion objects
      → for each portion:
          → draw text / field / fly / tab / etc.
          → OutputDevice calls (DrawText, DrawRect, ...)
```

### Tiled Rendering (LOK — LibreOffice Online)

```
SwViewShell::PaintTile()                              [viewsh.cxx:2160]
  → same pipeline as Paint() but renders to a tile buffer
  → used by LibreOffice Online / collaborative editing
```

### Key Rendering Functions

| Function | File:Line | Purpose |
|---|---|---|
| `SwViewShell::Paint()` | `viewsh.cxx:2026` | Top-level paint entry |
| `SwRootFrame::PaintSwFrame()` | `paintfrm.cxx:3225` | Master paint loop |
| `SwLayoutFrame::PaintSwFrame()` | `paintfrm.cxx:3672` | Recursive child painting |
| `SwTextFrame::PaintSwFrame()` | `frmpaint.cxx:659` | Text rendering |
| `SwTabFrame::PaintSwFrame()` | `paintfrm.cxx:4663` | Table painting (content + borders) |
| `SwFrame::PaintSwFrameBackground()` | `paintfrm.cxx:6891` | Frame background fill |
| `SwFrame::PaintSwFrameShadowAndBorder()` | `paintfrm.cxx:5537` | Frame shadow/border |
| `SwViewShell::PaintTile()` | `viewsh.cxx:2160` | Tiled rendering for LOK |

---

## Appendix A: Complete File Index

### DOCX Import Filter
| File | Description |
|---|---|
| `sw/source/filter/docx/swdocxreader.cxx` | Entry point `ImportDOCX()` |
| `sw/source/writerfilter/filter/WriterFilter.cxx` | UNO filter component |
| `sw/source/writerfilter/ooxml/OOXMLDocumentImpl.cxx` | Substream orchestration |
| `sw/source/writerfilter/ooxml/OOXMLFastContextHandler.cxx` | XML→Stream translation |
| `sw/source/writerfilter/ooxml/OOXMLFastDocumentHandler.cxx` | SAX handler |
| `sw/source/writerfilter/ooxml/OOXMLFactory.cxx` | Auto-generated element mapping |
| `sw/source/writerfilter/ooxml/model.xml` | Schema → code generation |
| `sw/source/writerfilter/dmapper/DomainMapper.cxx` | Stream implementation |
| `sw/source/writerfilter/dmapper/DomainMapper_Impl.cxx` | Core import logic |
| `sw/source/writerfilter/dmapper/StyleSheetTable.cxx` | Style import |

### Document Model
| File | Description |
|---|---|
| `sw/inc/doc.hxx` | SwDoc class |
| `sw/inc/ndarr.hxx` | SwNodes (BigPtrArray) |
| `sw/inc/node.hxx` | SwNode base |
| `sw/inc/ndtyp.hxx` | SwNodeType enum + node subclasses |

### Frame Tree
| File | Description |
|---|---|
| `sw/source/core/inc/frame.hxx` | SwFrame base |
| `sw/source/core/inc/layfrm.hxx` | SwLayoutFrame |
| `sw/source/core/inc/cntfrm.hxx` | SwContentFrame |
| `sw/source/core/inc/rootfrm.hxx` | SwRootFrame |
| `sw/source/core/inc/pagefrm.hxx` | SwPageFrame |
| `sw/source/core/inc/bodyfrm.hxx` | SwBodyFrame |
| `sw/source/core/inc/txtfrm.hxx` | SwTextFrame |
| `sw/source/core/inc/flowfrm.hxx` | SwFlowFrame (pagination mixin) |
| `sw/source/core/inc/frmtool.hxx` | MakeFrames() declarations |
| `sw/source/core/inc/node2lay.hxx` | SwNode2Layout bridge |

### Frame Creation
| File | Description |
|---|---|
| `sw/source/core/layout/frmtool.cxx` | MakeFrames(), InsertNewPage(), InsertCnt_() |
| `sw/source/core/layout/newfrm.cxx` | Frame constructors, SwRootFrame::Init() |
| `sw/source/core/docnode/node2lay.cxx` | Node-to-layout iteration |
| `sw/source/core/docnode/node.cxx` | MakeFramesForAdjacentContentNode() |

### Layout & Pagination
| File | Description |
|---|---|
| `sw/source/core/layout/layact.cxx` | SwLayAction::Action(), InternalAction() |
| `sw/source/core/layout/calcmove.cxx` | MakeAll() implementations |
| `sw/source/core/layout/pagechg.cxx` | SwRootFrame::MakeAll() |
| `sw/source/core/layout/flowfrm.cxx` | MoveFwd(), MoveBwd(), IsPageBreak() |
| `sw/source/core/layout/layouter.cxx` | SwLayouter, loop control |
| `sw/source/core/text/frmform.cxx` | SwTextFrame::Format() |

### Rendering
| File | Description |
|---|---|
| `sw/source/core/view/viewsh.cxx` | SwViewShell::Paint() |
| `sw/source/core/layout/paintfrm.cxx` | PaintSwFrame() implementations |
| `sw/source/core/text/frmpaint.cxx` | SwTextFrame::PaintSwFrame() |
| `sw/source/core/text/itrpaint.cxx` | SwTextPaint iterator |
| `sw/source/core/text/itrtxt.cxx` | SwTextFormatter |
| `sw/source/core/text/inftxt.cxx` | SwTextFormatInfo |

---

## Appendix B: Modular Separation Strategy

To achieve the goal of decoupling the core pipeline from unrelated features, the architecture naturally separates into these independent modules:

```
┌─────────────────────────────────────────────────────────────┐
│                    CORE PIPELINE                             │
│  (tightly coupled, must work together)                      │
│                                                             │
│  DOCX Parser → SwNodes → Frame Tree → Layout → Rendering   │
└─────────────────────────────────────────────────────────────┘

┌──────────────┐  ┌──────────────┐  ┌──────────────┐
│  Decoupled   │  │  Decoupled   │  │  Decoupled   │
│  Modules     │  │  Modules     │  │  Modules     │
│              │  │              │  │              │
│  • Undo/Redo │  │  • Spell     │  │  • Track     │
│  • Printing  │  │    Check     │  │    Changes   │
│  • PDF       │  │  • Auto-     │  │  • Comments  │
│    Export    │  │    Correct   │  │  • Fields    │
│  • Mail      │  │  • Thesaurus │  │  • Index     │
│    Merge     │  │  • Linguist  │  │  • TOC       │
└──────────────┘  └──────────────┘  └──────────────┘
```

### For "WPS Docs Quick Start Guide.docx" specifically:

The minimum viable pipeline requires:
1. **DOCX Parser**: ZIP extraction + SAX parsing of `document.xml`, `styles.xml`, `numbering.xml`
2. **DomainMapper**: Translate OOXML → UNO API calls (paragraphs, text, tables, styles)
3. **SwNodes**: Store the parsed content as SwTextNode / SwTableNode entries
4. **Frame Tree**: SwRootFrame → SwPageFrame → SwBodyFrame → SwTextFrame
5. **Layout**: SwLayAction + SwContentFrame::MakeAll() for text flow and pagination
6. **Rendering**: SwViewShell::Paint() → SwTextFrame::PaintSwFrame() → OutputDevice

Features that can be **stubbed or skipped** for the core flow:
- Undo/Redo system
- Spell checking / auto-correct
- Track changes / comments
- Mail merge
- Printing subsystem
- PDF export (use direct rendering instead)
- OLE object embedding
- Complex field types (TOC, index — can be deferred)

---

## Appendix C: Quick Reference — Sample File Processing

For `WPS Docs Quick Start Guide.docx`, the expected processing steps:

```
1. Open file
   → SwDOCXReader::Read()
   → WriterFilter::filter()
   → Extract ZIP package

2. Parse styles.xml
   → StyleSheetTable::ApplyStyleSheets()
   → Create paragraph/character styles in SwDoc

3. Parse document.xml
   → For each <w:p>:
       → startParagraphGroup() → PushProperties(CONTEXT_PARAGRAPH)
       → <w:pPr> → paragraph properties (alignment, spacing, indentation)
       → For each <w:r>:
           → startCharacterGroup() → PushProperties(CONTEXT_CHARACTER)
           → <w:rPr> → character properties (bold, italic, font, size)
           → <w:t> → appendTextPortion() → insertTextPortion() → SwTextNode
           → endCharacterGroup()
       → endOfParagraph() → finishParagraph() → finalize SwTextNode

4. Build frame tree
   → SwRootFrame::Init()
   → InsertNewPage() → SwPageFrame
   → InsertCnt_() → for each SwTextNode: MakeTextFrame() → SwTextFrame

5. Layout
   → SwLayAction::Action()
   → For each SwTextFrame: MakeAll() → Format() → line breaking
   → If overflow: MoveFwd() → create follow frame on next page
   → Repeat until all frames fit

6. Render
   → SwViewShell::Paint()
   → SwRootFrame::PaintSwFrame()
   → For each SwPageFrame: PaintSwFrame()
       → SwBodyFrame: PaintSwFrame()
           → SwTextFrame: PaintSwFrame() → draw text
```

---

## Appendix D: Implementation Status (aproj/docx)

The `aproj/docx` directory contains a standalone C++ implementation of the core DOCX pipeline.

### Implemented Features

| Feature | Status | Files |
|---|---|---|
| DOCX Import (ZIP + XML parsing) | ✅ Done | `docx_reader.cpp`, `xml_util.cpp` |
| Document Model | ✅ Done | `document.h/cpp` |
| Frame Tree | ✅ Done | `frame.h/cpp` |
| Layout & Pagination | ✅ Done | `layout.h/cpp` |
| Rendering | ✅ Done | `renderer.h/cpp` |
| Font Engine (stb_truetype) | ✅ Done | `font_engine.h/cpp` |
| **Numbering Support** | ✅ Done | `docx_reader.cpp`, `document.cpp`, `layout.cpp` |
| **Header/Footer Frames** | ✅ Done | `docx_reader.cpp`, `frame.h`, `layout.cpp`, `renderer.cpp` |
| **Text Frame Splitting** | ✅ Done | `layout.cpp` |
| **Section Break Handling** | ✅ Done | `docx_reader.cpp`, `layout.cpp` |
| **Keep-with-next / Widow-orphan** | ✅ Done | `layout.cpp` |

### Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        docx::DocxReader                         │
│  ZIP extraction → XML parsing → Document model                  │
│  Parses: document.xml, styles.xml, numbering.xml, headers/footers│
└───────────────────────────┬─────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│                        docx::Document                            │
│  paragraphs[], tables[], images[], styles{}, headers{}, footers{}│
│  abstractNums[], numDefs[], sectionBreakMap{}                    │
└───────────────────────────┬─────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│                     docx::FrameBuilder                           │
│  RootFrame → PageFrame → HeaderFooterFrame + BodyFrame           │
└───────────────────────────┬─────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│                     docx::LayoutEngine                           │
│  Text line breaking, pagination, numbering, section breaks       │
│  TextFrame splitting (master/follow), keep-with-next, widow/orphan│
└───────────────────────────┬─────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│                       docx::Renderer                             │
│  Page rendering → header/footer → body → text/table/image frames│
│  Bitmap output → PNG save (stb_image_write)                      │
└─────────────────────────────────────────────────────────────────┘
```

### Numbering Implementation Details

The numbering system follows LibreOffice's approach:

1. **Data Model**: `AbstractNumDef` (from `<w:abstractNum>`) + `NumDef` (from `<w:num>`)
2. **Level Definitions**: `NumLevelDef` per level (0-9) with format, text pattern, font
3. **Resolution**: `Document::resolveNumbering(numId, ilvl)` → level definition
4. **Formatting**: `Document::formatNumText(level, counters)` → formatted string
5. **Counter Tracking**: `LayoutEngine::numCounters_` tracks current count per numId/ilvl

Supported number formats:
- Decimal: 1, 2, 3
- Upper/Lower Roman: I, II, III / i, ii, iii
- Upper/Lower Letter: A, B, C / a, b, c
- Bullet: •, custom character

### Header/Footer Implementation

Headers and footers are parsed from separate XML files (e.g., `word/header1.xml`):
- Section properties contain `<w:headerReference>` and `<w:footerReference>` with relationship IDs
- Relationships file (`word/_rels/document.xml.rels`) maps IDs to file paths
- Header/footer content is stored as `std::vector<Paragraph>` in the Document
- Frame tree includes `HeaderFooterFrame` in each `PageFrame`
- Layout engine adjusts body frame height to account for header/footer

### Text Frame Splitting

When a paragraph doesn't fit on the current page:
1. Find the split point (how many lines fit)
2. Create a **master frame** with lines that fit
3. Create a **follow frame** with remaining lines
4. Add master to current page, follow to new page
5. Recursive splitting if follow frame also doesn't fit

### Pagination Rules

- **Page break before**: `<w:pageBreakBefore>` forces new page
- **Keep-with-next**: `<w:keepNext>` keeps paragraph with next paragraph
- **Widow-orphan control**: Ensures at least 2 lines on each page
- **Section breaks**: Different page properties per section
