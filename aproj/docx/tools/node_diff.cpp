// node_diff.cpp — Nodes 结构差异对比工具
// 对比 LibreOffice 与 aproj/docx 的 nodes 输出（lo_nodes.txt / aproj_nodes.txt）
//
// 根据 render_common/node_format.cxx 中 WriteNodeInstructionToStream() 的实际
// 输出格式，进行逐字段语义解析和精确对比：
//
//   START_NODE   <tab> nodeIndex <tab> (Normal|TableBox|Fly|Footnote|Header|Footer)
//                                             [<tab> anchor=<idx>]           [<tab> refs=<idx,...>]
//   END_NODE     <tab> nodeIndex                                             [<tab> refs=<idx,...>]
//   TEXT_NODE    <tab> nodeIndex <tab> "text"   <tab> styleName             [<tab> refs=<idx,...>]
//   GRF_NODE     <tab> nodeIndex                                             [<tab> refs=<idx,...>]
//   OLE_NODE     <tab> nodeIndex                                             [<tab> refs=<idx,...>]
//   TABLE_START  <tab> nodeIndex <tab> rows      <tab> cols                [<tab> refs=<idx,...>]
//   TABLE_END    <tab> nodeIndex                                             [<tab> refs=<idx,...>]
//   SECTION_START<tab> nodeIndex                                             [<tab> refs=<idx,...>]
//   SECTION_END  <tab> nodeIndex                                             [<tab> refs=<idx,...>]
//
// 另外，结构化部分每行有 2 空格 * nestLevel 的前导缩进。
// "text" 字段使用双引号包裹，内部的 \n / \t / \" / \r 被字面转义写入（见
// EscapeForTsv() in node_format.cxx），因此不能简单按 \t 切分。
//
// 用法:
//   node_diff <ref.txt> <test.txt>              对比
//   node_diff <ref.txt> <test.txt> --verbose    同时显示匹配项
//
// 编译：C++17 标准库，单文件。

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <cstdint>

// ----------------------------------------------------------------------------
// 工具：裁剪空白
// ----------------------------------------------------------------------------
static std::string trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r'))
        a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r'))
        b--;
    return s.substr(a, b - a);
}

// ----------------------------------------------------------------------------
// NodeEntry：统一的节点条目
// ----------------------------------------------------------------------------
enum class NodeType : std::uint8_t
{
    UNKNOWN = 0,
    START_NODE,
    END_NODE,
    TEXT_NODE,
    GRF_NODE,
    OLE_NODE,
    TABLE_START,
    TABLE_END,
    SECTION_START,
    SECTION_END,
};

static const char* NodeTypeName(NodeType t)
{
    switch (t)
    {
        case NodeType::START_NODE:
            return "START_NODE";
        case NodeType::END_NODE:
            return "END_NODE";
        case NodeType::TEXT_NODE:
            return "TEXT_NODE";
        case NodeType::GRF_NODE:
            return "GRF_NODE";
        case NodeType::OLE_NODE:
            return "OLE_NODE";
        case NodeType::TABLE_START:
            return "TABLE_START";
        case NodeType::TABLE_END:
            return "TABLE_END";
        case NodeType::SECTION_START:
            return "SECTION_START";
        case NodeType::SECTION_END:
            return "SECTION_END";
        default:
            return "UNKNOWN";
    }
}

static NodeType ParseNodeType(const std::string& tok)
{
    if (tok == "START_NODE")
        return NodeType::START_NODE;
    if (tok == "END_NODE")
        return NodeType::END_NODE;
    if (tok == "TEXT_NODE")
        return NodeType::TEXT_NODE;
    if (tok == "GRF_NODE")
        return NodeType::GRF_NODE;
    if (tok == "OLE_NODE")
        return NodeType::OLE_NODE;
    if (tok == "TABLE_START")
        return NodeType::TABLE_START;
    if (tok == "TABLE_END")
        return NodeType::TABLE_END;
    if (tok == "SECTION_START")
        return NodeType::SECTION_START;
    if (tok == "SECTION_END")
        return NodeType::SECTION_END;
    return NodeType::UNKNOWN;
}

struct NodeEntry
{
    int lineNum = 0; // 源文件行号
    int indent = 0; // 缩进层级（= 前导空格数 / 2）
    NodeType type = NodeType::UNKNOWN;

    // 所有节点通用
    int nodeIndex = -1;

