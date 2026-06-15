// render_diff.cpp — 渲染指令比对工具
// 比对 LibreOffice 和 aproj/docx 的渲染指令输出
//
// 用法:
//   render_diff frame                   对比 test/lo_frame.txt 和 test/aproj_frame.txt
//   render_diff vcl                     对比 test/lo_vcl.txt 和 test/aproj_vcl.txt
//   render_diff <ref.txt> <test.txt>    对比任意两个文件
//
// 选项:
//   --test-dir DIR   指定 test 目录，默认 "test"
//   --known-diffs F  已知差异文件 (跳过匹配差异)
//   --verbose        显示匹配行
//
// 编译: cmake --build build

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <algorithm>

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

// ── 指令类型 ──

enum class CmdType : uint8_t
{
    UNKNOWN = 0,
    PAGE_START,
    PAGE_END,
    TEXT_FRAME,
    TEXT_LINE,
    TEXT_RUN,
    TABLE_FRAME,
    TABLE_ROW,
    TABLE_CELL,
    IMAGE_FRAME,
    SECTION_FRAME,
    RECT,
    LINE,
    // VCL 绘制指令
    POLYGON,
    BITMAP,
    ELLIPSE,
    POLYLINE,
    // VCL 状态指令
    SET_FONT,
    SET_LINE_COLOR,
    SET_FILL_COLOR,
    SET_TEXT_COLOR,
    SET_CLIP_REGION,
    PUSH,
    POP,
};

static CmdType parseCmdType(const std::string& s)
{
    if (s == "PAGE_START")
        return CmdType::PAGE_START;
    if (s == "PAGE_END")
        return CmdType::PAGE_END;
    if (s == "TEXT_FRAME")
        return CmdType::TEXT_FRAME;
    if (s == "TEXT_LINE")
        return CmdType::TEXT_LINE;
    if (s == "TEXT_RUN")
        return CmdType::TEXT_RUN;
    if (s == "TABLE_FRAME")
        return CmdType::TABLE_FRAME;
    if (s == "TABLE_ROW")
        return CmdType::TABLE_ROW;
    if (s == "TABLE_CELL")
        return CmdType::TABLE_CELL;
    if (s == "IMAGE_FRAME")
        return CmdType::IMAGE_FRAME;
    if (s == "SECTION_FRAME")
        return CmdType::SECTION_FRAME;
    if (s == "RECT")
        return CmdType::RECT;
    if (s == "LINE")
        return CmdType::LINE;
    if (s == "POLYGON")
        return CmdType::POLYGON;
    if (s == "BITMAP")
        return CmdType::BITMAP;
    if (s == "ELLIPSE")
        return CmdType::ELLIPSE;
    if (s == "POLYLINE")
        return CmdType::POLYLINE;
    if (s == "SET_FONT")
        return CmdType::SET_FONT;
    if (s == "SET_LINE_COLOR")
        return CmdType::SET_LINE_COLOR;
    if (s == "SET_FILL_COLOR")
        return CmdType::SET_FILL_COLOR;
    if (s == "SET_TEXT_COLOR")
        return CmdType::SET_TEXT_COLOR;
    if (s == "SET_CLIP_REGION")
        return CmdType::SET_CLIP_REGION;
    if (s == "PUSH")
        return CmdType::PUSH;
    if (s == "POP")
        return CmdType::POP;
    return CmdType::UNKNOWN;
}

static const char* cmdTypeName(CmdType t)
{
    switch (t)
    {
        case CmdType::PAGE_START:
            return "PAGE_START";
        case CmdType::PAGE_END:
            return "PAGE_END";
        case CmdType::TEXT_FRAME:
            return "TEXT_FRAME";
        case CmdType::TEXT_LINE:
            return "TEXT_LINE";
        case CmdType::TEXT_RUN:
            return "TEXT_RUN";
        case CmdType::TABLE_FRAME:
            return "TABLE_FRAME";
        case CmdType::TABLE_ROW:
            return "TABLE_ROW";
        case CmdType::TABLE_CELL:
            return "TABLE_CELL";
        case CmdType::IMAGE_FRAME:
            return "IMAGE_FRAME";
        case CmdType::SECTION_FRAME:
            return "SECTION_FRAME";
        case CmdType::RECT:
            return "RECT";
        case CmdType::LINE:
            return "LINE";
        case CmdType::POLYGON:
            return "POLYGON";
        case CmdType::BITMAP:
            return "BITMAP";
        case CmdType::ELLIPSE:
            return "ELLIPSE";
        case CmdType::POLYLINE:
            return "POLYLINE";
        case CmdType::SET_FONT:
            return "SET_FONT";
        case CmdType::SET_LINE_COLOR:
            return "SET_LINE_COLOR";
        case CmdType::SET_FILL_COLOR:
            return "SET_FILL_COLOR";
        case CmdType::SET_TEXT_COLOR:
            return "SET_TEXT_COLOR";
        case CmdType::SET_CLIP_REGION:
            return "SET_CLIP_REGION";
        case CmdType::PUSH:
            return "PUSH";
        case CmdType::POP:
            return "POP";
        default:
            return "UNKNOWN";
    }
}

