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
//   node_diff <ref.txt> <test.txt>                    逐行对比（原始模式）
//   node_diff <ref.txt> <test.txt> --algo=lcs        LCS 算法对比
//   node_diff <ref.txt> <test.txt> --algo=myers      Myers Diff 算法对比
//   node_diff <ref.txt> <test.txt> --algo=needleman  Needleman-Wunsch 算法对比
//   node_diff <ref.txt> <test.txt> --all             输出所有算法结果
//   node_diff <ref.txt> <test.txt> --verbose          同时显示匹配项
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
// Diff 操作类型（用于对齐算法）
// ----------------------------------------------------------------------------
enum class DiffOp
{
    EQUAL, // 匹配
    INSERT, // test 中新增
    DELETE, // ref 中删除
    MODIFY // 字段修改（ref 和 test 都存在但不相等）
};

struct DiffResult
{
    DiffOp op;
    const NodeEntry* refEntry; // ref 中的条目（可为 nullptr）
    const NodeEntry* testEntry; // test 中的条目（可为 nullptr）
    int refIdx; // ref 中的索引（-1 表示不存在）
    int testIdx; // test 中的索引（-1 表示不存在）
    std::string description; // 差异描述
};

// 获取 DiffOp 的字符串表示
static const char* DiffOpName(DiffOp op)
{
    switch (op)
    {
        case DiffOp::EQUAL:
            return "EQUAL";
        case DiffOp::INSERT:
            return "INSERT";
        case DiffOp::DELETE:
            return "DELETE";
        case DiffOp::MODIFY:
            return "MODIFY";
        default:
            return "UNKNOWN";
    }
}

// 生成节点条目的简短描述
static std::string EntryToShortDesc(const NodeEntry& e)
{
    std::ostringstream oss;
    oss << NodeTypeName(e.type) << " idx=" << e.nodeIndex << " lvl=" << e.indent;
    if (e.type == NodeType::TEXT_NODE)
    {
        oss << " text=" << QuoteForDisplay(e.text);
        if (!e.styleName.empty())
            oss << " style=" << QuoteForDisplay(e.styleName);
    }
    else if (e.type == NodeType::START_NODE)
    {
        if (!e.startNodeSubType.empty())
            oss << " subType=" << e.startNodeSubType;
        if (e.anchorNodeIndex >= 0)
            oss << " anchor=" << e.anchorNodeIndex;
    }
    else if (e.type == NodeType::TABLE_START)
    {
        oss << " rows=" << e.tableRows << " cols=" << e.tableCols;
    }
    return oss.str();
}

// ----------------------------------------------------------------------------
// 算法 1: LCS (最长公共子序列)
// ----------------------------------------------------------------------------
static std::vector<DiffResult> ComputeLcsDiff(const std::vector<NodeEntry>& ref,
                                              const std::vector<NodeEntry>& test)
{
    size_t m = ref.size();
    size_t n = test.size();

    // dp[i][j] = LCS 长度 for ref[0..i-1], test[0..j-1]
    std::vector<std::vector<size_t>> dp(m + 1, std::vector<size_t>(n + 1, 0));

    for (size_t i = 1; i <= m; ++i)
    {
        for (size_t j = 1; j <= n; ++j)
        {
            if (NodeEntriesEqual(ref[i - 1], test[j - 1]))
                dp[i][j] = dp[i - 1][j - 1] + 1;
            else
                dp[i][j] = std::max(dp[i - 1][j], dp[i][j - 1]);
        }
    }

    // 回溯构建 diff 结果
    std::vector<DiffResult> results;
    int i = static_cast<int>(m), j = static_cast<int>(n);

    while (i > 0 || j > 0)
    {
        if (i > 0 && j > 0 && NodeEntriesEqual(ref[i - 1], test[j - 1]))
        {
            DiffResult r;
            r.op = DiffOp::EQUAL;
            r.refEntry = &ref[i - 1];
            r.testEntry = &test[j - 1];
            r.refIdx = i - 1;
            r.testIdx = j - 1;
            r.description = "";
            results.push_back(r);
            --i;
            --j;
        }
        else if (i > 0 && (j == 0 || dp[i - 1][j] >= dp[i][j - 1]))
        {
            DiffResult r;
            r.op = DiffOp::DELETE;
            r.refEntry = &ref[i - 1];
            r.testEntry = nullptr;
            r.refIdx = i - 1;
            r.testIdx = -1;
            std::ostringstream oss;
            oss << "缺失节点: " << EntryToShortDesc(ref[i - 1]);
            r.description = oss.str();
            results.push_back(r);
            --i;
        }
        else
        {
            DiffResult r;
            r.op = DiffOp::INSERT;
            r.refEntry = nullptr;
            r.testEntry = &test[j - 1];
            r.refIdx = -1;
            r.testIdx = j - 1;
            std::ostringstream oss;
            oss << "额外节点: " << EntryToShortDesc(test[j - 1]);
            r.description = oss.str();
            results.push_back(r);
            --j;
        }
    }

    std::reverse(results.begin(), results.end());
    return results;
}

