// node_diff.cpp — Nodes 结构差异对比工具
// 对比 LibreOffice 与 aproj/docx 的 nodes 输出（lo_nodes.txt / aproj_nodes.txt）
//
// 支持两种输入格式（可自动识别）：
//   A. 结构化 (structured)：直接以 <TYPE>\t<index>\t... 行构成，前导空格表示缩进层级
//   B. 平面+结构化混合 (mixed)：顶部若干 "# " 注释 / 空行 / 结构头（如 "Body Area Nodes (structured):"），
//      之后进入与 A 相同的结构化部分。
//
// 用法:
//   node_diff <ref.txt> <test.txt>          对比任意两个 nodes 文件
//   node_diff <ref.txt> <test.txt> --verbose   显示所有节点（含匹配项）
//
// 编译：见项目根 CMakeLists.txt —— 该文件为独立的 STANDALONE 单文件工具，
//       不依赖 render_diff / render_common，仅使用 C++17 标准库。

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdlib>

// ── Node 条目：一个节点一行（结构化部分） ──
struct NodeEntry
{
    int lineNum; // 源文件中原始行号（用于 diff 输出定位）
    int indent; // 缩进层级（以 2 空格为 1 级）
    std::string
        type; // 第一个字段: START_NODE / END_NODE / TEXT_NODE / GRF_NODE / TABLE_START / TABLE_END / SECTION_START / SECTION_END ...
    int index; // 第二个字段: 节点索引
    std::string rawRest; // 除前两字段外的剩余原始文本（用于附加属性对比）
};

// ── 工具：裁剪字符串两端空白 ──
static std::string trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r'))
        a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r'))
        b--;
    return s.substr(a, b - a);
}

// ── 工具：按 \t 拆分，最多 nParts 段（最后一段保留剩余所有内容） ──
static std::vector<std::string> splitTab(const std::string& line, int nParts)
{
    std::vector<std::string> out;
    out.reserve(nParts);
    size_t start = 0;
    int parts = 0;
    while (parts + 1 < nParts)
    {
        size_t tab = line.find('\t', start);
        if (tab == std::string::npos)
            break;
        out.push_back(line.substr(start, tab - start));
        start = tab + 1;
        parts++;
    }
    out.push_back(line.substr(start));
    return out;
}

// ── 判断行是否为"结构化"节点行（不以 "# " 开头，非空行，首字段为 XXXX_NODE / TABLE_* / SECTION_*） ──
static bool isStructuredNodeLine(const std::string& line)
{
    std::string t = trim(line);
    if (t.empty())
        return false;
    // 注释 / 标题行
    if (t[0] == '#' || t[0] == '=' || t[0] == '-')
        return false;
    // 跳过前导空格，得到第一个 token
    size_t i = 0;
    while (i < t.size() && t[i] == ' ')
        i++;
    size_t tokenEnd = t.find_first_of("\t ", i);
    std::string firstTok = t.substr(i, tokenEnd - i);
    // 合法的节点类型关键字（大小写敏感）
    auto endsWithNode = [](const std::string& s) {
        if (s.size() < 5)
            return false;
        return (s.compare(s.size() - 5, 5, "_NODE") == 0);
    };
    if (endsWithNode(firstTok))
        return true;
    if (firstTok == "TABLE_START" || firstTok == "TABLE_END")
        return true;
    if (firstTok == "SECTION_START" || firstTok == "SECTION_END")
        return true;
    return false;
}

// ── 解析文件：跳过顶部非结构化段，从第一行结构化数据开始读 ──
static std::vector<NodeEntry> parseNodesFile(const std::string& path, std::string& outError)
{
    std::vector<NodeEntry> entries;
    std::ifstream f(path);
    if (!f.is_open())
    {
        outError = "Cannot open file: " + path;
        return entries;
    }

    std::string line;
    int lineNum = 0;
    bool inStructured = false;

    while (std::getline(f, line))
    {
        lineNum++;
        // 去掉行尾 \r
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (!inStructured)
        {
            // 遇到首个结构化节点行 → 进入结构化部分
            if (isStructuredNodeLine(line))
                inStructured = true;
            else
                continue; // 跳过文件头、注释、空行
        }

        std::string trimmed = line;
        // 计算缩进（以 2 空格为一级）
        int spaces = 0;
        size_t si = 0;
        while (si < trimmed.size() && trimmed[si] == ' ')
        {
            spaces++;
            si++;
        }
        int indent = spaces / 2;

        std::string content = (si < trimmed.size()) ? trimmed.substr(si) : "";
        if (content.empty())
            continue;

        // 按 \t 拆分为 [type, index, rest...]
        auto parts = splitTab(content, 3);
        if (parts.size() < 1)
            continue;

        NodeEntry e;
        e.lineNum = lineNum;
        e.indent = indent;
        e.type = parts[0];
        e.index = -1;
        if (parts.size() >= 2)
        {
            try
            {
                e.index = std::stoi(trim(parts[1]));
            }
            catch (...)
            {
                e.index = -1;
            }
        }
        e.rawRest = (parts.size() >= 3) ? parts[2] : "";

        entries.push_back(e);
    }

    if (!inStructured)
    {
        outError = "No structured node entries found in file: " + path;
    }
    return entries;
}

