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

## 编译
### 使用 build.bat
```bash
# Debug 模式 (默认)
build.bat

# Release 模式
build.bat Release
```

### 手动编译
```bash
cmake --build build --config Debug
# 或
cmake --build build --config Release
```

## 运行测试（使用Debug产物进行测试）
```bash
.\build\Debug\docx_e2e_test.exe
```

## 构建产物
| 目标 | 类型 | 路径 |
|------|------|------|
| `docx_core` | 静态库 | `build/Debug/docx_core.lib` |
| `docx_e2e_test` | 可执行 | `build/Debug/docx_e2e_test.exe` |
| `render_diff` | 可执行 | `build/Debug/render_diff.exe` |
| `miniz` | 静态库 | `build/Debug/miniz.lib` |
| `pugixml` | 静态库 | `build/Debug/pugixml.lib` |
| `harfbuzz` | 静态库 | `build/Debug/harfbuzz.lib` |