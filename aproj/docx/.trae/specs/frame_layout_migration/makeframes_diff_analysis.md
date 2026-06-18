# MakeFrames 函数差异分析报告

## 1. LO MakeFrames 核心逻辑概述

### 1.1 函数架构

LibreOffice 的 `MakeFrames` 函数采用分层架构：

```
MakeFrames (入口函数)
    ├── FindPrvNxtFrameNode (查找参考节点)
    ├── SwNode2Layout (遍历现有 Frame)
    └── InsertCnt_ (核心内容插入函数)
        ├── SwLayHelper (分页预计算辅助类)
        ├── SwActualSection (嵌套 Section 管理)
        ├── 节点类型处理:
        │   ├── ContentNode → MakeTextFrame / MakeFrame
        │   ├── TableNode → MakeFrame (TabFrame)
        │   ├── SectionNode → MakeFrame (SectionFrame)
        │   ├── EndNode (Section) → 关闭 Section
        │   └── FlyStartNode → AppendObjs
        ├── AppendObjs (浮动对象处理)
        ├── lcl_SetPos (位置设置)
        └── AppendAllObjs (批量连接 Fly)
```

### 1.2 核心数据结构

| 结构 | 用途 | LO 文件位置 |
|------|------|-------------|
| `SwLayHelper` | 分页预计算、页面创建辅助 | `layhelp.hxx:107-137` |
| `SwActualSection` | 嵌套 Section 状态管理 | `layhelp.hxx:85-103` |
| `SwLayCacheImpl` | 布局缓存（从文件加载） | `layhelp.hxx:56-81` |
| `SwNode2Layout` | 现有 Frame 到新 Frame 的映射 | `frmtool.cxx` |
| `SwBorderAttrs` | 边框/阴影属性缓存 | `frmtool.cxx:2266+` |

### 1.3 核心流程详解

#### InsertCnt_ 函数 (行 1508-2071)

1. **初始化阶段**
   - 阻止 Timer Idle
   - 创建 `SwLayHelper` 用于分页预计算
   - 创建 `SwActualSection` 用于 Section 管理

2. **节点遍历循环** (行 1605-2025)
   - 遍历 `nIndex` 到 `nEndIndex` 的所有节点
   - 根据节点类型分发处理

3. **ContentNode 处理** (行 1609-1680)
   - 检查 redline 合并状态，跳过隐藏段落
   - 创建 TextFrame: `sw::MakeTextFrame()`
   - 调用 `SwLayHelper::CheckInsert()` 检查分页
   - `InsertBehind()` 插入到布局
   - `lcl_SetPos()` 设置初始位置
   - `AppendObjs()` 处理锚点 Fly

4. **TableNode 处理** (行 1681-1778)
   - 检查 redline 删除状态
   - `pTableNode->MakeFrame()` 创建 TabFrame
   - 处理表格公式转换
   - `GCLines()` 清理表格行
   - 跳转到 EndOfSectionIndex

5. **SectionNode 处理** (行 1780-1908)
   - 创建 `SwActualSection` 对象
   - `pNode->MakeFrame()` 创建 SectionFrame
   - 处理嵌套 Section 的 Split
   - `Init()` 初始化 SectionFrame
   - 切换 `pLay` 到 Section 内部

6. **EndNode (Section) 处理** (行 1910-2001)
   - 关闭当前 Section
   - 处理空 Section 删除
   - 处理 Section Split (`SplitSect()`)
   - 恢复到上层布局

7. **FlyStartNode 处理** (行 2003-2018)
   - 调用 `AppendObjs()` 处理 Fly 锚点

8. **结束阶段** (行 2028-2071)
   - 清理残留的 `SwActualSection`
   - `AppendAllObjs()` 连接所有 Fly
   - 处理隐藏 Section 的 Fly 显示
   - 清理布局缓存

#### MakeFrames 函数 (行 2073-2264)

1. **查找参考节点**
   ```cpp
   SwNode* pNd = rDoc.GetNodes().FindPrvNxtFrameNode(rSttIdx, ...)
   ```

2. **遍历现有 Frame**
   ```cpp
   SwNode2Layout aNode2Layout(*pNd, rSttIdx.GetIndex());
   while (SwFrame* pFrame = aNode2Layout.NextFrame())
   ```