// ── 对两个节点进行内容对比 ──
struct DiffItem
{
    int refLine;
    int testLine;
    int refIndex;
    int testIndex;
    std::string message;
};

static bool entriesEqual(const NodeEntry& a, const NodeEntry& b)
{
    return (a.indent == b.indent) && (a.type == b.type) && (a.index == b.index)
           && (a.rawRest == b.rawRest);
}

static std::string describeDiff(const NodeEntry& ref, const NodeEntry& test)
{
    std::string msg;
    if (ref.type != test.type)
        msg += " type=ref=" + ref.type + "/test=" + test.type;
    if (ref.index != test.index)
        msg += " index=ref=" + std::to_string(ref.index) + "/test=" + std::to_string(test.index);
    if (ref.indent != test.indent)
        msg += " indent=ref=" + std::to_string(ref.indent) + "/test=" + std::to_string(test.indent);
    if (ref.rawRest != test.rawRest)
        msg += " attrs=ref=[" + ref.rawRest + "]/test=[" + test.rawRest + "]";
    return msg;
}

// ── Main ──
int main(int argc, char* argv[])
{
    std::string refPath;
    std::string testPath;
    bool verbose = false;

    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "--verbose")
            verbose = true;
        else if (a == "-v")
            verbose = true;
        else if (refPath.empty())
            refPath = a;
        else if (testPath.empty())
            testPath = a;
    }

    if (refPath.empty() || testPath.empty())
    {
        std::cerr << "node_diff — 对比两个 nodes.txt 结构差异 (STANDALONE, 不依赖 render_diff)\n\n"
                  << "Usage:\n"
                  << "  node_diff <ref.txt> <test.txt> [--verbose]\n"
                  << "\n"
                  << "Exit code: 0 = 无差异, 1 = 有差异或错误.\n";
        return 1;
    }

    // 读取
    std::string refErr, testErr;
    auto ref = parseNodesFile(refPath, refErr);
    auto test = parseNodesFile(testPath, testErr);

    if (!refErr.empty())
    {
        std::cerr << "[ERROR] " << refErr << "\n";
        return 1;
    }
    if (!testErr.empty())
    {
        std::cerr << "[ERROR] " << testErr << "\n";
        return 1;
    }

    // 顺序 + 类型 + 属性 严格对比
    std::vector<DiffItem> diffs;
    size_t maxN = std::max(ref.size(), test.size());
    for (size_t i = 0; i < maxN; ++i)
    {
        if (i >= ref.size())
        {
            diffs.push_back({ 0, test[i].lineNum, -1, test[i].index,
                              "额外节点: " + test[i].type
                                  + " index=" + std::to_string(test[i].index) + " attrs=["
                                  + test[i].rawRest + "]" });
            continue;
        }
        if (i >= test.size())
        {
            diffs.push_back({ ref[i].lineNum, 0, ref[i].index, -1,
                              "缺失节点: " + ref[i].type + " index=" + std::to_string(ref[i].index)
                                  + " attrs=[" + ref[i].rawRest + "]" });
            continue;
        }
        if (!entriesEqual(ref[i], test[i]))
            diffs.push_back({ ref[i].lineNum, test[i].lineNum, ref[i].index, test[i].index,
                              describeDiff(ref[i], test[i]) });
    }

    // 输出报告
    std::cout << "=== Nodes Structure Comparison ===" << std::endl;
    std::cout << "Reference: " << refPath << " (" << ref.size() << " nodes)" << std::endl;
    std::cout << "Test:      " << testPath << " (" << test.size() << " nodes)" << std::endl;
    std::cout << std::endl;

    if (verbose)
    {
        std::cout << "-- verbose: matching pairs shown --" << std::endl;
        size_t n = std::min(ref.size(), test.size());
        for (size_t i = 0; i < n; ++i)
        {
            if (entriesEqual(ref[i], test[i]))
            {
                std::cout << "  [OK]  ref@" << ref[i].lineNum << "  test@" << test[i].lineNum
                          << "  " << ref[i].type << "  index=" << ref[i].index << "  ["
                          << ref[i].rawRest << "]" << std::endl;
            }
        }
        std::cout << std::endl;
    }

    if (diffs.empty())
    {
        std::cout << "Result: PASS — 无结构差异。" << std::endl;
        return 0;
    }

    std::cout << "Differences: " << diffs.size() << std::endl << std::endl;
    for (const auto& d : diffs)
    {
        std::cout << "  [DIFF] ref@" << d.refLine << "(idx=" << d.refIndex << ")"
                  << "  test@" << d.testLine << "(idx=" << d.testIndex << ")"
                  << "  " << d.message << std::endl;
    }
    std::cout << std::endl;
    std::cout << "Result: FAIL — " << diffs.size() << " 处差异。" << std::endl;
    return 1;
}
