# DOCX 独立组件开发总结

## 项目目标

从 LibreOffice 内核提取 DOCX 核心功能，构建独立的文档渲染管线：

```
OOXML ZIP → XML解析 → DocumentModel → Frame树 → 排版分页 → 渲染指令
```

输出与 LibreOffice 完全一致的排版结果。

---

## 已完成工作

### Phase 0: 基础设施

| 文件 | 说明 |
|------|------|
| `src/core/types.h` | 基础类型别名 (sal_Int32, SwTwips 等)，单位常量 (TWIPS_PER_INCH=1440, EMU_PER_TWIP=635) |
| `src/core/swrect.h` | SwRect 矩形类，支持 SetPos/SetSize/Intersection/Union/Contains/Move |
| `src/core/bparr.h/cpp` | BigPtrArray 块结构数组，MAXENTRY=1000，Index2Block 二分查找，Compress 内存优化 |

### Phase 1: 文档模型

| 文件 | 说明 |
|------|------|
| `src/core/node.h/cpp` | SwNode 类层级：SwStartNode, SwEndNode, SwContentNode, SwTextNode, SwTableNode, SwSectionNode |
| `src/core/ndarr.h/cpp` | SwNodes (BigPtrArray 子类) + SwNodeIndex，哨兵节点，MakeTextNode/InsertTable |
| `src/core/format.h/cpp` | 样式系统：SwFormat → SwFrameFormat / SwTextFormatColl / SwPageDesc，属性 ID 枚举 |
| `src/core/doc.h/cpp` | SwDoc 文档容器，管理样式集合，默认样式初始化 |

### Phase 2: Frame 树

| 文件 | 说明 |
|------|------|
| `src/frame/frame.h/cpp` | 完整 Frame 类层级：SwRootFrame, SwPageFrame, SwBodyFrame, SwContentFrame, SwTextFrame, SwTabFrame, SwRowFrame, SwCellFrame, SwSectionFrame |
| `src/frame/frmtree.h/cpp` | MakeFrames() 入口，InitLayout() 创建根框架，MakeFramesForNode() 递归构建 |

### Phase 3: DOCX 解析器

| 文件 | 说明 |
|------|------|
| `src/filter/docx_parser.h/cpp` | DocxParser 解析器：ExtractZip (miniz), ParseStyles, ParseNumbering, ParseDocument/Body/Paragraph/Table → SwDoc |

### Phase 4: 排版引擎

| 文件 | 说明|
|------|------|
| `src/layout/layact.h/cpp` | SwLayAction 排版控制器，TextFormatter 行格式化，FontEngine 字体存根 |

### Phase 5: 渲染指令记录

| 文件 | 说明 |
|------|------|
| `src/render/render_log.h/cpp` | RenderLogger 渲染日志记录器，DumpFrameTreeXml/DumpNodesXml 调试工具 |

---

## 当前架构

```
┌─────────────────────────────────────────────────────────┐
│                         SwDoc                            │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────┐  │
│  │   SwNodes    │  │  Styles     │  │  PageDescs      │  │
│  │  (BigPtrArray)  │  │  (FormatColls)  │  │  (页面尺寸)    │  │
│  └──────┬──────┘  └──────┬──────┘  └────────┬────────┘  │
│         │                │                   │           │
│         ▼                ▼                   ▼           │
│  ┌──────────────────────────────────────────────────┐   │
│  │                    Frame 树                       │   │
│  │  SwRootFrame → SwPageFrame → SwBodyFrame         │   │
│  │                    ↓                              │   │
│  │  SwContentFrame → SwTextFrame (SwFlowFrame)      │   │
│  └──────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
                          │
                          ▼
                   SwLayAction::Action()
                          │
                          ▼
                   RenderLogger (渲染指令)
```

---

## 后续工作

### 阶段 1: 端到端集成测试

**目标**: 验证完整管线可用

```
DocxParser::Read()
    ↓
SwDoc (nodes + styles)
    ↓
InitLayout() + MakeFrames()
    ↓
SwLayAction::Action()
    ↓
RenderLogger::Dump() → 渲染指令序列
```

**任务**:
- [x] 创建 `test_end_to_end.cpp` 集成测试
- [x] 用 sample.docx 验证完整流程
- [x] 输出渲染指令到文件

### 阶段 2: 字体度量完善

**目标**: 精确文本布局

**任务**:
- [ ] 集成 stb_truetype 真实字体加载
- [ ] 实现 GetGlyphWidth / GetTextWidth
- [ ] 处理字体 fallback 机制

### 阶段 3: 排版引擎增强

**目标**: 处理复杂文档结构

**任务**:
- [ ] 分栏排版 (SwColumnFrame)
- [ ] 表格跨页拆分 (SwTabFrame::Split/Follow)
- [ ] 文本框 (SwFlyFrame) 浮动对象
- [ ] 脚注/尾注 (FootnoteFrame)
- [ ] 页眉/页脚排版

### 阶段 4: 样式继承完善

**目标**: 正确的属性解析

**任务**:
- [ ] SwAttrSet 属性继承链实现
- [ ] 样式优先级 (直接格式 > 段落样式 > 字符样式)
- [ ] 默认属性计算

### 阶段 5: 渲染输出比对

**目标**: 验证与 LibreOffice 输出一致性

**任务**:
- [ ] LibreOffice 参考输出生成器
- [ ] 渲染指令 diff 工具
- [ ] 差异报告生成

### 阶段 6: 性能优化

**目标**: 处理大文档

**任务**:
- [ ] 增量布局 (只重排修改的 Frame)
- [ ] BigPtrArray 内存池优化
- [ ] 布局缓存机制

---

## 关键设计决策

1. **简化但保留核心**: 从 LibreOffice 提取核心逻辑，去掉 UI/打印/导出等非核心依赖
2. **渲染指令记录**: 输出结构化渲染指令而非直接绘制，便于比对测试
3. **块结构数组**: 保持 BigPtrArray 设计，支持高效节点插入/删除
4. **Frame 树分离**: 文档模型 (SwDoc) 与布局 (Frame 树) 解耦，支持多次重排

---

## 技术依赖

| 库 | 用途 |
|-----|------|
| miniz | ZIP 解压 |
| pugixml | XML 解析 |
| stb_truetype | 字体度量 |
| Google Test | 单元测试 |

---

## 构建状态

- ✅ 所有源文件编译通过
- ✅ 19 个单元测试全部通过
- ✅ CMake 构建系统配置完成