3. **锁定相关 Frame**
   - FootnoteFrame: `ColLock()`
   - SectionFrame: `ColLock()`

4. **移动检测与处理**
   - 检查是否需要移动到下一页
   - `SwFlowFrame::MoveFwd()` 移动 Frame

5. **调用 InsertCnt_**
   ```cpp
   ::InsertCnt_(pUpper, rDoc, rSttIdx.GetIndex(), ...)
   ```

6. **页面描述检查**
   ```cpp
   SwFrame::CheckPageDescs(pPage, false)
   ```

### 1.4 关键辅助函数

| 函数 | 位置 | 用途 |
|------|------|------|
| `lcl_SetPos` | 行 1489-1506 | 设置新 Frame 初始位置（+1 twips 偏移） |
| `AppendObjs` | 行 1296-1345 | 为节点添加锚点 Fly |
| `AppendObjsOfNode` | 行 1250-1293 | 单节点 Fly 处理 |
| `AppendAllObjs` | 行 1399-1448 | 批量连接所有 Fly |
| `AppendObj` | 行 1180+ | 单个 Fly 创建 |
| `IsShown` | 行 1160+ | 检查 Fly 是否显示（redline 合并） |

---

## 2. aproj 当前实现概述

### 2.1 函数架构

```
MakeFrames (入口函数)
    ├── InitLayout (初始化布局)
    ├── 节点遍历循环:
    │   ├── ProcessMultiColumnSection (多列节处理)
    │   ├── 溢出预检测
    │   ├── SectionNode 处理
    │   ├── EndNode 处理
    │   └── MakeFramesForNode (单节点处理)
    │       ├── TextNode → SwTextFrame
    │       ├── TableNode → SwTabFrame + RowFrame + CellFrame
    │       ├── GrfNode/OLENode → SwNoTextFrame
    │       └── SectionNode → SwSectionFrame
    └── MakeFlyFrames (Fly 处理)

辅助函数:
    ├── PreCalcNodeHeight (节点高度预计算)
    ├── InsertNewPage (新页面创建)
    ├── UpdateSectionFrameArea (Section 区域更新)
    └── MoveFrameTree (Frame 树移动)
```

### 2.2 已实现功能

| 功能 | 实现状态 | 代码位置 |
|------|----------|----------|
| 基本节点遍历 | ✅ 已实现 | 行 593-974 |
| TextFrame 创建 | ✅ 已实现 | 行 1038-1150 |
| TabFrame 创建 | ✅ 已实现 | 行 1151-1411 |
| SectionFrame 创建 | ✅ 已实现 | 行 901-933 |
| 多列布局处理 | ✅ 已实现 | 行 96-394 |
| 分页检测 | ⚠️ 简化实现 | 行 769-836 |
| Fly Frame 创建 | ⚠️ 简化实现 | 行 1486-1657 |
| 页面创建 | ✅ 已实现 | 行 1663-1700 |
| 布局初始化 | ✅ 已实现 | 行 981-1026 |

### 2.3 当前实现特点

1. **直接遍历模式**: 不使用 `InsertCnt_` 分层结构
2. **简化分页**: 基于高度预计算，无 `SwLayHelper`
3. **简化 Section**: 使用 `pOpenSectionFrame` 状态变量
4. **自定义多列**: `ProcessMultiColumnSection` 处理多列布局
5. **简化 Fly**: `MakeFlyFrames` 后处理 Fly

---

## 3. 详细差异对比表

### 3.1 核心架构差异

| 功能模块 | LO 实现 | aproj 实现 | 差异程度 |
|----------|---------|------------|----------|
| 函数分层 | `MakeFrames` → `InsertCnt_` | 直接遍历 | 🔴 高 |
| 分页辅助 | `SwLayHelper` 类 | 简化溢出检测 | 🔴 高 |
| Section 管理 | `SwActualSection` 类 | `pOpenSectionFrame` 变量 | 🟡 中 |
| 参考节点查找 | `FindPrvNxtFrameNode` | 无 | 🔴 高 |
| 现有 Frame 映射 | `SwNode2Layout` | 无 | 🔴 高 |
| 布局缓存 | `SwLayCacheImpl` | 无 | 🟡 中 |

### 3.2 节点处理差异

