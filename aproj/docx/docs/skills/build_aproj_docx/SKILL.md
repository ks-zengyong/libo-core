# 编译 aproj/docx 项目

> 在需要编译 aproj/docx 项目时触发。生成 docx_core 静态库、docx_e2e_test（端到端测试，用于生成 Nodes/Frame/VCL 产物）、node_diff、frame_diff 和 render_diff 可执行文件。

## 前置条件

- Windows + MSVC (Visual Studio)
- CMake >= 3.16
- 第三方依赖已下载（`powershell -ExecutionPolicy Bypass -File download_deps.ps1`）
- HarfBuzz 源码已复制到 `third_party/harfbuzz/`（来自 `libo-core/workdir/UnpackedTarball/harfbuzz/src/`）

## 操作步骤

### 首次构建（生成 VS 工程）

```bash
cd aproj/docx
cmake -B build -G "Visual Studio 17 2022" -A x64
```

### 编译（必须使用 build.bat）

> **⚠️ 重要：必须使用 `build.bat` 编译，不要直接调用 `cmake --build`！**

```bash
build.bat              # Debug 模式（默认）
build.bat Release      # Release 模式
```

**原因**：`build.bat` 是项目统一的编译入口，包含了完整的配置检查、编译流程和产物整理。直接使用 `cmake --build` 可能导致产物路径不一致或其他问题。

### 编译产物输出位置

所有编译产物（exe、lib、pdb）统一输出到 `aproj/docx/output/` 目录下，无 Debug/Release 子目录：

```
output/
├── docx_core.lib           # 核心静态库
├── harfbuzz.lib
├── miniz.lib
├── pugixml.lib
├── render_common.lib
├── docx_e2e_test_debug.exe   # 端到端测试（Debug）
├── docx_e2e_test.exe         # 端到端测试（Release）
├── node_diff_debug.exe
├── node_diff.exe
├── frame_diff_debug.exe
├── frame_diff.exe
├── render_diff_debug.exe
└── render_diff.exe
```

## 注意事项

- 测试必须使用 **Debug** 产物
- 首次构建后无需重复执行 cmake 生成，直接 `build.bat` 即可增量编译
- **不要直接调用 `docx_e2e_test.exe`** 来生成测试产物，使用 `python test\gen_aproj.py` 脚本（详见技能 `test_diff_workflow`）

## 构建产物

| 目标 | 类型 | 用途 |
|------|------|------|
| `docx_core` | 静态库 | DOCX 解析排版核心库 |
| `docx_e2e_test` | 可执行文件 | 端到端测试：解析 docx → 排版 → 生成 Nodes 结构、Frame 树和 VCL 渲染指令记录（由 `gen_aproj.py` 调用，不要手动执行） |
| `node_diff` | 可执行文件 | Nodes 结构差异对比工具（支持 position/lcs/myers/needleman 四种算法） |
| `frame_diff` | 可执行文件 | Frame 渲染指令差异对比工具（支持 position/lcs/myers/needleman 四种算法，由 `diff_frame.bat` 调用） |
| `render_diff` | 可执行文件 | 通用渲染指令逐行对比工具（支持 `--known-diffs` 跳过已知差异） |
| `miniz` | 静态库 | ZIP 解压库 |
| `pugixml` | 静态库 | XML 解析库 |
| `harfbuzz` | 静态库 | 字体 shaping 库 |

## 相关文件

- `aproj/docx/build.bat` — **编译入口脚本（必须使用）**
- `aproj/docx/CMakeLists.txt` — 构建配置
- `aproj/docx/test/test_end_to_end.cpp` — 端到端测试入口
- `aproj/docx/test/gen_aproj.py` — 调用 docx_e2e_test 生成产物的脚本