    // START_NODE 专用
    std::string startNodeSubType; // Normal / TableBox / Fly / Footnote / Header / Footer / ""
    int anchorNodeIndex = -1; // -1 表示无 anchor=

    // TEXT_NODE 专用
    std::string text; // 已解转义的文本内容（原样，不包括引号）
    std::string styleName; // 可能为空（不强制 Default Paragraph Style）

    // TABLE_START 专用
    int tableRows = -1;
    int tableCols = -1;

    // 通用：refs= 字段，可能为空
    std::string refs;
};

// ----------------------------------------------------------------------------
// 解转义：与 node_format.cxx 中的 EscapeForTsv 反向
//   \n -> LF,  \t -> TAB,  \" -> ",  \r -> CR
// 其余字符保持不变。输入是引号内部的原始文本内容。
// ----------------------------------------------------------------------------
static std::string UnescapeForTsv(const std::string& raw)
{
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i)
    {
        if (raw[i] == '\\' && i + 1 < raw.size())
        {
            switch (raw[i + 1])
            {
                case 'n':
                    out += '\n';
                    i++;
                    break;
                case 't':
                    out += '\t';
                    i++;
                    break;
                case '"':
                    out += '"';
                    i++;
                    break;
                case 'r':
                    out += '\r';
                    i++;
                    break;
                case '\\':
                    out += '\\';
                    i++;
                    break;
                default:
                    out += raw[i];
                    break;
            }
        }
        else
        {
            out += raw[i];
        }
    }
    return out;
}

// ----------------------------------------------------------------------------
// 解析一行：从结构化区域的 "TYPE\t..." 中提取字段
//
// 关键难点：TEXT_NODE 的 "text" 字段内部可能含有字面 \t、\n，所以必须以
//           " 字符作为引号边界处理，而不能一刀切地按 \t 切分。
// ----------------------------------------------------------------------------
static std::vector<std::string> TokenizeNodeLine(const std::string& content,
                                                 std::string& parseError)
{
    std::vector<std::string> tokens;
    size_t i = 0;
    const size_t n = content.size();

    // 第 1 个 token：类型（不允许引号）
    {
        size_t j = content.find('\t', i);
        if (j == std::string::npos)
        {
            // 可能没有任何 tab —— 只包含类型
            tokens.push_back(trim(content.substr(i)));
            return tokens;
        }
        tokens.push_back(trim(content.substr(i, j - i)));
        i = j + 1;
    }

    // 后续 tokens：以 \t 为分隔；若某 token 首字符是 " 则按引号解析
    while (i < n)
    {
        // 跳过可能的前导空白
        size_t s = i;
        while (s < n && content[s] == ' ')
            s++;

        if (s < n && content[s] == '"')
        {
            // 引号 token：寻找结束引号（考虑转义）
            size_t k = s + 1;
            std::string raw;
            while (k < n)
            {
                if (content[k] == '\\' && k + 1 < n)
                {
                    raw += content[k];
                    raw += content[k + 1];
                    k += 2;
                    continue;
                }
                if (content[k] == '"')
                {
                    k++; // 越过结束引号
                    break;
                }
                raw += content[k];
                k++;
            }
            tokens.push_back(UnescapeForTsv(raw));
            // 下一个字段（可能存在）
            if (k < n && content[k] == '\t')
                i = k + 1;
            else
                i = k;
        }
        else
        {
            // 普通 token：读到下一个 \t
            size_t j = content.find('\t', i);
            if (j == std::string::npos)
            {
                tokens.push_back(trim(content.substr(i)));
                i = n;
            }
            else
            {
                tokens.push_back(trim(content.substr(i, j - i)));
                i = j + 1;
            }
        }
    }

    (void)parseError;
    return tokens;
}

