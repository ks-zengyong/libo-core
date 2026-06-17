# 编译 libo-core (LibreOffice)

> 在需要编译 LibreOffice 源码时触发。支持全量编译和模块级增量编译（如仅编译 sw 模块）。

## 前置条件

- `git bash` 环境进行编译（`make` 命令）
- 已运行 `wsl ./autogen.sh` 生成 `config_host.mk`
- wsl 只负责生成配置，不参与编译过程
- LibreOffice 已完成首次全量编译，后续仅做增量编译

## 操作步骤

### 使用 build_lo.bat 编译脚本（推荐）

```batch
build_lo.bat              # 全量编译
build_lo.bat sw           # 编译 Writer 模块
build_lo.bat calc         # 编译 Calc 模块
```

脚本自动完成：进程清理 → 编译 → 三条件构建成功判定 → 日志保存。

### 手动编译（git bash 环境）

```bash
make                        # 全量编译
make -j$(nproc)             # 并行编译
make sw                     # 编译 sw (Writer) 模块
make -j$(nproc) sw          # 并行编译 sw
```

### 重新配置

```bash
wsl ./autogen.sh            # 重新生成 config_host.mk
make                        # 重新编译
```

## 注意事项

- **禁止 `make clean` / `make distclean`**：LO 已完成首次编译，清理会破坏编译环境，只能做增量编译。此规则无例外。
- **禁止 `make fetch`**：无需手动下载依赖。
- 编译前脚本会自动终止占用产物的 LibreOffice 进程（soffice.exe、swriter.exe 等），无需手动处理。

## 常用 make 目标

| 目标 | 说明 | 可用 |
|------|------|------|
| `make` | 全量编译 | 是 |
| `make sw` | 编译 Writer 模块 | 是 |
| `make check` | 运行单元测试 | 是 |
| `make unitcheck` | 仅运行单元测试 | 是 |
| `make clean` | 清理构建产物 | **否，禁止使用** |
| `make distclean` | 完全清理（含 config） | **否，禁止使用** |
| `make fetch` | 下载外部依赖 | **否，无需使用** |

## build_lo.bat 脚本特性

### 进程清理机制

脚本启动前自动终止可能占用输出文件的 LibreOffice 进程：
`soffice.exe`、`soffice.bin`、`swriter.exe`、`scalc.exe`、`simpress.exe`、`sdraw.exe`、`smath.exe`、`sbase.exe`

### 构建完成判定（三条件验证）

| 条件 | 检测方式 | 说明 |
|------|----------|------|
| 条件 A | 日志中搜索完成标志 | `Built target`、`100%.*Built`、`[3/3]`、`Build succeeded` |
| 条件 B | 目标产物存在性 | 检查 `instdir/program/soffice.exe` 是否存在 |
| 条件 C | 产物时间戳更新 | 对比编译前后的修改时间，确保是新产物 |

仅当三个条件全部满足时，才判定构建成功。

## 故障排除

| 错误类型 | 表现 | 解决方法 |
|----------|------|----------|
| Permission denied | 链接阶段无法写入 exe 文件 | 确保没有运行中的 LibreOffice 进程，脚本会自动清理 |
| Target not updated | 编译完成但产物未更新 | 检查是否误用了 `make clean`，确保是增量编译 |
| Build incomplete | 脚本退出码为 0 但判定失败 | 查看日志文件中的完成标志是否存在 |

## 相关文件

- `libo-core/build_lo.bat` — 编译封装脚本
- `libo-core/config_host.mk` — 编译配置（由 autogen.sh 生成）
- `libo-core/build_YYYY_MM_DD.log` — 构建日志

## 工作目录

| 用途 | 路径 |
|------|------|
| 源码 | `/libo-core` |
| 构建 | `libo-core/workdir` |
| 产物 | `/libo-core/instdir` |
| 日志 | `/libo-core/build_YYYY_MM_DD.log` |
