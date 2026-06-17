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
#include "nodes_log.h"

#include <iostream>
#include <string>
#include <cstdio>
#include <fstream>
#include <vector>
#include <algorithm>

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

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#include <unistd.h>
#endif

// ── Get executable directory ─────────────────────────────────
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

// ── Resolve path relative to exe directory (if relative) ─────
static std::string resolvePath(const std::string& path)
{
    if (path.empty())
        return path;
        // 绝对路径直接返回
#ifdef _WIN32
    if (path.size() >= 2 && path[1] == ':')
        return path;
#else
    if (path[0] == '/')
        return path;
#endif
    std::string resolved = getExeDir() + path;
    // Normalize path separators for Windows
#ifdef _WIN32
    for (auto& c : resolved)
        if (c == '/')
            c = '\\';
    // Resolve .. components
    std::string::size_type pos;
    while ((pos = resolved.find("\\..\\")) != std::string::npos)
    {
        auto prev = resolved.rfind('\\', pos - 1);
        if (prev != std::string::npos)
            resolved.erase(prev, pos + 3 - prev);
        else
            break;
    }
#endif
    return resolved;
}

// ── Find samples directory ──────────────────────────────────────
static std::string findSamplesDir()
{
    // 搜索相对于 exe 所在目录的路径
    std::string exeDir = getExeDir();
    // exe 在 output/ 下，samples 在 ../samples
    // exe 在 build/Debug/ 下，samples 在 ../../samples
    const char* dirs[] = {
        // 相对 exe 目录 (output/)
        "../samples",
        "../../samples",
        // 相对 CWD
        "samples",
        "../samples",
        "../../samples",
        "../../../samples",
        "../../../../aproj/docx/samples",
        "aproj/docx/samples",
    };
    for (auto d : dirs)
    {
        // 先尝试相对于 exe 目录
        std::string exeRelative = exeDir + d;
#ifdef _WIN32
        DWORD attr = GetFileAttributesA(exeRelative.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
            return exeRelative;
        // 再尝试相对于 CWD
        attr = GetFileAttributesA(d);
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
            return d;
#else
        DIR* dir = opendir(exeRelative.c_str());
        if (dir)
        {
            closedir(dir);
            return exeRelative;
        }
        dir = opendir(d);
        if (dir)
        {
            closedir(dir);
            return d;
        }
#endif
    }
    return "";
}

// ── Scan DOCX files in directory ───────────────────────────────
static std::vector<std::string> scanDocxFiles(const std::string& dirPath)
{
    std::vector<std::string> files;
#ifdef _WIN32
    WIN32_FIND_DATAA findData;
    std::string searchPattern = dirPath + "/*.docx";
    HANDLE hFind = FindFirstFileA(searchPattern.c_str(), &findData);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            {
                files.push_back(dirPath + "/" + findData.cFileName);
            }
        } while (FindNextFileA(hFind, &findData));
        FindClose(hFind);
    }
#else
    DIR* dir = opendir(dirPath.c_str());
    if (dir)
    {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr)
        {
            std::string name = entry->d_name;
            if (name.size() > 5 && name.substr(name.size() - 5) == ".docx")
            {
                files.push_back(dirPath + "/" + name);
            }
        }
        closedir(dir);
    }
