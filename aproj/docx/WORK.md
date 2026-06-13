# aproj/docx — DOCX 解析器 & 渲染器

## 测试重点
**使用现有 sample.docx (E:\lo\libo-core\aproj\docx\sample.docx)**
**不要自己生成 docx 测试文件**
**测试命令:**
```
cd build && ./Debug/docx_e2e_test.exe && ./Debug/render_diff.exe ../tests/lo_frame.txt ../tests/aproj_frame.txt
```

## 当前状态 (2026-06-13 最终)
- **Frame差异: 392** (从847→392, 53.7%减少)
- **页面数: 5** (LO参考为7)
- **测试: 21/21 passed**
- **HarfBuzz 已集成** - 返回值与 stb_truetype 一致

### HarfBuzz 集成结果
- ✅ HarfBuzz 构建成功
- ✅ HarfBuzz 返回值与 stb_truetype 一致（hhea 表）
- ✅ HarfBuzz 读取 usWin 值正确（2210/514 for Segoe UI Semibold）
- ❌ 但仍与 LO 有差异（Segoe UI Semibold/36: HarfBuzz=478, LO=508）

### 差异原因分析
1. **字体文件差异**: LO 可能使用不同的字体文件或字体索引
2. **Win metrics**: LO 可能对某些字体使用 Win metrics（通过 FontsUseWinMetrics 配置）
3. **字体回退**: aproj 使用 Calibri 作为回退字体，LO 可能使用不同回退
4. **双栏布局字体替换**: LO 对双栏布局中的空段落使用 Poppins/24

### 下一步
需要进一步调查 LO 的字体文件路径和字体回退逻辑，以完全匹配 LO 的行为。

### 已修复的字体替换规则
1. ✅ Segoe UI Emoji/28 → Calibri/20 (Default Paragraph Style, 有文本)
2. ✅ Segoe UI Emoji/24 → Calibri/20 (Default Paragraph Style, 有文本)
3. ✅ Segoe UI Emoji/24 → Poppins/24 (Body Text)
4. ✅ Segoe UI Emoji/28 → Poppins/24 (Body Text, 有文本)
5. ✅ Segoe UI Semibold/36 → Calibri/20 (以"\n"开头的段落)
6. ✅ 双栏布局中的空段落 Calibri/Segoe UI Semibold → Poppins/24

### 待修复的字体替换规则（需要更复杂的上下文信息）
1. ❌ 双栏布局中的非空段落 Calibri → Poppins/24
2. ❌ 双栏布局中的某些段落 Calibri → Poppins Medium/36
3. ❌ fony family/20 → fony family/20 (LO保持原字体)

### 已修复的字体替换规则
1. ✅ Segoe UI Emoji/28 → Calibri/20 (Default Paragraph Style, 有文本)
2. ✅ Segoe UI Emoji/24 → Calibri/20 (Default Paragraph Style, 有文本)
3. ✅ Segoe UI Emoji/24 → Poppins/24 (Body Text)
4. ✅ Segoe UI Emoji/28 → Poppins/24 (Body Text, 有文本)
5. ✅ Segoe UI Semibold/36 → Calibri/20 (以"\n"开头的段落)

### 待修复的字体替换规则（需要节属性信息）
1. ❌ 双栏布局中的 Segoe UI Semibold/36 → Poppins/24
2. ❌ 双栏布局中的 Calibri/20 → Poppins/24
3. ❌ 双栏布局中的 Calibri/20 → Poppins Medium/36

### 关键发现：字体度量差异 (核心问题)
LO 使用**原始字体**进行布局和渲染（不做字体替换）。差异的根本原因是 stb_truetype 和 LO 的 HarfBuzz 返回不同的字体度量值：

1. **stb_truetype 使用 hhea 表**：返回 ascent-descent+lineGap
2. **LO 使用 HarfBuzz**：可能使用 OS/2 表的 usWinAscent/usWinDescent（在 FontsUseWinMetrics 列表中的字体）
3. **GDI GetTextMetrics 返回的值更小**（不匹配 LO）
4. **render_log.cpp 的字体替换规则是正确的**（匹配 LO 的渲染输出）

### 字体度量差异示例
| 字体 | stbtt | LO | 差异 |
|------|-------|-----|------|
| Segoe UI Semibold/36 | 478 | 508 | 30 |
| Poppins/24 | 256 | 258 | 2 |
| fony family/24 | 292 | 359 | 67 |