| 节点类型 | LO 处理 | aproj 处理 | 差异程度 |
|----------|---------|------------|----------|
| ContentNode | `sw::MakeTextFrame()` + `InsertBehind()` | `new SwTextFrame()` + `InsertBehind()` | 🟢 低 |
| TableNode | `MakeFrame()` + `GCLines()` + 公式转换 | 手动构建 TabFrame/RowFrame/CellFrame | 🟡 中 |
| SectionNode | `MakeFrame()` + `Init()` + Split 处理 | `new SwSectionFrame()` + 简化 Init | 🟡 中 |
| EndNode (Section) | `SplitSect()` + 空节删除 + 上层恢复 | `UpdateSectionFrameArea()` + 状态切换 | 🟡 中 |
| FlyStartNode | `AppendObjs()` | `MakeFlyFrames()` 后处理 | 🟡 中 |

### 3.3 分页机制差异

| 功能 | LO 实现 | aproj 实现 | 差异程度 |
|------|---------|------------|----------|
| 分页预计算 | `SwLayHelper::CalcPageCount()` | 无 | 🔴 高 |
| 分页插入检查 | `SwLayHelper::CheckInsert()` | `PreCalcNodeHeight()` + 溢出检测 | 🟡 中 |
| 页面描述检查 | `SwFrame::CheckPageDescs()` | 无 | 🔴 高 |
| 布局缓存利用 | `SwLayCacheImpl` | 无 | 🟡 中 |
| 移动检测 | `SwFlowFrame::MoveFwd()` | 无 | 🔴 高 |

### 3.4 Section 处理差异

| 功能 | LO 实现 | aproj 实现 | 差异程度 |
|------|---------|------------|----------|
| 嵌套 Section | `SwActualSection` 链式管理 | `pOpenSectionFrame` 单层 | 🔴 高 |
| Section Split | `SplitSect()` 函数 | 无 | 🔴 高 |
| 空 Section 删除 | `DelEmpty()` + `DestroyFrame()` | 无 | 🟡 中 |
| Section 锁定 | `ColLock()` / `ColUnlock()` | 无 | 🟡 中 |
| 外层 Section 查找 | 向上遍历 `StartOfSectionNode` | 无 | 🔴 高 |

### 3.5 Fly 处理差异

| 功能 | LO 实现 | aproj 实现 | 差异程度 |
|------|---------|------------|----------|
| Fly 创建时机 | 节点处理时 `AppendObjs()` | 后处理 `MakeFlyFrames()` | 🟡 中 |
| Fly 锚点查找 | `SwFormatAnchor::GetAnchorNode()` | `g_nodeToTextFrame` 映射 | 🟢 低 |
| Fly 连接 | `AppendAllObjs()` 批量处理 | 单个 Fly 注册 | 🟡 中 |
| Fly 显示检查 | `IsShown()` (redline 合并) | 无 | 🔴 高 |
| Fly 缓存 | `SwFlyCache` | 无 | 🟡 中 |

### 3.6 其他功能差异

| 功能 | LO 实现 | aproj 实现 | 差异程度 |
|------|---------|------------|----------|
| Redline 合并处理 | `HasMergedParas()` + `GetRedlineMergeFlag()` | 无 | 🔴 高 |
| Footnote 处理 | `SwFootnoteFrame` + `ColLock()` | 无 | 🔴 高 |
| 位置设置 | `lcl_SetPos()` (+1 twips 偏移) | 直接设置 `FrameArea` | 🟢 低 |
| RTL 布局 | `IsRightToLeft()` + RTL 边距处理 | 无 | 🔴 高 |
| Gutter margin | `GetGutterMargin()` 处理 | 无 | 🔴 高 |
| 边框属性 | `SwBorderAttrs` 缓存 | 无 | 🔴 高 |
| Accessibility 通知 | `InvalidateAccessibleParaFlowRelation()` | 无 | 🟡 中 |
| Timer Idle 控制 | `BlockIdling()` / `UnblockIdling()` | 无 | 🟡 中 |
| Callback Action | `SetCallbackActionEnabled()` | 无 | 🟡 中 |

---

## 4. 需要迁移的功能清单

### 4.1 高优先级 (P0) - 核心功能

