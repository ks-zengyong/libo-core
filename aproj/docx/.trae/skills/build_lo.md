# 编译 libo-core (LibreOffice)

## 前置条件
- `git bash`环境进行编译(make ...)
- 已运行 `wsl ./autogen.sh` 生成 `config_host.mk`
- wsl只负责生成配置，不参与编译过程

## 禁止清理
- `禁止make clean等清理编译环境，lo已经编译完成，只需增量编译`

## 全量编译（禁止make clean）
- **git bash环境下切到lib-core目录执行**
```
make                        # 默认 build 目标
make -j$(nproc)             # 并行编译
```

## 编译单个模块
- **git bash环境下切到lib-core目录执行**
```
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
- **git bash环境下切到lib-core目录执行**
```
wsl ./autogen.sh            # 重新生成 config_host.mk
make                        # 重新编译
```

## 工作目录
- 源码: `/libo-core`
- 构建: `libo-core/workdir`
- 产物: `/libo-core/instdir`
