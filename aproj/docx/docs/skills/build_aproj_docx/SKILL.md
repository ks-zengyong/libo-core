# 编译 aproj/docx 项目

> 在需要编译 aproj/docx 项目时触发。生成 docx_core 静态库、docx_e2e_test（端到端测试，用于生成 Nodes/Frame/VCL 产物）、node_diff 和 render_diff 可执行文件。

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

### 编译

使用 `build.bat`（推荐）：

```bash
build.bat              # Debug 模式（默认）
build.bat Release      # Release 模式
```

或手动编译：

```bash
cmake --build build --config Debug
cmake --build build --config Release
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
| `node_diff` | 可执行文件 | Nodes 结构差异对比工具 |
| `render_diff` | 可执行文件 | Frame/VCL 渲染指令差异对比工具（由 `diff_*.bat` 调用） |
| `miniz` | 静态库 | ZIP 解压库 |
| `pugixml` | 静态库 | XML 解析库 |
| `harfbuzz` | 静态库 | 字体 shaping 库 |

产物路径：`build/Debug/`（Debug）或 `build/Release/`（Release），编译后自动拷贝到 `output/`。

## 相关文件

- `aproj/docx/build.bat` — 编译入口脚本
- `aproj/docx/CMakeLists.txt` — 构建配置
- `aproj/docx/test/test_end_to_end.cpp` — 端到端测试入口
- `aproj/docx/test/gen_aproj.py` — 调用 docx_e2e_test 生成产物的脚本
