// 端到端集成测试：验证 SwDoc 完整管线
// DocxParser::Read() → SwDoc → InitLayout() + MakeFrames() → SwLayAction::Action() → RenderLogger

#include "docx_parser.h"
#include "doc.h"
#include "node.h"
#include "ndarr.h"
#include "format.h"
#include "frmtree.h"
#include "frame.h"
#include "layact.h"
#include "render_log.h"

#include <iostream>
#include <string>
#include <cstdio>
#include <fstream>

// ── Simple Assert Macros ───────────────────────────────────────
static int g_tests = 0;
static int g_passed = 0;
static int g_failed = 0;

#define TEST_ASSERT(cond, msg)                                                                     \
    do                                                                                             \
    {                                                                                              \
        g_tests++;                                                                                 \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::cerr << "  FAIL: " << msg << "  (" << __FILE__ << ":" << __LINE__ << ")"          \
                      << std::endl;                                                                \
            g_failed++;                                                                            \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            g_passed++;                                                                            \
        }                                                                                          \
    } while (0)

#define TEST_ASSERT_EQ(a, b, msg)                                                                  \
    TEST_ASSERT((a) == (b), msg << " (expected " << (b) << ", got " << (a) << ")")
#define TEST_ASSERT_GT(a, b, msg)                                                                  \
    TEST_ASSERT((a) > (b), msg << " (expected " << (a) << " > " << (b) << ")")
#define TEST_ASSERT_GE(a, b, msg)                                                                  \
    TEST_ASSERT((a) >= (b), msg << " (expected " << (a) << " >= " << (b) << ")")
#define TEST_ASSERT_TRUE(cond, msg) TEST_ASSERT((cond), msg)
#define TEST_ASSERT_FALSE(cond, msg) TEST_ASSERT(!(cond), msg)

// ── Find test file ─────────────────────────────────────────────
static std::string findTestFile()
{
    const char* paths[] = {
        "tests/sample.docx",      "sample.docx",          "../sample.docx",
        "../../sample.docx",      "../../../sample.docx", "../../../../aproj/docx/sample.docx",
        "aproj/docx/sample.docx",
    };
    for (auto p : paths)
    {
        FILE* f = fopen(p, "rb");
        if (f)
        {
            fclose(f);
            return p;
        }
    }
    return "";
}

// ── Test 1: DocxParser → SwDoc ─────────────────────────────────
void test_swdoc_parse(const std::string& filePath)
{
    std::cout << "[Test] SwDoc Parse: " << filePath << std::endl;

    SwDoc doc;
    DocxParser parser;
    bool ok = parser.Read(filePath, doc);
    TEST_ASSERT_TRUE(ok, "DocxParser::Read succeeds");

    SwNodes& rNodes = doc.GetNodes();
    SwNodeOffset nCount = rNodes.Count();
    std::cout << "  Nodes: " << nCount << std::endl;
    TEST_ASSERT_GT(nCount, 0, "Node count > 0");

    int nTextNodes = 0;
    for (SwNodeOffset i = 0; i < nCount; ++i)
    {
        SwNode* pNode = rNodes[i];
        if (pNode && pNode->IsTextNode())
            ++nTextNodes;
    }
    std::cout << "  Text nodes: " << nTextNodes << std::endl;
    TEST_ASSERT_GT(nTextNodes, 0, "Has text nodes");

    SwTextFormatColl* pDefColl = doc.GetDefaultTextFormatColl();
    TEST_ASSERT_TRUE(pDefColl != nullptr, "Has default text format collection");

    SwPageDesc* pDefDesc = doc.GetDefaultPageDesc();
    TEST_ASSERT_TRUE(pDefDesc != nullptr, "Has default page descriptor");

    bool hasContent = false;
    for (SwNodeOffset i = 0; i < nCount; ++i)
    {
        SwNode* pNode = rNodes[i];
        if (pNode && pNode->IsTextNode())
        {
            SwTextNode* pText = static_cast<SwTextNode*>(pNode);
            if (!pText->GetText().empty())
            {
                hasContent = true;
                break;
            }
        }
    }
    TEST_ASSERT_TRUE(hasContent, "At least one text node has content");

    DumpNodesXml(doc, "tests/nodes_dump.xml");
    std::cout << "  Nodes dumped to nodes_dump.xml" << std::endl;
}