// ----------------------------------------------------------------------------
// 算法 2: Myers Diff (经典 diff 算法，O(ND))
// 参考 Eugene W. Myers, "An O(ND) Difference Algorithm and Its Variations"
// V[k] 存储到达对角线 k 的最远 x 坐标；k = x - y
// ----------------------------------------------------------------------------
static std::vector<DiffResult> ComputeMyersDiff(const std::vector<NodeEntry>& ref,
                                                const std::vector<NodeEntry>& test)
{
    const int N = static_cast<int>(ref.size());
    const int M = static_cast<int>(test.size());

    if (N == 0)
    {
        std::vector<DiffResult> results;
        for (int j = 0; j < M; ++j)
        {
            DiffResult r;
            r.op = DiffOp::INSERT;
            r.refEntry = nullptr;
            r.testEntry = &test[j];
            r.refIdx = -1;
            r.testIdx = j;
            std::ostringstream oss;
            oss << "额外节点: " << EntryToShortDesc(test[j]);
            r.description = oss.str();
            results.push_back(r);
        }
        return results;
    }
    if (M == 0)
    {
        std::vector<DiffResult> results;
        for (int i = 0; i < N; ++i)
        {
            DiffResult r;
            r.op = DiffOp::DELETE;
            r.refEntry = &ref[i];
            r.testEntry = nullptr;
            r.refIdx = i;
            r.testIdx = -1;
            std::ostringstream oss;
            oss << "缺失节点: " << EntryToShortDesc(ref[i]);
            r.description = oss.str();
            results.push_back(r);
        }
        return results;
    }

    const int maxD = N + M;
    const int offset = maxD;
    std::vector<int> V(2 * maxD + 1, 0);
    auto Vget = [&](int k) { return V[static_cast<size_t>(k + offset)]; };
    auto Vset = [&](int k, int val) { V[static_cast<size_t>(k + offset)] = val; };

    Vset(1, 0);
    std::vector<std::vector<int>> trace;
    trace.reserve(static_cast<size_t>(maxD + 1));

    int endD = maxD;
    for (int d = 0; d <= maxD; ++d)
    {
        trace.push_back(V);
        for (int k = -d; k <= d; k += 2)
        {
            int x;
            if (k == -d || (k != d && Vget(k - 1) < Vget(k + 1)))
                x = Vget(k + 1);
            else
                x = Vget(k - 1) + 1;

            int y = x - k;
            while (x < N && y < M
                   && NodeEntriesEqual(ref[static_cast<size_t>(x)], test[static_cast<size_t>(y)]))
            {
                ++x;
                ++y;
            }
            Vset(k, x);

            if (x >= N && y >= M)
            {
                endD = d;
                goto found_path;
            }
        }
    }

found_path:
    std::vector<DiffResult> results;
    int x = N;
    int y = M;

    for (int d = endD; d >= 0; --d)
    {
        int k = x - y;
        const std::vector<int>& prevV = trace[static_cast<size_t>(d > 0 ? d - 1 : 0)];
        auto prevGet = [&](int kk) { return prevV[static_cast<size_t>(kk + offset)]; };

        int prevK;
        if (d == 0)
        {
            prevK = 1;
        }
        else if (k == -d || (k != d && prevGet(k - 1) < prevGet(k + 1)))
            prevK = k + 1;
        else
            prevK = k - 1;

        int prevX = prevGet(prevK);
        int prevY = prevX - prevK;

        while (x > prevX && y > prevY)
        {
            --x;
            --y;
            DiffResult r;
            r.op = DiffOp::EQUAL;
            r.refEntry = &ref[static_cast<size_t>(x)];
            r.testEntry = &test[static_cast<size_t>(y)];
            r.refIdx = x;
            r.testIdx = y;
            r.description = "";
            results.push_back(r);
        }

        if (d > 0)
        {
            if (x > prevX)
            {
                --x;
                DiffResult r;
                r.op = DiffOp::DELETE;
                r.refEntry = &ref[static_cast<size_t>(x)];
                r.testEntry = nullptr;
                r.refIdx = x;
                r.testIdx = -1;
                std::ostringstream oss;
                oss << "缺失节点: " << EntryToShortDesc(ref[static_cast<size_t>(x)]);
                r.description = oss.str();
                results.push_back(r);
            }
            else if (y > prevY)
            {
                --y;
                DiffResult r;
                r.op = DiffOp::INSERT;
                r.refEntry = nullptr;
                r.testEntry = &test[static_cast<size_t>(y)];
                r.refIdx = -1;
                r.testIdx = y;
                std::ostringstream oss;
                oss << "额外节点: " << EntryToShortDesc(test[static_cast<size_t>(y)]);
                r.description = oss.str();
                results.push_back(r);
            }
        }
    }

    std::reverse(results.begin(), results.end());
    return results;
}