#endif
    std::sort(files.begin(), files.end());
    return files;
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

    // 3. 创建 Frame（仅正文区 EndOfContent 内节点）
    SwNodes& rNodes = doc.GetNodes();
    SwNode& rEndOfContent = rNodes.GetEndOfContent();
    TEST_ASSERT_TRUE(rEndOfContent.IsEndNode(), "EndOfContent is EndNode");
    SwEndNode* pBodyEndNode = rEndOfContent.GetEndNode();
    SwStartNode* pBodyStart = pBodyEndNode ? pBodyEndNode->GetStartNode() : nullptr;
    TEST_ASSERT_TRUE(pBodyStart != nullptr, "Body start node exists");
    SwNodeOffset nBodyStt = pBodyStart->GetIndex() + SwNodeOffset(1);
    SwNodeOffset nBodyEnd = rEndOfContent.GetIndex() - SwNodeOffset(1);
    std::cerr << "[TEST] About to call MakeFrames, body range " << nBodyStt << ".." << nBodyEnd
              << std::endl;
    MakeFrames(doc, *rNodes[nBodyStt], *rNodes[nBodyEnd]);
    MakeFlyFrames(doc);
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
    logger.LogFrameTree(pRoot);
    std::cerr << "[TEST] LogFrameTree done" << std::endl;

    std::cerr << "[TEST] WriteFrameLayerToFile..." << std::endl;
    logger.WriteFrameLayerToFile(resolvePath("../test/aproj_frame.txt"));
    std::cerr << "[TEST] WriteFrameLayerToFile done" << std::endl;
    std::cerr << "[TEST] WriteVclLayerToFile..." << std::endl;
    logger.WriteVclLayerToFile(resolvePath("../test/aproj_vcl.txt"));
    std::cerr << "[TEST] WriteVclLayerToFile done" << std::endl;

    // 6.5 节点结构输出
    std::cerr << "[TEST] NodesLogger..." << std::endl;
    NodesLogger nodesLogger;
    nodesLogger.LogNodes(rNodes);
    nodesLogger.WriteToFile(resolvePath("../test/aproj_nodes.txt"));
    std::cerr << "[TEST] NodesLogger done" << std::endl;

    // 7. 验证 frame 层
    std::string framePath = resolvePath("../test/aproj_frame.txt");
    std::ifstream fFrame(framePath);
    TEST_ASSERT_TRUE(fFrame.good(), "test/aproj_frame.txt created");
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
    std::string vclPath = resolvePath("../test/aproj_vcl.txt");
    std::ifstream fVcl(vclPath);
    TEST_ASSERT_TRUE(fVcl.good(), "test/aproj_vcl.txt created");
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

    // 9. 汇总
    std::cout << "  Summary: body range " << nBodyStt << ".." << nBodyEnd << std::endl;
}

// ── Main ───────────────────────────────────────────────────────
int main(int argc, char* argv[])
{
    std::cout << "========================================" << std::endl;
    std::cout << "  SwDoc End-to-End Pipeline — Test Suite" << std::endl;
    std::cout << "========================================" << std::endl;

    std::vector<std::string> testFiles;

    if (argc >= 2)
    {
        testFiles.push_back(argv[1]);
    }
    else
    {
        std::string samplesDir = findSamplesDir();
        if (samplesDir.empty())
        {
            std::cerr << "ERROR: samples directory not found." << std::endl;
            std::cerr << "  Pass the path as argument: docx_e2e_test <path.docx>" << std::endl;
            return 1;
        }

        testFiles = scanDocxFiles(samplesDir);
        if (testFiles.empty())
        {
            std::cerr << "ERROR: No DOCX files found in samples directory." << std::endl;
            std::cerr << "  Pass the path as argument: docx_e2e_test <path.docx>" << std::endl;
            return 1;
        }

        std::cout << "Found " << testFiles.size()
                  << " DOCX file(s) in samples directory:" << std::endl;
        for (const auto& f : testFiles)
            std::cout << "  - " << f << std::endl;
        std::cout << std::endl;
    }

    for (size_t i = 0; i < testFiles.size(); ++i)
    {
        const std::string& testFile = testFiles[i];
        std::cout << "[" << (i + 1) << "/" << testFiles.size() << "] Test file: " << testFile
                  << std::endl;

        test_swdoc_parse(testFile);
        std::cout << std::endl;
        test_swdoc_layout_and_render(testFile);
        std::cout << std::endl;
    }

    std::cout << "========================================" << std::endl;
    std::cout << "  Results: " << g_passed << "/" << g_tests << " passed";
    if (g_failed > 0)
        std::cout << ", " << g_failed << " FAILED";
    std::cout << std::endl;
    std::cout << "========================================" << std::endl;

    return g_failed > 0 ? 1 : 0;
}
