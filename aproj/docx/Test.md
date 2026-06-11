# 渲染指令比对测试方案

## 核心目标

确保 `aproj/docx` 的渲染指令输出与 LibreOffice **完全一致**。两者必须使用**同一套渲染指令记录逻辑**，而不是各自独立实现。

---

## 一、架构设计：共享渲染指令层

### 1.1 问题分析

当前状态：
- **aproj/docx**：`RenderLogger` 遍历 Frame 树，记录 `PAGE_START / TEXT_FRAME / TABLE_FRAME / TEXT` 等指令
- **LibreOffice**：`PaintSwFrame()` 链将像素绘制到 `OutputDevice`，没有渲染指令记录机制

两者使用不同的代码路径，无法保证一致性。

### 1.2 解决方案：统一渲染指令接口

在 LibreOffice 中植入与 aproj/docx 相同的渲染指令记录器，拦截 `PaintSwFrame()` 调用链，输出结构化渲染指令。aproj/docx 直接复用同一份头文件定义。

```
┌─────────────────────────────────────────────────────────────────┐
│                    共享渲染指令定义                                │
│  render_instruction.h  (独立头文件，无外部依赖)                    │
│  - RenderInstructionType 枚举                                    │
│  - RenderInstruction 结构体                                      │
│  - RenderInstructionSink 接口 (纯虚类)                            │
└──────────────┬──────────────────────────────┬────────────────────┘
               │                              │
    ┌──────────▼──────────┐       ┌───────────▼───────────┐
    │   LibreOffice        │       │   aproj/docx          │
    │                      │       │                       │
    │ SwPaintEventListener │       │ RenderLogger          │
    │ 实现 Sink 接口       │       │ 实现 Sink 接口        │
    │                      │       │                       │
    │ 拦截点:              │       │ 拦截点:               │
    │ SwTextFrame::        │       │ RenderLogger::        │
    │   PaintSwFrame()     │       │   LogFrameTree()      │
    │ SwTabFrame::         │       │                       │
    │   PaintSwFrame()     │       │                       │
    └──────────┬───────────┘       └───────────┬───────────┘
               │                               │
               ▼                               ▼
    lo_render_instructions.txt    aproj_render_instructions.txt
               │                               │
               └─────────── diff ──────────────┘
```

---

## 二、共享渲染指令格式

### 2.1 文件：`render_instruction.h`

这是 LibreOffice 和 aproj/docx 共同包含的头文件，定义渲染指令的结构。

```cpp
#pragma once
// 共享渲染指令定义 — LibreOffice 和 aproj/docx 共用
// 零外部依赖，纯 POD 结构

#include <cstdint>
#include <string>
#include <vector>

// 渲染指令类型
enum class RenderCmdType : uint8_t {
    PAGE_START,      // 页面开始
    PAGE_END,        // 页面结束
    TEXT_FRAME,      // 文本段落 (整段)
    TEXT_LINE,       // 单行文本
    TEXT_RUN,        // 文本片段 (同一字体/样式的连续文本)
    TABLE_FRAME,     // 表格
    TABLE_ROW,       // 表格行
    TABLE_CELL,      // 表格单元格
    IMAGE_FRAME,     // 图片
    SECTION_FRAME,   // 节
    RECT,            // 矩形 (背景/边框)
    LINE,            // 线段 (分隔线)
};

// 渲染指令数据
struct RenderInstruction {
    RenderCmdType type;
    int pageNum;
    int x, y;           // 位置 (twips)
    int width, height;  // 尺寸 (twips)

    // 文本相关
    const char* text;       // 文本内容 (UTF-8, 可能为 nullptr)
    int textLen;            // 文本长度
    const char* fontName;   // 字体名 (可能为 nullptr)
    int fontSize;           // 字号 (半点)
    uint32_t fontColor;     // 字体颜色 (RGB)
    uint8_t fontWeight;     // 字重 (100-900, 400=normal, 700=bold)
    uint8_t fontItalic;     // 斜体 (0/1)
    uint8_t underline;      // 下划线类型
    uint8_t strikeout;      // 删除线类型

    // 段落相关
    const char* styleName;  // 段落样式名 (可能为 nullptr)
};

// 渲染指令接收接口 (纯虚类)
class RenderInstructionSink {
public:
    virtual ~RenderInstructionSink() = default;
    virtual void OnInstruction(const RenderInstruction& inst) = 0;
};
```

