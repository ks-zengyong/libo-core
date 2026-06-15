# 当需要进行commit提交时

## 那些目录及文件需要添加
- 当前已追踪的所有文件
- `docx/src`目录下所有文件
- `docx/test`目录新增代码文件.cpp|.h
- `docx/.trae`下所有文件.cpp|.h
- `tools`下新增代码文件

## 排除目录
- docx/build
- tools/debug

## 提交命令
git commit --no-verify -m `你本次修改关键总结字符串`

# 推送代码命令
git push myrepo master:master

# 更新代码命令
git pull myrepo master:master --rebase
- 如果更新代码失败，则先暂存修改，然后更新