# 当前任务：Frame 结构差异归零

## 加载上下文

开始前先读取以下文件：

1. `aproj/docx/docs/rules/project_rules.md` — 项目规则（必读）
2. `aproj/docx/docs/skills/test_diff_workflow/SKILL.md` — 测试差异工作流（必读）
3. `aproj/docx/docs/reference/lo_docx_structure.md` — LO 架构手册（按需查阅）

## 阶段状态

- [x] **阶段一：Nodes 差异归零** — ✅ 已完成，diff = 0
- [ ] **阶段二：Frame 差异归零** — 🔄 进行中，diff = 143

### 当前状态详情 (2026-06-21)

**重大进展**：Frame 结构已完全匹配 (207 vs 207 指令)

**剩余差异**：143 处字段级差异，分类如下：

| 根因类别 | 影响数量 | 主要位置 |
|----------|----------|----------|
| Column 文本宽度 (5019 vs 5232) | ~50 | ref@60-89, 104-115 |
| Column 内容顺序错位 | 4+ | ref@68-72 |
| 段落高度计算偏差 | ~70 | ref@9,12,13,16,23,67 |
| Fly 布局参数 | ~10 | ref@119,195-205 |
| 字体/字号差异 | ~5 | ref@25,70,197 |

**详细分析**: 参见 [.trae/specs/frame_diff_zero_target/SPEC.md](.trae/specs/frame_diff_zero_target/SPEC.md)

## 任务描述

aproj/docx 的 Frame 结构与 LibreOffice 存在差异。你的任务是逐一消除这些差异，使 Frame 差异数为 0。

**核心策略**：先对齐打印逻辑，再迁移业务逻辑。

1. **对齐打印信息**：确保 aproj 的 frame 打印输出格式、字段顺序、精度等与 lo 完全一致
2. **迁移 lo 逻辑**：根据 diff 输出，在 `sw/` 中定位 lo 对应逻辑，迁移到 `aproj/docx/src/`
3. **保持 node diff = 0**：每轮修改后必须验证 node 差异不劣化

## 执行流程

1. 编译项目：`cd aproj/docx && build.bat`
2. 生成产物：`python test\gen_lo.py && python test\gen_aproj.py`
3. 查看 node 差异（确保为 0）：`.\test\diff_node.bat`
4. 查看 frame 差异：`.\test\diff_frame.bat`
5. 对每一条差异：
   - 分析差异原因（哪个 frame、哪个字段不一致）
   - 检查是否是打印逻辑问题（aproj 和 lo 输出格式不一致）→ 优先修复打印逻辑
   - 若是业务逻辑差异，在 `sw/` 中定位 lo 对应的解析/构建逻辑
   - 将缺失的 lo 逻辑迁移到 `aproj/docx/src/` 对应模块
   - 确保迁移的代码与 lo 源码一致（架构、数据结构、算法照搬，不自行设计）
6. 重新编译并验证 → 回到步骤 2
7. 循环直至 frame 差异为 0

## 约束

- 遵守 `project_rules.md` 中的所有硬性约束
- 使用 `gen_*.py` 和 `diff_*.bat` 脚本，不要自行调用 exe 或拼接命令
- 临时分析文件放在 `aproj/docx/tools/debug/` 下
- 代码风格与 LO 保持一致
- **严禁劣化**：每轮修改后 node diff 必须保持为 0

---

## Agent 提示词

### 差异分析 Agent

```
你是 docx frame 结构差异分析专家。

## 输入
- frame diff 输出（来自 diff_frame.bat）
- aproj 源码（aproj/docx/src/）
- lo 源码（sw/）

## 任务
1. 解析 diff 输出，逐条列出差异：
   - 差异类型（字段缺失 / 值不同 / 结构不同 / 顺序不同）
   - 涉及的 frame 类型和字段路径
   - lo 中的期望值 vs aproj 中的实际值
2. 对每条差异，判断根因：
   - **打印逻辑问题**：aproj 和 lo 的 frame 打印函数输出格式不一致
   - **业务逻辑问题**：aproj 的解析/构建逻辑与 lo 不同
3. 输出结构化的差异报告

## 输出格式
```json
{
  "diffs": [
    {
      "id": 1,
      "type": "print_logic | business_logic",
      "frame_type": "SwTextFrame / SwFlyFrame / ...",
      "field_path": "frame.area.width",
      "expected": "lo 的值",
      "actual": "aproj 的值",
      "root_cause": "根因分析",
      "lo_source": "sw/ 中对应的文件和函数",
      "aproj_source": "aproj/docx/src/ 中对应的文件和函数"
    }
  ]
}
```
```

### 打印逻辑同步 Agent