### 2.2 指令输出格式 (文本)

每条指令一行，字段用 `\t` 分隔：

```
PAGE_START	1	11906	16838
TEXT_FRAME	1	1800	1440	8306	400	"Hello World"	Arial	22	0	400	0	0	0	Normal
TEXT_LINE	1	1800	1440	8306	264	"Hello World"	Arial	22	0	400	0	0	0	Normal
TEXT_RUN	1	1800	1440	5040	264	"Hello"	Arial	22	0	400	0	0	0	Normal
TEXT_RUN	1	6840	1440	3266	264	" World"	Arial	22	0	400	0	0	0	0
TABLE_FRAME	1	1800	2000	8306	2000
TABLE_ROW	1	1800	2000	8306	600
TABLE_CELL	1	1800	2000	2768	600
PAGE_END	1
```

字段说明：

| 字段 | 含义 |
|------|------|
| 指令类型 | `PAGE_START` / `TEXT_FRAME` / `TEXT_LINE` / `TEXT_RUN` / ... |
| 页码 | 从 1 开始 |
| x, y | 左上角位置 (twips) |
| width, height | 尺寸 (twips) |
| text | UTF-8 文本，用双引号包裹 |
| fontName | 字体族名 |
| fontSize | 字号 (半点，如 22 = 11pt) |
| fontColor | RGB 十六进制 (如 FF0000) |
| fontWeight | 400=正常, 700=粗体 |
| fontItalic | 0=正常, 1=斜体 |
| underline | 0=无, 1=单线, 2=双线, ... |
| strikeout | 0=无, 1=单线, ... |
| styleName | 段落样式名 |

---

## 三、LibreOffice 侧植入方案

### 3.1 植入点选择

LibreOffice 的渲染链：

```
SwRootFrame::PaintSwFrame()           paintfrm.cxx:3225
  → SwPageFrame::PaintSwFrame()       (遍历页面)
    → SwLayoutFrame::PaintSwFrame()   paintfrm.cxx:3672 (遍历子 Frame)
      → SwTextFrame::PaintSwFrame()   frmpaint.cxx:659  (绘制文本)
      → SwTabFrame::PaintSwFrame()    tabfrm.cxx        (绘制表格)
```

**关键植入点**：

| 植入点 | 文件 | 记录什么 |
|--------|------|----------|
| `SwRootFrame::PaintSwFrame` | paintfrm.cxx | 遍历页面时记录 `PAGE_START` / `PAGE_END` |
| `SwTextFrame::PaintSwFrame` | frmpaint.cxx | 记录 `TEXT_FRAME` (段落级) |
| `SwTextFrame::VisitPortions` | txtfrm.cxx | 遍历行和 Portion，记录 `TEXT_LINE` / `TEXT_RUN` |
| `SwTabFrame::PaintSwFrame` | tabfrm.cxx | 记录 `TABLE_FRAME` / `TABLE_ROW` / `TABLE_CELL` |

### 3.2 实现方案：SwPaintEventListener 类

在 `sw/source/core/layout/` 下新增文件：

**`sw/source/core/inc/paint_listener.hxx`**
```cpp
#pragma once
// 渲染指令监听器 — 拦截 PaintSwFrame 调用，输出结构化指令
// 使用 render_instruction.h 共享定义

#include "render_instruction.h"
#include <fstream>
#include <memory>

class SwFrame;
class SwTextFrame;
class SwTabFrame;
class SwRootFrame;

class SwPaintEventListener : public RenderInstructionSink {
public:
    // 单例访问
    static SwPaintEventListener& Get();

    // 开始/结束记录
    void StartLog(const std::string& filePath);
    void EndLog();
    bool IsLogging() const { return m_bLogging; }

    // RenderInstructionSink 实现
    void OnInstruction(const RenderInstruction& inst) override;

    // 高级接口 — 由 PaintSwFrame 调用
    void OnPageStart(int pageNum, int width, int height);
    void OnPageEnd(int pageNum);
    void OnTextFrame(SwTextFrame* pFrame);
    void OnTableFrame(SwTabFrame* pFrame);

    // 写入文件
    void Flush();

private:
    SwPaintEventListener() = default;

    std::vector<RenderInstruction> m_aInstructions;
    std::ofstream m_File;
    bool m_bLogging = false;
};
```