// ----------------------------------------------------------------------------
// 算法 3: Needleman-Wunsch (全局序列对齐)
// ----------------------------------------------------------------------------
static std::vector<DiffResult> ComputeNeedlemanWunschDiff(const std::vector<NodeEntry>& ref,
                                                          const std::vector<NodeEntry>& test)
{
    size_t m = ref.size();
    size_t n = test.size();

    const int GAP_PENALTY = -1; // 空位惩罚
    const int MATCH_SCORE = 2; // 匹配得分
    const int MISMATCH_PENALTY = -1; // 不匹配惩罚

    // dp[i][j] = 最大对齐得分 for ref[0..i-1], test[0..j-1]
    std::vector<std::vector<int>> dp(m + 1, std::vector<int>(n + 1, 0));

    // 初始化第一行和第一列
    for (size_t i = 1; i <= m; ++i)
        dp[i][0] = dp[i - 1][0] + GAP_PENALTY;
    for (size_t j = 1; j <= n; ++j)
        dp[0][j] = dp[0][j - 1] + GAP_PENALTY;

    // 填充矩阵
    for (size_t i = 1; i <= m; ++i)
    {
        for (size_t j = 1; j <= n; ++j)
        {
            int match = NodeEntriesEqual(ref[i - 1], test[j - 1]) ? MATCH_SCORE : MISMATCH_PENALTY;
            int scoreDiag = dp[i - 1][j - 1] + match;
            int scoreUp = dp[i - 1][j] + GAP_PENALTY; // 删除
            int scoreLeft = dp[i][j - 1] + GAP_PENALTY; // 插入
            dp[i][j] = std::max({ scoreDiag, scoreUp, scoreLeft });
        }
    }

    // 回溯找对齐路径
    std::vector<DiffResult> results;
    int i = static_cast<int>(m), j = static_cast<int>(n);

    while (i > 0 || j > 0)
    {
        if (i > 0 && j > 0)
        {
            int diagScore
                = dp[i - 1][j - 1]
                  + (NodeEntriesEqual(ref[i - 1], test[j - 1]) ? MATCH_SCORE : MISMATCH_PENALTY);
            if (dp[i][j] == diagScore)
            {
                DiffResult r;
                if (NodeEntriesEqual(ref[i - 1], test[j - 1]))
                {
                    r.op = DiffOp::EQUAL;
                    r.description = "";
                }
                else
                {
                    r.op = DiffOp::MODIFY;
                    r.description = CollectFieldDiffs(ref[i - 1], test[j - 1]);
                }
                r.refEntry = &ref[i - 1];
                r.testEntry = &test[j - 1];
                r.refIdx = i - 1;
                r.testIdx = j - 1;
                results.push_back(r);
                --i;
                --j;
                continue;
            }
        }
        if (i > 0 && dp[i][j] == dp[i - 1][j] + GAP_PENALTY)
        {
            // 上移动（删除）
            DiffResult r;
            r.op = DiffOp::DELETE;
            r.refEntry = &ref[i - 1];
            r.testEntry = nullptr;
            r.refIdx = i - 1;
            r.testIdx = -1;
            std::ostringstream oss;
            oss << "缺失节点: " << EntryToShortDesc(ref[i - 1]);
            r.description = oss.str();
            results.push_back(r);
            --i;
        }
        else if (j > 0)
        {
            // 左移动（插入）
            DiffResult r;
            r.op = DiffOp::INSERT;
            r.refEntry = nullptr;
            r.testEntry = &test[j - 1];
            r.refIdx = -1;
            r.testIdx = j - 1;
            std::ostringstream oss;
            oss << "额外节点: " << EntryToShortDesc(test[j - 1]);
            r.description = oss.str();
            results.push_back(r);
            --j;
        }
    }

    std::reverse(results.begin(), results.end());
    return results;
}