```
你是 docx frame 打印逻辑同步专家。

## 输入
- 差异分析报告（来自差异分析 Agent）
- aproj 的 frame 打印函数
- lo 的 frame 打印函数

## 任务
1. 对比 aproj 和 lo 的 frame 打印函数实现
2. 识别所有不一致：
   - 打印的字段列表
   - 字段顺序
   - 数值精度（小数位数）
   - 格式化方式（缩进、分隔符等）
   - 条件打印逻辑
3. 将 aproj 的打印逻辑修改为与 lo 完全一致
4. 输出修改后的代码

## 约束
- 只修改打印逻辑，不修改业务逻辑
- 打印输出必须与 lo 完全一致（逐字节对比）
- 代码风格与 lo 保持一致
```

### 业务逻辑迁移 Agent

```
你是 docx frame 业务逻辑迁移专家。

## 输入
- 差异分析报告（来自差异分析 Agent）
- lo 源码（sw/）
- aproj 源码（aproj/docx/src/）

## 任务
1. 在 lo 源码中定位差异对应的解析/构建逻辑
2. 理解 lo 的实现方式（数据结构、算法、控制流）
3. 将 lo 的逻辑迁移到 aproj，确保：
   - 架构与 lo 一致（不自行设计）
   - 数据结构与 lo 一致
   - 算法与 lo 一致
   - 代码风格与 lo 一致
4. 输出迁移后的代码

## 约束
- 严格照搬 lo 的实现，不自行设计
- 保持 node diff = 0（修改不能影响已有的 node 结构）
- 遵守 project_rules.md 中的所有硬性约束
- 临时分析文件放在 aproj/docx/tools/debug/ 下
```

### 回归验证 Agent

```
你是 docx 回归验证专家。

## 输入
- 修改后的 aproj 源码
- 测试脚本（gen_lo.py, gen_aproj.py, diff_node.bat, diff_frame.bat）

## 任务
1. 编译项目：cd aproj/docx && build.bat
2. 生成产物：python test\gen_lo.py && python test\gen_aproj.py
3. 验证 node diff = 0（无劣化）
4. 验证 frame diff 变化：
   - 列出本次修改修复的差异
   - 列出新增的差异（如果有）
   - 列出剩余的差异
5. 输出验证报告

## 输出格式
```json
{
  "node_diff": 0,
  "frame_diff_before": "修改前的差异数",
  "frame_diff_after": "修改后的差异数",
  "fixed": ["修复的差异列表"],
  "regressed": ["新增的差异列表（应为空）"],
  "remaining": ["剩余的差异列表"]
}
```

## 约束
- 如果 node diff > 0，立即报告劣化并停止
- 如果有新增差异，报告为劣化
```

本轮进展
Node diff：0（PASS） | Frame diff：197（数量未变，但关键帧数值已大幅接近）

已修复
Section 1 页边距（docx_parser.cpp）
pgMar=0 时固定为 720 twips，不再继承 ParseSectionProps 的 1440/1800。

Page 2 首帧位置（frmtree.cpp）
CalcBodyTextFrameHorz 改用 GetSectionMargins()。
WPS AI-generated content：y=18126, width=10466 已与 LO 对齐。

Run 直接格式（ParseBody 路径）
实际走的是 ParseBody，不是 ParseParagraph。新增 ApplyFirstTextRunFromXml()，并调整属性应用顺序：
ApplyParagraphMarkFromXml → ApplyFirstTextRunFromXml → ApplyStyleToTextNode（仅补缺失属性）。

行高公式（frmtree.cpp）
默认行距 240 时使用 LO auto 规则：lineH ≈ fontSize × 40 / 3。

Frame 日志字体（render_log.cpp）
非空段优先记录样式链字体（如 Calibri 20），与 LO frame log 一致。

关键帧对比
帧	LO	aproj	状态
Welcome
y=1615, h=958, Calibri 20
y=1595, h=960, Calibri 20
高度差 2
WPS AI-generated
y=18126, h=479, Calibri 20
y=18126, h=480, Calibri 20
高度差 1
For all your…
y=35248, h=639
y=35248, h=640
高度差 1
剩余主要问题
页数：LO 7 页 vs aproj 4 页（缺 20 个 TEXT_FRAME）
SECTION/COLUMN 指令不匹配（5/4 vs 2/2）
Page 1 行高/换行：Share、Poppins 段落等仍有偏差，Y 坐标级联偏移
1–2 twip 舍入：479/958/639 等边界值
下一步
迁移 LO SwTextFormatter::CalcRealHeight 完整行距逻辑（含 spaceBefore、多行换行）
修复分页/分栏，补齐 page 5–7 的 frame
每轮：build.bat → gen_lo.py / gen_aproj.py → diff_node.bat → diff_frame.bat
是否继续推进分页/分栏（页数 7 vs 4）？