**`sw/source/core/layout/paint_listener.cxx`**
```cpp
#include "paint_listener.hxx"
#include "txtfrm.hxx"
#include "tabfrm.hxx"
#include "frame.hxx"
#include "ndtxt.hxx"
#include "swfont.hxx"

SwPaintEventListener& SwPaintEventListener::Get() {
    static SwPaintEventListener s_Instance;
    return s_Instance;
}

void SwPaintEventListener::StartLog(const std::string& filePath) {
    m_File.open(filePath);
    m_bLogging = true;
    m_aInstructions.clear();
}

void SwPaintEventListener::EndLog() {
    Flush();
    m_File.close();
    m_bLogging = false;
}

void SwPaintEventListener::OnInstruction(const RenderInstruction& inst) {
    m_aInstructions.push_back(inst);
    // 实时写入文件
    if (m_bLogging && m_File.is_open()) {
        WriteInstruction(m_File, inst);
    }
}

void SwPaintEventListener::OnPageStart(int pageNum, int width, int height) {
    if (!m_bLogging) return;
    RenderInstruction inst = {};
    inst.type = RenderCmdType::PAGE_START;
    inst.pageNum = pageNum;
    inst.width = width;
    inst.height = height;
    OnInstruction(inst);
}

void SwPaintEventListener::OnTextFrame(SwTextFrame* pFrame) {
    if (!m_bLogging || !pFrame) return;

    // 获取 Frame 几何
    const SwRect& rArea = pFrame->getFrameArea();
    const SwRect& rPrt = pFrame->getFramePrintArea();

    // 获取文本节点
    SwTextNode* pNode = pFrame->GetTextNode();
    if (!pNode) return;

    // 获取字体信息
    const SwAttrSet& rAttrSet = pNode->GetSwAttrSet();
    // ... 提取 fontName, fontSize, color, bold, italic ...

    RenderInstruction inst = {};
    inst.type = RenderCmdType::TEXT_FRAME;
    inst.pageNum = pFrame->FindPageFrame()->GetPhyPageNum();
    inst.x = rArea.Left();
    inst.y = rArea.Top();
    inst.width = rArea.Width();
    inst.height = rArea.Height();
    // ... 填充文本和字体字段 ...
    OnInstruction(inst);
}
```

### 3.3 植入 PaintSwFrame

在 `SwTextFrame::PaintSwFrame()` 中添加监听器调用：

```cpp
// frmpaint.cxx — SwTextFrame::PaintSwFrame()
void SwTextFrame::PaintSwFrame( ... )
{
    // --- 新增：渲染指令记录 ---
    if (SwPaintEventListener::Get().IsLogging()) {
        SwPaintEventListener::Get().OnTextFrame(this);
    }
    // --- 原有绘制逻辑 ---
    ...
}
```

类似地在 `SwRootFrame::PaintSwFrame()` 中记录 `PAGE_START` / `PAGE_END`。

### 3.4 通过环境变量控制

使用环境变量 `SW_RENDER_LOG` 控制是否启用渲染指令记录：

```cpp
// 在 SwRootFrame 构造函数或初始化时
const char* logPath = getenv("SW_RENDER_LOG");
if (logPath) {
    SwPaintEventListener::Get().StartLog(logPath);
}
```

使用方式：
```bash
SW_RENDER_LOG=render_instructions.txt soffice --headless sample.docx
```

---

## 四、aproj/docx 侧适配方案

### 4.1 修改 RenderLogger

将 `RenderLogger` 改为实现 `RenderInstructionSink` 接口：

```cpp
// aproj/docx/src/render/render_log.h
#include "render_instruction.h"  // 共享头文件

class RenderLogger : public RenderInstructionSink {
public:
    void OnInstruction(const RenderInstruction& inst) override;

    // 保留原有高级接口
    void LogFrameTree(SwRootFrame* pRoot);

    // 写入文件 — 与 LibreOffice 相同格式
    void WriteToFile(const std::string& filePath);
};
```

### 4.2 修改 LogFrameTree

`LogFrameTree()` 遍历 Frame 树时，输出与 LibreOffice 完全相同的指令格式：