// ── 解析后的指令 ──

struct Instruction
{
    int lineNum;
    std::string rawLine;
    CmdType type;
    int pageNum;
    int x, y, width, height;
    std::string text;
    std::string fontName;
    int fontSize;
    uint32_t fontColor;
    int fontWeight;
    int fontItalic;
    int underline;
    int strikeout;
    std::string styleName;
    int color; // for SET_TEXT_COLOR/SET_FILL_COLOR/SET_LINE_COLOR
};

// ── 解析字段 ──

static std::string extractQuoted(const std::string& s)
{
    auto start = s.find('"');
    if (start == std::string::npos)
        return "";
    auto end = s.find('"', start + 1);
    if (end == std::string::npos)
        return s.substr(start + 1);
    return s.substr(start + 1, end - start - 1);
}

static std::vector<std::string> splitTSV(const std::string& line)
{
    std::vector<std::string> fields;
    std::istringstream iss(line);
    std::string field;
    while (std::getline(iss, field, '\t'))
    {
        fields.push_back(field);
    }
    return fields;
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

static Instruction parseInstruction(const std::string& line, int lineNum)
{
    Instruction inst;
    inst.lineNum = lineNum;
    inst.rawLine = line;
    inst.type = CmdType::UNKNOWN;
    inst.pageNum = 0;
    inst.x = inst.y = inst.width = inst.height = 0;
    inst.fontSize = 0;
    inst.fontColor = 0;
    inst.fontWeight = 400;
    inst.fontItalic = 0;
    inst.underline = 0;
    inst.strikeout = 0;
    inst.color = 0;

    auto fields = splitTSV(line);
    if (fields.empty())
        return inst;

    inst.type = parseCmdType(fields[0]);

    // PAGE_START: TYPE pageNum width height
    if (inst.type == CmdType::PAGE_START)
    {
        if (fields.size() >= 2)
            inst.pageNum = parseInt(fields[1]);
        if (fields.size() >= 3)
            inst.width = parseInt(fields[2]);
        if (fields.size() >= 4)
            inst.height = parseInt(fields[3]);
    }
    // PAGE_END / SET_CLIP_REGION / PUSH / POP: TYPE pageNum
    else if (inst.type == CmdType::PAGE_END || inst.type == CmdType::SET_CLIP_REGION
             || inst.type == CmdType::PUSH || inst.type == CmdType::POP)
    {
        if (fields.size() >= 2)
            inst.pageNum = parseInt(fields[1]);
    }
    // TEXT_FRAME / TEXT_LINE / TEXT_RUN: TYPE pageNum x y w h "text" fontName fontSize fontColor fontWeight fontItalic underline strikeout styleName
    else if (inst.type == CmdType::TEXT_FRAME || inst.type == CmdType::TEXT_LINE
             || inst.type == CmdType::TEXT_RUN)
    {
        if (fields.size() >= 2)
            inst.pageNum = parseInt(fields[1]);
        if (fields.size() >= 3)
            inst.x = parseInt(fields[2]);
        if (fields.size() >= 4)
            inst.y = parseInt(fields[3]);
        if (fields.size() >= 5)
            inst.width = parseInt(fields[4]);
        if (fields.size() >= 6)
            inst.height = parseInt(fields[5]);
        if (fields.size() >= 7)
            inst.text = extractQuoted(fields[6]);
        if (fields.size() >= 8)
            inst.fontName = fields[7];
        if (fields.size() >= 9)
            inst.fontSize = parseInt(fields[8]);
        if (fields.size() >= 10)
            inst.fontColor = parseInt(fields[9]);
        if (fields.size() >= 11)
            inst.fontWeight = parseInt(fields[10]);
        if (fields.size() >= 12)
            inst.fontItalic = parseInt(fields[11]);
        if (fields.size() >= 13)
            inst.underline = parseInt(fields[12]);
        if (fields.size() >= 14)
            inst.strikeout = parseInt(fields[13]);
        if (fields.size() >= 15)
            inst.styleName = fields[14];
    }
    // SET_FONT: TYPE pageNum fontName fontSize fontWeight fontItalic
    else if (inst.type == CmdType::SET_FONT)
    {
        if (fields.size() >= 2)
            inst.pageNum = parseInt(fields[1]);
        if (fields.size() >= 3)
            inst.fontName = fields[2];
        if (fields.size() >= 4)
            inst.fontSize = parseInt(fields[3]);
        if (fields.size() >= 5)
            inst.fontWeight = parseInt(fields[4]);
        if (fields.size() >= 6)
            inst.fontItalic = parseInt(fields[5]);
    }
    // SET_TEXT_COLOR / SET_FILL_COLOR / SET_LINE_COLOR: TYPE pageNum color
    else if (inst.type == CmdType::SET_TEXT_COLOR || inst.type == CmdType::SET_FILL_COLOR
             || inst.type == CmdType::SET_LINE_COLOR)
    {
        if (fields.size() >= 2)
            inst.pageNum = parseInt(fields[1]);
        if (fields.size() >= 3)
            inst.color = parseInt(fields[2]);
    }
    // LINE / POLYLINE: TYPE pageNum x1 y1 x2 y2
    else if (inst.type == CmdType::LINE || inst.type == CmdType::POLYLINE)
    {
        if (fields.size() >= 2)
            inst.pageNum = parseInt(fields[1]);
        if (fields.size() >= 3)
            inst.x = parseInt(fields[2]);
        if (fields.size() >= 4)
            inst.y = parseInt(fields[3]);
        if (fields.size() >= 5)
            inst.width = parseInt(fields[4]); // x2
        if (fields.size() >= 6)
            inst.height = parseInt(fields[5]); // y2
    }
    // TABLE/IMAGE/SECTION/RECT/POLYGON/BITMAP/ELLIPSE: TYPE pageNum x y w h
    else
    {
        if (fields.size() >= 2)
            inst.pageNum = parseInt(fields[1]);
        if (fields.size() >= 3)
            inst.x = parseInt(fields[2]);
        if (fields.size() >= 4)
            inst.y = parseInt(fields[3]);
        if (fields.size() >= 5)
            inst.width = parseInt(fields[4]);
        if (fields.size() >= 6)
            inst.height = parseInt(fields[5]);
    }

    return inst;
}

// ── 比对 ──

struct DiffResult
{
    int refLine;
    int testLine;
    std::string description;
};

static std::vector<DiffResult> compareInstructions(const std::vector<Instruction>& ref,
                                                   const std::vector<Instruction>& test)
{
    std::vector<DiffResult> diffs;
    size_t maxLen = std::max(ref.size(), test.size());

    for (size_t i = 0; i < maxLen; ++i)
    {
        if (i >= ref.size())
        {
            diffs.push_back(
                { static_cast<int>(i) + 1, 0, "Extra instruction in test: " + test[i].rawLine });
            continue;
        }
        if (i >= test.size())
        {
            diffs.push_back({ 0, static_cast<int>(i) + 1,
                              "Missing instruction in test (ref has): " + ref[i].rawLine });
            continue;
        }

        const auto& r = ref[i];
        const auto& t = test[i];

        // 比较类型
        if (r.type != t.type)
        {
            diffs.push_back({ r.lineNum, t.lineNum,
                              std::string("Type: ref=") + cmdTypeName(r.type)
                                  + " test=" + cmdTypeName(t.type) });
            continue; // 类型不同，跳过详细比较
        }

        // 比较各字段 — 严格相等，不允许容差
        auto fieldDiff = [&](const std::string& name, auto refVal, auto testVal) {
            if (refVal != testVal)
            {
                diffs.push_back({ r.lineNum, t.lineNum,
                                  name + ": ref=" + std::to_string(refVal)
                                      + " test=" + std::to_string(testVal) });
            }
        };

        auto strDiff = [&](const std::string& name, const std::string& refVal,
                           const std::string& testVal) {
            if (refVal != testVal)
            {
                diffs.push_back({ r.lineNum, t.lineNum,
                                  name + ": ref=\"" + refVal + "\" test=\"" + testVal + "\"" });
            }
        };

        fieldDiff("pageNum", r.pageNum, t.pageNum);

        if (r.type == CmdType::PAGE_START)
        {
            fieldDiff("width", r.width, t.width);
            fieldDiff("height", r.height, t.height);
        }
        else if (r.type == CmdType::PAGE_END || r.type == CmdType::SET_CLIP_REGION
                 || r.type == CmdType::PUSH || r.type == CmdType::POP)
        {
            // 只有 pageNum，已在上方比较
        }
        else if (r.type == CmdType::SET_FONT)
        {
            strDiff("fontName", r.fontName, t.fontName);
            fieldDiff("fontSize", r.fontSize, t.fontSize);
            fieldDiff("fontWeight", r.fontWeight, t.fontWeight);
            fieldDiff("fontItalic", r.fontItalic, t.fontItalic);
        }
        else if (r.type == CmdType::SET_TEXT_COLOR || r.type == CmdType::SET_FILL_COLOR
                 || r.type == CmdType::SET_LINE_COLOR)
        {
            fieldDiff("color", r.color, t.color);
        }
        else if (r.type == CmdType::LINE || r.type == CmdType::POLYLINE)
        {
            fieldDiff("x1", r.x, t.x);
            fieldDiff("y1", r.y, t.y);
            fieldDiff("x2", r.width, t.width);
            fieldDiff("y2", r.height, t.height);
        }
        else if (r.type == CmdType::TEXT_FRAME || r.type == CmdType::TEXT_LINE
                 || r.type == CmdType::TEXT_RUN)
        {
            fieldDiff("x", r.x, t.x);
            fieldDiff("y", r.y, t.y);
            fieldDiff("width", r.width, t.width);
            fieldDiff("height", r.height, t.height);
            strDiff("text", r.text, t.text);
            strDiff("fontName", r.fontName, t.fontName);
            fieldDiff("fontSize", r.fontSize, t.fontSize);
            fieldDiff("fontColor", r.fontColor, t.fontColor);
            fieldDiff("fontWeight", r.fontWeight, t.fontWeight);
            fieldDiff("fontItalic", r.fontItalic, t.fontItalic);
            strDiff("styleName", r.styleName, t.styleName);
        }
        else
        {
            // TABLE/IMAGE/SECTION/RECT/POLYGON/BITMAP/ELLIPSE
            fieldDiff("x", r.x, t.x);
            fieldDiff("y", r.y, t.y);
            fieldDiff("width", r.width, t.width);
            fieldDiff("height", r.height, t.height);
        }
    }

    return diffs;
}

// ── 已知差异 ──

struct KnownDiff
{
    int lineNum;
    std::string pattern;
};

static std::vector<KnownDiff> loadKnownDiffs(const std::string& path)
{
    std::vector<KnownDiff> known;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line))
    {
        // 去除行尾 \r
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line[0] == '#')
            continue;
        auto tab = line.find('\t');
        if (tab != std::string::npos)
        {
            // 格式: lineNum<TAB>pattern
            known.push_back({ parseInt(line.substr(0, tab)), line.substr(tab + 1) });
        }
        else
        {
            // 格式: pattern (无行号，匹配所有行)
            known.push_back({ 0, line });
        }
    }
    return known;
}