// ----------------------------------------------------------------------------
// 解析单行
// ----------------------------------------------------------------------------
static bool ParseEntry(const std::string& line, int lineNum, NodeEntry& out)
{
    out.lineNum = lineNum;

    // 计算前导空格缩进（2 空格 = 1 level）
    size_t spaces = 0;
    while (spaces < line.size() && line[spaces] == ' ')
        spaces++;
    out.indent = static_cast<int>(spaces / 2);

    std::string content = (spaces < line.size()) ? line.substr(spaces) : std::string();
    if (content.empty())
        return false;

    // 切分成 tokens（识别 TEXT_NODE 的引号字段）
    std::string err;
    auto tokens = TokenizeNodeLine(content, err);
    if (tokens.empty())
        return false;

    out.type = ParseNodeType(tokens[0]);
    if (out.type == NodeType::UNKNOWN)
        return false;

    // nodeIndex：所有类型都需要第 2 个 token 作为整数索引
    if (tokens.size() < 2)
        return false;
    try
    {
        out.nodeIndex = std::stoi(tokens[1]);
    }
    catch (...)
    {
        return false;
    }

    // 类型相关字段
    switch (out.type)
    {
        case NodeType::START_NODE:
        {
            // tokens[2] = 子类型；可能 tokens[3] = anchor=xxx；可能末尾 tokens[...] = refs=xxx
            for (size_t k = 2; k < tokens.size(); ++k)
            {
                const auto& t = tokens[k];
                if (t.rfind("anchor=", 0) == 0)
                {
                    try
                    {
                        out.anchorNodeIndex = std::stoi(t.substr(7));
                    }
                    catch (...)
                    {
                        out.anchorNodeIndex = -1;
                    }
                }
                else if (t.rfind("refs=", 0) == 0)
                {
                    out.refs = t.substr(5);
                }
                else if (out.startNodeSubType.empty())
                {
                    out.startNodeSubType = t;
                }
                else
                {
                    // 未知字段（保留到 refs 里作回退）—— 避免漏判
                    // 这里不做处理，保持严格模式。
                }
            }
            break;
        }
        case NodeType::TEXT_NODE:
        {
            // tokens[2] = text（引号内已解转义）；tokens[3] = styleName；可能 tokens[4] = refs=...
            if (tokens.size() >= 3)
                out.text = tokens[2];
            if (tokens.size() >= 4)
            {
                // styleName 可能以 "refs=" 开头（若 style 缺失时），判断一下
                if (tokens[3].rfind("refs=", 0) == 0)
                {
                    out.refs = tokens[3].substr(5);
                }
                else
                {
                    out.styleName = tokens[3];
                }
            }
            if (tokens.size() >= 5)
            {
                if (tokens[4].rfind("refs=", 0) == 0)
                    out.refs = tokens[4].substr(5);
            }
            break;
        }
        case NodeType::TABLE_START:
        {
            if (tokens.size() >= 3)
            {
                try
                {
                    out.tableRows = std::stoi(tokens[2]);
                }
                catch (...)
                {
                }
            }
            if (tokens.size() >= 4)
            {
                try
                {
                    out.tableCols = std::stoi(tokens[3]);
                }
                catch (...)
                {
                }
            }
            if (tokens.size() >= 5)
            {
                if (tokens[4].rfind("refs=", 0) == 0)
                    out.refs = tokens[4].substr(5);
            }
            break;
        }
        case NodeType::END_NODE:
        case NodeType::GRF_NODE:
        case NodeType::OLE_NODE:
        case NodeType::TABLE_END:
        case NodeType::SECTION_START:
        case NodeType::SECTION_END:
        default:
        {
            // 仅可能存在 refs=xxx 在第 3 个或之后 token
            for (size_t k = 2; k < tokens.size(); ++k)
            {
                if (tokens[k].rfind("refs=", 0) == 0)
                {
                    out.refs = tokens[k].substr(5);
                    break;
                }
            }
            break;
        }
    }

    return true;
}

// ----------------------------------------------------------------------------
// 文件头 vs 结构化判断
// ----------------------------------------------------------------------------
static bool IsStructuredNodeLine(const std::string& lineTrimmed)
{
    if (lineTrimmed.empty())
        return false;
    if (lineTrimmed[0] == '#' || lineTrimmed[0] == '=')
        return false;

    // 跳过前导空格
    size_t i = 0;
    while (i < lineTrimmed.size() && lineTrimmed[i] == ' ')
        i++;
    size_t tokenEnd = lineTrimmed.find_first_of("\t ", i);
    std::string firstTok = lineTrimmed.substr(i, tokenEnd - i);
    return ParseNodeType(firstTok) != NodeType::UNKNOWN;
}

static std::vector<NodeEntry> ParseNodesFile(const std::string& path, std::string& outError)
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
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (!inStructured)
        {
            if (IsStructuredNodeLine(line))
                inStructured = true;
            else
                continue;
        }
        else
        {
            // 结构化区内的空行：跳过
            if (trim(line).empty())
                continue;
            // 注释行（如 "All Nodes ..." 之类的头部再次出现）：跳过
            if (line[0] == '#' || line[0] == '=')
                continue;
        }

        NodeEntry e;
        if (ParseEntry(line, lineNum, e))
            entries.push_back(e);
    }

    if (!inStructured)
        outError = "No structured node entries found in file: " + path;
    return entries;
}

