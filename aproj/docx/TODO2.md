# Do1.md — 渲染指令对齐与测试比对总结

## 一、已完成工作

### Phase 1: SwDoc 管线修复

- **SwDoc 缺失 RootFrame 存储**：InitLayout() 创建了 SwRootFrame* 但从未存储到 SwDoc，MakeFrames() 的逻辑有缺陷（始终返回 nullptr）。修复：在 SwDoc 中添加 `m_pRootFrame` 成员及 Get/Set 方法。
- 修复文件：
  - `aproj/docx/src/core/doc.h` — 添加 m_pRootFrame 成员
  - `aproj/docx/src/frame/frmtree.cpp` — InitLayout 调用 SetRootFrame，MakeFrames 调用 GetRootFrame

### Phase 2: 共享渲染指令格式

- **render_instruction.h** — 零外部依赖的 POD 结构体，定义 RenderCmdType 枚举和 RenderInstruction 结构体
- **RenderInstructionSink** — 纯虚接口，统一 aproj 和 LibreOffice 的输出格式
- **TSV 格式** — tab 分隔字段，确保两侧输出字节级一致

### Phase 3: aproj/docx 侧实现

- `aproj/docx/src/render/render_instruction.h` — 共享定义
- `aproj/docx/src/render/render_log.h / .cpp` — RenderLogger 实现 RenderInstructionSink 接口
- `aproj/docx/test/test_end_to_end.cpp` — 端到端集成测试（parse → layout → render）
- `aproj/docx/tools/render_diff.cpp` — 比对工具，支持 `--tolerance` 和 `--known-diffs`
- `aproj/docx/tools/run_comparison_tests.ps1` — 自动化比对脚本
- `aproj/docx/tests/known_diffs.txt` — 已知差异模板

### Phase 4: LibreOffice 侧植入

| 文件 | 修改类型 | 说明 |
|------|----------|------|
| `sw/source/core/inc/render_instruction.h` | 新建 | 共享渲染指令定义 |
| `sw/source/core/inc/paint_listener.hxx` | 新建 | SwPaintEventListener 头文件 |
| `sw/source/core/layout/paint_listener.cxx` | 新建 | 实现，检查 SW_RENDER_LOG 环境变量 |
| `sw/source/core/layout/paintfrm.cxx` | 修改 | 植入 PAGE_START / PAGE_END |
| `sw/source/core/text/frmpaint.cxx` | 修改 | 植入 TEXT_FRAME |
| `sw/source/core/layout/newfrm.cxx` | 修改 | SwRootFrame 构造函数中调用 CheckEnvAndStart() |
| `sw/Library_sw.mk` | 修改 | 添加 paint_listener 编译目标 |

### 编译修复

| 错误 | 修复 |
|------|------|
| `GetTextNode` 不是 SwTextFrame 成员 | → `GetTextNodeFirst()` |
| `GetColor` 不是 SvxColorItem 成员 | → `GetValue()` |
| UIName 无法转为 string_view | → `GetName().toString()` + `#include <names.hxx>` |

**LibreOffice `make sw` 编译成功**，swlo.dll 已更新。

---

## 二、后续待完成

### 1. 构建 aproj/docx 侧可执行文件

需要 CMake 环境：

```bash
cd aproj/docx
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --target docx_e2e_test
cmake --build . --target render_diff
```

### 2. 执行端到端比对测试

```bash
# LibreOffice 侧生成参考渲染指令
SW_RENDER_LOG=lo_render.txt instdir/program/soffice.exe --headless aproj/docx/tests/sample.docx

# aproj 侧生成渲染指令
./docx_e2e_test ../tests/sample.docx

# 比对
./render_diff lo_render.txt render_output.txt --tolerance 10
```

### 3. 测试文件准备

- 在 `aproj/docx/tests/` 目录下准备测试 .docx 文件
- 覆盖场景：纯文本、表格、图片、多页、脚注等
- 更新 `known_diffs.txt` 记录已知差异

### 4. 比对结果处理

- 首次运行预期存在差异（IMAGE_FRAME、Header、Footer、FlyFrame 等尚未支持的元素）
- 逐步修复 aproj 侧渲染逻辑，直到 render_diff 输出 PASS
- 最终目标：aproj/docx 的渲染指令与 LibreOffice 完全一致（±10 twips 容差内）

---

## 三、核心架构

```
DocxParser::Read() → SwDoc (SwNodes + 节点树)
        ↓
   InitLayout() + MakeFrames() → SwRootFrame → SwPageFrame → SwTextFrame
        ↓
   SwLayAction::Action() (布局计算)
        ↓
   RenderLogger / SwPaintEventListener (共享 RenderInstructionSink 接口)
        ↓
   TSV 输出 → render_diff 比对
```

**关键设计决策**：
- 渲染指令使用纯 POD 结构体（零外部依赖），确保 aproj 和 LibreOffice 可共享同一份头文件
- TSV 格式统一，字段顺序和分隔符一致，支持直接 diff 比对
- LibreOffice 侧通过环境变量 `SW_RENDER_LOG` 控制，生产环境无性能影响
