# Frame 树类型迁移待办项

> 对应 LibreOffice 的 `sw/source/core/layout/frmtool.cxx` 和 `sw/source/core/inc/frame.hxx`

## 已迁移（7 种）

| Frame 类型 | 枚举值 | 类 | 说明 |
|---|---|---|---|
| Root | 0x0001 | `SwRootFrame` | 根 Frame |
| Page | 0x0002 | `SwPageFrame` | 页面 Frame |
| Body | 0x0080 | `SwBodyFrame` | 正文容器 |
| Tab | 0x0400 | `SwTabFrame` | 表格 Frame |
| Row | 0x0800 | `SwRowFrame` | 表格行 Frame |
| Cell | 0x1000 | `SwCellFrame` | 表格单元格 Frame |
| Txt | 0x2000 | `SwTextFrame` | 文本段落 Frame |

`MakeFramesForNode` 当前只处理 `IsTextNode()` 和 `IsTableNode()` 两种节点类型。

## 待迁移（8 种）

### 1. Section（0x0200）— 节 Frame
- 类 `SwSectionFrame` 已声明在 [frame.h](src/frame/frame.h#L415-L421)，但从未在 `frmtree.cpp` 中创建
- `MakeFramesForNode` 中 `IsStartNode()` 分支只有空注释: `"节区开始，可能需要创建子布局"`
- 参考 LO: `SwSectionFrame` 在 `sw/source/core/layout/frmtool.cxx` 中由 `SwSectionNode::MakeFrames` 创建
- 依赖: 需要先迁移 `SwSectionNode` 的完整创建逻辑

### 2. Column（0x0004）— 分栏 Frame
- 无类定义，只有枚举值
- 当前多列布局在 `ProcessMultiColumnSection` 中直接操作 `SwTextFrame`，没有真正的 `SwColumnFrame`
- 参考 LO: `SwColumnFrame` 继承自 `SwLayoutFrame`，在 `sw/source/core/layout/colfrm.cxx`

### 3. Header（0x0008）— 页眉 Frame
- 无类定义，只有枚举值和 `IsHeaderFrame()` 判断
- `PreparePage()` 注释提到要创建 Header/Footer 但实际未实现
- 参考 LO: `SwHeaderFrame` 在 `sw/source/core/layout/hffrm.cxx`

### 4. Footer（0x0010）— 页脚 Frame
- 无类定义，只有枚举值和 `IsFooterFrame()` 判断
- 同上，`PreparePage()` 未实现
- 参考 LO: `SwFooterFrame` 在 `sw/source/core/layout/hffrm.cxx`

### 5. FootnoteCont（0x0020）— 脚注容器 Frame
- 无类定义，只有枚举值
- 参考 LO: `SwFootnoteContFrame` 在 `sw/source/core/layout/ftnfrm.cxx`

### 6. Footnote（0x0040）— 脚注 Frame
- 无类定义，只有枚举值
- 参考 LO: `SwFootnoteFrame` 在 `sw/source/core/layout/ftnfrm.cxx`

### 7. Fly（0x0100）— 浮动框 Frame
- 无类定义，只有枚举值和 `FindFlyFrame()` 方法
- 用于文本框、图片锚定框等浮动对象
- 参考 LO: `SwFlyFrame` 在 `sw/source/core/layout/fly.cxx` 及相关文件

### 8. NoTxt（0x4000）— 非文本内容 Frame
- 无类定义，只有枚举值
- 对应图片节点（`SwGrfNode`）和 OLE 节点（`SwOleNode`）
- `MakeFramesForNode` 未处理 `IsGrfNode()` / `IsOleNode()` 节点类型
- 参考 LO: `SwNoTextFrame` 在 `sw/source/core/layout/notxtfrm.cxx`

## 迁移优先级建议

按对排版结果影响的重要性和依赖关系排序:

1. **Section** — 节分隔符已经在节点模型中处理，但缺少 `SwSectionFrame` 容器，影响多节文档的 section 级属性应用
2. **NoTxt** — 图片是常见文档元素，缺失会导致图片无法渲染
3. **Header / Footer** — 页眉页脚是常见文档要素
4. **Column** — 多栏布局的正式 ColumnFrame 支持
5. **Fly** — 浮动对象（文本框等），复杂度较高
6. **Footnote / FootnoteCont** — 脚注，复杂度较高，依赖其他模块

## 同时需要处理的节点类型

`MakeFramesForNode` 还需要补充以下节点类型的处理:

- `IsGrfNode()` — 图片节点 → 创建 `SwNoTextFrame`
- `IsOleNode()` — OLE 对象节点 → 创建 `SwNoTextFrame`
- `IsStartNode()` / `IsSectionNode()` — 节开始节点 → 创建 `SwSectionFrame`

## 渲染层对应

`render_log.cpp` 的 `LogFrameTree` 也需要同步扩展，目前只对 `TextFrame` 和 `LayoutFrame` 有特殊处理。

渲染指令类型中已声明但未使用的有: `IMAGE_FRAME`、`SECTION_FRAME`，需要在对应 Frame 类型迁移后接入。