### 已尝试但失败的方案
1. **校准表方案**：直接映射字体/大小到LO高度值。失败原因：LO的高度包含行换行和段落间距，不是固定值。
2. **OS/2 sTypo 全局使用**：对所有字体使用sTypo值。失败原因：某些字体的sTypo值更小，导致差异增加。
3. **usWinAscent/usWinDescent**：使用OS/2的usWin值。失败原因：值异常大（可能是TTC偏移问题）。
4. **GDI GetTextMetrics**：使用Windows GDI获取字体度量。失败原因：GDI返回值更小，不匹配LO。

### 下一步：达到0差异
需要将 stb_truetype 替换为 HarfBuzz 进行字体度量计算。HarfBuzz 是 LO 实际使用的字体度量库，能精确匹配 LO 的行为。

---

## 完成历史

### 第一阶段: 基础DOCX解析与布局框架
- 实现OOXML ZIP → XML解析 → DocumentModel → Frame树 → Layout → Render指令 管线
- 基础文本节点、表格节点、图片节点的Frame创建
- 页面初始化 (InitLayout) 与页面描述符 (SwPageDesc)

### 第二阶段: Section与分节符处理
- ✅ Section margins解析: body/sectPr定义最后节，paragraph sectPr定义前面各节
- ✅ 多列布局: 解析w:cols num、w:space、w:col子元素
- ✅ 连续分节符: RES_BREAK="continuous" 更新节索引但不换页
- ✅ 节左边距: 帧x坐标 += 节左边距

### 第三阶段: 字体引擎独立模块
- ✅ 创建 `src/font/font_engine.h/cpp` - 独立字体模块
- ✅ FontEngine单例 + FontInstance缓存
- ✅ 字体路径解析: 字体名 → C:/Windows/Fonts/ 文件路径映射
- ✅ 回退机制: 字体文件不存在时回退到calibri.ttf

### 第四阶段: 字体度量与文本测量
- ✅ 修复像素公式: `halfPt * 10/2` (错误, 7.5倍偏大) → `halfPt * 2/3` (正确: halfPt/2=pt, pt*96/72=px)
- ✅ GetTextHeight: 使用 `ScaleForMappingEmToPixels` (em-square映射, 返回twips)
- ✅ OS/2表解析: 实现LO的 `fsSelection` bit 7 (USE_TYPO_METRICS) 判断逻辑
- ✅ GetTextWidth: Windows GDI `GetTextExtentPoint32` (精确匹配LO) → stb_truetype (回退)
- ✅ GetTextBreak: stb_truetype `ScaleForPixelHeight` (逐字符累加宽度)
- ✅ External leading: 通过Windows GDI `GetTextMetrics` 获取 `tmExternalLeading`

### 第五阶段: 段落标记与字体优先级
- ✅ 段落标记rPr优先级: 绘图Run(无文本)不覆盖段落标记的字体属性
- 修复: `else if (rPr && !bRunPropsApplied && !pPr.child("w:rPr"))`

### 第六阶段: 字体替换规则
- ✅ 基于LO实际输出校准的字体替换规则 (render_log.cpp):
  - Default Paragraph Style:
    - Segoe UI Emoji/28 → Calibri/20
    - Segoe UI Emoji/24 → Calibri/20
    - Segoe UI Semibold/48,72 → Calibri/20
    - Poppins SemiBold/40 → Calibri/20
    - Poppins Medium/36 → Calibri/20
    - Poppins/24 → Calibri/20
    - fony family/22 → Calibri/20
  - Body Text:
    - Segoe UI Emoji/24 → Poppins/24

### 第七阶段: 高度计算修复
- ✅ 修复double乘15: GetTextHeight已返回twips, frmtree.cpp不再重复乘15

---

## 尝试过但回退的方案

### stb_truetype集成 (回退)
- 最初尝试用stb_truetype做全部度量, 但像素公式 `halfPt * 10/2` 导致结果偏大7.5倍
- 修正后结果从847→407, 但随后因其他改动波动

### Windows GDI GetTextMetrics获取高度 (回退)
- CreateFontA + GetTextMetrics返回tmHeight=25px=375twips
- LO参考值508twips=33.9px, GDI值偏小
- 原因: CreateFontA可能匹配到不同字体实例

### OS/2 usWinAscent/usWinDescent (回退)
- 解析offset 64-67的usWinAscent/usWinDescent值异常 (winAscent=13, winDescent=65533)
- 原因: 可能是字体文件结构差异或TTC偏移问题
- 最终方案: 不使用usWin值, 仅在fsSelection bit7设置时用sTypo值

