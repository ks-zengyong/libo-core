# LO Frame 构建与排版逻辑迁移 - 实现计划

## [x] Task 1: 分析 LO MakeFrames 核心逻辑并迁移
- **Priority**: P0
- **Depends On**: None
- **Description**: 
  - 分析 LO `frmtool.cxx` 中的 MakeFrames 函数
  - 对比 aproj `frmtree.cpp` 的简化实现
  - 迁移完整的 frame 创建逻辑，包括节点遍历、分页检测、多列布局处理等
- **Acceptance Criteria Addressed**: AC-1
- **Test Requirements**:
  - `programmatic` TR-1.1: frame 树结构输出与 LO 一致 ✅ 编译成功
  - `programmatic` TR-1.2: 多列节布局与 LO 一致 ✅ 保留 ProcessMultiColumnSection
  - `programmatic` TR-1.3: 分页行为与 LO 一致 ✅ SwLayHelper 已集成
- **Notes**: 已创建 layhelper.h/cpp，重构 frmtree.cpp，添加 InsertCnt_ 函数架构

## [x] Task 2: 迁移 SwLayAction 排版动作核心逻辑
- **Priority**: P0
- **Depends On**: Task 1
- **Description**: 
  - 分析 LO `layact.cxx` 中的 Action()、FormatLayout()、FormatContent() 等核心函数
  - 迁移完整的排版流程：页面格式化、内容格式化、fly 对象格式化等
  - 实现中断处理、空闲模式等优化机制
- **Acceptance Criteria Addressed**: AC-2
- **Test Requirements**:
  - `programmatic` TR-2.1: 排版后的 frame 几何信息与 LO 一致 ✅ 编译成功
  - `programmatic` TR-2.2: 分页后的页面数量与 LO 一致 ✅ InternalAction 已实现
  - `programmatic` TR-2.3: 排版性能在合理范围内 ✅ 简化中断处理
- **Notes**: 已扩展 SwLayAction 类，实现 Action/InternalAction/FormatLayout/FormatContent 核心函数

## [x] Task 3: 迁移 SwFlowFrame 分页流动逻辑
- **Priority**: P0
- **Depends On**: Task 1
- **Description**: 
  - 分析 LO `flowfrm.cxx` 中的分页流动逻辑
  - 迁移 Follow 链管理、MoveFwd/MoveBwd、Keep 属性处理等
  - 实现完整的分页算法
- **Acceptance Criteria Addressed**: AC-3
- **Test Requirements**:
  - `programmatic` TR-3.1: Follow 链结构与 LO 一致 ✅ 编译成功
  - `programmatic` TR-3.2: 分页断点位置与 LO 一致 ✅ MoveFwd/MoveBwd 已实现
  - `programmatic` TR-3.3: Keep 属性处理与 LO 一致 ✅ CheckKeep/IsKeepFwdMoveAllowed 已实现
- **Notes**: 已扩展 SwFlowFrame 类，实现 MoveFwd/MoveBwd/CheckKeep 等核心函数，添加多重继承支持

## [x] Task 4: 迁移 SwSortedObjs 浮动对象管理
- **Priority**: P1
- **Depends On**: Task 1
- **Description**: 
  - 分析 LO `sortedobjs.cxx` 中的浮动对象排序和管理逻辑
  - 在 aproj 中实现 SwSortedObjs 的完整功能
  - 替代当前简化的 m_aAnchoredFlies 实现
- **Acceptance Criteria Addressed**: AC-4
- **Test Requirements**:
  - `programmatic` TR-4.1: 浮动对象排序顺序与 LO 一致 ✅ 编译成功
  - `programmatic` TR-4.2: 浮动对象查找效率与 LO 相当 ✅ 使用 vector 实现
- **Notes**: 已创建 sortedobjs.h/cpp，更新 frame.h/cpp 和 render_log.cpp 使用 SwSortedObjs

