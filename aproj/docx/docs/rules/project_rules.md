# RULES.md 项目硬性标准

> 版本: 1.0 | 最后更新: 2026-06-17
>
> 这是本项目的硬性标准，在迁移核心实现代码、问题修复、方案考虑、后续迭代等过程中要始终遵守约定。违反硬性约束的代码不得合入。

## 项目主旨

将 LibreOffice 关于 DOCX 组件的核心代码迁移重构到 `aproj/docx` 下。目标是实现一个独立的 DOCX 解析器和排版引擎，最终渲染输出与 LibreOffice 做到 **0 差异**。

## 目录结构说明

当以 `aproj` 作为工作区根目录打开时，其相对位置和目录结构如下：

```
libo-core/                    # LibreOffice 源代码根目录（即 aproj 的父目录）
├── sw/                       # Writer 模块（DOCX 解析与排版核心代码）
├── vcl/                      # VCL 渲染层模块
├── oox/                      # OOXML 解析模块
├── sax/                      # XML 解析模块
├── svl/                      # 共享工具库
├── svx/                      # 图形/编辑引擎
├── tools/                    # 通用工具库
├── sal/                      # 系统抽象层
├── ...                       # 其他 LO 模块
│
└── aproj/                    # 本项目工作区根目录
    ├── docx/                 # DOCX 独立解析排版引擎
    │   ├── src/
    │   │   ├── core/         # 核心数据结构（doc, node, format, types 等）
    │   │   ├── filter/       # DOCX 文件解析器
    │   │   ├── font/         # 字体引擎
    │   │   ├── frame/        # Frame 排版树
    │   │   ├── layout/       # 排版动作（layact）
    │   │   └── render/       # 渲染层（接入 LO 的 VCL）
    │   ├── test/             # 测试用例与差异对比脚本
    │   ├── samples/          # 测试用 .docx 样本文件
    │   ├── third_party/      # 三方库（如 harfbuzz）
    │   └── docs/             # docx 模块级配置（规则、技能）
    │
    ├── cache/                # 缓存目录（机器配置信息等）
    └── docs/                 # 项目级配置（与 docx/docs/ 区分）
        └── rules/            # 项目规则文件
```

### 重要说明

- **LibreOffice 源码位置**: `aproj` 的父目录 `libo-core/` 即为 LibreOffice 的源代码目录。当需要查找 LO 原始代码实现时，应向上查找 `../` 即可访问 `libo-core` 下的各模块（如 `../sw/`、`../vcl/`、`../oox/` 等）。
- **路径对应关系**: 以 `aproj` 为工作区根目录时，`../` = `libo-core/`。例如：
  - `../sw/` → LibreOffice Writer 模块
  - `../vcl/` → LibreOffice VCL 渲染模块
  - `../oox/` → LibreOffice OOXML 解析模块

## 技术实现约束（硬性）

1. **LibreOffice 源码位置**: `libo-core` 下，主要在 `sw` 模块（解析文档和排版）
2. **最小实现但功能完整**: 迁移 LibreOffice 关于 DOCX 文档核心最小实现，但功能要完整
3. **0 差异目标**: 最终文档排版效果和 LibreOffice 做到 0 差异
4. **三方库复用**: `libo-core` 依赖的三方库，如有必要 `aproj/docx` 也需要引进，而不是自行实现。切记只迁移 DOCX 文档相关的核心实现
5. **禁止硬编码**: 不能通过写死逻辑或者为了解决问题不考虑通用逻辑自行进行硬编码，需要参考 `libo-core` 源代码来迁移适配
6. **参考源而非猜测**: 如果测试发现差异，一定是 LibreOffice 部分实现逻辑没有迁移适配到 `aproj/docx` 下。请参考 LibreOffice 源代码（位置在 `libo-core` 下）寻找代码位置然后迁移过来，而不要自行决策寻找解决方案
7. **剔除历史错误实现**: 如果项目中已经存在违反上述原则，按自己想法实现的逻辑，则应该删除并采用 libo-core 的方案，记住我们只是精炼提取，不自己设计方案实现

### 违反处理

- 硬编码或自行实现的逻辑：**必须删除重写**，不得以"能用就行"为由保留
- 未参考 LO 源码的猜测性实现：**要求回溯验证**，对照 LO 源码确认逻辑一致性后方可合入

## 代码规范