```cpp
void RenderLogger::LogFrameTree(SwRootFrame* pRoot) {
    if (!pRoot) return;

    int pageNum = 1;
    SwPageFrame* pPage = pRoot->GetLastPage();
    while (pPage) {
        // PAGE_START
        RenderInstruction inst = {};
        inst.type = RenderCmdType::PAGE_START;
        inst.pageNum = pageNum;
        inst.width = pPage->getFrameArea().Width();
        inst.height = pPage->getFrameArea().Height();
        OnInstruction(inst);

        // 遍历页面内容
        LogFrameRecursive(pPage, pageNum);

        // PAGE_END
        inst = {};
        inst.type = RenderCmdType::PAGE_END;
        inst.pageNum = pageNum;
        OnInstruction(inst);

        pPage = pPage->GetPrevPage();
        ++pageNum;
    }
}
```

### 4.3 指令输出格式对齐

关键：aproj/docx 的 `WriteToFile()` 必须使用与 LibreOffice 完全相同的格式和字段顺序。

```cpp
void WriteInstruction(std::ostream& out, const RenderInstruction& inst) {
    switch (inst.type) {
        case RenderCmdType::PAGE_START:
            out << "PAGE_START\t" << inst.pageNum << "\t"
                << inst.width << "\t" << inst.height << "\n";
            break;
        case RenderCmdType::TEXT_FRAME:
            out << "TEXT_FRAME\t" << inst.pageNum << "\t"
                << inst.x << "\t" << inst.y << "\t"
                << inst.width << "\t" << inst.height << "\t"
                << "\"" << (inst.text ? inst.text : "") << "\"\t"
                << (inst.fontName ? inst.fontName : "") << "\t"
                << inst.fontSize << "\t"
                << inst.fontColor << "\t"
                << (int)inst.fontWeight << "\t"
                << (int)inst.fontItalic << "\t"
                << (int)inst.underline << "\t"
                << (int)inst.strikeout << "\t"
                << (inst.styleName ? inst.styleName : "") << "\n";
            break;
        // ... 其他指令类型
    }
}
```

---

## 五、比对测试流程

### 5.1 整体流程

```
sample.docx
    │
    ├─→ LibreOffice (headless)
    │   SW_RENDER_LOG=lo_render.txt soffice --headless sample.docx
    │   → lo_render.txt
    │
    ├─→ aproj/docx
    │   docx_e2e_test sample.docx
    │   → aproj_render.txt
    │
    └─→ 比对工具
        render_diff lo_render.txt aproj_render.txt
        → diff_report.txt
```

### 5.2 比对工具 `render_diff`

```
用法: render_diff <reference.txt> <test.txt>

输出:
  - 逐行比对
  - 忽略空白差异
  - 数值字段允许误差 (如位置 ±10 twips)
  - 文本内容精确匹配
  - 统计: 总指令数、匹配数、差异数
  - 差异报告: 每条差异的详细信息
```

比对规则：

| 字段 | 比对方式 | 容差 |
|------|----------|------|
| 指令类型 | 精确匹配 | 0 |
| 页码 | 精确匹配 | 0 |
| x, y, width, height | 数值匹配 | ±10 twips (约 0.05mm) |
| 文本内容 | 精确匹配 | 0 |
| 字体名 | 精确匹配 | 0 |
| 字号 | 精确匹配 | 0 |
| 颜色 | 精确匹配 | 0 |
| 粗体/斜体 | 精确匹配 | 0 |
| 样式名 | 精确匹配 | 0 |

### 5.3 分层验证策略

不是一次性比对所有内容，而是分层逐步验证：

#### Layer 1: 文档模型 (SwNodes)

```
LibreOffice:  Shift+F12 → nodes.xml
aproj/docx:   DumpNodesXml() → nodes_dump.xml

比对: 节点数量、节点类型、文本内容
```

验证点：
- [ ] 节点总数一致
- [ ] 每个 SwTextNode 的文本内容一致
- [ ] 表格结构 (SwTableNode) 一致
- [ ] 样式引用一致

#### Layer 2: Frame 树结构

```
LibreOffice:  F12 (SW_DEBUG) → layout.xml
aproj/docx:   DumpFrameTreeXml() → frmtree_dump.xml

比对: Frame 类型、层级关系、几何尺寸
```