// ----------------------------------------------------------------------------
// 对比两个 NodeEntry 条目，返回精确的差异说明
// ----------------------------------------------------------------------------
struct DiffMessage
{
    int refLine;
    int testLine;
    std::string msg;
};

static bool NodeEntriesEqual(const NodeEntry& a, const NodeEntry& b)
{
    if (a.type != b.type)
        return false;
    if (a.nodeIndex != b.nodeIndex)
        return false;
    if (a.indent != b.indent)
        return false;

    switch (a.type)
    {
        case NodeType::START_NODE:
            if (a.startNodeSubType != b.startNodeSubType)
                return false;
            if (a.anchorNodeIndex != b.anchorNodeIndex)
                return false;
            break;
        case NodeType::TEXT_NODE:
            if (a.text != b.text)
                return false;
            if (a.styleName != b.styleName)
                return false;
            break;
        case NodeType::TABLE_START:
            if (a.tableRows != b.tableRows)
                return false;
            if (a.tableCols != b.tableCols)
                return false;
            break;
        default:
            break;
    }
    if (a.refs != b.refs)
        return false;
    return true;
}

static std::string QuoteForDisplay(const std::string& s)
{
    // 用于 diff 消息中的展示——对空白字符做可视化。
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s)
    {
        switch (c)
        {
            case '\n':
                out += "\\n";
                break;
            case '\t':
                out += "\\t";
                break;
            case '\r':
                out += "\\r";
                break;
            case '"':
                out += "\\\"";
                break;
            default:
                out += c;
                break;
        }
    }
    return "\"" + out + "\"";
}

static std::string CollectFieldDiffs(const NodeEntry& ref, const NodeEntry& test)
{
    std::string msg;

    auto addLine = [&](const std::string& field, const std::string& rv, const std::string& tv) {
        if (!msg.empty())
            msg += " | ";
        msg += field + ": ref=" + rv + " test=" + tv;
    };

    if (ref.type != test.type)
        addLine("type", NodeTypeName(ref.type), NodeTypeName(test.type));
    if (ref.nodeIndex != test.nodeIndex)
        addLine("nodeIndex", std::to_string(ref.nodeIndex), std::to_string(test.nodeIndex));
    if (ref.indent != test.indent)
        addLine("nestLevel", std::to_string(ref.indent), std::to_string(test.indent));

    switch (ref.type)
    {
        case NodeType::START_NODE:
            if (ref.startNodeSubType != test.startNodeSubType)
                addLine("subType", ref.startNodeSubType, test.startNodeSubType);
            if (ref.anchorNodeIndex != test.anchorNodeIndex)
                addLine("anchor",
                        ref.anchorNodeIndex == -1 ? "<none>" : std::to_string(ref.anchorNodeIndex),
                        test.anchorNodeIndex == -1 ? "<none>"
                                                   : std::to_string(test.anchorNodeIndex));
            break;
        case NodeType::TEXT_NODE:
            if (ref.text != test.text)
                addLine("text", QuoteForDisplay(ref.text), QuoteForDisplay(test.text));
            if (ref.styleName != test.styleName)
                addLine("styleName", QuoteForDisplay(ref.styleName),
                        QuoteForDisplay(test.styleName));
            break;
        case NodeType::TABLE_START:
            if (ref.tableRows != test.tableRows)
                addLine("rows", std::to_string(ref.tableRows), std::to_string(test.tableRows));
            if (ref.tableCols != test.tableCols)
                addLine("cols", std::to_string(ref.tableCols), std::to_string(test.tableCols));
            break;
        default:
            break;
    }

    if (ref.refs != test.refs)
        addLine("refs", ref.refs.empty() ? "<none>" : ref.refs,
                test.refs.empty() ? "<none>" : test.refs);

    return msg;
}