- **命名风格**: 迁移代码应保持与 libo-core 一致的命名风格（如 `CamelCase` 类名、`m_` 成员前缀、`p` 指针前缀等），不得自行改为其他风格
- **注释语言**: 代码注释使用英文，与 libo-core 保持一致
- **头文件组织**: 迁移的头文件保持与 libo-core 相同的 include 层级和依赖关系
- **新增代码**: 对于非迁移的辅助代码（如测试、工具脚本），注释和命名风格与迁移代码保持一致

## 测试标准

### 差异对比机制

采用三级差异对比量化 aproj/docx 与 LibreOffice 的输出差距：

| 级别 | 对比内容 | 产物 | 说明 |
|------|----------|------|------|
| Step 1 | Nodes 结构 | `*_nodes.txt` | 解析后的文档节点树结构 |
| Step 2 | Frame 树 | `*_frame.txt` | 排版后的 Frame 布局树结构 |
| Step 3 | VCL 渲染指令 | `*_vcl.txt` | 最终渲染层的绘制指令 |

- frame 树记录即解析排版完成后以 RootFrame 为根的整个树以一定格式打印相关信息出来
- vcl 渲染记录为真正在渲染层的绘制指令记录，aproj/docx 接入 libo-core 的 vcl 模块，不单独实现渲染框架，这样渲染指令的不同只可能来自 aproj/docx 自身排版等逻辑的差异
- nodes 结构记录为文档解析后的 SwNodes 节点树结构信息
- 两侧产物进行逐字段严格比对，不设容差（tolerance）

### 测试规则

- **使用现有 `*.docx`**（`aproj/docx/samples/*.docx`），**不要自己生成 docx 测试文件**
- 测试差异即为 frame、vcl 指令记录和 nodes 结构记录的差异项
- 测试目标是三个指标差异数均为 **0**

### 迭代改进策略

- 不一定完全按照差异总数递减或者页面总数不同来验证方案可行性
- 按差异项的顺序逐个解决，每次在前面已解决的顺序序列上，能解决按顺序的下一个或多个也是一种解决方案
- 详细工作流参见技能 `test_diff_workflow`

## 实用工具标准

- **调试工具脚本**: 探查、调试等工具脚本均放在 `aproj/docx/tools/debug` 下面
- **配置信息缓存**: 搜索 `python`、`cmake` 等在工作机器的安装位置以及其他和工作机器相关的配置信息，在查询成功后均缓存在 `aproj/cache` 下面，以便后续直接复用，而不是每次都重新搜索查询
- **DOCX 解压缓存**: 如果需要将 `*.docx`（`aproj/docx/samples/*.docx`）解压为 xml 以便用 python 脚本进行 ooxml 内容洞察，则将其解压至 `aproj/docx/samples/*_extracted_xml` 下面缓存起来，以便后续复用，而不是每次需要时多次解压创建很多副本

## 项目技能

项目技能文件位于 `docx/docs/skills/` 目录下，每个技能是一个独立的 SKILL.md 文件，描述特定操作的详细流程和规范。

| 技能 | 路径 | 能力说明 |
|------|------|----------|
| **build_aproj_docx** | `skills/build_aproj_docx/SKILL.md` | 编译 aproj/docx 项目：CMake 生成 VS 工程、Debug/Release 编译、运行测试，产物包括 `docx_core.lib`、`docx_e2e_test.exe`、`render_diff.exe` 等 |
| **build_lo** | `skills/build_lo/SKILL.md` | 编译 libo-core (LibreOffice)：增量编译（禁止 clean）、模块级编译（`make sw`）、进程自动清理、三条件构建成功判定、故障排除 |
| **git_ops** | `skills/git_ops/SKILL.md` | Git 操作规范：commit 提交的文件范围（src/test/docs/sw 等）、排除目录（build/tools/debug）、提交/推送/拉取命令 |
| **lo_docx_structure** | `skills/lo_docx_structure/SKILL.md` | LibreOffice DOCX 组件架构参考：三层架构（模型层/布局层/渲染层）、DOCX 导入/导出管线、SwDoc 文档模型、Frame 树排版引擎、浮动对象体系、关键文件索引 |
| **test_diff_workflow** | `skills/test_diff_workflow/SKILL.md` | 测试差异对比与迭代修复工作流：三级差异对比（Nodes→Frame→VCL）、render_diff 工具使用、差异定位策略、标准修复步骤、脚本与产物约定 |