// ── Test 2: InitLayout + MakeFrames + SwLayAction ─────────────
void test_swdoc_layout(const std::string& filePath)
{
    std::cout << "[Test] SwDoc Layout" << std::endl;

    SwDoc doc;
    DocxParser parser;
    parser.Read(filePath, doc);

    SwRootFrame* pRoot = InitLayout(doc);
    TEST_ASSERT_TRUE(pRoot != nullptr, "InitLayout returns root frame");
    TEST_ASSERT_EQ(doc.GetRootFrame(), pRoot, "RootFrame registered in SwDoc");

    SwNodes& rNodes = doc.GetNodes();
    SwNodeOffset nCount = rNodes.Count();
    SwNode* pFirst = nullptr;
    SwNode* pLast = nullptr;
    for (SwNodeOffset i = 0; i < nCount; ++i)
    {
        SwNode* pNode = rNodes[i];
        if (pNode && pNode->IsTextNode())
        {
            if (!pFirst)
                pFirst = pNode;
            pLast = pNode;
        }
    }

    TEST_ASSERT_TRUE(pFirst != nullptr, "Found first text node");
    if (pFirst && pLast)
        MakeFrames(doc, *pFirst, *pLast);

    SwPageFrame* pPage = pRoot->GetLastPage();
    TEST_ASSERT_TRUE(pPage != nullptr, "Has at least one page");

    if (pPage)
    {
        TEST_ASSERT_GT(pPage->getFrameArea().Width(), 0, "Page width > 0");
        TEST_ASSERT_GT(pPage->getFrameArea().Height(), 0, "Page height > 0");

        int nTextFrames = 0;
        // 递归统计所有文本 Frame（包括 body frame 的子节点）
        std::function<void(SwFrame*)> countTextFrames = [&](SwFrame* pF) {
            while (pF)
            {
                if (pF->IsTextFrame())
                    ++nTextFrames;
                if (pF->IsLayoutFrame())
                    countTextFrames(static_cast<SwLayoutFrame*>(pF)->GetLower());
                pF = pF->GetNext();
            }
        };
        countTextFrames(pPage->GetLower());
        std::cout << "  Text frames on page 1: " << nTextFrames << std::endl;
        TEST_ASSERT_GT(nTextFrames, 0, "Has text frames");
    }

    SwLayAction layAction(*pRoot);
    layAction.Action();

    DumpFrameTreeXml(pRoot, "tests/frmtree_dump.xml");
    std::cout << "  Frame tree dumped to frmtree_dump.xml" << std::endl;
}

// ── Test 3: RenderLogger (共享格式) ────────────────────────────
void test_swdoc_render(const std::string& filePath)
{
    std::cout << "[Test] SwDoc Render" << std::endl;

    SwDoc doc;
    DocxParser parser;
    parser.Read(filePath, doc);

    SwRootFrame* pRoot = InitLayout(doc);
    SwNodes& rNodes = doc.GetNodes();
    SwNodeOffset nCount = rNodes.Count();
    SwNode* pFirst = nullptr;
    SwNode* pLast = nullptr;
    for (SwNodeOffset i = 0; i < nCount; ++i)
    {
        SwNode* pNode = rNodes[i];
        if (pNode && pNode->IsTextNode())
        {
            if (!pFirst)
                pFirst = pNode;
            pLast = pNode;
        }
    }
    if (pFirst && pLast)
        MakeFrames(doc, *pFirst, *pLast);
    SwLayAction layAction(*pRoot);
    layAction.Action();

    // 使用 RenderLogger 记录渲染指令 (输出到 tests/ 目录)
    RenderLogger logger;
    logger.StartLog("tests/render_test.log");
    logger.LogFrameTree(pRoot);
    logger.EndLog();

    const auto& instructions = logger.GetInstructions();
    std::cout << "  Render instructions: " << instructions.size() << std::endl;
    TEST_ASSERT_GT(static_cast<int>(instructions.size()), 0, "Has render instructions");

    // 统计指令类型
    int nPages = 0, nTextFrames = 0, nTextLines = 0, nTextRuns = 0;
    for (const auto& inst : instructions)
    {
        switch (inst.type)
        {
            case RenderCmdType::PAGE_START:
                ++nPages;
                break;
            case RenderCmdType::TEXT_FRAME:
                ++nTextFrames;
                break;
            case RenderCmdType::TEXT_LINE:
                ++nTextLines;
                break;
            case RenderCmdType::TEXT_RUN:
                ++nTextRuns;
                break;
            default:
                break;
        }
    }
    std::cout << "  Pages: " << nPages << ", TextFrames: " << nTextFrames
              << ", TextLines: " << nTextLines << ", TextRuns: " << nTextRuns << std::endl;
    TEST_ASSERT_GT(nPages, 0, "Has page instructions");
    TEST_ASSERT_TRUE(nTextFrames > 0 || nTextRuns > 0, "Has text frame or text run instructions");

    // 验证 TSV 格式输出
    logger.WriteToFile("tests/render_write_test.txt");
    std::ifstream f("tests/render_write_test.txt");
    TEST_ASSERT_TRUE(f.good(), "WriteToFile produces readable file");

    // 检查第一行是否是 PAGE_START
    std::string firstLine;
    if (std::getline(f, firstLine))
    {
        TEST_ASSERT_TRUE(firstLine.find("PAGE_START") == 0, "First line starts with PAGE_START");
        // 检查是否使用 TAB 分隔
        TEST_ASSERT_TRUE(firstLine.find('\t') != std::string::npos, "Uses TAB separator");
    }
    f.close();
}