验证点：
- [ ] 页面数量一致
- [ ] 每页的 Frame 数量一致
- [ ] Frame 类型序列一致 (TextFrame, TabFrame, ...)
- [ ] Frame 几何 (位置/尺寸) 一致

#### Layer 3: 排版结果

```
LibreOffice:  VisitPortions() → portions.txt
aproj/docx:   TextFormatter → line_breaks.txt

比对: 行数、每行的文本范围、行高
```

验证点：
- [ ] 每段的行数一致
- [ ] 每行的文本范围 (startPos, endPos) 一致
- [ ] 行高一致
- [ ] 分页点一致

#### Layer 4: 渲染指令 (最终比对)

```
LibreOffice:  SwPaintEventListener → lo_render.txt
aproj/docx:   RenderLogger → aproj_render.txt

比对: 完整渲染指令序列
```

验证点：
- [ ] 指令总数一致
- [ ] 指令类型序列一致
- [ ] 每条指令的所有字段一致

### 5.4 差异分类

比对发现的差异分为以下类别：

| 类别 | 说明 | 处理方式 |
|------|------|----------|
| **解析差异** | SwNodes 不一致 | 修复 DocxParser |
| **布局差异** | Frame 树不一致 | 修复 MakeFrames / InitLayout |
| **排版差异** | 行数/断行不一致 | 修复 TextFormatter / SwLayAction |
| **渲染差异** | 渲染指令不一致 | 修复 RenderLogger |
| **已知差异** | 预期的简化差异 | 记录到 known_diffs.txt |

---

## 六、测试用例

### 6.1 基础测试集

| 测试文件 | 验证重点 |
|----------|----------|
| `sample.docx` | 综合测试：段落、表格、图片、样式 |
| `simple_paragraph.docx` | 单段落纯文本 |
| `multi_paragraph.docx` | 多段落，不同样式 |
| `table_2x3.docx` | 简单表格 |
| `bold_italic.docx` | 字符格式 |
| `page_break.docx` | 硬分页 |
| `headers.docx` | 标题样式层级 |

### 6.2 生成测试用例

```bash
# 使用 LibreOffice 生成参考渲染指令
SW_RENDER_LOG=tests/ref_simple_paragraph.txt soffice --headless tests/simple_paragraph.docx

# 使用 aproj/docx 生成测试渲染指令
docx_e2e_test tests/simple_paragraph.docx
mv render_output.txt tests/out_simple_paragraph.txt

# 比对
render_diff tests/ref_simple_paragraph.txt tests/out_simple_paragraph.txt
```

### 6.3 自动化测试脚本

```powershell
# run_comparison_tests.ps1
# 遍历所有测试用例，生成比对报告

$testDir = "aproj/docx/tests"
$results = @()

foreach ($docx in Get-ChildItem "$testDir/*.docx") {
    $name = $docx.BaseName

    # LibreOffice 参考输出
    $refFile = "$testDir/ref_$name.txt"
    $env:SW_RENDER_LOG = $refFile
    & soffice --headless $docx.FullName
    Remove-Item Env:SW_RENDER_LOG

    # aproj 输出
    $outFile = "$testDir/out_$name.txt"
    & docx_e2e_test $docx.FullName
    Move-Item render_output.txt $outFile -Force

    # 比对
    $diff = & render_diff $refFile $outFile
    $results += [PSCustomObject]@{
        Test = $name
        Status = if ($diff) { "FAIL" } else { "PASS" }
        Differences = ($diff | Measure-Object).Count
    }
}

$results | Format-Table
```

---

## 七、已知差异与容错

### 7.1 预期差异

以下差异是 aproj/docx 简化设计的预期结果，不算测试失败：

| 差异 | 原因 | 处理 |
|------|------|------|
| 图片 Frame 缺失 | aproj 尚未实现图片解析 | 记录到 known_diffs.txt，后续补充 |
| 页眉/页脚缺失 | aproj 尚未实现页眉页脚排版 | 同上 |
| 浮动对象缺失 | aproj 尚未实现 SwFlyFrame | 同上 |
| 脚注/尾注缺失 | aproj 尚未实现 | 同上 |
| 字体度量微小差异 | stb_truetype vs 系统字体引擎 | 允许 ±10 twips 误差 |
| 文本换行差异 | 简化换行算法 vs LibreOffice 完整算法 | 逐步对齐 |

