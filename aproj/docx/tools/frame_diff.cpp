// frame_diff.cpp — Frame 结构差异对比工具
// 对比 LibreOffice 与 aproj/docx 的 frame/vcl 渲染指令输出
//
// 支持多种对齐算法，解决逐行对比无法处理插入/删除的问题。
// 精确对比，不支持容差。
//
// 改进（严格比对，不设容差）:
//   - StructurallySimilar 用于 Myers/LCS 对齐，识别"同条目不同几何"为 CHANGE
//   - FrameEntriesEqual 保持不变，最终验证仍为严格 0 差异
//   - --by-page 按页分组对比，避免跨页污染
//   - 差异分类（结构性/几何/内容/样式）+ 根因统计报告
//
// 用法:
//   frame_diff <ref.txt> <test.txt>                    逐行对比（默认）
//   frame_diff <ref.txt> <test.txt> --algo=lcs         LCS 算法对比
//   frame_diff <ref.txt> <test.txt> --algo=myers       Myers Diff 算法对比
//   frame_diff <ref.txt> <test.txt> --algo=needleman   Needleman-Wunsch 算法对比
//   frame_diff <ref.txt> <test.txt> --all              输出所有算法结果
//   frame_diff <ref.txt> <test.txt> --by-page          按页分组对比
//   frame_diff <ref.txt> <test.txt> --verbose          同时显示匹配项
//
//   frame_diff frame                   快捷: 对比 test/lo_frame.txt vs test/aproj_frame.txt
//   frame_diff vcl                     快捷: 对比 test/lo_vcl.txt vs test/aproj_vcl.txt
//
// 编译: cmake --build build

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>

#include "../../../render_common/render_instruction.h"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

// ── Get executable directory ──
static std::string getExeDir()
{
    std::string exePath;
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, buf, sizeof(buf));
    if (len > 0)
        exePath.assign(buf, len);
#else
    char buf[4096];
#ifdef __APPLE__
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0)
        exePath = buf;
#else
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0)
        exePath.assign(buf, len);
#endif
#endif
    auto pos = exePath.find_last_of("/\\");
    if (pos != std::string::npos)
        return exePath.substr(0, pos + 1);
    return "";
}

// ── 工具函数 ──

static std::string trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r'))
        a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r'))
        b--;
    return s.substr(a, b - a);
}

static int parseInt(const std::string& s)
{
    try
    {
        return std::stoi(s);
    }
    catch (...)
    {
        return 0;
    }
}

static uint32_t parseHex(const std::string& s)
{
    try
    {
        return static_cast<uint32_t>(std::stoul(s, nullptr, 16));
    }
    catch (...)
    {
        return 0;
    }
}

// ── 转义处理（与 render_format.cxx 中 EscapeForTsv 反向）──

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

// ── Tokenize（处理引号字段，参考 node_diff.cpp）──

static std::vector<std::string> TokenizeLine(const std::string& content)
{
    std::vector<std::string> tokens;
    size_t i = 0;
    const size_t n = content.size();

    while (i < n)
    {
        // 跳过前导空白
        while (i < n && content[i] == ' ')
            i++;
        if (i >= n)
            break;

        if (content[i] == '"')
        {
            // 引号 token：寻找结束引号（考虑转义）
            size_t k = i + 1;
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
                    k++;
                    break;
                }
                raw += content[k];
                k++;
            }
            tokens.push_back(UnescapeForTsv(raw));
            // 跳过 tab 分隔符
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

    return tokens;
}

// ── FrameEntry：统一的帧条目 ──

struct FrameEntry
{
    int lineNum = 0; // 源文件行号
    int indent = 0; // 缩进层级（= 前导空格数 / 2）
    RenderCmdType type = RenderCmdType::UNKNOWN;

    // 几何
    int pageNum = 0;
    int x = 0, y = 0, width = 0, height = 0;

    // 文本相关
    std::string text;
    std::string fontName;
    int fontSize = 0;
    uint32_t fontColor = 0;
    int fontWeight = 400;
    int fontItalic = 0;
    int underline = 0;
    int strikeout = 0;
    std::string styleName;

    // 状态变更
    int color = 0; // SET_TEXT_COLOR / SET_FILL_COLOR / SET_LINE_COLOR
};

// ── 解析一行 ──

static bool ParseFrameEntry(const std::string& line, int lineNum, FrameEntry& out)
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

    auto tokens = TokenizeLine(content);
    if (tokens.empty())
        return false;

    out.type = RenderCmdTypeFromName(tokens[0].c_str());
    if (out.type == RenderCmdType::UNKNOWN)
        return false;

    // PAGE_START: TYPE pageNum width height
    if (out.type == RenderCmdType::PAGE_START)
    {
        if (tokens.size() >= 2)
            out.pageNum = parseInt(tokens[1]);
        if (tokens.size() >= 3)
            out.width = parseInt(tokens[2]);
        if (tokens.size() >= 4)
            out.height = parseInt(tokens[3]);
    }
    // PAGE_END / SET_CLIP_REGION / PUSH / POP: TYPE pageNum
    else if (out.type == RenderCmdType::PAGE_END || out.type == RenderCmdType::SET_CLIP_REGION
             || out.type == RenderCmdType::PUSH || out.type == RenderCmdType::POP)
    {
        if (tokens.size() >= 2)
            out.pageNum = parseInt(tokens[1]);
    }
    // TEXT_FRAME / TEXT_LINE / TEXT_RUN: TYPE pageNum x y w h "text" fontName fontSize fontColor fontWeight fontItalic underline strikeout styleName
    else if (out.type == RenderCmdType::TEXT_FRAME || out.type == RenderCmdType::TEXT_LINE
             || out.type == RenderCmdType::TEXT_RUN)
    {
        if (tokens.size() >= 2)
            out.pageNum = parseInt(tokens[1]);
        if (tokens.size() >= 3)
            out.x = parseInt(tokens[2]);
        if (tokens.size() >= 4)
            out.y = parseInt(tokens[3]);
        if (tokens.size() >= 5)
            out.width = parseInt(tokens[4]);
        if (tokens.size() >= 6)
            out.height = parseInt(tokens[5]);
        if (tokens.size() >= 7)
            out.text = tokens[6]; // 已由 TokenizeLine 解转义
        if (tokens.size() >= 8)
            out.fontName = tokens[7];
        if (tokens.size() >= 9)
            out.fontSize = parseInt(tokens[8]);
        if (tokens.size() >= 10)
            out.fontColor = static_cast<uint32_t>(parseInt(tokens[9]));
        if (tokens.size() >= 11)
            out.fontWeight = parseInt(tokens[10]);
        if (tokens.size() >= 12)
            out.fontItalic = parseInt(tokens[11]);
        if (tokens.size() >= 13)
            out.underline = parseInt(tokens[12]);
        if (tokens.size() >= 14)
            out.strikeout = parseInt(tokens[13]);
        if (tokens.size() >= 15)
            out.styleName = tokens[14];
    }
    // SET_FONT: TYPE pageNum fontName fontSize fontWeight fontItalic
    else if (out.type == RenderCmdType::SET_FONT)
    {
        if (tokens.size() >= 2)
            out.pageNum = parseInt(tokens[1]);
        if (tokens.size() >= 3)
            out.fontName = tokens[2];
        if (tokens.size() >= 4)
            out.fontSize = parseInt(tokens[3]);
        if (tokens.size() >= 5)
            out.fontWeight = parseInt(tokens[4]);
        if (tokens.size() >= 6)
            out.fontItalic = parseInt(tokens[5]);
    }
    // SET_TEXT_COLOR / SET_FILL_COLOR / SET_LINE_COLOR: TYPE pageNum color
    else if (out.type == RenderCmdType::SET_TEXT_COLOR || out.type == RenderCmdType::SET_FILL_COLOR
             || out.type == RenderCmdType::SET_LINE_COLOR)
    {
        if (tokens.size() >= 2)
            out.pageNum = parseInt(tokens[1]);
        if (tokens.size() >= 3)
            out.color = parseInt(tokens[2]);
    }
    // LINE / POLYLINE: TYPE pageNum x1 y1 x2 y2
    else if (out.type == RenderCmdType::LINE || out.type == RenderCmdType::POLYLINE)
    {
        if (tokens.size() >= 2)
            out.pageNum = parseInt(tokens[1]);
        if (tokens.size() >= 3)
            out.x = parseInt(tokens[2]);
        if (tokens.size() >= 4)
            out.y = parseInt(tokens[3]);
        if (tokens.size() >= 5)
            out.width = parseInt(tokens[4]); // x2
        if (tokens.size() >= 6)
            out.height = parseInt(tokens[5]); // y2
    }
    // 容器型 START / IMAGE / RECT 等: TYPE pageNum x y w h
    else
    {
        if (tokens.size() >= 2)
            out.pageNum = parseInt(tokens[1]);
        if (tokens.size() >= 3)
            out.x = parseInt(tokens[2]);
        if (tokens.size() >= 4)
            out.y = parseInt(tokens[3]);
        if (tokens.size() >= 5)
            out.width = parseInt(tokens[4]);
        if (tokens.size() >= 6)
            out.height = parseInt(tokens[5]);
    }

    return true;
}

