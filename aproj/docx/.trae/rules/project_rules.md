# RULES.md 项目硬性标准
这是本项目的硬性标准，在迁移核心实现代码、问题修复、方案考虑、后续迭代等过程中要始终遵守约定

## 项目主旨
将 LibreOffice 关于 DOCX 组件的核心代码迁移重构到 `aproj/docx` 下。目标是实现一个独立的 DOCX 解析器和排版引擎，最终渲染输出与 LibreOffice 做到 **0 差异**。

## 技术实现约束（硬性）
1. **LibreOffice 源码位置**: `libo-core` 下，主要在 `sw` 模块（解析文档和排版）
2. **最小实现但功能完整**: 迁移 LibreOffice 关于 DOCX 文档核心最小实现，但功能要完整
3. **0 差异目标**: 最终文档排版效果和 LibreOffice 做到 0 差异
4. **三方库复用**: `libo-core` 依赖的三方库，如有必要 `aproj/docx` 也需要引进，而不是自行实现。切记只迁移 DOCX 文档相关的核心实现
5. **禁止硬编码**: 不能通过写死逻辑或者为了解决问题不考虑通用逻辑自行进行硬编码，需要参考 `libo-core` 源代码来迁移适配
6. **参考源而非猜测**: 如果测试发现差异，一定是 LibreOffice 部分实现逻辑没有迁移适配到 `aproj/docx` 下。请参考 LibreOffice 源代码（位置在 `libo-core` 下）寻找代码位置然后迁移过来，而不要自行决策寻找解决方案
7. **剔除历史错误实现**: 如果项目中已经存在违反上述原则，按自己想法实现的逻辑，则应该删除并采用libo-core的方案，记住我们只是精炼提取，不自己设计方案实现

## 实用工具标准
- **在整个过程中使用的探查、调试等工具脚本均放在`aproj/docx/tools/debug`下面**
- **搜索`python`、`cmake`等等在工作机器的安装位置以及其他和工作机器相关的配置信息，在查询成功后均缓存在`aproj/cache`下面，以便后续直接服用，而不是每次都重新搜索查询**
- **如果需要将 `*.docx`（`aproj/docx/samples/*.docx`）解压为xml以便用python脚本进行ooxml内容洞察，则将其解压至`aproj/docx/samples/*_extracted_xml`下面缓存起来，以便后续复用，而不是每次需要时多次解压创建很多副本**
- **

# 集成测试技术方案框架
- **如何生成frame树记录和vcl渲染指令记录** frame树记录即解析排版完成后以RootFrame为根的整个树以一定格式打印相关信息出来。vcl渲染记录为真正在渲染层的绘制指令记录，为了避免渲染框架的不同而导致渲染指令的不同，`aproj/docx也接入libo-core的vcl模块，不单独实现渲染框架`，这样渲染指令的不同只可能来自aproj/docx自身排版等逻辑的差异。
- **frame树差异对比** 将 LibreOffice 生成的 frame 树记录（`lo_frame.txt`）和 VCL 层渲染指令记录（`lo_vcl.txt`）与 aproj/docx 生成的对应记录（`aproj_frame.txt`、`aproj_vcl.txt`）进行逐字段严格比对，不设容差（tolerance）
- **测试差异迭代改进策略** 不一定完全按照差异总数递减或者页面总数不同来验证方案可行性，还可以按照差异项的顺序，按顺序逐个解决，每次在前面已解决的顺序序列上，能解决按顺序的下一个或多个也是一种解决方案。

## 测试标准
- **使用现有 `*.docx`**（`aproj/docx/samples/*.docx`），**不要自己生成 docx 测试文件**
- 测试差异即为frame和vcl指令记录的差异项
- 测试目标是两个指标差异数为 **0**

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
    │   └── .trae/            # Trae IDE 配置（规则、技能、代理）
    │
    ├── cache/                # 缓存目录（机器配置信息等）
    └── .trae/                # 项目级 Trae IDE 配置
        └── rules/            # 项目规则文件
```

### 重要说明

- **LibreOffice 源码位置**: `aproj` 的父目录 `libo-core/` 即为 LibreOffice 的源代码目录。当需要查找 LO 原始代码实现时，应向上查找 `../` 即可访问 `libo-core` 下的各模块（如 `../sw/`、`../vcl/`、`../oox/` 等）。
- **路径对应关系**: 以 `aproj` 为工作区根目录时，`../` = `libo-core/`。例如：
  - `../sw/` → LibreOffice Writer 模块
  - `../vcl/` → LibreOffice VCL 渲染模块
  - `../oox/` → LibreOffice OOXML 解析模块