### 7.2 差异报告格式

```
=== Render Comparison Report ===
Reference: lo_render.txt (LibreOffice)
Test:      aproj_render.txt (aproj/docx)

Summary:
  Total instructions: 1234 (ref) vs 1200 (test)
  Matched: 1180
  Differences: 20
  Known differences: 15
  New differences: 5  ← 需要修复

--- New Differences ---

Line 42:
  Ref:  TEXT_FRAME  1  1800  1440  8306  400  "Hello World"  Arial  22
  Test: TEXT_FRAME  1  1800  1440  8306  380  "Hello World"  Arial  22
  Diff: height 400 vs 380 (diff=20, tol=10)
  → 排版差异：行高计算不一致

Line 156:
  Ref:  TEXT_FRAME  2  1800  1440  8306  400  "Second page"  Arial  22
  Test: (missing)
  → 布局差异：分页点不一致
```

---

## 八、实施步骤

### Phase A: 共享渲染指令定义 (第 1 周)

- [ ] 创建 `render_instruction.h` 共享头文件
- [ ] 定义 `RenderCmdType` 枚举和 `RenderInstruction` 结构体
- [ ] 定义 `RenderInstructionSink` 接口
- [ ] 实现 `WriteInstruction()` 格式化函数

### Phase B: aproj/docx 适配 (第 1 周)

- [ ] 修改 `RenderLogger` 实现 `RenderInstructionSink`
- [ ] 重写 `LogFrameTree()` 输出 TEXT_LINE / TEXT_RUN 级别指令
- [ ] 修改 `WriteToFile()` 使用共享格式
- [ ] 端到端测试验证输出格式

### Phase C: LibreOffice 植入 (第 2 周)

- [ ] 创建 `SwPaintEventListener` 类
- [ ] 在 `SwTextFrame::PaintSwFrame()` 植入 TEXT_FRAME 记录
- [ ] 在 `SwRootFrame::PaintSwFrame()` 植入 PAGE_START / PAGE_END 记录
- [ ] 在 `SwTabFrame::PaintSwFrame()` 植入 TABLE 记录
- [ ] 通过 `SW_RENDER_LOG` 环境变量控制
- [ ] 编译验证

### Phase D: 比对工具 (第 2 周)

- [ ] 实现 `render_diff` 比对工具
- [ ] 支持数值容差、已知差异过滤
- [ ] 生成差异报告

### Phase E: 测试执行 (第 3 周)

- [ ] 创建测试用例 DOCX 文件
- [ ] 执行 Layer 1 (SwNodes) 比对
- [ ] 执行 Layer 2 (Frame 树) 比对
- [ ] 执行 Layer 3 (排版) 比对
- [ ] 执行 Layer 4 (渲染指令) 比对
- [ ] 修复发现的差异
- [ ] 生成最终比对报告

---

## 九、关键源码索引

### LibreOffice 侧需要修改的文件

| 文件 | 修改内容 |
|------|----------|
| `sw/source/core/inc/paint_listener.hxx` | **新建** — SwPaintEventListener 头文件 |
| `sw/source/core/layout/paint_listener.cxx` | **新建** — SwPaintEventListener 实现 |
| `sw/source/core/text/frmpaint.cxx` | **修改** — SwTextFrame::PaintSwFrame() 植入 |
| `sw/source/core/layout/paintfrm.cxx` | **修改** — SwRootFrame::PaintSwFrame() 植入 |
| `sw/source/core/layout/newfrm.cxx` | **修改** — SwRootFrame 初始化时检查环境变量 |
| `sw/source/core/layout/CMakeLists.txt` | **修改** — 添加 paint_listener.cxx |

### aproj/docx 侧需要修改的文件

| 文件 | 修改内容 |
|------|----------|
| `src/render/render_instruction.h` | **新建** — 共享定义 (与 LibreOffice 同一份) |
| `src/render/render_log.h` | **修改** — 实现 RenderInstructionSink |
| `src/render/render_log.cpp` | **修改** — 重写输出格式 |
| `test/test_end_to_end.cpp` | **修改** — 验证新格式 |

### 比对工具

| 文件 | 说明 |
|------|------|
| `tools/render_diff.cpp` | **新建** — 比对工具 |
| `tools/run_comparison_tests.ps1` | **新建** — 自动化测试脚本 |
