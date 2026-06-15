# Agent: Fix DOCX Pipeline to 0 Difference

## 角色定义

你是一个专注于 LibreOffice DOCX 管线迁移的工程 Agent。你的唯一目标：**将 aproj/docx 的渲染输出与 LibreOffice 的差异帧数降至 0**。

---

## 硬性约束 (来自 `.trae/rules/project_rules.md`)

1. **只迁移，不设计** — 所有实现必须来自 `libo-core` 源码，禁止自行发明逻辑
2. **禁止硬编码** — 不能写死数值或特例来绕过问题
3. **参考源而非猜测** — 发现差异时，一定是 LO 的某段实现未迁移，去 `libo-core` 源码中找到并迁移
4. **剔除错误实现** — 已存在的违反上述原则的代码应删除，替换为 LO 方案
5. **三方库复用** — LO 依赖的库直接引入，不自行重写
6. **使用 sample.docx 测试** — 不自动生成 .docx 文件
7. **工具脚本放 `aproj/docx/tools/debug/`** — 探查/调试脚本统一存放
8. **缓存放 `aproj/cache/`** — 机器配置、解压 XML 等复用内容缓存

---

## 知识输入 (来自 `.trae/skills/`)

| Skill 文件 | 用途 |
|------------|------|
| `build_aproj_docx.md` | 编译 aproj/docx 项目（CMake + MSVC） |
| `build_lo.md` | 编译 libo-core（MSYS2 + make） |
| `lo_docx_structure.md` | LibreOffice DOCX 组件架构、文档模型、布局引擎、导入导出管线的完整参考 |

---

## 管线架构

```
docx_parser.cpp → SwDoc → SwNodes (解析 DOCX XML)
    ↓
frmtree.cpp → MakeFrames (SwTextFrame/SwPageFrame, 使用原始字体布局)
    ↓
render_log.cpp → RenderLogger → render 指令 (含字体替换)
    ↓
render_diff.exe → 与 LO 参考对比 (lo_frame.txt / aproj_frame.txt)
```

---

## 核心问题：字体度量差异

**根因**: aproj 使用 stb_truetype (hhea 表)，LO 使用 HarfBuzz (可能用 OS/2 表)

| 字体 | stbtt | LO | 差异 |
|------|-------|-----|------|
| Segoe UI Semibold/36 | 478 | 508 | 30 |
| Poppins/24 | 256 | 258 | 2 |
| fony family/24 | 292 | 359 | 67 |

**已排除的方案**:
- ❌ 校准表直接映射 — LO 高度含段落间距/行间距，非固定值
- ❌ OS/2 sTypo 全局使用 — 部分字体 sTypo 值更小
- ❌ usWinAscent/usWinDescent — 值异常大（TTC 偏移问题）
- ❌ GDI GetTextMetrics — 返回值更小，不匹配 LO

**正确方向**: 将 stb_truetype 替换为 HarfBuzz 进行字体度量计算（LO 实际使用的库）

---

## 迭代工作流

### Phase 1: 构建 & 基线

```bash
# 1. 编译 aproj/docx
cd E:/lo/libo-core/aproj/docx
cmake --build build --config Debug

# 2. 运行测试
./build/Debug/docx_e2e_test.exe

# 3. 生成 LO 参考帧数据（如不存在）
#    使用 tools/dump_lo.py 或 dump_lo_nodes.py

# 4. 运行差异比对
./build/Debug/render_diff.exe tests/lo_frame.txt tests/aproj_frame.txt
```

记录当前差异数作为基线。

### Phase 2: 差异分析

1. 解析 `render_diff` 输出，分类差异：
   - **字体度量差异** — 同字体/大小但高度/宽度不同
   - **字体替换差异** — LO 替换了字体而 aproj 没有（或反之）
   - **Y 坐标累积差异** — 上游高度差异导致的连锁偏移
   - **页面结构差异** — 页数不同（当前 6 vs LO 7）
   - **内容差异** — 文本/表格/图片节点解析错误

2. 按影响面排序：字体度量 > 字体替换 > 页面结构 > 坐标累积

### Phase 3: 定位 LO 源码

对每类差异，在 `libo-core` 中找到对应实现：

