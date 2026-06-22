# TASK — Frame 差异归零计划

> 最后更新：2026-06-22  
> 测试样本：`samples/sample0.docx`  
> 约束：遵循 [project_rules.md](.cursor/rules/project_rules.md)，所有修复必须迁移 LO 逻辑，禁止自行设计

---

## 1. 当前状态

| 指标 | LO | aproj | 状态 |
|------|----|-------|------|
| Node diff | — | **0** | ✅ PASS（不可劣化） |
| Frame diff | — | **146** | 🔄 进行中（较 148 ↓2） |
| Frame 指令总数 | 207 | 207 | ✅ 结构数量一致 |
| 页数 (PAGE_START) | 7 | 7 | ✅ 一致 |

### 本轮已落地修改（2026-06-22）

| 模块 | 修改 | 效果 |
|------|------|------|
| `font_engine.cpp` | Windows GDI `GetTextWidth`/`GetTextBreak`/`GetCharWidth`；修复 HarfBuzz `x_advance/65536` 回退路径 | ref@9 高度 514→**1028**（LO 1002）；ref@13 711→**946**；结构稳定 207 条 |
| `render_log.cpp` | `fontColor` 默认 16777215；`fontWeight` 默认 144；仅解析 6 位 hex RGB | 消除部分 fontColor 差异 |
| `frmtree.cpp` | 移除 `GetEffectiveTextLineWidths` 对 9000–11000 误扣 1440；栏宽扣 213 | 节内 ~10466 不再被二次扣边距 |
| `layact.cpp` | `InternalAction` 格式化后 `pPage->Validate()` | 消除 `loop limit reached` 死循环 |
| `docx_parser.cpp` | 迁移 `ParseFlyAnchorLayout`/`SetFlyLayout`/`ParseInlineDrawingHeight` | page4 fly **957,55048,5119×3306**；spacer 段高 **3830** |

### 关键发现

1. **HarfBuzz 宽度换算错误**：`x_advance` 未除 `65536`，导致宽度约大 30 倍，段落被算作单行
2. **换行宽度语义**：`CalcBodyTextFrameHorz` 对节内正文已传入 ~10466，**不应**再扣 pgMar；全页宽 11906 亦不应扣 1440（LO 单行为证：ref@8 高度 958）。仅栏宽 ~5232 扣 213
3. **SwLayAction 死循环**：`SwPageFrame::Format` 未设 `mbFrameAreaPositionValid`，`InternalAction` 须在页格式化后 `Validate()`——**已修复**
4. **GDI 高度**：Poppins 24 GDI=285 twips，LO=336——需 HarfBuzz 度量或 LO 字体回退链对齐后再切换

---

## 2. 差异根因分析（对照 LO 源码）

（完整分析见下方各节，因果链不变）

### 2.1 【P0】字体度量 — `GetTextHeight`

| LO 参考 | `vcl/source/font/fontmetric.cxx` → `ImplCalcLineSpacing` |
| aproj | `font_engine.cpp` → HarfBuzz 路径（高度仍偏差 ~19 twips） |
| 典型 | ref@6: 336→355 |

### 2.2 【P0】断行/宽度 — `GetTextWidth` / `GetTextBreak` ✅ 部分修复

| LO 参考 | `vcl` GDI / `sallayout.cxx` |
| aproj | **已改** Windows GDI `GetTextExtentPoint32W` + 二分断行 |
| 残留 | 节内 ~10466 已正确；全页 11906 不扣 pgMar（与 LO 一致） |

### 2.3 【P0】行距 — `CalcRealHeight`

| LO 参考 | `sw/source/core/text/itrform2.cxx:2264` |
| 典型 | ref@109: 空格段 3830→244 |

### 2.4–2.7

段间距、浮动对象、分栏/分节、日志打印 — 见原 TASK 分析，优先级不变。

---

## 3. 后续实施顺序（修订）

### 阶段 A2：换行宽度 + 分页联动 — ✅ 部分完成

| 步骤 | 状态 | 说明 |
|------|------|------|
| A2.1 | ✅ | 移除 9000–11000 误扣 1440；栏宽扣 213；全页 11906 不扣 pgMar |
| A2.2 | ✅ | `layact.cpp` 页格式化后 `pPage->Validate()`，消除死循环 |
| A2.3 | ⏳ | 页内续排节首帧 Y 须跟节顶（`GetSectionFirstLineTop`），须与 Reflow 分页联动一并迁移 |

### 阶段 B：行距与段间距 — ⏳ 待做（本轮试探未收敛）

| 步骤 | 状态 | 说明 |
|------|------|------|
| B.1 | ⏳ | `PROP_LINE_SPACING_SHRINKS_FIRST_LINE` 待接入文档设置 |
| B.2 | ⏳ | `CalcUpperSpace` 须与 fly 前驱帧联动，不能简单拆 height/Y |
| B.3 | ⏳ | `ADD_EXT_LEADING` 须从 `settings.xml` 解析且与分页联动验证 |

**本轮试探结论**：
- ref@6：aproj 355 = HarfBuzz 276 + spaceBefore 79；LO 336 = 258 + 78 → 需 `ADD_EXT_LEADING=false` 但单独切换会引发 page3 分栏结构劣化（diff 151）
- B.2 拆分 spaceBefore 会在 fly 后续段落误加 Y（+79），须迁移 LO `GetPrevFrameForUpperSpaceCalc_`

### 阶段 C：浮动对象 — 🔄 部分完成

| 步骤 | 状态 | 说明 |
|------|------|------|
| C.1 | ✅ | `ParseFlyAnchorLayout` + `doc.SetFlyLayout`（`docx_parser.cpp`） |
| C.2 | ✅ | `ParseInlineDrawingHeight` → `RES_IMAGE_HEIGHT`（spacer 段 3830/3969） |
| C.3 | ⏳ | 栏内 TEXT fly 位置（ref@24-26）；page4 长节 Y 级联（ref@105-113） |

### 阶段 D：分栏/分节

| 步骤 | 操作 | LO 参考 |
|------|------|---------|
| D.1 | `UpdateSectionFrameArea` 单列节聚合 | `sectfrm.cxx` |
| D.2 | 多栏右栏优先流式算法 | `frmtool.cxx` `colfrm.cxx` |
| 验证 | ref@119 Section 宽/高 |

### 阶段 E：日志打印 ✅ 部分完成

- `fontColor`/`fontWeight` 已对齐 LO 格式
- 待做：非空段 Run 级字体（ref@133 Poppins SemiBold 40pt）

---

## 4. 迭代验证流程

```
build.bat → gen_lo.py → gen_aproj.py → diff_node.bat → diff_frame.bat
```

| 关卡 | 要求 |
|------|------|
| Node diff | **0**（不可劣化） |
| Frame 指令数 | **207**（结构不可破坏） |
| Frame diff | 单调下降 |

### 回归锚点

ref@6, @9, @24, @56, @68, @119, @133

---

## 5. 风险备忘

1. **禁止对全页宽 11906 扣 pgMar**（已验证会导致 ref@8 双行、204 帧结构破坏）
2. **GDI 宽度 + 当前换行宽度** 为稳定基线，Frame diff=148
3. `ReflowTextFrameGeometry` 分页逻辑是 Y 级联偏差主因，见 A2.3

---

*最后验证：Node=0, Frame diff=146, 指令=207/207, 页数=7/7*