// ----------------------------------------------------------------------------
// 输出对齐结果
// ----------------------------------------------------------------------------
static void PrintAlignedDiff(const std::string& algorithm, const std::vector<DiffResult>& results)
{
    std::cout << "\n========================================\n";
    std::cout << "算法: " << algorithm << "\n";
    std::cout << "========================================\n";

    int equalCount = 0, insertCount = 0, deleteCount = 0, modifyCount = 0;

    for (const auto& r : results)
    {
        switch (r.op)
        {
            case DiffOp::EQUAL:
                ++equalCount;
                break;
            case DiffOp::INSERT:
                ++insertCount;
                break;
            case DiffOp::DELETE:
                ++deleteCount;
                break;
            case DiffOp::MODIFY:
                ++modifyCount;
                break;
        }
    }

    int diffCount = insertCount + deleteCount + modifyCount;
    std::cout << "统计: 匹配=" << equalCount << " 插入=" << insertCount << " 删除=" << deleteCount
              << " 修改=" << modifyCount << " 差异总数=" << diffCount << "\n\n";
    std::cout.flush();

    // 输出所有操作
    for (const auto& r : results)
    {
        std::cout << "[" << DiffOpName(r.op) << "]";
        if (r.refIdx >= 0)
            std::cout << " ref@" << r.refEntry->lineNum << "(idx=" << r.refIdx << ")";
        if (r.testIdx >= 0)
            std::cout << " test@" << r.testEntry->lineNum << "(idx=" << r.testIdx << ")";
        if (!r.description.empty())
            std::cout << " " << r.description;
        std::cout << "\n";
    }

    if (diffCount == 0)
        std::cout << "\nResult: PASS — 无差异\n";
    else
        std::cout << "\nResult: FAIL — " << diffCount << " 处差异\n";
    std::cout.flush();
}

// 算法类型枚举
enum class AlgoType
{
    POSITION, // 位置逐行对比（原始模式）
    LCS, // LCS 算法
    MYERS, // Myers Diff 算法
    NEEDLEMAN, // Needleman-Wunsch 算法
    ALL // 输出所有算法结果
};

static AlgoType ParseAlgoType(const std::string& s)
{
    if (s == "position" || s == "pos")
        return AlgoType::POSITION;
    if (s == "lcs")
        return AlgoType::LCS;
    if (s == "myers")
        return AlgoType::MYERS;
    if (s == "needleman" || s == "nw")
        return AlgoType::NEEDLEMAN;
    if (s == "all")
        return AlgoType::ALL;
    return AlgoType::POSITION;
}

static const char* AlgoTypeName(AlgoType t)
{
    switch (t)
    {
        case AlgoType::POSITION:
            return "逐行对比（原始位置索引）";
        case AlgoType::LCS:
            return "LCS（最长公共子序列）";
        case AlgoType::MYERS:
            return "Myers Diff（最短编辑路径）";
        case AlgoType::NEEDLEMAN:
            return "Needleman-Wunsch（全局序列对齐）";
        case AlgoType::ALL:
            return "全部算法";
        default:
            return "未知";
    }
}

