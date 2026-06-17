# 测试差异对比与迭代修复工作流

> 在需要生成测试产物（Nodes/Frame/VCL）、对比差异或迭代修复排版差异时触发。封装了完整的生成与对比流程。

## 前置条件

- aproj/docx 项目已编译（Debug 配置），产物在 `output/` 目录下
- LibreOffice 已编译完成（用于生成参考输出）
- Python 可用（用于运行 gen_*.py 脚本）

## 操作步骤

### 标准操作序列

```powershell
# 1. 编译 aproj/docx 项目（Debug 配置）
.\build.bat

# 2. 生成 LibreOffice 侧参考输出（Nodes/Frame/VCL）
python test\gen_lo.py

# 3. 生成 aproj/docx 侧输出（Nodes/Frame/VCL）
python test\gen_aproj.py

# 4. 逐级对比差异
.\test\diff_node.bat    # Nodes 文档节点树结构对比
.\test\diff_frame.bat   # Frame 布局树结构对比
.\test\diff_vcl.bat     # VCL 渲染指令对比
```

按此顺序执行即可，无需额外操作。

### 生成产物说明

调用 `aproj/docx/test/` 下的 Python 脚本生成产物，产物自动写入 `aproj/docx/test/` 目录：

| 脚本 | 作用 | 产物 |
|------|------|------|
| `python test/gen_aproj.py` | 编译 aproj/docx 项目并运行端到端测试（docx_e2e_test），生成 Nodes 结构、Frame 树和 VCL 渲染指令记录 | `test/aproj_nodes.txt`, `test/aproj_frame.txt`, `test/aproj_vcl.txt` |
| `python test/gen_lo.py` | 启动 LibreOffice soffice 无界面模式转换文档，生成 LO 侧的 Nodes 结构、Frame 树和 VCL 渲染指令参考记录 | `test/lo_nodes.txt`, `test/lo_frame.txt`, `test/lo_vcl.txt` |

### 差异对比说明

调用 `aproj/docx/test/` 下的 bat 脚本执行对比，脚本自动引用同目录下的上述产物文件：

| 脚本 | 对比内容 |
|------|----------|
| `.\test\diff_node.bat` | 对比 aproj 与 LO 的 Nodes 文档节点树结构差异 |
| `.\test\diff_frame.bat` | 对比 aproj 与 LO 的 Frame 布局树结构差异 |
| `.\test\diff_vcl.bat` | 对比 aproj 与 LO 的 VCL 渲染层绘制指令差异 |

## 注意事项

- **不要自己调用** `docx_e2e_test.exe` 或 LibreOffice `soffice` 来生成产物文件，使用 `gen_*.py` 脚本
- **不要自己调用** `render_diff.exe` 并手动拼接路径参数，使用 `diff_*.bat` 脚本
- **不要自己编写** 生成或对比脚本
- **使用现有 `*.docx`**（`aproj/docx/samples/*.docx`），不要自己生成 docx 测试文件

## 差异修复策略

### 执行顺序

```
              ┌─ Step 1: Nodes 结构对比 ──→ Nodes 差异为 0？ ──No──→ 修复 Nodes 层差异
              │                                    │
              │                                   Yes │
              │                                    ▼
samples/*.docx┤                         Step 2: Frame 树对比 ──→ Frame 差异为 0？ ──No──→ 修复 Frame 层差异
              │                                              │
              │                                             Yes │
              │                                              ▼
              └─ Step 3: VCL 指令对比 ──→ VCL 差异为 0？ ───No──→ 修复 VCL 层差异
                                              │
                                         Yes  │
                                              ▼
                                         目标达成：0 差异
```

Nodes 结构差异最先对比，因为解析阶段的节点树差异会影响后续排版；Frame 树差异基本消除后再进入 VCL 层对比，因为 VCL 层差异可能源自 Frame 树的布局差异。

### 差异定位流程

1. 运行 `diff_*.bat` 查看差异
2. 根据差异类型（如 `TEXT_FRAME` 的 `x` 坐标偏差）确定涉及的排版逻辑
3. 在 `libo-core/sw/source/core/layout/` 中搜索对应的排版计算代码
4. 将缺失或有差异的逻辑迁移到 `aproj/docx/src/layout/` 或相关模块
5. 重新编译并运行对比验证

### 迭代原则

- **不追求差异总数递减**：差异总数可能受指令数量变化影响，不必强约束
- **按序逐个解决**：按差异出现顺序（行号顺序），逐个分析、定位、修复
- **每次修复后验证**：修复后重新运行 `diff_*.bat`，确认目标差异消除且未引入新差异
- **已知差异管理**：暂时无法解决的差异写入 `test/known_diffs.txt`，标记为 `[KNOWN]`

## 产物目录结构

```
aproj/docx/
  ├── output/
  │   ├── docx_e2e_test_debug.exe      # Debug 编译产物
  │   ├── render_diff_debug.exe        # Debug 编译产物
  │   ├── docx_e2e_test.exe            # Release 编译产物
  │   └── render_diff.exe              # Release 编译产物
  ├── test/
  │   ├── test_end_to_end.cpp          # 端到端测试（C++）
  │   ├── gen_aproj.py                 # 运行 e2e 测试并收集产物
  │   ├── gen_lo.py                    # 生成 LO 参考输出
  │   ├── diff_node.bat                # Nodes 层差异比对脚本
  │   ├── diff_frame.bat               # Frame 层差异比对脚本
  │   ├── diff_vcl.bat                 # VCL 层差异比对脚本
  │   ├── aproj_nodes.txt              # aproj Nodes 层产物
  │   ├── aproj_frame.txt              # aproj Frame 层产物
  │   ├── aproj_vcl.txt                # aproj VCL 层产物
  │   ├── lo_nodes.txt                 # LO Nodes 层产物
  │   ├── lo_frame.txt                 # LO Frame 层产物
  │   ├── lo_vcl.txt                   # LO VCL 层产物
  │   └── known_diffs.txt              # 已知差异清单
  ├── tools/
  │   └── render_diff.cpp              # 差异对比工具源码
  └── samples/
      └── *.docx                       # 测试样本文档
```