static bool isKnownDiff(const DiffResult& diff, const std::vector<KnownDiff>& known)
{
    for (const auto& k : known)
    {
        if (diff.description.find(k.pattern) != std::string::npos)
            return true;
    }
    return false;
}

// ── Main ──

static std::string resolveLayerPath(const std::string& testDir, const std::string& prefix,
                                    const std::string& layer)
{
    // 如果 testDir 是相对路径，则相对于 exe 所在目录解析
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

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "Usage:\n"
                  << "  render_diff frame                 Compare frame layer\n"
                  << "  render_diff vcl                   Compare VCL layer\n"
                  << "  render_diff <ref.txt> <test.txt>  Compare arbitrary files\n"
                  << "Options:\n"
                  << "  --test-dir DIR   Test directory (default: ../test, relative to exe)\n"
                  << "  --known-diffs F  File with known differences to skip\n"
                  << "  --verbose        Show matching lines too\n";
        return 1;
    }

    std::string refPath;
    std::string testPath;
    std::string testDir = "../test";
    std::string knownDiffsPath;
    bool verbose = false;

    // 解析第一个参数：判断是 layer 名称还是文件路径
    std::string arg1 = argv[1];
    bool isLayer = (arg1 == "frame" || arg1 == "vcl");

    // 解析 --test-dir (必须在其他参数之前，因为可能影响 layer 路径解析)
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--test-dir") == 0 && i + 1 < argc)
        {
            testDir = argv[++i];
        }
    }

    if (isLayer)
    {
        // 快捷模式: render_diff frame / render_diff vcl
        refPath = resolveLayerPath(testDir, "lo_", arg1);
        testPath = resolveLayerPath(testDir, "aproj_", arg1);
        // 从第 2 个参数开始解析选项
        for (int i = 2; i < argc; ++i)
        {
            if (strcmp(argv[i], "--known-diffs") == 0 && i + 1 < argc)
                knownDiffsPath = argv[++i];
            else if (strcmp(argv[i], "--verbose") == 0)
                verbose = true;
        }
    }
    else
    {
        // 传统模式: render_diff <ref.txt> <test.txt>
        if (argc < 3)
        {
            std::cerr << "ERROR: two file paths required in file mode\n";
            return 1;
        }
        refPath = argv[1];
        testPath = argv[2];
        for (int i = 3; i < argc; ++i)
        {
            if (strcmp(argv[i], "--known-diffs") == 0 && i + 1 < argc)
                knownDiffsPath = argv[++i];
            else if (strcmp(argv[i], "--verbose") == 0)
                verbose = true;
        }
    }

    // 读取文件
    std::vector<Instruction> refInsts, testInsts;
    {
        std::ifstream f(refPath);
        std::string line;
        int n = 0;
        while (std::getline(f, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (!line.empty())
                refInsts.push_back(parseInstruction(line, ++n));
        }
    }
    {
        std::ifstream f(testPath);
        std::string line;
        int n = 0;
        while (std::getline(f, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (!line.empty())
                testInsts.push_back(parseInstruction(line, ++n));
        }
    }

    // 加载已知差异
    std::vector<KnownDiff> knownDiffs;
    if (!knownDiffsPath.empty())
    {
        knownDiffs = loadKnownDiffs(knownDiffsPath);
    }

    // 比对
    auto diffs = compareInstructions(refInsts, testInsts);

    // 分类差异
    int knownCount = 0;
    int newCount = 0;

    // 输出报告
    std::cout << "=== Render Comparison Report ===" << std::endl;
    std::cout << "Reference: " << refPath << " (" << refInsts.size() << " instructions)"
              << std::endl;
    std::cout << "Test:      " << testPath << " (" << testInsts.size() << " instructions)"
              << std::endl;
    std::cout << std::endl;

    // 统计各类型指令数
    auto countByType = [](const std::vector<Instruction>& insts) {
        std::vector<std::pair<CmdType, int>> counts;
        for (const auto& inst : insts)
        {
            bool found = false;
            for (auto& c : counts)
            {
                if (c.first == inst.type)
                {
                    c.second++;
                    found = true;
                    break;
                }
            }
            if (!found)
                counts.push_back({ inst.type, 1 });
        }
        return counts;
    };

    auto refCounts = countByType(refInsts);
    auto testCounts = countByType(testInsts);

    std::cout << "Instruction counts by type:" << std::endl;
    std::cout << "  Type          Ref    Test   Match" << std::endl;
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
        std::cout << "  " << cmdTypeName(rc.first) << "  " << rc.second << "    " << tc
                  << (match ? "   OK" : "   MISMATCH") << std::endl;
    }
    std::cout << std::endl;

    // 输出差异
    if (diffs.empty())
    {
        std::cout << "Result: PASS — No differences found." << std::endl;
        return 0;
    }

    std::cout << "Differences found: " << diffs.size() << std::endl << std::endl;

    for (const auto& diff : diffs)
    {
        bool known = isKnownDiff(diff, knownDiffs);
        if (known)
        {
            knownCount++;
            if (verbose)
            {
                std::cout << "  [KNOWN] " << diff.description << std::endl;
            }
        }
        else
        {
            newCount++;
            std::cout << "  [DIFF] Line ref=" << diff.refLine << " test=" << diff.testLine << ": "
                      << diff.description << std::endl;
        }
    }

    std::cout << std::endl;
    std::cout << "Summary:" << std::endl;
    std::cout << "  Total differences: " << diffs.size() << std::endl;
    std::cout << "  Known differences: " << knownCount << std::endl;
    std::cout << "  New differences:   " << newCount << std::endl;

    if (newCount > 0)
    {
        std::cout << std::endl << "Result: FAIL — " << newCount << " new differences." << std::endl;
        return 1;
    }

    std::cout << std::endl << "Result: PASS — All differences are known." << std::endl;
    return 0;
}