| 序号 | 功能 | LO 代码位置 | 迁移建议 |
|------|------|-------------|----------|
| 1 | `SwLayHelper` 分页预计算 | `layhelp.hxx:107-137` | 创建 `LayHelper` 类，实现 `CalcPageCount()` 和 `CheckInsert()` |
| 2 | `SwActualSection` 嵌套 Section 管理 | `layhelp.hxx:85-103` | 创建 `ActualSection` 类，支持链式管理 |
| 3 | `FindPrvNxtFrameNode` 参考节点查找 | `nodes.cxx` | 实现 `FindPrvNxtFrameNode()` 函数 |
| 4 | `SwNode2Layout` 现有 Frame 映射 | `frmtool.cxx` | 创建 `Node2Layout` 类 |
| 5 | Section Split (`SplitSect()`) | `sectionframe.cxx` | 实现 `SectionFrame::SplitSect()` |
| 6 | `SwFrame::CheckPageDescs` | `pagefrm.cxx` | 实现页面描述检查 |

### 4.2 中优先级 (P1) - 重要功能

| 序号 | 功能 | LO 代码位置 | 迁移建议 |
|------|------|-------------|----------|
| 7 | `SwLayCacheImpl` 布局缓存 | `layhelp.hxx:56-81` | 从 DOCX 读取布局缓存信息 |
| 8 | 空 Section 删除 | `frmtool.cxx:1936-1942` | 实现 `SectionFrame::DelEmpty()` |
| 9 | Section 锁定机制 | `frmtool.cxx:2111-2117` | 实现 `ColLock()` / `ColUnlock()` |
| 10 | `SwFlowFrame::MoveFwd` | `flowfrm.cxx` | 实现 Frame 移动逻辑 |
| 11 | Fly 显示检查 (`IsShown`) | `frmtool.cxx:1160+` | 实现 redline 相关 Fly 显示逻辑 |
| 12 | Timer Idle 控制 | `frmtool.cxx:1512` | 添加 Idle 阻止机制 |
| 13 | Callback Action 控制 | `frmtool.cxx:1514-1516` | 添加回调控制 |

### 4.3 低优先级 (P2) - 可延后功能

| 序号 | 功能 | LO 代码位置 | 迁移建议 |
|------|------|-------------|----------|
| 14 | Redline 合并处理 | `frmtool.cxx:1612-1620` | 实现段落合并逻辑 |
| 15 | Footnote 处理 | `frmtool.cxx:2096-2104` | 创建 `FootnoteFrame` 类 |
| 16 | RTL 布局支持 | `frmtool.cxx:2362-2467` | 添加 RTL 方向处理 |
| 17 | Gutter margin | `frmtool.cxx:2341-2347` | 添加 Gutter 边距处理 |
| 18 | `SwBorderAttrs` 边框缓存 | `frmtool.cxx:2266+` | 创建边框属性缓存 |
| 19 | Accessibility 通知 | `frmtool.cxx:1644-1667` | 添加可访问性通知（可选） |
| 20 | `SwFlyCache` Fly 缓存 | `layhelp.hxx:209-216` | 从 DOCX 读取 Fly 位置缓存 |

---

## 5. 关键辅助函数迁移清单

### 5.1 必须迁移

| 函数签名 | 用途 | 迁移复杂度 |
|----------|------|------------|
| `void InsertCnt_(SwLayoutFrame*, SwDoc&, SwNodeOffset, bool, SwNodeOffset, SwFrame*, FrameMode)` | 核心内容插入 | 🔴 高 |
| `SwNode* FindPrvNxtFrameNode(SwNode&, SwNode&, SwLayout*)` | 查找参考节点 | 🟡 中 |
| `void lcl_SetPos(SwFrame&, SwLayoutFrame&)` | 设置初始位置 | 🟢 低 |
| `void AppendObjs(SpzFrameFormats*, SwNodeOffset, SwFrame*, SwPageFrame*, SwDoc&)` | Fly 锚点处理 | 🟡 中 |
| `void AppendAllObjs(SpzFrameFormats*, SwFrame*)` | 批量 Fly 连接 | 🟡 中 |

### 5.2 建议迁移