// ----------------------------------------------------------------------------
// 主程序
// ----------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    std::string refPath;
    std::string testPath;
    bool verbose = false;

    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "--verbose" || a == "-v")
            verbose = true;
        else if (refPath.empty())
            refPath = a;
        else if (testPath.empty())
            testPath = a;
    }

    if (refPath.empty() || testPath.empty())
    {
        std::cerr << "node_diff — 对比两个 nodes.txt 结构差异（C++17, 单文件）\n\n"
                  << "Usage:\n"
                  << "  node_diff <ref.txt> <test.txt> [--verbose]\n"
                  << "\n"
                  << "Exit code: 0 = 无差异, 1 = 有差异或错误.\n";
        return 1;
    }

    std::string refErr, testErr;
    auto ref = ParseNodesFile(refPath, refErr);
    auto test = ParseNodesFile(testPath, testErr);

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

    std::cout << "=== Nodes Structure Comparison (Precise) ===" << std::endl;
    std::cout << "Reference: " << refPath << " (" << ref.size() << " nodes)" << std::endl;
    std::cout << "Test:      " << testPath << " (" << test.size() << " nodes)" << std::endl;
    std::cout << std::endl;

    // 汇总差异列表
    std::vector<DiffMessage> diffs;
    diffs.reserve(std::max(ref.size(), test.size()));

    size_t maxN = std::max(ref.size(), test.size());
    for (size_t i = 0; i < maxN; ++i)
    {
        if (i >= ref.size())
        {
            // test 比 ref 多
            std::ostringstream oss;
            oss << "额外节点（ref 不存在）: " << NodeTypeName(test[i].type)
                << " nodeIndex=" << test[i].nodeIndex << " nestLevel=" << test[i].indent;
            if (test[i].type == NodeType::TEXT_NODE)
            {
                oss << " text=" << QuoteForDisplay(test[i].text)
                    << " style=" << QuoteForDisplay(test[i].styleName);
            }
            if (!test[i].refs.empty())
                oss << " refs=" << test[i].refs;
            diffs.push_back({ 0, test[i].lineNum, oss.str() });
            continue;
        }
        if (i >= test.size())
        {
            // ref 比 test 多
            std::ostringstream oss;
            oss << "缺失节点（test 不存在）: " << NodeTypeName(ref[i].type)
                << " nodeIndex=" << ref[i].nodeIndex << " nestLevel=" << ref[i].indent;
            if (ref[i].type == NodeType::TEXT_NODE)
            {
                oss << " text=" << QuoteForDisplay(ref[i].text)
                    << " style=" << QuoteForDisplay(ref[i].styleName);
            }
            if (!ref[i].refs.empty())
                oss << " refs=" << ref[i].refs;
            diffs.push_back({ ref[i].lineNum, 0, oss.str() });
            continue;
        }
        if (!NodeEntriesEqual(ref[i], test[i]))
        {
            diffs.push_back(
                { ref[i].lineNum, test[i].lineNum, CollectFieldDiffs(ref[i], test[i]) });
        }
    }

    // 详细输出（--verbose）：显示所有匹配对
    if (verbose)
    {
        std::cout << "-- verbose: matching pairs shown --\n";
        size_t n = std::min(ref.size(), test.size());
        for (size_t i = 0; i < n; ++i)
        {
            if (NodeEntriesEqual(ref[i], test[i]))
            {
                std::cout << "  [OK]  ref@" << ref[i].lineNum << "  test@" << test[i].lineNum
                          << "  " << NodeTypeName(ref[i].type) << "  nodeIndex=" << ref[i].nodeIndex
                          << "  nestLevel=" << ref[i].indent;
                if (ref[i].type == NodeType::TEXT_NODE)
                    std::cout << "  text=" << QuoteForDisplay(ref[i].text)
                              << "  style=" << QuoteForDisplay(ref[i].styleName);
                if (ref[i].type == NodeType::START_NODE)
                {
                    std::cout << "  subType=" << ref[i].startNodeSubType;
                    if (ref[i].anchorNodeIndex >= 0)
                        std::cout << "  anchor=" << ref[i].anchorNodeIndex;
                }
                if (ref[i].type == NodeType::TABLE_START)
                    std::cout << "  rows=" << ref[i].tableRows << "  cols=" << ref[i].tableCols;
                if (!ref[i].refs.empty())
                    std::cout << "  refs=" << ref[i].refs;
                std::cout << "\n";
            }
        }
        std::cout << "\n";
    }

    if (diffs.empty())
    {
        std::cout << "Result: PASS — 无结构差异。\n";
        return 0;
    }

    std::cout << "Differences: " << diffs.size() << "\n\n";
    for (const auto& d : diffs)
    {
        std::cout << "  [DIFF] ref@" << d.refLine << "  test@" << d.testLine << "  " << d.msg
                  << "\n";
    }
    std::cout << "\nResult: FAIL — " << diffs.size() << " 处差异。\n";
    return 1;
}