### 命名约定

- **产物文件**：`{prefix}_{type}.txt`
  - `prefix`：`aproj` 或 `lo`
  - `type`：`frame`、`vcl` 或 `nodes`
- **Python 脚本**：`gen_{prefix}.py`

## 三级差异数据结构

### Nodes 层（文档节点树）

| 指令类型 | 字段 | 说明 |
|---------|------|------|
| `START_NODE` | nodeId, nodeType | 节点开始，附带节点类型（如 Normal, TableBox 等） |
| `END_NODE` | nodeId | 节点结束 |
| `TEXT_NODE` | nodeId, text, styleName | 文本节点，附带文本内容和样式名 |

### Frame 层（布局树）

| 指令类型 | 字段 | 说明 |
|---------|------|------|
| `PAGE_START` | pageNum, width, height | 页面开始，附带页面尺寸 |
| `PAGE_END` | pageNum | 页面结束 |
| `TEXT_FRAME` | pageNum, x, y, w, h, text, fontName, fontSize, fontColor, fontWeight, fontItalic, underline, strikeout, styleName | 文本段落 Frame |
| `TEXT_LINE` | 同 TEXT_FRAME | 文本行 Frame |
| `TABLE_FRAME` | pageNum, x, y, w, h | 表格 Frame |
| `TABLE_ROW` | pageNum, x, y, w, h | 表格行 Frame |
| `TABLE_CELL` | pageNum, x, y, w, h | 表格单元格 Frame |
| `IMAGE_FRAME` | pageNum, x, y, w, h | 图片 Frame |
| `SECTION_FRAME` | pageNum, x, y, w, h | 节 Frame |

### VCL 层（渲染指令）

| 指令类型 | 字段 | 说明 |
|---------|------|------|
| `PAGE_START` | pageNum, width, height | 页面开始 |
| `PAGE_END` | pageNum | 页面结束 |
| `SET_FONT` | pageNum, fontName, fontSize, fontWeight, fontItalic | 设置当前字体 |
| `SET_TEXT_COLOR` | pageNum, color | 设置文字颜色 |
| `SET_FILL_COLOR` | pageNum, color | 设置填充色 |
| `SET_LINE_COLOR` | pageNum, color | 设置线条色 |
| `SET_CLIP_REGION` | pageNum | 设置裁剪区域 |
| `TEXT_RUN` | pageNum, x, y, w, h, text, fontName, fontSize, fontColor, fontWeight, fontItalic | 文本绘制 |
| `RECT` | pageNum, x, y, w, h | 矩形绘制 |
| `LINE` | pageNum, x1, y1, x2, y2 | 线段绘制 |
| `ELLIPSE` | pageNum, x, y, w, h | 椭圆绘制 |
| `POLYGON` | pageNum, x, y, w, h | 多边形绘制 |
| `POLYLINE` | pageNum, x1, y1, x2, y2 | 折线绘制 |
| `BITMAP` | pageNum, x, y, w, h | 位图绘制 |
| `PUSH` / `POP` | pageNum | 状态保存/恢复 |

## 当前进度

| 步骤 | 组件 | 状态 |
|------|------|------|
| Step 1 | Nodes 结构记录（aproj 侧） | 已完成 |
| Step 1 | Nodes 结构记录（LO 侧） | 已完成 |
| Step 2 | Frame 树记录（aproj 侧） | 已完成 |
| Step 2 | Frame 树记录（LO 侧） | 已完成 |
| Step 3 | VCL 模块接入 aproj/docx | **待完成** — 当前为简化 RenderOutputDevice |
| Step 3 | VCL 记录（LO 侧） | 已完成 |
| - | render_diff 对比工具 | 已完成 |
| - | Python 生成脚本 | 已完成 |
| 迭代修复 | 差异驱动修复工作流 | 进行中 |

## 相关文件

| 文件 | 说明 |
|------|------|
| `test/test_end_to_end.cpp` | 端到端测试入口 |
| `test/gen_aproj.py` | 运行 e2e 测试并收集产物 |
| `test/gen_lo.py` | 生成 LO 参考输出 |
| `test/diff_node.bat` | Nodes 层差异比对 |
| `test/diff_frame.bat` | Frame 层差异比对 |
| `test/diff_vcl.bat` | VCL 层差异比对 |
| `tools/render_diff.cpp` | 渲染指令逐行比对工具源码 |
| `src/render/render_log.cpp` | Frame 树遍历 + TSV 指令记录 |
| `src/render/nodes_log.cpp` | Nodes 结构遍历 + 记录 |
| `src/render/render_output_device.cpp` | 简化的 OutputDevice |
| `libo-core/sw/source/core/inc/instruction_builder.h` | 共享指令构建器 |
| `libo-core/sw/source/core/inc/render_instruction.h` | 共享指令数据结构 |