| 函数签名 | 用途 | 迁移复杂度 |
|----------|------|------------|
| `bool IsShown(SwNodeOffset, SwFormatAnchor&, ...)` | Fly 显示检查 | 🟡 中 |
| `void AppendObj(SwFrame*, SwPageFrame*, SwFrameFormat*, SwFormatAnchor&)` | 单 Fly 创建 | 🟢 低 |
| `void SwSectionFrame::SplitSect(SwFrame*, SwFrame*)` | Section 分割 | 🔴 高 |
| `void SwSectionFrame::DelEmpty(bool)` | 空 Section 删除 | 🟢 低 |
| `void SwFrame::CheckPageDescs(SwPageFrame*, bool)` | 页面描述检查 | 🔴 高 |

---

## 6. 迁移策略建议

### 6.1 分阶段迁移

**第一阶段：核心架构重构**
1. 创建 `InsertCnt_` 函数，重构 `MakeFrames` 为入口函数
2. 创建 `LayHelper` 类，实现基本分页预计算
3. 创建 `ActualSection` 类，支持嵌套 Section 管理
4. 实现 `FindPrvNxtFrameNode` 函数

**第二阶段：Section 完善**
1. 实现 `SectionFrame::SplitSect()`
2. 实现空 Section 删除逻辑
3. 实现 Section 锁定机制
4. 处理外层 Section 查找

**第三阶段：Fly 完善**
1. 将 `MakeFlyFrames` 改为节点处理时调用
2. 实现 `AppendObjs` 和 `AppendAllObjs`
3. 实现 Fly 显示检查逻辑

**第四阶段：高级功能**
1. 实现布局缓存读取
2. 实现 Redline 合并处理
3. 实现 RTL 布局支持
4. 实现 Footnote 处理

### 6.2 代码组织建议

```
aproj/docx/src/frame/
├── frmtree.cpp          # MakeFrames 入口
├── frmtree_insert.cpp   # InsertCnt_ 实现
├── layhelper.cpp        # SwLayHelper 实现
├── layhelper.h          # SwLayHelper + SwActualSection 定义
├── flyappend.cpp        # AppendObjs/AppendAllObjs 实现
└── section_split.cpp    # Section Split 逻辑
```

---

## 7. 总结

### 7.1 当前差距评估

| 类别 | 完成度 | 说明 |
|------|--------|------|
| 基础 Frame 创建 | 80% | TextFrame/TableFrame/SectionFrame 基本实现 |
| 分页机制 | 30% | 简化实现，缺少预计算和缓存 |
| Section 管理 | 40% | 单层管理，缺少嵌套和 Split |
| Fly 处理 | 50% | 后处理模式，缺少实时锚点 |
| 高级功能 | 10% | Redline/Footnote/RTL 未实现 |

### 7.2 迁移工作量估算

| 阶段 | 工作量 | 优先级 |
|------|--------|--------|
| 第一阶段（核心架构） | 高 | P0 |
| 第二阶段（Section） | 中 | P1 |
| 第三阶段（Fly） | 中 | P1 |
| 第四阶段（高级功能） | 高 | P2 |

### 7.3 关键风险

1. **分页逻辑复杂度**: LO 的 `SwLayHelper` 涉及大量边界条件
2. **Section Split**: 嵌套 Section 的 Split 逻辑复杂
3. **Redline 合并**: 需要完整的修订追踪支持
4. **RTL 布局**: 需要重新设计坐标系统

---

## 附录：LO 关键代码位置索引

| 文件 | 关键函数/类 | 行号 |
|------|-------------|------|
| `frmtool.cxx` | `MakeFrames` | 2073-2264 |
| `frmtool.cxx` | `InsertCnt_` | 1508-2071 |
| `frmtool.cxx` | `lcl_SetPos` | 1489-1506 |
| `frmtool.cxx` | `AppendObjs` | 1296-1345 |
| `frmtool.cxx` | `AppendAllObjs` | 1399-1448 |
| `frmtool.cxx` | `SwBorderAttrs` | 2266-2560 |
| `layhelp.hxx` | `SwLayHelper` | 107-137 |
| `layhelp.hxx` | `SwActualSection` | 85-103 |
| `layhelp.hxx` | `SwLayCacheImpl` | 56-81 |
| `layhelp.hxx` | `SwFlyCache` | 209-216 |
| `nodes.cxx` | `FindPrvNxtFrameNode` | (需查找) |
| `sectionframe.cxx` | `SplitSect` | (需查找) |
| `flowfrm.cxx` | `MoveFwd` | (需查找) |
| `pagefrm.cxx` | `CheckPageDescs` | (需查找) |