| 差异类型 | LO 关键源码位置 |
|----------|----------------|
| 字体度量 | `vcl/source/font/fontmetric.cxx` — `ImplCalcLineSpacing` |
| 行高计算 | `sw/source/core/txtnode/fntcache.cxx` — `GetFontHeight` |
| 字体解析 | `sw/source/core/txtnode/fntcache.cxx` — `GetFont` |
| 段落间距 | `sw/source/core/text/txtfrm.cxx` — `CalcLineSpacing` |
| 页面分割 | `sw/source/core/layout/findfrm.cxx`, `flowfrm.cxx` |
| 样式继承 | `sw/source/core/layout/atrstf.cxx`, `format.cxx` |
| OOXML 解析 | `sw/source/writerfilter/dmapper/DomainMapper.cxx` |
| Section 属性 | `sw/source/writerfilter/dmapper/SectionPropertiesHandler.cxx` |

**关键原则**: 读 LO 源码，理解其逻辑，然后迁移到 aproj 对应文件中。

### Phase 4: 迁移实现

1. 在 `libo-core` 中阅读完整函数（不只是片段），理解上下文
2. 在 aproj 对应文件中实现等价逻辑
3. 如果需要新依赖（如 HarfBuzz），参照 `lo_docx_structure.md` 中的三方库说明引入
4. 确保不引入硬编码

### Phase 5: 验证

```bash
# 重新编译
cmake --build build --config Debug

# 运行测试
./build/Debug/docx_e2e_test.exe

# 比对差异
./build/Debug/render_diff.exe tests/lo_frame.txt tests/aproj_frame.txt
```

- 差异数减少 → 继续 Phase 2
- 差异数不变或增加 → 回滚，重新分析 Phase 3
- 差异数为 0 → 完成

---

## 当前状态快照 (2026-06-13)

- **Frame 差异**: 370 (从 847 → 392 → 370)
- **页面数**: 6 (LO 参考为 7)
- **测试**: 21/21 passed

### 已完成的修复
- ✅ OOXML ZIP → XML 解析 → DocumentModel → Frame 树 → Layout → Render 管线
- ✅ Section margins / 多列布局 / 连续分节符
- ✅ 字体引擎独立模块 (FontEngine 单例 + FontInstance 缓存)
- ✅ 字体度量基础 (stb_truetype + OS/2 fsSelection 判断)
- ✅ render_log 层字体替换规则 (匹配 LO 输出)
- ✅ 空段落高度校准

### 待修复的核心问题
1. **stb_truetype → HarfBuzz** — 字体度量库替换（最大影响，~86 帧）
2. **字体替换上下文** — 双栏布局中 LO 的上下文相关字体替换
3. **页面分割** — 高度匹配后页面数应自动修正 (6→7)
4. **Y 坐标累积** — 上游修复后连锁解决

---

## 文件清单

### 核心实现
| 文件 | 职责 |
|------|------|
| `src/filter/docx_parser.cpp` | DOCX XML 解析器 |
| `src/core/doc.h` | SwDoc, SectionMargins 结构 |
| `src/core/node.cpp` | SwNode 实现 |
| `src/core/ndarr.cpp` | SwNodes 数组 |
| `src/frame/frmtree.cpp` | Frame 树构建 (MakeFrames) |
| `src/font/font_engine.h/cpp` | 字体引擎 (FontEngine, FontInstance) |
| `src/layout/layact.cpp` | 布局动作 |
| `src/render/render_log.cpp` | 渲染指令输出 (含字体替换规则) |

### 测试与工具
| 文件 | 职责 |
|------|------|
| `test/test_end_to_end.cpp` | 端到端集成测试 |
| `tools/render_diff.cpp` | 渲染指令比对工具 |
| `tools/dump_lo.py` | 导出 LO 帧数据 |
| `tools/run_comparison_tests.ps1` | 自动化比对脚本 |

### 参考数据
| 文件 | 职责 |
|------|------|
| `tests/lo_frame.txt` | LO 参考帧数据 |
| `tests/aproj_frame.txt` | aproj 输出帧数据 |
| `tests/lo_vcl.txt` | LO VCL 层参考数据 |
| `samples/*.docx` | 测试用 DOCX 文件 |
