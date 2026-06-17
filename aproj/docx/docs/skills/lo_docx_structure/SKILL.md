# LibreOffice DOCX 组件架构参考

> 在需要查阅 LibreOffice DOCX 相关模块的源码位置、数据结构或处理流程时触发。本技能为索引文档，完整内容见参考手册。

## 参考手册

完整架构文档位于 `docx/docs/reference/lo_docx_structure.md`，涵盖以下内容：

| 章节 | 内容 |
|------|------|
| 1. 总体架构 | 三层架构（模型层/布局层/渲染层）、核心设计原则 |
| 2. DOCX 导入管线 | 数据流、入口点、OOXML 三层架构、子流解析顺序 |
| 3. DOCX 导出管线 | 类层次、策略模式、导出入口、OOXML Part |
| 4. 文档模型 (SwDoc) | SwDoc、SwNodes、SwNode 类型层次、SwTextNode、表格节点、位置与选择系统 |
| 5. 样式/格式系统 | SwFormat 类层次、属性系统（SwAttrPool/SwAttrSet） |
| 6. 布局引擎 (Frame Tree) | Frame 类层次、SwFlowFrame 跨页机制、MakeAll() 流程、分页决策、Follow 链、表格跨页 |
| 7. 浮动对象体系 | SwAnchoredObject、SwFlyFrame、锚定类型、位置计算、环绕交互 |
| 8. Node-Frame 连接机制 | 观察者模式、Node→Frame / Frame→Node 映射 |
| 9. 渲染流程 | Paint 调用链、文本渲染路径、排版时序 |
| 10. 回调/观察者系统 | SwModify/SwClient 通知机制 |
| 11. oox 共享模块 | OOXML 工具包结构 |
| 12-14. 文件索引与时序图 | 关键文件路径、导入/排版时序图 |

## 快速查找指南

按场景快速定位：

| 我需要... | 查阅章节 |
|-----------|----------|
| 了解 DOCX 导入的完整流程 | 第 2 节 |
| 查找某个 Frame 类的定义和成员 | 第 6.1 节 |
| 理解分页决策逻辑（MoveFwd/MoveBwd） | 第 6.4 节 |
| 找到浮动对象相关的源码文件 | 第 7 节 + 第 12 节文件索引 |
| 定位某个排版计算的 LO 源码位置 | 第 12 节布局引擎文件索引 |
| 理解 SwNode/SwTextNode 的数据结构 | 第 4.3-4.4 节 |

## 相关文件

- `docx/docs/reference/lo_docx_structure.md` — 完整架构参考手册（1470 行）
