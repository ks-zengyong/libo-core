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

// ── Test 2: Layout + Render 完整管线 ─────────────────────────
void test_swdoc_layout_and_render(const std::string& filePath)
{
    std::cout << "[Test] Layout + Render" << std::endl;

    // 1. 解析
    SwDoc doc;
    DocxParser parser;
    TEST_ASSERT_TRUE(parser.Read(filePath, doc), "Parse DOCX");

    // 2. 布局初始化
    SwRootFrame* pRoot = InitLayout(doc);
    TEST_ASSERT_TRUE(pRoot != nullptr, "InitLayout returns root frame");
    TEST_ASSERT_EQ(doc.GetRootFrame(), pRoot, "RootFrame registered in SwDoc");

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
    std::cerr << "[TEST] About to call MakeFrames, nCount=" << rNodes.Count() << std::endl;
    if (pFirst && pLast)
        MakeFrames(doc, *pFirst, *pLast);
    std::cerr << "[TEST] MakeFrames done" << std::endl;

    // 4. 验证 Frame 树
    std::cerr << "[TEST] Verifying Frame tree..." << std::endl;
    SwPageFrame* pPage = pRoot->GetLastPage();
    TEST_ASSERT_TRUE(pPage != nullptr, "Has at least one page");
    if (pPage)
    {
        TEST_ASSERT_GT(pPage->getFrameArea().Width(), 0, "Page width > 0");
        TEST_ASSERT_GT(pPage->getFrameArea().Height(), 0, "Page height > 0");

        int nTextFrames = 0;
        std::function<void(SwFrame*)> countFrames = [&](SwFrame* pF) {
            while (pF)
            {
                if (pF->IsTextFrame())
                    ++nTextFrames;
                if (pF->IsLayoutFrame())
                    countFrames(static_cast<SwLayoutFrame*>(pF)->GetLower());
                pF = pF->GetNext();
            }
        };
        countFrames(pPage->GetLower());
        std::cout << "  Text frames: " << nTextFrames << std::endl;
        TEST_ASSERT_GT(nTextFrames, 0, "Has text frames");
    }

    // 5. 排版
    std::cerr << "[TEST] SwLayAction::Action..." << std::endl;
    SwLayAction layAction(*pRoot);
    layAction.Action();
    std::cerr << "[TEST] SwLayAction done" << std::endl;

    // 6. 渲染指令输出 (分层)
    RenderLogger logger;
    std::cerr << "[TEST] LogFrameTree..." << std::endl;
    logger.StartLog("tests/aproj_all.log");
    logger.LogFrameTree(pRoot);
    logger.EndLog();
    std::cerr << "[TEST] LogFrameTree done" << std::endl;

    std::cerr << "[TEST] WriteFrameLayerToFile..." << std::endl;
    logger.WriteFrameLayerToFile("tests/aproj_frame.txt");
    std::cerr << "[TEST] WriteFrameLayerToFile done" << std::endl;
    std::cerr << "[TEST] WriteVclLayerToFile..." << std::endl;
    logger.WriteVclLayerToFile("tests/aproj_vcl.txt");
    std::cerr << "[TEST] WriteVclLayerToFile done" << std::endl;

    // 7. 验证 frame 层
    std::ifstream fFrame("tests/aproj_frame.txt");
    TEST_ASSERT_TRUE(fFrame.good(), "tests/aproj_frame.txt created");
    int nFrameLines = 0;
    bool hasFramePageStart = false, hasTextFrame = false;
    std::string line;
    while (std::getline(fFrame, line))
    {
        ++nFrameLines;
        if (line.find("PAGE_START") == 0)
            hasFramePageStart = true;
        if (line.find("TEXT_FRAME") == 0)
            hasTextFrame = true;
    }
    fFrame.close();
    std::cout << "  Frame layer: " << nFrameLines << " instructions" << std::endl;
    TEST_ASSERT_TRUE(hasFramePageStart, "Frame layer has PAGE_START");
    TEST_ASSERT_TRUE(hasTextFrame, "Frame layer has TEXT_FRAME");

    // 8. 验证 VCL 层
    std::ifstream fVcl("tests/aproj_vcl.txt");
    TEST_ASSERT_TRUE(fVcl.good(), "tests/aproj_vcl.txt created");
    int nVclLines = 0;
    bool hasVclPageStart = false, hasTextRun = false, hasSetFont = false;
    while (std::getline(fVcl, line))
    {
        ++nVclLines;
        if (line.find("PAGE_START") == 0)
            hasVclPageStart = true;
        if (line.find("TEXT_RUN") == 0)
            hasTextRun = true;
        if (line.find("SET_FONT") == 0)
            hasSetFont = true;
    }
    fVcl.close();
    std::cout << "  VCL layer: " << nVclLines << " instructions" << std::endl;
    TEST_ASSERT_TRUE(hasVclPageStart, "VCL layer has PAGE_START");
    TEST_ASSERT_TRUE(hasTextRun, "VCL layer has TEXT_RUN");
    TEST_ASSERT_TRUE(hasSetFont, "VCL layer has SET_FONT");

    // 9. 输出调试文件
    DumpFrameTreeXml(pRoot, "tests/frmtree_dump.xml");

    std::cout << "  Summary: " << nTextNodes << " text nodes" << std::endl;
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
    test_swdoc_layout_and_render(testFile);
    std::cout << std::endl;

    std::cout << "========================================" << std::endl;
    std::cout << "  Results: " << g_passed << "/" << g_tests << " passed";
    if (g_failed > 0)
        std::cout << ", " << g_failed << " FAILED";
    std::cout << std::endl;
    std::cout << "========================================" << std::endl;

    return g_failed > 0 ? 1 : 0;
}