// ── Test 4: 完整管线 + 输出共享格式 ────────────────────────────
void test_full_swdoc_pipeline(const std::string& filePath)
{
    std::cout << "[Test] Full SwDoc Pipeline" << std::endl;

    // 1. 解析
    SwDoc doc;
    DocxParser parser;
    TEST_ASSERT_TRUE(parser.Read(filePath, doc), "Parse DOCX");

    // 2. 布局初始化
    SwRootFrame* pRoot = InitLayout(doc);
    TEST_ASSERT_TRUE(pRoot != nullptr, "InitLayout");

    // 3. 创建 Frame
    SwNodes& rNodes = doc.GetNodes();
    SwNodeOffset nCount = rNodes.Count();
    SwNode* pFirst = nullptr;
    SwNode* pLast = nullptr;
    int nTextNodes = 0;
    for (SwNodeOffset i = 0; i < nCount; ++i)
    {
        SwNode* pNode = rNodes[i];
        if (pNode && pNode->IsTextNode())
        {
            if (!pFirst)
                pFirst = pNode;
            pLast = pNode;
            ++nTextNodes;
        }
    }
    TEST_ASSERT_TRUE(pFirst != nullptr, "Has content nodes");
    if (pFirst && pLast)
        MakeFrames(doc, *pFirst, *pLast);

    // 4. 排版
    SwLayAction layAction(*pRoot);
    layAction.Action();

    // 5. 渲染指令输出 (共享格式)
    RenderLogger logger;
    logger.StartLog("tests/render_output.txt");
    logger.LogFrameTree(pRoot);
    logger.EndLog();
    // 注意：WriteToFile 不再需要，因为 OnInstruction 已实时写入文件
    // logger.WriteToFile("render_output.txt");

    // 验证输出文件
    std::ifstream f("tests/render_output.txt");
    TEST_ASSERT_TRUE(f.good(), "tests/render_output.txt created");

    std::string line;
    int nLines = 0;
    bool hasPageStart = false, hasPageEnd = false, hasTextRun = false;
    while (std::getline(f, line))
    {
        ++nLines;
        if (line.find("PAGE_START") == 0)
            hasPageStart = true;
        if (line.find("PAGE_END") == 0)
            hasPageEnd = true;
        if (line.find("TEXT_RUN") == 0)
            hasTextRun = true;
    }
    f.close();

    std::cout << "  render_output.txt: " << nLines << " lines" << std::endl;
    TEST_ASSERT_GT(nLines, 0, "Output file has content");
    TEST_ASSERT_TRUE(hasPageStart, "Output has PAGE_START instructions");
    TEST_ASSERT_TRUE(hasPageEnd, "Output has PAGE_END instructions");
    TEST_ASSERT_TRUE(hasTextRun, "Output has TEXT_RUN instructions");

    // 6. 验证指令格式与 render_instruction.h 一致
    // 读取第一条 TEXT_RUN 指令，检查字段数
    std::ifstream f2("tests/render_output.txt");
    while (std::getline(f2, line))
    {
        if (line.find("TEXT_RUN") == 0)
        {
            // 统计 TAB 数量 (应为 14 个 TAB = 15 个字段)
            int tabCount = 0;
            for (char c : line)
            {
                if (c == '\t')
                    ++tabCount;
            }
            // TEXT_FRAME fields: type, pageNum, x, y, w, h, text, fontName, fontSize,
            //                   fontColor, fontWeight, fontItalic, underline, strikeout, styleName = 15 fields
            TEST_ASSERT_GE(tabCount, 13, "TEXT_FRAME has enough fields (>=14 TABs)");
            break;
        }
    }
    f2.close();

    std::cout << "  Summary: " << nTextNodes << " text nodes, " << nLines << " render instructions"
              << std::endl;
}

// ── Main ───────────────────────────────────────────────────────
int main(int argc, char* argv[])
{
    std::cout << "========================================" << std::endl;
    std::cout << "  SwDoc End-to-End Pipeline — Test Suite" << std::endl;
    std::cout << "========================================" << std::endl;

    std::string testFile = findTestFile();
    if (testFile.empty() && argc >= 2)
        testFile = argv[1];

    if (testFile.empty())
    {
        std::cerr << "ERROR: sample.docx not found." << std::endl;
        std::cerr << "  Pass the path as argument: docx_e2e_test <path.docx>" << std::endl;
        return 1;
    }

    std::cout << "Test file: " << testFile << std::endl << std::endl;

    test_swdoc_parse(testFile);
    std::cout << std::endl;
    test_swdoc_layout(testFile);
    std::cout << std::endl;
    test_swdoc_render(testFile);
    std::cout << std::endl;
    test_full_swdoc_pipeline(testFile);
    std::cout << std::endl;

    std::cout << "========================================" << std::endl;
    std::cout << "  Results: " << g_passed << "/" << g_tests << " passed";
    if (g_failed > 0)
        std::cout << ", " << g_failed << " FAILED";
    std::cout << std::endl;
    std::cout << "========================================" << std::endl;

    return g_failed > 0 ? 1 : 0;
}