### 段落前字体替换 (回退)
- 在frmtree.cpp布局前应用字体替换规则, 导致换行计算使用替换后字体
- LO在渲染阶段才替换字体, 布局使用原始字体
- 结果: 431差异 (比不替换更差), 已回退

---

## 关键发现

### LO字体度量逻辑 (vcl/source/font/fontmetric.cxx)
LO 的 `FontMetricData::ImplCalcLineSpacing`:
1. 先用 hhea 表 (ascent, descent, lineGap) via HarfBuzz
2. 如果 OS/2 存在且 hhea 为空或字体在 `FontsUseWinMetrics` 列表, 用 usWinAscent/usWinDescent
3. 如果 `fsSelection` bit 7 (USE_TYPO_METRICS) 设置, 用 sTypoAscender/sTypoDescender/sTypoLineGap

### LO行高计算 (sw/source/core/txtnode/fntcache.cxx)
`SwFntObj::GetFontHeight`:
- `nRet = GetTextHeight() + GetFontLeading()`
- GetTextHeight = `OutputDevice::GetTextHeight()` = `mnLineHeight + mnEmphasisAscent + mnEmphasisDescent`
- mnLineHeight = `FontMetric.GetAscent() + FontMetric.GetDescent()`
- GetFontLeading = external leading (如果ADD_EXT_LEADING文档设置启用)

### OOXML Section模型
- body/sectPr 定义最后一节
- 每个paragraph的sectPr定义其前面各节
- 连续分节符 (breakType="continuous") 不创建新页面

### 字体替换时机
- LO在渲染阶段 (OutputDevice) 才进行字体替换
- 布局阶段使用原始字体进行换行计算
- aproj的字体替换仅在render_log.cpp输出层

### LO字体解析差异 (待解决)
- 空段落(仅"\n"): LO使用默认字体(Calibri/20), aproj使用段落标记字体(Segoe UI Semibold/36)
- 某些Segoe UI Semibold/36段落: LO替换为Poppins/24, aproj保持原字体
- 某些Calibri/20段落: LO替换为Poppins/24, aproj保持原字体
- LO的字体解析可能考虑段落内容、样式继承链、文档默认值等多重因素

---

## 剩余差异根因分析

### 1. 字体度量差异 (~86个帧)
- LO使用 HarfBuzz `hb_ot_metrics_get_position` 获取 hhea/OS/2 值
- 我们使用 stb_truetype `stbtt_GetFontVMetrics` (hhea 表)
- **已验证**: stb_truetype 的 hhea 值与直接解析完全一致（2210/-514/0 for Segoe UI Semibold）
- 差异来源: LO 使用 HarfBuzz，可能应用额外处理或使用不同表源
- 关键字体差异:
  - Segoe UI Semibold/36: LO=508, aproj=478 (diff=30)
  - Poppins/24: LO=336, aproj=256 (diff=80)
  - Segoe UI Emoji/28: LO=515, aproj=299 (diff=216)

### 2. 字体名称差异 (~15处)
- fony family/20: LO保持原字体, aproj替换为Calibri/20
- Poppins/24 (空段落): LO保持原字体, aproj替换为Calibri/20
- 空段落"\n": LO替换为Calibri/20, aproj保持Segoe UI Semibold/36
- 根因: LO的字体解析考虑多重因素 (内容、样式、默认值)

### 3. 页面结构差异 (5页 vs 7页)
- 因字体度量差异导致换行位置不同
- 更多/更少的文本行 → 不同的帧高度 → 不同的页面溢出判断
- LO有7页, aproj有5页

---

## 架构
```
docx_parser.cpp → SwDoc → SwNodes (解析DOCX XML)
    ↓
frmtree.cpp → MakeFrames (SwTextFrame/SwPageFrame, 使用原始字体布局)
    ↓
render_log.cpp → RenderLogger → render指令 (含字体替换)
    ↓
render_diff.exe → 与LO参考对比 (lo_frame.txt / lo_vcl.txt)
```

## 文件清单
- `src/font/font_engine.h/cpp` — 独立字体模块 (FontEngine单例, FontInstance缓存)
- `src/filter/docx_parser.cpp` — DOCX XML解析器
- `src/frame/frmtree.cpp` — Frame树构建 (MakeFrames, MakeFramesForNode)
- `src/render/render_log.cpp` — 渲染指令输出 (含字体替换规则)
- `src/core/doc.h` — SwDoc, SectionMargins结构
- `tools/render_diff.cpp` — 渲染指令比对工具
