# 当前任务：Nodes 结构差异归零

## 加载上下文

开始前先读取以下文件：

1. `aproj/docx/docs/rules/project_rules.md` — 项目规则（必读）
2. `aproj/docx/docs/skills/test_diff_workflow/SKILL.md` — 测试差异工作流（必读）
3. `aproj/docx/docs/reference/lo_docx_structure.md` — LO 架构手册（按需查阅）

## 任务描述

aproj/docx 的 Nodes 结构与 LibreOffice 存在差异。你的任务是逐一消除这些差异，使 Nodes 差异数为 0。

## 执行流程

1. 编译项目：`cd aproj/docx && build.bat`
2. 生成产物：`python test\gen_lo.py && python test\gen_aproj.py`
3. 查看差异：`.\test\diff_node.bat`
4. 对每一条差异：
   - 分析差异原因（哪个节点、哪个字段不一致）
   - 在 `sw/` 中定位 LO 对应的解析/构建逻辑
   - 将缺失的 LO 逻辑迁移到 `aproj/docx/src/` 对应模块
   - 确保迁移的代码与 LO 源码一致（架构、数据结构、算法照搬，不自行设计）
5. 重新编译并验证 → 回到步骤 2
6. 循环直至差异为 0

## 约束

- 遵守 `project_rules.md` 中的所有硬性约束
- 使用 `gen_*.py` 和 `diff_*.bat` 脚本，不要自行调用 exe 或拼接命令
- 临时分析文件放在 `aproj/docx/tools/debug/` 下
- 代码风格与 LO 保持一致
