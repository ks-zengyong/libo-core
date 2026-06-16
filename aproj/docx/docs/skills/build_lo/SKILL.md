# 编译 libo-core (LibreOffice)

## 前置条件
- `git bash`环境进行编译(make ...)
- 已运行 `wsl ./autogen.sh` 生成 `config_host.mk`
- wsl只负责生成配置，不参与编译过程

## 禁止清理
- `禁止make clean等清理编译环境，lo已经编译完成，只需增量编译`

## 使用 build_lo.bat 编译脚本

### 概述
`build_lo.bat` 脚本提供了增强的编译封装，包含以下特性：
- 自动清理占用目标文件的进程
- 构建完成标志检测
- 产物时间戳验证
- 详细日志记录

### 使用方法
```batch
build_lo.bat              # 全量编译
build_lo.bat sw           # 编译 Writer 模块
build_lo.bat calc         # 编译 Calc 模块
```

### 脚本特性

#### 1. 进程清理机制
脚本启动前会自动终止所有可能占用输出文件的 LibreOffice 进程：
- `soffice.exe`
- `soffice.bin`
- `swriter.exe`
- `scalc.exe`
- `simpress.exe`
- `sdraw.exe`
- `smath.exe`
- `sbase.exe`

#### 2. 构建完成判定（三条件验证）

脚本采用**三条件闭环判定**确保编译成功：

| 条件 | 检测方式 | 说明 |
|------|----------|------|
| **条件A** | 日志中搜索完成标志 | `Built target`、`100%.*Built`、`[3/3]`、`Build succeeded` |
| **条件B** | 目标产物存在性 | 检查 `instdir/program/soffice.exe` 是否存在 |
| **条件C** | 产物时间戳更新 | 对比编译前后的修改时间，确保是新产物 |

**仅当三个条件全部满足时，才判定构建成功**。

#### 3. 日志文件
构建日志会保存到：
```
libo-core/build_YYYY_MM_DD.log
```

#### 4. 错误处理
- 编译前自动清理占用进程，避免 "Permission denied" 错误
- 编译失败时输出详细错误信息，指出具体失败原因
- 区分"脚本退出码失败"与"完成标志缺失"两种情况

### 手动编译（git bash环境）

#### 全量编译（禁止make clean）
```bash
make                        # 默认 build 目标
make -j$(nproc)             # 并行编译
```

#### 编译单个模块
```bash
make sw                     # 编译 sw (Writer) 模块
make -j$(nproc) sw          # 并行编译 sw
```

## 常用 make 目标

| 目标 | 说明 | 是否可用|
|------|------|------|
| `make` | 全量编译 | 是 |
| `make sw` | 编译 Writer 模块 | 是 |
| `make check` | 运行单元测试 | 是 |
| `make unitcheck` | 仅运行单元测试 | 是 |
| `make clean` | 清理构建产物 | 否！禁止使用 |
| `make distclean` | 完全清理 (含 config) | 否！禁止使用 |
| `make fetch` | 下载外部依赖 | 否！无需使用 |

## 重新配置
```bash
wsl ./autogen.sh            # 重新生成 config_host.mk
make                        # 重新编译
```

## 工作目录
- 源码: `/libo-core`
- 构建: `libo-core/workdir`
- 产物: `/libo-core/instdir`
- 日志: `/libo-core/build_YYYY_MM_DD.log`

## 故障排除

### 常见错误

| 错误类型 | 表现 | 解决方法 |
|----------|------|----------|
| Permission denied | 链接阶段无法写入 exe 文件 | 确保没有运行中的 LibreOffice 进程，脚本会自动清理 |
| Target not updated | 编译完成但产物未更新 | 检查是否使用了 `make clean`，确保是增量编译 |
| Build incomplete | 脚本退出码为0但判定失败 | 检查日志文件中的完成标志是否存在 |

### 日志分析
当脚本报告构建失败时，查看日志文件寻找：
- `Built target` - 确认构建是否真正完成
- `error` / `Error` / `ERROR` - 查找编译错误
- `Permission denied` / `cannot open output file` - 文件被占用