// ── 文件解析 ──

static std::vector<FrameEntry> ParseFrameFile(const std::string& path, std::string& outError)
{
    std::vector<FrameEntry> entries;
    std::ifstream f(path);
    if (!f.is_open())
    {
        outError = "Cannot open file: " + path;
        return entries;
    }

    std::string line;
    int lineNum = 0;
    while (std::getline(f, line))
    {
        lineNum++;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (trim(line).empty())
            continue;

        FrameEntry e;
        if (ParseFrameEntry(line, lineNum, e))
            entries.push_back(e);
    }

    if (entries.empty())
        outError = "No valid frame entries found in file: " + path;
    return entries;
}

// ── 对比两个 FrameEntry ──

static bool FrameEntriesEqual(const FrameEntry& a, const FrameEntry& b)
{
    if (a.type != b.type)
        return false;
    if (a.indent != b.indent)
        return false;
    if (a.pageNum != b.pageNum)
        return false;

    // PAGE_START
    if (a.type == RenderCmdType::PAGE_START)
    {
        return a.width == b.width && a.height == b.height;
    }

    // PAGE_END / SET_CLIP_REGION / PUSH / POP — 只有 pageNum
    if (a.type == RenderCmdType::PAGE_END || a.type == RenderCmdType::SET_CLIP_REGION
        || a.type == RenderCmdType::PUSH || a.type == RenderCmdType::POP)
    {
        return true; // pageNum 已比较
    }

    // TEXT_FRAME / TEXT_LINE / TEXT_RUN
    if (a.type == RenderCmdType::TEXT_FRAME || a.type == RenderCmdType::TEXT_LINE
        || a.type == RenderCmdType::TEXT_RUN)
    {
        return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height
               && a.text == b.text && a.fontName == b.fontName && a.fontSize == b.fontSize
               && a.fontColor == b.fontColor && a.fontWeight == b.fontWeight
               && a.fontItalic == b.fontItalic && a.underline == b.underline
               && a.strikeout == b.strikeout && a.styleName == b.styleName;
    }

    // SET_FONT
    if (a.type == RenderCmdType::SET_FONT)
    {
        return a.fontName == b.fontName && a.fontSize == b.fontSize && a.fontWeight == b.fontWeight
               && a.fontItalic == b.fontItalic;
    }

    // SET_TEXT_COLOR / SET_FILL_COLOR / SET_LINE_COLOR
    if (a.type == RenderCmdType::SET_TEXT_COLOR || a.type == RenderCmdType::SET_FILL_COLOR
        || a.type == RenderCmdType::SET_LINE_COLOR)
    {
        return a.color == b.color;
    }

    // LINE / POLYLINE
    if (a.type == RenderCmdType::LINE || a.type == RenderCmdType::POLYLINE)
    {
        return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
    }

    // 其他（容器 START / IMAGE / RECT 等）
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

// ── 结构相似性判定（用于 Myers/LCS 对齐，非容差）──
// 判断是否为"同一条目但几何/数值不同"，用于算法对齐路径。
// 对齐后仍用 FrameEntriesEqual 判断 EQUAL/CHANGE，差异不减少。
static bool StructurallySimilar(const FrameEntry& a, const FrameEntry& b)
{
    if (a.type != b.type)
        return false;
    if (a.pageNum != b.pageNum)
        return false;
    if (a.indent != b.indent)
        return false;

    // TEXT_FRAME / TEXT_LINE / TEXT_RUN: 文本+字体相同即视为同一条目
    // （几何差异 y/height 等会在对齐后报告为 CHANGE）
    if (a.type == RenderCmdType::TEXT_FRAME || a.type == RenderCmdType::TEXT_LINE
        || a.type == RenderCmdType::TEXT_RUN)
    {
        return a.text == b.text && a.fontName == b.fontName && a.fontSize == b.fontSize;
    }

    // SET_FONT: 字体名+大小相同即视为同一条目
    if (a.type == RenderCmdType::SET_FONT)
    {
        return a.fontName == b.fontName && a.fontSize == b.fontSize;
    }

    // 其他类型: 类型+页码+缩进相同即视为同一条目
    return true;
}

// ── 差异描述 ──

struct DiffMessage
{
    int refLine;
    int testLine;
    std::string msg;
};

static std::string QuoteForDisplay(const std::string& s)
{
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

static std::string CollectFieldDiffs(const FrameEntry& ref, const FrameEntry& test)
{
    std::string msg;

    auto addLine = [&](const std::string& field, const std::string& rv, const std::string& tv) {
        if (!msg.empty())
            msg += " | ";
        msg += field + ": ref=" + rv + " test=" + tv;
    };

    if (ref.type != test.type)
        addLine("type", RenderCmdTypeName(ref.type), RenderCmdTypeName(test.type));
    if (ref.pageNum != test.pageNum)
        addLine("pageNum", std::to_string(ref.pageNum), std::to_string(test.pageNum));
    if (ref.indent != test.indent)
        addLine("nestLevel", std::to_string(ref.indent), std::to_string(test.indent));

    // PAGE_START
    if (ref.type == RenderCmdType::PAGE_START)
    {
        if (ref.width != test.width)
            addLine("width", std::to_string(ref.width), std::to_string(test.width));
        if (ref.height != test.height)
            addLine("height", std::to_string(ref.height), std::to_string(test.height));
        return msg;
    }

    // TEXT_FRAME / TEXT_LINE / TEXT_RUN
    if (ref.type == RenderCmdType::TEXT_FRAME || ref.type == RenderCmdType::TEXT_LINE
        || ref.type == RenderCmdType::TEXT_RUN)
    {
        if (ref.x != test.x)
            addLine("x", std::to_string(ref.x), std::to_string(test.x));
        if (ref.y != test.y)
            addLine("y", std::to_string(ref.y), std::to_string(test.y));
        if (ref.width != test.width)
            addLine("width", std::to_string(ref.width), std::to_string(test.width));
        if (ref.height != test.height)
            addLine("height", std::to_string(ref.height), std::to_string(test.height));
        if (ref.text != test.text)
            addLine("text", QuoteForDisplay(ref.text), QuoteForDisplay(test.text));
        if (ref.fontName != test.fontName)
            addLine("fontName", QuoteForDisplay(ref.fontName), QuoteForDisplay(test.fontName));
        if (ref.fontSize != test.fontSize)
            addLine("fontSize", std::to_string(ref.fontSize), std::to_string(test.fontSize));
        if (ref.fontColor != test.fontColor)
            addLine("fontColor", std::to_string(ref.fontColor), std::to_string(test.fontColor));
        if (ref.fontWeight != test.fontWeight)
            addLine("fontWeight", std::to_string(ref.fontWeight), std::to_string(test.fontWeight));
        if (ref.fontItalic != test.fontItalic)
            addLine("fontItalic", std::to_string(ref.fontItalic), std::to_string(test.fontItalic));
        if (ref.underline != test.underline)
            addLine("underline", std::to_string(ref.underline), std::to_string(test.underline));
        if (ref.strikeout != test.strikeout)
            addLine("strikeout", std::to_string(ref.strikeout), std::to_string(test.strikeout));
        if (ref.styleName != test.styleName)
            addLine("styleName", QuoteForDisplay(ref.styleName), QuoteForDisplay(test.styleName));
        return msg;
    }

    // SET_FONT
    if (ref.type == RenderCmdType::SET_FONT)
    {
        if (ref.fontName != test.fontName)
            addLine("fontName", QuoteForDisplay(ref.fontName), QuoteForDisplay(test.fontName));
        if (ref.fontSize != test.fontSize)
            addLine("fontSize", std::to_string(ref.fontSize), std::to_string(test.fontSize));
        if (ref.fontWeight != test.fontWeight)
            addLine("fontWeight", std::to_string(ref.fontWeight), std::to_string(test.fontWeight));
        if (ref.fontItalic != test.fontItalic)
            addLine("fontItalic", std::to_string(ref.fontItalic), std::to_string(test.fontItalic));
        return msg;
    }

    // SET_TEXT_COLOR / SET_FILL_COLOR / SET_LINE_COLOR
    if (ref.type == RenderCmdType::SET_TEXT_COLOR || ref.type == RenderCmdType::SET_FILL_COLOR
        || ref.type == RenderCmdType::SET_LINE_COLOR)
    {
        if (ref.color != test.color)
            addLine("color", std::to_string(ref.color), std::to_string(test.color));
        return msg;
    }

    // 通用几何字段
    if (ref.x != test.x)
        addLine("x", std::to_string(ref.x), std::to_string(test.x));
    if (ref.y != test.y)
        addLine("y", std::to_string(ref.y), std::to_string(test.y));
    if (ref.width != test.width)
        addLine("width", std::to_string(ref.width), std::to_string(test.width));
    if (ref.height != test.height)
        addLine("height", std::to_string(ref.height), std::to_string(test.height));

    return msg;
}

// ── 简短描述 ──

static std::string EntryToShortDesc(const FrameEntry& e)
{
    std::ostringstream oss;
    oss << RenderCmdTypeName(e.type) << " pg=" << e.pageNum << " lvl=" << e.indent;
    if (e.type == RenderCmdType::TEXT_FRAME || e.type == RenderCmdType::TEXT_LINE
        || e.type == RenderCmdType::TEXT_RUN)
    {
        oss << " pos=(" << e.x << "," << e.y << ") size=(" << e.width << "," << e.height << ")";
        oss << " text=" << QuoteForDisplay(e.text);
        if (!e.fontName.empty())
            oss << " font=" << QuoteForDisplay(e.fontName) << " sz=" << e.fontSize;
    }
    else if (e.type == RenderCmdType::PAGE_START)
    {
        oss << " size=(" << e.width << "," << e.height << ")";
    }
    else if (e.type == RenderCmdType::SET_FONT)
    {
        oss << " font=" << QuoteForDisplay(e.fontName) << " sz=" << e.fontSize;
    }
    else
    {
        oss << " pos=(" << e.x << "," << e.y << ") size=(" << e.width << "," << e.height << ")";
    }
    return oss.str();
}

// ── Diff 操作类型 ──

enum class DiffOp
{
    OP_EQUAL,
    OP_INSERT, // test 中新增
    OP_DELETE, // ref 中删除
    OP_CHANGE // 字段修改
};

// ── 差异分类（严格比对下的分类，非容差）──

enum class DiffCategory
{
    NONE,                     // 无差异（EQUAL）
    STRUCTURAL_MISSING,       // ref 有 test 无（真正的缺失）
    STRUCTURAL_EXTRA,         // test 有 ref 无（真正的多余）
    STRUCTURAL_TYPE_MISMATCH, // 同位置但类型不同
    GEOMETRIC_X,
    GEOMETRIC_Y,
    GEOMETRIC_WIDTH,
    GEOMETRIC_HEIGHT,
    GEOMETRIC_Y_CUMULATIVE, // y 累积偏移标记（非容差，仅分类）
    CONTENT_TEXT,           // 文本内容不同
    STYLE_FONT_NAME,
    STYLE_FONT_SIZE,
    STYLE_FONT_COLOR,
    STYLE_FONT_WEIGHT,
    STYLE_STYLE_NAME,
    OTHER // 其他字段差异
};

static const char* DiffCategoryName(DiffCategory c)
{
    switch (c)
    {
        case DiffCategory::NONE:
            return "NONE";
        case DiffCategory::STRUCTURAL_MISSING:
            return "STRUCTURAL_MISSING";
        case DiffCategory::STRUCTURAL_EXTRA:
            return "STRUCTURAL_EXTRA";
        case DiffCategory::STRUCTURAL_TYPE_MISMATCH:
            return "STRUCTURAL_TYPE_MISMATCH";
        case DiffCategory::GEOMETRIC_X:
            return "GEOMETRIC_X";
        case DiffCategory::GEOMETRIC_Y:
            return "GEOMETRIC_Y";
        case DiffCategory::GEOMETRIC_WIDTH:
            return "GEOMETRIC_WIDTH";
        case DiffCategory::GEOMETRIC_HEIGHT:
            return "GEOMETRIC_HEIGHT";
        case DiffCategory::GEOMETRIC_Y_CUMULATIVE:
            return "GEOMETRIC_Y_CUMULATIVE";
        case DiffCategory::CONTENT_TEXT:
            return "CONTENT_TEXT";
        case DiffCategory::STYLE_FONT_NAME:
            return "STYLE_FONT_NAME";
        case DiffCategory::STYLE_FONT_SIZE:
            return "STYLE_FONT_SIZE";
        case DiffCategory::STYLE_FONT_COLOR:
            return "STYLE_FONT_COLOR";
        case DiffCategory::STYLE_FONT_WEIGHT:
            return "STYLE_FONT_WEIGHT";
        case DiffCategory::STYLE_STYLE_NAME:
            return "STYLE_STYLE_NAME";
        case DiffCategory::OTHER:
            return "OTHER";
        default:
            return "UNKNOWN";
    }
}

static const char* DiffCategoryGroup(DiffCategory c)
{
    switch (c)
    {
        case DiffCategory::STRUCTURAL_MISSING:
        case DiffCategory::STRUCTURAL_EXTRA:
        case DiffCategory::STRUCTURAL_TYPE_MISMATCH:
            return "结构性";
        case DiffCategory::GEOMETRIC_X:
        case DiffCategory::GEOMETRIC_Y:
        case DiffCategory::GEOMETRIC_WIDTH:
        case DiffCategory::GEOMETRIC_HEIGHT:
        case DiffCategory::GEOMETRIC_Y_CUMULATIVE:
            return "几何";
        case DiffCategory::CONTENT_TEXT:
            return "内容";
        case DiffCategory::STYLE_FONT_NAME:
        case DiffCategory::STYLE_FONT_SIZE:
        case DiffCategory::STYLE_FONT_COLOR:
        case DiffCategory::STYLE_FONT_WEIGHT:
        case DiffCategory::STYLE_STYLE_NAME:
            return "样式";
        default:
            return "其他";
    }
}

struct DiffResult
{
    DiffOp op;
    const FrameEntry* refEntry;
    const FrameEntry* testEntry;
    int refIdx;
    int testIdx;
    std::string description;
    DiffCategory category = DiffCategory::NONE;
    std::vector<DiffCategory> subCategories;
};

static const char* DiffOpName(DiffOp op)
{
    switch (op)
    {
        case DiffOp::OP_EQUAL:
            return "EQUAL";
        case DiffOp::OP_INSERT:
            return "INSERT";
        case DiffOp::OP_DELETE:
            return "DELETE";
        case DiffOp::OP_CHANGE:
            return "CHANGE";
        default:
            return "UNKNOWN";
    }
}

// ── 差异分类（严格比对下的分类，非容差）──
// 对 CHANGE 操作，根据字段差异确定主类别和子类别
static void ClassifyDiff(DiffResult& r)
{
    if (r.op == DiffOp::OP_EQUAL)
    {
        r.category = DiffCategory::NONE;
        return;
    }
    if (r.op == DiffOp::OP_DELETE)
    {
        r.category = DiffCategory::STRUCTURAL_MISSING;
        return;
    }
    if (r.op == DiffOp::OP_INSERT)
    {
        r.category = DiffCategory::STRUCTURAL_EXTRA;
        return;
    }

    // OP_CHANGE: 根据字段差异分类
    const FrameEntry& ref = *r.refEntry;
    const FrameEntry& test = *r.testEntry;

    if (ref.type != test.type)
    {
        r.category = DiffCategory::STRUCTURAL_TYPE_MISMATCH;
        return;
    }

    // 收集所有子类别
    std::vector<DiffCategory> subs;

    // 文本内容
    if (ref.type == RenderCmdType::TEXT_FRAME || ref.type == RenderCmdType::TEXT_LINE
        || ref.type == RenderCmdType::TEXT_RUN)
    {
        if (ref.text != test.text)
            subs.push_back(DiffCategory::CONTENT_TEXT);
        if (ref.fontName != test.fontName)
            subs.push_back(DiffCategory::STYLE_FONT_NAME);
        if (ref.fontSize != test.fontSize)
            subs.push_back(DiffCategory::STYLE_FONT_SIZE);
        if (ref.fontColor != test.fontColor)
            subs.push_back(DiffCategory::STYLE_FONT_COLOR);
        if (ref.fontWeight != test.fontWeight)
            subs.push_back(DiffCategory::STYLE_FONT_WEIGHT);
        if (ref.styleName != test.styleName)
            subs.push_back(DiffCategory::STYLE_STYLE_NAME);
    }

    // 几何字段（适用于大多数类型）
    if (ref.type != RenderCmdType::PAGE_END && ref.type != RenderCmdType::SET_CLIP_REGION
        && ref.type != RenderCmdType::PUSH && ref.type != RenderCmdType::POP
        && ref.type != RenderCmdType::SET_TEXT_COLOR && ref.type != RenderCmdType::SET_FILL_COLOR
        && ref.type != RenderCmdType::SET_LINE_COLOR)
    {
        if (ref.x != test.x)
            subs.push_back(DiffCategory::GEOMETRIC_X);
        if (ref.y != test.y)
            subs.push_back(DiffCategory::GEOMETRIC_Y);
        if (ref.width != test.width)
            subs.push_back(DiffCategory::GEOMETRIC_WIDTH);
        if (ref.height != test.height)
            subs.push_back(DiffCategory::GEOMETRIC_HEIGHT);
    }

    r.subCategories = subs;

    // 主类别优先级: 内容 > 几何 > 样式 > 其他
    for (auto c : subs)
        if (c == DiffCategory::CONTENT_TEXT)
        {
            r.category = c;
            return;
        }
    for (auto c : subs)
        if (c == DiffCategory::GEOMETRIC_Y || c == DiffCategory::GEOMETRIC_HEIGHT
            || c == DiffCategory::GEOMETRIC_X || c == DiffCategory::GEOMETRIC_WIDTH)
        {
            r.category = c;
            return;
        }
    for (auto c : subs)
        if (c == DiffCategory::STYLE_FONT_NAME || c == DiffCategory::STYLE_FONT_SIZE
            || c == DiffCategory::STYLE_FONT_COLOR || c == DiffCategory::STYLE_FONT_WEIGHT
            || c == DiffCategory::STYLE_STYLE_NAME)
        {
            r.category = c;
            return;
        }

    r.category = DiffCategory::OTHER;
}

// ── 算法 1: LCS (最长公共子序列) ──
// 改进: 使用 StructurallySimilar 进行对齐，FrameEntriesEqual 判断 EQUAL/CHANGE
// 这样"同文本框 y 不同"会被报告为 CHANGE 而非 INSERT+DELETE

static std::vector<DiffResult> ComputeLcsDiff(const std::vector<FrameEntry>& ref,
                                              const std::vector<FrameEntry>& test)
{
    size_t m = ref.size();
    size_t n = test.size();

    // DP 使用 StructurallySimilar 判断对齐（识别"同条目不同几何"）
    std::vector<std::vector<size_t>> dp(m + 1, std::vector<size_t>(n + 1, 0));

    for (size_t i = 1; i <= m; ++i)
    {
        for (size_t j = 1; j <= n; ++j)
        {
            if (StructurallySimilar(ref[i - 1], test[j - 1]))
                dp[i][j] = dp[i - 1][j - 1] + 1;
            else
                dp[i][j] = std::max(dp[i - 1][j], dp[i][j - 1]);
        }
    }

    std::vector<DiffResult> results;
    int i = static_cast<int>(m), j = static_cast<int>(n);

    while (i > 0 || j > 0)
    {
        if (i > 0 && j > 0 && StructurallySimilar(ref[i - 1], test[j - 1])
            && dp[i][j] == dp[i - 1][j - 1] + 1)
        {
            // 对齐成功: 用 FrameEntriesEqual 判断 EQUAL 还是 CHANGE
            DiffResult r;
            r.refEntry = &ref[i - 1];
            r.testEntry = &test[j - 1];
            r.refIdx = i - 1;
            r.testIdx = j - 1;
            if (FrameEntriesEqual(ref[i - 1], test[j - 1]))
            {
                r.op = DiffOp::OP_EQUAL;
                r.description = "";
            }
            else
            {
                r.op = DiffOp::OP_CHANGE;
                r.description = CollectFieldDiffs(ref[i - 1], test[j - 1]);
            }
            ClassifyDiff(r);
            results.push_back(r);
            --i;
            --j;
        }
        else if (i > 0 && (j == 0 || dp[i - 1][j] >= dp[i][j - 1]))
        {
            DiffResult r;
            r.op = DiffOp::OP_DELETE;
            r.refEntry = &ref[i - 1];
            r.testEntry = nullptr;
            r.refIdx = i - 1;
            r.testIdx = -1;
            std::ostringstream oss;
            oss << "缺失: " << EntryToShortDesc(ref[i - 1]);
            r.description = oss.str();
            ClassifyDiff(r);
            results.push_back(r);
            --i;
        }
        else
        {
            DiffResult r;
            r.op = DiffOp::OP_INSERT;
            r.refEntry = nullptr;
            r.testEntry = &test[j - 1];
            r.refIdx = -1;
            r.testIdx = j - 1;
            std::ostringstream oss;
            oss << "额外: " << EntryToShortDesc(test[j - 1]);
            r.description = oss.str();
            ClassifyDiff(r);
            results.push_back(r);
            --j;
        }
    }

    std::reverse(results.begin(), results.end());
    return results;
}

// ── 算法 2: Myers Diff (O(ND)) ──
// 改进: 使用 StructurallySimilar 进行对齐，FrameEntriesEqual 判断 EQUAL/CHANGE
// 这样"同文本框 y 不同"会被报告为 CHANGE 而非 INSERT+DELETE

static std::vector<DiffResult> ComputeMyersDiff(const std::vector<FrameEntry>& ref,
                                                const std::vector<FrameEntry>& test)
{
    const int N = static_cast<int>(ref.size());
    const int M = static_cast<int>(test.size());

    if (N == 0)
    {
        std::vector<DiffResult> results;
        for (int j = 0; j < M; ++j)
        {
            DiffResult r;
            r.op = DiffOp::OP_INSERT;
            r.refEntry = nullptr;
            r.testEntry = &test[j];
            r.refIdx = -1;
            r.testIdx = j;
            std::ostringstream oss;
            oss << "额外: " << EntryToShortDesc(test[j]);
            r.description = oss.str();
            ClassifyDiff(r);
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
            r.op = DiffOp::OP_DELETE;
            r.refEntry = &ref[i];
            r.testEntry = nullptr;
            r.refIdx = i;
            r.testIdx = -1;
            std::ostringstream oss;
            oss << "缺失: " << EntryToShortDesc(ref[i]);
            r.description = oss.str();
            ClassifyDiff(r);
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

    // 前向传递: 使用 StructurallySimilar 扩展对角线（识别"同条目不同几何"）
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
                   && StructurallySimilar(ref[static_cast<size_t>(x)],
                                          test[static_cast<size_t>(y)]))
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

    // 回溯: 对角线移动时用 FrameEntriesEqual 判断 EQUAL 还是 CHANGE
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
            r.refEntry = &ref[static_cast<size_t>(x)];
            r.testEntry = &test[static_cast<size_t>(y)];
            r.refIdx = x;
            r.testIdx = y;
            // 用 FrameEntriesEqual 判断 EQUAL 还是 CHANGE
            if (FrameEntriesEqual(ref[static_cast<size_t>(x)], test[static_cast<size_t>(y)]))
            {
                r.op = DiffOp::OP_EQUAL;
                r.description = "";
            }
            else
            {
                r.op = DiffOp::OP_CHANGE;
                r.description = CollectFieldDiffs(ref[static_cast<size_t>(x)],
                                                  test[static_cast<size_t>(y)]);
            }
            ClassifyDiff(r);
            results.push_back(r);
        }

        if (d > 0)
        {
            if (x > prevX)
            {
                --x;
                DiffResult r;
                r.op = DiffOp::OP_DELETE;
                r.refEntry = &ref[static_cast<size_t>(x)];
                r.testEntry = nullptr;
                r.refIdx = x;
                r.testIdx = -1;
                std::ostringstream oss;
                oss << "缺失: " << EntryToShortDesc(ref[static_cast<size_t>(x)]);
                r.description = oss.str();
                ClassifyDiff(r);
                results.push_back(r);
            }
            else if (y > prevY)
            {
                --y;
                DiffResult r;
                r.op = DiffOp::OP_INSERT;
                r.refEntry = nullptr;
                r.testEntry = &test[static_cast<size_t>(y)];
                r.refIdx = -1;
                r.testIdx = y;
                std::ostringstream oss;
                oss << "额外: " << EntryToShortDesc(test[static_cast<size_t>(y)]);
                r.description = oss.str();
                ClassifyDiff(r);
                results.push_back(r);
            }
        }
    }

    std::reverse(results.begin(), results.end());
    return results;
}

// ── 算法 3: Needleman-Wunsch (全局序列对齐) ──
// 改进: 使用 StructurallySimilar 进行对齐评分，FrameEntriesEqual 判断 EQUAL/CHANGE

static std::vector<DiffResult> ComputeNeedlemanWunschDiff(const std::vector<FrameEntry>& ref,
                                                          const std::vector<FrameEntry>& test)
{
    size_t m = ref.size();
    size_t n = test.size();

    const int GAP_PENALTY = -1;
    const int MATCH_SCORE = 2;       // 结构相似（对齐）
    const int MISMATCH_PENALTY = -1; // 结构不同

    std::vector<std::vector<int>> dp(m + 1, std::vector<int>(n + 1, 0));

    for (size_t i = 1; i <= m; ++i)
        dp[i][0] = dp[i - 1][0] + GAP_PENALTY;
    for (size_t j = 1; j <= n; ++j)
        dp[0][j] = dp[0][j - 1] + GAP_PENALTY;

    // DP 使用 StructurallySimilar 评分（识别"同条目不同几何"）
    for (size_t i = 1; i <= m; ++i)
    {
        for (size_t j = 1; j <= n; ++j)
        {
            int match
                = StructurallySimilar(ref[i - 1], test[j - 1]) ? MATCH_SCORE : MISMATCH_PENALTY;
            int scoreDiag = dp[i - 1][j - 1] + match;
            int scoreUp = dp[i - 1][j] + GAP_PENALTY;
            int scoreLeft = dp[i][j - 1] + GAP_PENALTY;
            dp[i][j] = std::max({ scoreDiag, scoreUp, scoreLeft });
        }
    }

    std::vector<DiffResult> results;
    int i = static_cast<int>(m), j = static_cast<int>(n);

    while (i > 0 || j > 0)
    {
        if (i > 0 && j > 0)
        {
            int diagScore = dp[i - 1][j - 1]
                            + (StructurallySimilar(ref[i - 1], test[j - 1]) ? MATCH_SCORE
                                                                             : MISMATCH_PENALTY);
            if (dp[i][j] == diagScore)
            {
                // 对齐成功: 用 FrameEntriesEqual 判断 EQUAL 还是 CHANGE
                DiffResult r;
                r.refEntry = &ref[i - 1];
                r.testEntry = &test[j - 1];
                r.refIdx = i - 1;
                r.testIdx = j - 1;
                if (FrameEntriesEqual(ref[i - 1], test[j - 1]))
                {
                    r.op = DiffOp::OP_EQUAL;
                    r.description = "";
                }
                else
                {
                    r.op = DiffOp::OP_CHANGE;
                    r.description = CollectFieldDiffs(ref[i - 1], test[j - 1]);
                }
                ClassifyDiff(r);
                results.push_back(r);
                --i;
                --j;
                continue;
            }
        }
        if (i > 0 && dp[i][j] == dp[i - 1][j] + GAP_PENALTY)
        {
            DiffResult r;
            r.op = DiffOp::OP_DELETE;
            r.refEntry = &ref[i - 1];
            r.testEntry = nullptr;
            r.refIdx = i - 1;
            r.testIdx = -1;
            std::ostringstream oss;
            oss << "缺失: " << EntryToShortDesc(ref[i - 1]);
            r.description = oss.str();
            ClassifyDiff(r);
            results.push_back(r);
            --i;
        }
        else if (j > 0)
        {
            DiffResult r;
            r.op = DiffOp::OP_INSERT;
            r.refEntry = nullptr;
            r.testEntry = &test[j - 1];
            r.refIdx = -1;
            r.testIdx = j - 1;
            std::ostringstream oss;
            oss << "额外: " << EntryToShortDesc(test[j - 1]);
            r.description = oss.str();
            ClassifyDiff(r);
            results.push_back(r);
            --j;
        }
    }

    std::reverse(results.begin(), results.end());
    return results;
}

// ── 输出对齐结果 ──

static void PrintAlignedDiff(const std::string& algorithm, const std::vector<DiffResult>& results,
                             bool verbose)
{
    std::cout << "\n========================================\n";
    std::cout << "算法: " << algorithm << "\n";
    std::cout << "========================================\n";

    int equalCount = 0, insertCount = 0, deleteCount = 0, modifyCount = 0;

    for (const auto& r : results)
    {
        switch (r.op)
        {
            case DiffOp::OP_EQUAL:
                ++equalCount;
                break;
            case DiffOp::OP_INSERT:
                ++insertCount;
                break;
            case DiffOp::OP_DELETE:
                ++deleteCount;
                break;
            case DiffOp::OP_CHANGE:
                ++modifyCount;
                break;
        }
    }

    int diffCount = insertCount + deleteCount + modifyCount;
    std::cout << "统计: 匹配=" << equalCount << " 插入=" << insertCount << " 删除=" << deleteCount
              << " 修改=" << modifyCount << " 差异总数=" << diffCount << "\n\n";
    std::cout.flush();

    for (const auto& r : results)
    {
        if (r.op == DiffOp::OP_EQUAL)
        {
            if (verbose)
            {
                std::cout << "  [OK]";
                if (r.refIdx >= 0)
                    std::cout << " ref@" << r.refEntry->lineNum << "(idx=" << r.refIdx << ")";
                if (r.testIdx >= 0)
                    std::cout << " test@" << r.testEntry->lineNum << "(idx=" << r.testIdx << ")";
                std::cout << " " << EntryToShortDesc(*r.refEntry) << "\n";
            }
            continue;
        }

        std::cout << "  [" << DiffOpName(r.op) << "]";
        // 显示分类（非 EQUAL 时）
        if (r.category != DiffCategory::NONE)
            std::cout << "[" << DiffCategoryName(r.category) << "]";
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

// ── y 累积偏移检测（分析工具，非容差）──
// 检测 y 偏移是否单调累积，找到根因候选

struct CumulativeOffset
{
    bool detected = false;
    int startPage = 0;
    int startRefLineNum = 0;
    int startTestLineNum = 0;
    std::string startText;
    std::string startFontName;
    int startFontSize = 0;
    int initialHeightDelta = 0; // 根因候选（如 height 差 +28）
    int initialRefHeight = 0;
    int initialTestHeight = 0;
    int offsetAtEnd = 0; // 累积到的偏移量
    int endPage = 0;
    std::string likelyRootCause;
};

static CumulativeOffset DetectCumulativeYOffset(const std::vector<DiffResult>& results)
{
    CumulativeOffset co;

    // 找到第一个有 y 差异的 CHANGE 操作
    int firstYDiff = -1;
    int firstYDelta = 0;
    int firstHeightDelta = 0;
    for (size_t i = 0; i < results.size(); ++i)
    {
        const auto& r = results[i];
        if (r.op != DiffOp::OP_CHANGE || !r.refEntry || !r.testEntry)
            continue;

        const FrameEntry& ref = *r.refEntry;
        const FrameEntry& test = *r.testEntry;

        // 检查是否有 y 差异
        bool hasYDiff = false;
        bool hasHeightDiff = false;
        for (auto c : r.subCategories)
        {
            if (c == DiffCategory::GEOMETRIC_Y)
                hasYDiff = true;
            if (c == DiffCategory::GEOMETRIC_HEIGHT)
                hasHeightDiff = true;
        }

        if (hasYDiff && firstYDiff < 0)
        {
            firstYDiff = static_cast<int>(i);
            firstYDelta = test.y - ref.y;
            if (hasHeightDiff)
                firstHeightDelta = test.height - ref.height;
            co.startPage = ref.pageNum;
            co.startRefLineNum = ref.lineNum;
            co.startTestLineNum = test.lineNum;
            co.startText = ref.text;
            co.startFontName = ref.fontName;
            co.startFontSize = ref.fontSize;
            co.initialHeightDelta = firstHeightDelta;
            co.initialRefHeight = ref.height;
            co.initialTestHeight = test.height;
        }

        if (firstYDiff >= 0)
        {
            // 记录最后一个 y 偏移
            if (hasYDiff)
            {
                co.offsetAtEnd = test.y - ref.y;
                co.endPage = ref.pageNum;
            }
        }
    }

    if (firstYDiff < 0)
        return co; // 没有 y 差异

    // 判断是否单调累积（偏移方向一致）
    // 简单判断: 如果有多个 y 差异且方向一致，认为是累积偏移
    int posCount = 0, negCount = 0;
    for (const auto& r : results)
    {
        if (r.op != DiffOp::OP_CHANGE || !r.refEntry || !r.testEntry)
            continue;
        for (auto c : r.subCategories)
        {
            if (c == DiffCategory::GEOMETRIC_Y)
            {
                int delta = r.testEntry->y - r.refEntry->y;
                if (delta > 0)
                    ++posCount;
                else if (delta < 0)
                    ++negCount;
            }
        }
    }

    // 如果同方向的 y 偏移 >= 3 个，认为是累积偏移
    if (posCount >= 3 || negCount >= 3)
    {
        co.detected = true;
        std::ostringstream oss;
        oss << "page " << co.startPage << " 第一个 TEXT_FRAME 的 height 计算偏差";
        if (co.initialHeightDelta != 0)
        {
            oss << " (height 差 " << (co.initialHeightDelta > 0 ? "+" : "")
                << co.initialHeightDelta << " twips, ref=" << co.initialRefHeight << " test="
                << co.initialTestHeight << ")";
        }
        co.likelyRootCause = oss.str();
    }

    return co;
}

// ── 根因统计报告 ──

struct DiffStatistics
{
    int equalCount = 0, insertCount = 0, deleteCount = 0, modifyCount = 0;
    int diffCount = 0;

    // 按类别统计
    std::map<DiffCategory, int> countByCategory;

    // 几何字段统计
    struct GeometricStats
    {
        int count = 0;
        int minDelta = 0;
        int maxDelta = 0;
        long long sumDelta = 0;
        double meanDelta = 0;
    };
    GeometricStats xDelta, yDelta, widthDelta, heightDelta;

    // 按页分布
    std::map<int, int> diffsPerPage;

    // y 累积偏移
    CumulativeOffset yCumulative;
};

static DiffStatistics ComputeStatistics(const std::vector<DiffResult>& results)
{
    DiffStatistics stats;

    auto updateGeoStats = [](DiffStatistics::GeometricStats& gs, int delta) {
        gs.count++;
        if (gs.count == 1)
        {
            gs.minDelta = gs.maxDelta = delta;
        }
        else
        {
            gs.minDelta = std::min(gs.minDelta, delta);
            gs.maxDelta = std::max(gs.maxDelta, delta);
        }
        gs.sumDelta += delta;
    };

    for (const auto& r : results)
    {
        switch (r.op)
        {
            case DiffOp::OP_EQUAL:
                ++stats.equalCount;
                continue;
            case DiffOp::OP_INSERT:
                ++stats.insertCount;
                break;
            case DiffOp::OP_DELETE:
                ++stats.deleteCount;
                break;
            case DiffOp::OP_CHANGE:
                ++stats.modifyCount;
                break;
        }

        stats.diffCount++;
        stats.countByCategory[r.category]++;

        // 按页统计
        int pageNum = 0;
        if (r.refEntry)
            pageNum = r.refEntry->pageNum;
        else if (r.testEntry)
            pageNum = r.testEntry->pageNum;
        stats.diffsPerPage[pageNum]++;

        // 几何字段统计
        if (r.op == DiffOp::OP_CHANGE && r.refEntry && r.testEntry)
        {
            for (auto c : r.subCategories)
            {
                switch (c)
                {
                    case DiffCategory::GEOMETRIC_X:
                        updateGeoStats(stats.xDelta, r.testEntry->x - r.refEntry->x);
                        break;
                    case DiffCategory::GEOMETRIC_Y:
                        updateGeoStats(stats.yDelta, r.testEntry->y - r.refEntry->y);
                        break;
                    case DiffCategory::GEOMETRIC_WIDTH:
                        updateGeoStats(stats.widthDelta, r.testEntry->width - r.refEntry->width);
                        break;
                    case DiffCategory::GEOMETRIC_HEIGHT:
                        updateGeoStats(stats.heightDelta, r.testEntry->height - r.refEntry->height);
                        break;
                    default:
                        break;
                }
            }
        }
    }

    // 计算平均值
    if (stats.xDelta.count > 0)
        stats.xDelta.meanDelta = static_cast<double>(stats.xDelta.sumDelta) / stats.xDelta.count;
    if (stats.yDelta.count > 0)
        stats.yDelta.meanDelta = static_cast<double>(stats.yDelta.sumDelta) / stats.yDelta.count;
    if (stats.widthDelta.count > 0)
        stats.widthDelta.meanDelta
            = static_cast<double>(stats.widthDelta.sumDelta) / stats.widthDelta.count;
    if (stats.heightDelta.count > 0)
        stats.heightDelta.meanDelta
            = static_cast<double>(stats.heightDelta.sumDelta) / stats.heightDelta.count;

    // y 累积偏移检测
    stats.yCumulative = DetectCumulativeYOffset(results);

    return stats;
}

static void PrintRootCauseReport(const std::vector<DiffResult>& results)
{
    auto stats = ComputeStatistics(results);

    std::cout << "\n========================================\n";
    std::cout << " 根因分析报告（严格比对，差异总数不变）\n";
    std::cout << "========================================\n";

    // [1] 差异总览
    std::cout << "\n[1] 差异总览\n";
    std::cout << "  匹配: " << stats.equalCount << "  插入: " << stats.insertCount
              << "  删除: " << stats.deleteCount << "  修改: " << stats.modifyCount << "\n";
    std::cout << "  差异总数: " << stats.diffCount << "\n";

    // [2] 按类别分布
    std::cout << "\n[2] 按类别分布\n";
    std::cout << "  结构性:\n";
    std::cout << "    缺失节点: " << stats.countByCategory[DiffCategory::STRUCTURAL_MISSING] << "\n";
    std::cout << "    多余节点: " << stats.countByCategory[DiffCategory::STRUCTURAL_EXTRA] << "\n";
    std::cout << "    类型不匹配: " << stats.countByCategory[DiffCategory::STRUCTURAL_TYPE_MISMATCH]
              << "\n";
    std::cout << "  几何:\n";
    std::cout << "    x 偏移: " << stats.countByCategory[DiffCategory::GEOMETRIC_X];
    if (stats.xDelta.count > 0)
        std::cout << " (平均 " << (stats.xDelta.meanDelta > 0 ? "+" : "")
                  << stats.xDelta.meanDelta << " twips, 范围 [" << stats.xDelta.minDelta << ", "
                  << stats.xDelta.maxDelta << "])";
    std::cout << "\n";
    std::cout << "    y 偏移: " << stats.countByCategory[DiffCategory::GEOMETRIC_Y];
    if (stats.yDelta.count > 0)
        std::cout << " (平均 " << (stats.yDelta.meanDelta > 0 ? "+" : "")
                  << stats.yDelta.meanDelta << " twips, 范围 [" << stats.yDelta.minDelta << ", "
                  << stats.yDelta.maxDelta << "])";
    std::cout << "\n";
    std::cout << "    width 差异: " << stats.countByCategory[DiffCategory::GEOMETRIC_WIDTH];
    if (stats.widthDelta.count > 0)
        std::cout << " (平均 " << (stats.widthDelta.meanDelta > 0 ? "+" : "")
                  << stats.widthDelta.meanDelta << " twips)";
    std::cout << "\n";
    std::cout << "    height 差异: " << stats.countByCategory[DiffCategory::GEOMETRIC_HEIGHT];
    if (stats.heightDelta.count > 0)
        std::cout << " (平均 " << (stats.heightDelta.meanDelta > 0 ? "+" : "")
                  << stats.heightDelta.meanDelta << " twips)";
    std::cout << "\n";
    std::cout << "  内容:\n";
    std::cout << "    文本不同: " << stats.countByCategory[DiffCategory::CONTENT_TEXT] << "\n";
    std::cout << "  样式:\n";
    std::cout << "    fontName 不同: " << stats.countByCategory[DiffCategory::STYLE_FONT_NAME]
              << "\n";
    std::cout << "    fontSize 不同: " << stats.countByCategory[DiffCategory::STYLE_FONT_SIZE]
              << "\n";
    std::cout << "    fontColor 不同: " << stats.countByCategory[DiffCategory::STYLE_FONT_COLOR]
              << "\n";
    std::cout << "    fontWeight 不同: " << stats.countByCategory[DiffCategory::STYLE_FONT_WEIGHT]
              << "\n";
    std::cout << "    styleName 不同: " << stats.countByCategory[DiffCategory::STYLE_STYLE_NAME]
              << "\n";

    // [3] y 累积偏移检测
    std::cout << "\n[3] y 累积偏移检测\n";
    if (stats.yCumulative.detected)
    {
        std::cout << "  [!] 检测到 y 单调累积偏移\n";
        std::cout << "  起始点: page " << stats.yCumulative.startPage << ", ref line "
                  << stats.yCumulative.startRefLineNum << " (TEXT_FRAME \""
                  << stats.yCumulative.startText << "\"";
        if (!stats.yCumulative.startFontName.empty())
            std::cout << " " << stats.yCumulative.startFontName << " "
                      << stats.yCumulative.startFontSize;
        std::cout << ")\n";
        if (stats.yCumulative.initialHeightDelta != 0)
        {
            std::cout << "  初始 height 差异: "
                      << (stats.yCumulative.initialHeightDelta > 0 ? "+" : "")
                      << stats.yCumulative.initialHeightDelta << " twips (ref="
                      << stats.yCumulative.initialRefHeight
                      << ", test=" << stats.yCumulative.initialTestHeight << ")\n";
        }
        std::cout << "  累积至 page " << stats.yCumulative.endPage << ": "
                  << (stats.yCumulative.offsetAtEnd > 0 ? "+" : "")
                  << stats.yCumulative.offsetAtEnd << " twips\n";
        std::cout << "  根因推断: " << stats.yCumulative.likelyRootCause << "\n";
        std::cout << "  建议排查: 段落间距/行距计算 (sw/source/core/text/itrform2.cxx)\n";
    }
    else
    {
        std::cout << "  未检测到明显的 y 单调累积偏移\n";
    }

    // [4] 按页分布
    std::cout << "\n[4] 按页分布\n";
    for (const auto& [pageNum, count] : stats.diffsPerPage)
    {
        std::cout << "  Page " << pageNum << ": " << count << " 处差异";
        // 标记结构性差异
        int structuralCount = 0;
        for (const auto& r : results)
        {
            int rPage = r.refEntry ? r.refEntry->pageNum : (r.testEntry ? r.testEntry->pageNum : 0);
            if (rPage == pageNum && (r.op == DiffOp::OP_INSERT || r.op == DiffOp::OP_DELETE))
                ++structuralCount;
        }
        if (structuralCount > 0)
            std::cout << " [!] 结构性差异: " << structuralCount;
        std::cout << "\n";
    }

    // [5] 优先修复建议
    std::cout << "\n[5] 优先修复建议\n";
    int suggestionIdx = 1;
    if (stats.yCumulative.detected)
    {
        std::cout << "  " << suggestionIdx++ << ". [根因] 修复 page "
                  << stats.yCumulative.startPage << " TEXT_FRAME height 计算 (影响后续 "
                  << stats.countByCategory[DiffCategory::GEOMETRIC_Y] << " 处 y 偏移)\n";
    }
    if (stats.countByCategory[DiffCategory::STRUCTURAL_MISSING] > 0)
    {
        std::cout << "  " << suggestionIdx++ << ". [结构] 修复缺失节点 ("
                  << stats.countByCategory[DiffCategory::STRUCTURAL_MISSING]
                  << " 处, 检查分页/内容生成逻辑)\n";
    }
    if (stats.countByCategory[DiffCategory::STRUCTURAL_EXTRA] > 0)
    {
        std::cout << "  " << suggestionIdx++ << ". [结构] 修复多余节点 ("
                  << stats.countByCategory[DiffCategory::STRUCTURAL_EXTRA]
                  << " 处, 检查是否重复生成)\n";
    }
    if (stats.countByCategory[DiffCategory::CONTENT_TEXT] > 0)
    {
        std::cout << "  " << suggestionIdx++ << ". [内容] 修复文本内容差异 ("
                  << stats.countByCategory[DiffCategory::CONTENT_TEXT] << " 处)\n";
    }
    if (stats.countByCategory[DiffCategory::GEOMETRIC_WIDTH] > 0
        || stats.countByCategory[DiffCategory::GEOMETRIC_X] > 0)
    {
        std::cout << "  " << suggestionIdx++ << ". [几何] 修复 x/width 差异 (检查 Section/Column 边距计算)\n";
    }
    if (suggestionIdx == 1)
        std::cout << "  无差异，无需修复\n";

    std::cout.flush();
}

// ── 算法类型 ──

enum class AlgoType
{
    POSITION,
    LCS,
    MYERS,
    NEEDLEMAN,
    ALL
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

// ── 按页分组对比 ──

struct PageGroup
{
    int pageNum = 0;
    std::vector<FrameEntry> entries;
    std::vector<int> originalIndices; // 原始索引映射
};

static std::vector<PageGroup> GroupByPage(const std::vector<FrameEntry>& entries)
{
    std::vector<PageGroup> groups;
    std::map<int, size_t> pageToGroupIdx;

    for (size_t i = 0; i < entries.size(); ++i)
    {
        int pg = entries[i].pageNum;
        auto it = pageToGroupIdx.find(pg);
        if (it == pageToGroupIdx.end())
        {
            pageToGroupIdx[pg] = groups.size();
            groups.push_back({ pg, {}, {} });
            it = pageToGroupIdx.find(pg);
        }
        groups[it->second].entries.push_back(entries[i]);
        groups[it->second].originalIndices.push_back(static_cast<int>(i));
    }
    return groups;
}

// 按页分组对比，页内用指定算法对齐
static std::vector<DiffResult> ComputePagedDiff(const std::vector<FrameEntry>& ref,
                                                const std::vector<FrameEntry>& test,
                                                AlgoType innerAlgo)
{
    auto refPages = GroupByPage(ref);
    auto testPages = GroupByPage(test);

    // 收集所有页码（按顺序）
    std::vector<int> allPages;
    {
        std::set<int> seen;
        for (const auto& g : refPages)
        {
            if (seen.insert(g.pageNum).second)
                allPages.push_back(g.pageNum);
        }
        for (const auto& g : testPages)
        {
            if (seen.insert(g.pageNum).second)
                allPages.push_back(g.pageNum);
        }
    }

    std::vector<DiffResult> allResults;

    for (int pg : allPages)
    {
        // 找到该页的 ref 和 test 组
        const std::vector<FrameEntry>* refPage = nullptr;
        const std::vector<FrameEntry>* testPage = nullptr;
        for (const auto& g : refPages)
            if (g.pageNum == pg)
            {
                refPage = &g.entries;
                break;
            }
        for (const auto& g : testPages)
            if (g.pageNum == pg)
            {
                testPage = &g.entries;
                break;
            }

        std::vector<DiffResult> pageResults;
        if (refPage && testPage)
        {
            switch (innerAlgo)
            {
                case AlgoType::LCS:
                    pageResults = ComputeLcsDiff(*refPage, *testPage);
                    break;
                case AlgoType::NEEDLEMAN:
                    pageResults = ComputeNeedlemanWunschDiff(*refPage, *testPage);
                    break;
                case AlgoType::MYERS:
                default:
                    pageResults = ComputeMyersDiff(*refPage, *testPage);
                    break;
            }
        }
        else if (refPage)
        {
            // test 缺少该页
            for (const auto& e : *refPage)
            {
                DiffResult r;
                r.op = DiffOp::OP_DELETE;
                r.refEntry = &e;
                r.testEntry = nullptr;
                r.refIdx = 0;
                r.testIdx = -1;
                std::ostringstream oss;
                oss << "缺失: " << EntryToShortDesc(e);
                r.description = oss.str();
                ClassifyDiff(r);
                pageResults.push_back(r);
            }
        }
        else if (testPage)
        {
            // ref 缺少该页
            for (const auto& e : *testPage)
            {
                DiffResult r;
                r.op = DiffOp::OP_INSERT;
                r.refEntry = nullptr;
                r.testEntry = &e;
                r.refIdx = -1;
                r.testIdx = 0;
                std::ostringstream oss;
                oss << "额外: " << EntryToShortDesc(e);
                r.description = oss.str();
                ClassifyDiff(r);
                pageResults.push_back(r);
            }
        }

        // 添加页分隔标记
        if (!pageResults.empty())
        {
            DiffResult pageMarker;
            pageMarker.op = DiffOp::OP_EQUAL;
            pageMarker.refEntry = nullptr;
            pageMarker.testEntry = nullptr;
            pageMarker.refIdx = -1;
            pageMarker.testIdx = -1;
            std::ostringstream oss;
            oss << "--- Page " << pg << " ---";
            pageMarker.description = oss.str();
            allResults.push_back(pageMarker);
        }

        for (auto& r : pageResults)
            allResults.push_back(r);
    }

    return allResults;
}

// ── 快捷模式路径解析 ──

static std::string resolveLayerPath(const std::string& testDir, const std::string& prefix,
                                    const std::string& layer)
{
    std::string resolved = testDir;
    if (!testDir.empty())
    {
#ifdef _WIN32
        bool isAbsolute = (testDir.size() >= 2 && testDir[1] == ':');
#else
        bool isAbsolute = (testDir[0] == '/');
#endif
        if (!isAbsolute)
            resolved = getExeDir() + testDir;
    }
    return resolved + "/" + prefix + layer + ".txt";
}

// ── 主程序 ──

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cerr
            << "frame_diff — Frame 结构差异对比工具（C++17, 单文件）\n\n"
            << "Usage:\n"
            << "  frame_diff <ref.txt> <test.txt>                       逐行对比（默认）\n"
            << "  frame_diff <ref.txt> <test.txt> --algo=lcs           LCS 算法对比\n"
            << "  frame_diff <ref.txt> <test.txt> --algo=myers         Myers Diff 算法对比\n"
            << "  frame_diff <ref.txt> <test.txt> --algo=needleman     Needleman-Wunsch 算法对比\n"
            << "  frame_diff <ref.txt> <test.txt> --all                输出所有算法结果\n"
            << "  frame_diff <ref.txt> <test.txt> --by-page            按页分组对比\n"
            << "  frame_diff <ref.txt> <test.txt> --root-cause         输出根因分析报告\n"
            << "  frame_diff <ref.txt> <test.txt> --verbose            同时显示匹配项\n"
            << "\n"
            << "  frame_diff frame                   快捷: 对比 test/lo_frame.txt vs "
               "test/aproj_frame.txt\n"
            << "  frame_diff vcl                     快捷: 对比 test/lo_vcl.txt vs "
               "test/aproj_vcl.txt\n"
            << "\n"
            << "算法说明:\n"
            << "  position  - 逐行按位置索引对比，适合行列完全对齐的场景（默认）\n"
            << "  lcs       - 最长公共子序列，忽略中间缺失/新增行\n"
            << "  myers     - 最短编辑路径，生成最小编辑操作数\n"
            << "  needleman - 全局序列对齐，考虑全局最优对齐\n"
            << "\n"
            << "改进说明（严格比对，不设容差）:\n"
            << "  - StructurallySimilar 用于 Myers/LCS 对齐，识别\"同条目不同几何\"为 CHANGE\n"
            << "  - FrameEntriesEqual 保持不变，最终验证仍为严格 0 差异\n"
            << "  - --by-page 按页分组对比，避免跨页污染\n"
            << "  - --root-cause 输出差异分类与根因统计报告\n"
            << "\n"
            << "Exit code: 0 = 无差异, 1 = 有差异或错误.\n";
        return 1;
    }

    std::string refPath;
    std::string testPath;
    std::string testDir = "../test";
    bool verbose = false;
    bool byPage = false;
    bool rootCause = false;
    AlgoType algo = AlgoType::POSITION;

    // 解析第一个参数：判断是 layer 名称还是文件路径
    std::string arg1 = argv[1];
    bool isLayer = (arg1 == "frame" || arg1 == "vcl");

    // 解析 --test-dir
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--test-dir") == 0 && i + 1 < argc)
        {
            testDir = argv[++i];
        }
    }

    if (isLayer)
    {
        refPath = resolveLayerPath(testDir, "lo_", arg1);
        testPath = resolveLayerPath(testDir, "aproj_", arg1);
        for (int i = 2; i < argc; ++i)
        {
            std::string a = argv[i];
            if (a.rfind("--algo=", 0) == 0)
                algo = ParseAlgoType(a.substr(7));
            else if (a == "--all")
                algo = AlgoType::ALL;
            else if (a == "--verbose" || a == "-v")
                verbose = true;
            else if (a == "--by-page")
                byPage = true;
            else if (a == "--root-cause")
                rootCause = true;
        }
    }
    else
    {
        for (int i = 1; i < argc; ++i)
        {
            std::string a = argv[i];
            if (a == "--verbose" || a == "-v")
                verbose = true;
            else if (a.rfind("--algo=", 0) == 0)
                algo = ParseAlgoType(a.substr(7));
            else if (a == "--all")
                algo = AlgoType::ALL;
            else if (a == "--by-page")
                byPage = true;
            else if (a == "--root-cause")
                rootCause = true;
            else if (refPath.empty())
                refPath = a;
            else if (testPath.empty())
                testPath = a;
        }
    }

    if (refPath.empty() || testPath.empty())
    {
        std::cerr << "ERROR: two file paths required (or use 'frame'/'vcl' shortcut)\n";
        return 1;
    }

    // 解析文件
    std::string refErr, testErr;
    auto ref = ParseFrameFile(refPath, refErr);
    auto test = ParseFrameFile(testPath, testErr);

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

    std::cout << "=== Frame Structure Comparison ===" << std::endl;
    std::cout << "Reference: " << refPath << " (" << ref.size() << " instructions)" << std::endl;
    std::cout << "Test:      " << testPath << " (" << test.size() << " instructions)" << std::endl;

    // 统计各类型指令数
    auto countByType = [](const std::vector<FrameEntry>& entries) {
        std::vector<std::pair<RenderCmdType, int>> counts;
        for (const auto& e : entries)
        {
            bool found = false;
            for (auto& c : counts)
            {
                if (c.first == e.type)
                {
                    c.second++;
                    found = true;
                    break;
                }
            }
            if (!found)
                counts.push_back({ e.type, 1 });
        }
        return counts;
    };

    auto refCounts = countByType(ref);
    auto testCounts = countByType(test);

    std::cout << "\nInstruction counts by type:" << std::endl;
    std::cout << "  Type               Ref    Test   Match" << std::endl;
    for (const auto& rc : refCounts)
    {
        int tc = 0;
        for (const auto& t : testCounts)
        {
            if (t.first == rc.first)
            {
                tc = t.second;
                break;
            }
        }
        bool match = (rc.second == tc);
        std::cout << "  " << RenderCmdTypeName(rc.first);
        // 对齐列宽
        size_t nameLen = strlen(RenderCmdTypeName(rc.first));
        for (size_t pad = nameLen; pad < 20; ++pad)
            std::cout << " ";
        std::cout << rc.second << "    " << tc << (match ? "   OK" : "   MISMATCH") << std::endl;
    }
    std::cout << std::endl;

    bool runPosition = (algo == AlgoType::POSITION || algo == AlgoType::ALL);
    bool runLcs = (algo == AlgoType::LCS || algo == AlgoType::ALL);
    bool runMyers = (algo == AlgoType::MYERS || algo == AlgoType::ALL);
    bool runNeedleman = (algo == AlgoType::NEEDLEMAN || algo == AlgoType::ALL);

    // --by-page 模式下，跳过逐行对比，使用按页分组的对齐算法
    if (byPage)
    {
        runPosition = false;
        // 如果用户没有指定算法，默认用 Myers
        if (algo == AlgoType::POSITION)
            algo = AlgoType::MYERS;
        runLcs = (algo == AlgoType::LCS || algo == AlgoType::ALL);
        runMyers = (algo == AlgoType::MYERS || algo == AlgoType::ALL);
        runNeedleman = (algo == AlgoType::NEEDLEMAN || algo == AlgoType::ALL);
    }

    int finalExitCode = 0;

    // 1. 逐行对比
    if (runPosition)
    {
        std::cout << "\n========================================\n";
        std::cout << "算法: 逐行对比（原始位置索引）\n";
        std::cout << "========================================\n";

        std::vector<DiffMessage> diffs;
        size_t maxN = std::max(ref.size(), test.size());
        for (size_t i = 0; i < maxN; ++i)
        {
            if (i >= ref.size())
            {
                std::ostringstream oss;
                oss << "额外（ref 不存在）: " << EntryToShortDesc(test[i]);
                diffs.push_back({ 0, test[i].lineNum, oss.str() });
                continue;
            }
            if (i >= test.size())
            {
                std::ostringstream oss;
                oss << "缺失（test 不存在）: " << EntryToShortDesc(ref[i]);
                diffs.push_back({ ref[i].lineNum, 0, oss.str() });
                continue;
            }
            if (!FrameEntriesEqual(ref[i], test[i]))
            {
                diffs.push_back(
                    { ref[i].lineNum, test[i].lineNum, CollectFieldDiffs(ref[i], test[i]) });
            }
        }

        if (verbose)
        {
            std::cout << "-- verbose: matching pairs shown --\n";
            size_t n = std::min(ref.size(), test.size());
            for (size_t i = 0; i < n; ++i)
            {
                if (FrameEntriesEqual(ref[i], test[i]))
                {
                    std::cout << "  [OK]  ref@" << ref[i].lineNum << "  test@" << test[i].lineNum
                              << "  " << EntryToShortDesc(ref[i]) << "\n";
                }
            }
            std::cout << "\n";
        }

        if (diffs.empty())
        {
            std::cout << "Result: PASS — 无差异。\n";
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

    // 辅助函数: 检查是否有差异
    auto hasDiffs = [](const std::vector<DiffResult>& results) {
        for (const auto& r : results)
            if (r.op != DiffOp::OP_EQUAL)
                return true;
        return false;
    };

    // 2. LCS
    if (runLcs)
    {
        std::vector<DiffResult> results;
        if (byPage)
        {
            results = ComputePagedDiff(ref, test, AlgoType::LCS);
            PrintAlignedDiff("LCS（最长公共子序列，按页分组）", results, verbose);
        }
        else
        {
            results = ComputeLcsDiff(ref, test);
            PrintAlignedDiff("LCS（最长公共子序列）", results, verbose);
        }
        if (hasDiffs(results))
        {
            finalExitCode = 1;
            if (rootCause)
                PrintRootCauseReport(results);
        }
    }

    // 3. Myers
    if (runMyers)
    {
        std::vector<DiffResult> results;
        if (byPage)
        {
            results = ComputePagedDiff(ref, test, AlgoType::MYERS);
            PrintAlignedDiff("Myers Diff（最短编辑路径，按页分组）", results, verbose);
        }
        else
        {
            results = ComputeMyersDiff(ref, test);
            PrintAlignedDiff("Myers Diff（最短编辑路径）", results, verbose);
        }
        if (hasDiffs(results))
        {
            finalExitCode = 1;
            if (rootCause)
                PrintRootCauseReport(results);
        }
    }

    // 4. Needleman-Wunsch
    if (runNeedleman)
    {
        std::vector<DiffResult> results;
        if (byPage)
        {
            results = ComputePagedDiff(ref, test, AlgoType::NEEDLEMAN);
            PrintAlignedDiff("Needleman-Wunsch（全局序列对齐，按页分组）", results, verbose);
        }
        else
        {
            results = ComputeNeedlemanWunschDiff(ref, test);
            PrintAlignedDiff("Needleman-Wunsch（全局序列对齐）", results, verbose);
        }
        if (hasDiffs(results))
        {
            finalExitCode = 1;
            if (rootCause)
                PrintRootCauseReport(results);
        }
    }

    return finalExitCode;
}