// ----------------------------------------------------------------------------
// 主程序
// ----------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    std::string refPath;
    std::string testPath;
    bool verbose = false;
    AlgoType algo = AlgoType::POSITION;

    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "--verbose" || a == "-v")
            verbose = true;
        else if (a.rfind("--algo=", 0) == 0)
        {
            std::string algoStr = a.substr(7);
            algo = ParseAlgoType(algoStr);
        }
        else if (a == "--all")
        {
            algo = AlgoType::ALL;
        }
        else if (refPath.empty())
            refPath = a;
        else if (testPath.empty())
            testPath = a;
    }

    if (refPath.empty() || testPath.empty())
    {
        std::cerr
            << "node_diff — 对比两个 nodes.txt 结构差异（C++17, 单文件）\n\n"
            << "Usage:\n"
            << "  node_diff <ref.txt> <test.txt>                       逐行对比（原始模式）\n"
            << "  node_diff <ref.txt> <test.txt> --algo=lcs          LCS 算法对比\n"
            << "  node_diff <ref.txt> <test.txt> --algo=myers        Myers Diff 算法对比\n"
            << "  node_diff <ref.txt> <test.txt> --algo=needleman    Needleman-Wunsch 算法对比\n"
            << "  node_diff <ref.txt> <test.txt> --all               输出所有算法结果\n"
            << "  node_diff <ref.txt> <test.txt> --verbose            同时显示匹配项\n"
            << "\n"
            << "算法说明:\n"
            << "  position  - 逐行按位置索引对比，适合行列完全对齐的场景\n"
            << "  lcs       - 最长公共子序列，忽略中间缺失/新增行，适合检测整体差异\n"
            << "  myers     - 最短编辑路径，生成最小编辑操作数，适合精确修改检测\n"
            << "  needleman - 全局序列对齐，考虑全局最优对齐，适合模糊匹配场景\n"
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

    std::cout << "=== Nodes Structure Comparison ===" << std::endl;
    std::cout << "Reference: " << refPath << " (" << ref.size() << " nodes)" << std::endl;
    std::cout << "Test:      " << testPath << " (" << test.size() << " nodes)" << std::endl;

    // 如果选择全部算法或单独选择某个算法
    bool runPosition = (algo == AlgoType::POSITION || algo == AlgoType::ALL);
    bool runLcs = (algo == AlgoType::LCS || algo == AlgoType::ALL);
    bool runMyers = (algo == AlgoType::MYERS || algo == AlgoType::ALL);
    bool runNeedleman = (algo == AlgoType::NEEDLEMAN || algo == AlgoType::ALL);

    int finalExitCode = 0;

    // 1. 原始逐行对比
    if (runPosition)
    {
        std::cout << "\n========================================\n";
        std::cout << "算法: 逐行对比（原始位置索引）\n";
        std::cout << "========================================\n";

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
                              << "  " << NodeTypeName(ref[i].type)
                              << "  nodeIndex=" << ref[i].nodeIndex
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
        }
        else
        {
            std::cout << "Differences: " << diffs.size() << "\n\n";
            for (const auto& d : diffs)
            {
                std::cout << "  [DIFF] ref@" << d.refLine << "  test@" << d.testLine << "  "
                          << d.msg << "\n";
            }
            std::cout << "\nResult: FAIL — " << diffs.size() << " 处差异。\n";
            finalExitCode = 1;
        }
    }

    // 2. LCS 算法
    if (runLcs)
    {
        auto lcsResults = ComputeLcsDiff(ref, test);
        PrintAlignedDiff("LCS（最长公共子序列）", lcsResults);
        // 检查是否有差异
        bool hasDiff = false;
        for (const auto& r : lcsResults)
        {
            if (r.op != DiffOp::EQUAL)
            {
                hasDiff = true;
                break;
            }
        }
        if (hasDiff)
            finalExitCode = 1;
    }

    // 3. Myers Diff 算法
    if (runMyers)
    {
        auto myersResults = ComputeMyersDiff(ref, test);
        PrintAlignedDiff("Myers Diff（最短编辑路径）", myersResults);
        // 检查是否有差异
        bool hasDiff = false;
        for (const auto& r : myersResults)
        {
            if (r.op != DiffOp::EQUAL)
            {
                hasDiff = true;
                break;
            }
        }
        if (hasDiff)
            finalExitCode = 1;
    }

    // 4. Needleman-Wunsch 算法
    if (runNeedleman)
    {
        auto nwResults = ComputeNeedlemanWunschDiff(ref, test);
        PrintAlignedDiff("Needleman-Wunsch（全局序列对齐）", nwResults);
        // 检查是否有差异
        bool hasDiff = false;
        for (const auto& r : nwResults)
        {
            if (r.op != DiffOp::EQUAL)
            {
                hasDiff = true;
                break;
            }
        }
        if (hasDiff)
            finalExitCode = 1;
    }

    return finalExitCode;
}
