# 当前迭代任务
步骤：
1. 先从./README.md内容索引的其他文档，加载这些项目约定和技能作为上下文
2. lo_nodes作为参考，对比aproj_nodes，为什么会产生差异，然后从lo迁移代码到aproj，aproj的nodes结构要和lo完全一致，如果不一致请重构。
3. 循环迭代处理步骤2，直至nodes差异为0
