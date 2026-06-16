# 当前迭代任务
* 先从./README.md加载项目约定和技能
* 然后，apro_nodes.txt和lo_nodes.txt差异很大，首先确认lo生成nodes结构是否完全，如果不完整，先补充完整。
* 再对比aproj_nodes，为什么会产生差异，然后从lo迁移代码到aproj，aproj的nodes结构要和lo完全一致，如果不一致请重构。
* 循环迭代处理，直至nodes差异为0