# Git 操作规范

> 在需要提交代码（commit）、推送（push）或拉取（pull）时触发。定义了提交文件范围、排除规则和标准命令。

## 前置条件

- 已配置 git 远程仓库 `myrepo`
- 工作区根目录为 `aproj`

## 操作步骤

### 1. 添加文件到暂存区

需要添加的目录和文件：

| 范围 | 说明 |
|------|------|
| 当前已追踪的所有文件 | `git add -u` |
| `docx/src/` 下所有文件 | 源代码 |
| `docx/test/` 下新增 `.cpp` / `.h` 文件 | 测试代码 |
| `docx/docs/` 下所有文件 | 规则与技能文档 |
| `tools/` 下新增代码文件 | 工具脚本 |
| `sw/` 下所有代码文件 | LO 迁移代码 |

**排除目录**（不要添加）：

| 目录 | 原因 |
|------|------|
| `docx/build/` | 编译产物，不入库 |
| `tools/debug/` | 调试临时脚本，不入库 |

### 2. 提交

```bash
git commit --no-verify -m "本次修改的关键总结"
```

提交信息应简洁描述本次修改的核心内容。

### 3. 推送

```bash
git push myrepo master:master
```

### 4. 拉取更新

```bash
git pull myrepo master:master --rebase
```

## 注意事项

- 如果拉取失败（有本地未提交修改），先暂存修改再拉取：
  ```bash
  git stash
  git pull myrepo master:master --rebase
  git stash pop
  ```
- `--no-verify` 跳过 pre-commit hook，确保提交不被拦截