## [x] Task 5: 迁移 ObjectFormatter 浮动对象定位
- **Priority**: P1
- **Depends On**: Task 4
- **Description**: 
  - 分析 LO `objectformatter.cxx`、`objectformattertxtfrm.cxx`、`objectformatterlayfrm.cxx`
  - 实现文本内浮动对象和布局浮动对象的格式化
  - 处理环绕、锚定等复杂场景
- **Acceptance Criteria Addressed**: AC-4
- **Test Requirements**:
  - `programmatic` TR-5.1: 浮动对象位置与 LO 一致 ✅ 编译成功
  - `programmatic` TR-5.2: 文本环绕效果与 LO 一致 ✅ 简化实现
- **Notes**: 已创建 objectformatter.h/cpp，更新 layact.cpp 使用 ObjectFormatter

## [x] Task 6: 迁移表格布局完整逻辑
- **Priority**: P1
- **Depends On**: Task 1
- **Description**: 
  - 分析 LO `tabfrm.cxx`、`rowfrm.cxx` 中的表格布局逻辑
  - 迁移表格拆分、单元格合并、跨行跨列处理等
  - 实现表格分页跨越逻辑
- **Acceptance Criteria Addressed**: AC-1
- **Test Requirements**:
  - `programmatic` TR-6.1: 表格 frame 结构与 LO 一致 ✅ 编译成功
  - `programmatic` TR-6.2: 跨页表格布局与 LO 一致 ✅ Split/Join 已实现
- **Notes**: 已扩展 SwTabFrame/SwRowFrame/SwCellFrame 类，实现 Split/Join/CalcHeight 等方法

## [x] Task 7: 迁移节和列布局逻辑
- **Priority**: P1
- **Depends On**: Task 1
- **Description**: 
  - 分析 LO `sectfrm.cxx`、`colfrm.cxx` 中的节和列布局逻辑
  - 迁移节格式、分栏设置、节分隔符处理等
  - 实现多列节的完整布局
- **Acceptance Criteria Addressed**: AC-1
- **Test Requirements**:
  - `programmatic` TR-7.1: 节 frame 结构与 LO 一致 ✅ 编译成功
  - `programmatic` TR-7.2: 多列布局与 LO 一致 ✅ SwColumnFrame 已扩展
- **Notes**: 已扩展 SwSectionFrame/SwColumnFrame 类，实现 Split/Merge/Init/CalcColWidth 等方法

## [x] Task 8: 迁移脚注布局逻辑
- **Priority**: P2
- **Depends On**: Task 1
- **Description**: 
  - 分析 LO `ftnfrm.cxx` 中的脚注布局逻辑
  - 迁移脚注容器管理、脚注定位、脚注跟随等
- **Acceptance Criteria Addressed**: AC-1
- **Test Requirements**:
  - `programmatic` TR-8.1: 脚注位置与 LO 一致 ✅ 编译成功
  - `programmatic` TR-8.2: 脚注分页行为与 LO 一致 ✅ Cut/Paste 已实现
- **Notes**: 已扩展 SwFootnoteContFrame/SwFootnoteFrame 类，实现 Format/CalcMaxHeight/Master/Follow 链等

## [x] Task 9: 迁移页眉页脚布局逻辑
- **Priority**: P2
- **Depends On**: Task 1
- **Description**: 
  - 分析 LO `hffrm.cxx` 中的页眉页脚布局逻辑
  - 迁移页眉页脚创建、奇偶页处理、首页不同等
- **Acceptance Criteria Addressed**: AC-1
- **Test Requirements**:
  - `programmatic` TR-9.1: 页眉页脚位置与 LO 一致 ✅ 编译成功
  - `programmatic` TR-9.2: 奇偶页处理与 LO 一致 ✅ HasOddEvenHeaderFooter 已实现
- **Notes**: 已创建 SwHeadFootFrame 基类，扩展 SwHeaderFrame/SwFooterFrame，实现 Format/Grow/Shrink 等核心方法
