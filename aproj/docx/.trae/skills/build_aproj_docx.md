# 编译 aproj/docx 项目

## 前置条件
- Windows + MSVC (Visual Studio)
- CMake >= 3.16
- 第三方依赖已下载 (`powershell -ExecutionPolicy Bypass -File download_deps.ps1`)
- HarfBuzz 源码已复制到 `third_party/harfbuzz/` (来自 libo-core/workdir/UnpackedTarball/harfbuzz/src/)

## 首次构建 (生成 VS 工程)
```bash
cd aproj/docx
cmake -B build -G "Visual Studio 17 2022" -A x64
```

## 编译 (使用 build.bat)
```bash
# Debug 模式 (默认)
build.bat

# Release 模式
build.bat Release
```

## 手动编译
```bash
cmake --build build --config Debug
# 或
cmake --build build --config Release
```

## 运行测试
```bash
./output/docx_e2e_test.exe
```

## 构建产物
| 目标 | 类型 | 说明 |
|------|------|------|
| `docx_core` | 静态库 | 核心 DOCX 解析/布局/渲染管线 |
| `docx_e2e_test` | 可执行 | 端到端测试 |
| `render_diff` | 可执行 | 渲染结果对比工具 |

> `build.bat` 编译完成后会将产物自动复制到 `output/` 目录下。