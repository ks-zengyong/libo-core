// Test suite for DOCX Core Pipeline.
// Simple assert-based tests — no external framework needed.

// stb implementations — must be in exactly one .cpp file per target
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "document.h"
#include "docx_reader.h"
#include "frame.h"
#include "layout.h"
#include "renderer.h"
#include "font_engine.h"

#include <iostream>
#include <string>
#include <cstdlib>
#include <cmath>

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
    // Try common locations relative to executable
    const char* paths[] = {
        "../WPS Docs Quick Start Guide.docx",
        "../../WPS Docs Quick Start Guide.docx",
        "../../../WPS Docs Quick Start Guide.docx",
        "../../../../aproj/WPS Docs Quick Start Guide.docx",
        "WPS Docs Quick Start Guide.docx",
        "aproj/WPS Docs Quick Start Guide.docx",
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

// ── Test: Document Model ───────────────────────────────────────
void test_document_model()
{
    std::cout << "[Test] Document Model" << std::endl;

    // Test Color::fromHex
    {
        docx::Color c = docx::Color::fromHex("FF0000");
        TEST_ASSERT_EQ(c.r, 255, "Color red");
        TEST_ASSERT_EQ(c.g, 0, "Color green");
        TEST_ASSERT_EQ(c.b, 0, "Color blue");
        TEST_ASSERT_TRUE(c.valid, "Color valid");
    }
    {
        docx::Color c = docx::Color::fromHex("#00FF00");
        TEST_ASSERT_EQ(c.r, 0, "Color #00FF00 red");
        TEST_ASSERT_EQ(c.g, 255, "Color #00FF00 green");
    }
    {
        docx::Color c = docx::Color::fromHex("auto");
        TEST_ASSERT_TRUE(c.valid, "Color auto valid");
        TEST_ASSERT_EQ(c.r, 0, "Color auto is black");
    }

    // Test ParagraphProps::mergeFrom
    {
        docx::ParagraphProps base;
        base.alignment = docx::TextAlign::Left;
        base.spaceBefore = 100;

        docx::ParagraphProps over;
        over.alignment = docx::TextAlign::Center;
        over.spaceAfter = 200;

        base.mergeFrom(over);
        TEST_ASSERT(base.alignment == docx::TextAlign::Center, "Merge alignment");
        TEST_ASSERT_EQ(base.spaceBefore, 100, "Merge keep spaceBefore");
        TEST_ASSERT_EQ(base.spaceAfter, 200, "Merge spaceAfter");
    }

    // Test RunProps::mergeFrom
    {
        docx::RunProps base;
        base.fontName = "Arial";
        base.fontSize = 22;

        docx::RunProps over;
        over.bold = true;
        over.color = docx::Color::fromHex("FF0000");

        base.mergeFrom(over);
        TEST_ASSERT_TRUE(base.bold, "Merge bold");
        TEST_ASSERT_EQ(base.fontName, std::string("Arial"), "Merge keep fontName");
        TEST_ASSERT_TRUE(base.color.valid, "Merge color valid");
    }
}

// ── Test: DOCX Parsing ────────────────────────────────────────
void test_docx_parsing(const std::string& filePath)
{
    std::cout << "[Test] DOCX Parsing" << std::endl;

    docx::Document doc;
    docx::DocxReader reader;
    bool ok = reader.read(filePath, doc);
    TEST_ASSERT_TRUE(ok, "Read DOCX file");

    // Verify paragraph count
    TEST_ASSERT_GT((int)doc.paragraphs.size(), 10, "Has many paragraphs");
    // The exact count depends on how we count, but should be around 122
    TEST_ASSERT_GE((int)doc.paragraphs.size(), 50, "At least 50 paragraphs");

    // Verify table
    TEST_ASSERT_GE((int)doc.tables.size(), 1, "Has at least 1 table");

    // Verify styles
    TEST_ASSERT_TRUE(doc.styles.count("Normal") > 0, "Has Normal style");
    // heading 1 style might be named differently
    bool hasHeading = doc.styles.count("heading 1") > 0 || doc.styles.count("Heading1") > 0
                      || doc.styles.count("Heading") > 0 || doc.styles.count("2") > 0;
    if (!hasHeading)
    {
        // Check by name field
        for (auto & [ id, s ] : doc.styles)
        {
            if (s.name.find("heading") != std::string::npos
                || s.name.find("Heading") != std::string::npos)
            {
                hasHeading = true;
                break;
            }
        }
    }
    TEST_ASSERT_TRUE(hasHeading, "Has heading style");

    // Verify first paragraph has content
    if (!doc.paragraphs.empty())
    {
        bool hasContent = false;
        for (auto& para : doc.paragraphs)
        {
            if (!para.fullText().empty())
            {
                hasContent = true;
                break;
            }
        }
        TEST_ASSERT_TRUE(hasContent, "At least one paragraph has text");
    }

    // Verify page dimensions
    TEST_ASSERT_GT(doc.sectionProps.pageWidth, 0, "Page width > 0");
    TEST_ASSERT_GT(doc.sectionProps.pageHeight, 0, "Page height > 0");

    // Verify images
    TEST_ASSERT_GE((int)doc.images.size(), 1, "Has at least 1 image");
}

// ── Test: Font Engine ──────────────────────────────────────────
void test_font_engine()
{
    std::cout << "[Test] Font Engine" << std::endl;

    docx::FontEngine fonts;

    // Try loading a common font
    bool loaded = fonts.loadFont("Arial");
    TEST_ASSERT_TRUE(loaded, "Load Arial font");

    if (loaded)
    {
        float width = fonts.getStringWidth("Hello", "Arial", 22);
        TEST_ASSERT_GT(width, 0, "String width > 0");

        float height = fonts.getLineHeight("Arial", 22);
        TEST_ASSERT_GT(height, 0, "Line height > 0");

        float ascent = fonts.getAscent("Arial", 22);
        TEST_ASSERT_GT(ascent, 0, "Ascent > 0");
    }

    // Test fallback
    bool fallback = fonts.loadFont("NonExistentFont");
    TEST_ASSERT_TRUE(fallback, "Fallback font loads");
}

// ── Test: Frame Tree ───────────────────────────────────────────
void test_frame_tree(const std::string& filePath)
{
    std::cout << "[Test] Frame Tree" << std::endl;

    docx::Document doc;
    docx::DocxReader reader;
    reader.read(filePath, doc);

    docx::RootFrame root;
    docx::FrameBuilder::build(doc, root, 96.0f);

    TEST_ASSERT_GE((int)root.pages.size(), 1, "At least 1 page");

    if (!root.pages.empty())
    {
        auto& page = root.pages[0];
        TEST_ASSERT_GT(page.width, 0, "Page width > 0");
        TEST_ASSERT_GT(page.height, 0, "Page height > 0");
        TEST_ASSERT_GT(page.body.width, 0, "Body width > 0");
        TEST_ASSERT_GT(page.body.height, 0, "Body height > 0");
    }
}

// ── Test: Layout ───────────────────────────────────────────────
void test_layout(const std::string& filePath)
{
    std::cout << "[Test] Layout" << std::endl;

    docx::Document doc;
    docx::DocxReader reader;
    reader.read(filePath, doc);

    docx::RootFrame root;
    docx::FrameBuilder::build(doc, root, 96.0f);

    docx::FontEngine fonts;
    docx::LayoutEngine layout;
    layout.layout(doc, root, fonts);

    TEST_ASSERT_GE((int)root.pages.size(), 1, "Has pages after layout");

    // Check that text frames were created
    int totalTextFrames = 0;
    for (auto& page : root.pages)
    {
        totalTextFrames += (int)page.body.textFrames.size();
    }
    TEST_ASSERT_GT(totalTextFrames, 0, "Has text frames");

    // Check that text frames have lines
    for (auto& page : root.pages)
    {
        for (auto& tf : page.body.textFrames)
        {
            TEST_ASSERT_GT((int)tf.lines.size(), 0, "TextFrame has lines");
            for (auto& line : tf.lines)
            {
                TEST_ASSERT_GT(line.height, 0, "Line height > 0");
            }
        }
    }
}

// ── Test: Renderer ─────────────────────────────────────────────
void test_renderer(const std::string& filePath)
{
    std::cout << "[Test] Renderer" << std::endl;

    docx::Document doc;
    docx::DocxReader reader;
    reader.read(filePath, doc);

    docx::RootFrame root;
    docx::FrameBuilder::build(doc, root, 96.0f);

    docx::FontEngine fonts;
    docx::LayoutEngine layout;
    layout.layout(doc, root, fonts);

    docx::Renderer renderer;
    docx::Bitmap bmp = renderer.render(root, fonts);

    TEST_ASSERT_GT(bmp.width, 0, "Bitmap width > 0");
    TEST_ASSERT_GT(bmp.height, 0, "Bitmap height > 0");
    TEST_ASSERT_FALSE(bmp.pixels.empty(), "Bitmap has pixels");

    // Save test output
    bool saved = bmp.savePNG("test_output.png");
    TEST_ASSERT_TRUE(saved, "Save test_output.png");
}

// ── Test: Full Pipeline ────────────────────────────────────────
void test_full_pipeline(const std::string& filePath)
{
    std::cout << "[Test] Full Pipeline" << std::endl;

    // Parse
    docx::Document doc;
    docx::DocxReader reader;
    TEST_ASSERT_TRUE(reader.read(filePath, doc), "Parse DOCX");

    // Build frames
    docx::RootFrame root;
    docx::FrameBuilder::build(doc, root, 96.0f);
    TEST_ASSERT_GE((int)root.pages.size(), 1, "Has pages");

    // Layout
    docx::FontEngine fonts;
    docx::LayoutEngine layout;
    layout.layout(doc, root, fonts);

    // Render
    docx::Renderer renderer;
    docx::Bitmap bmp = renderer.render(root, fonts);
    TEST_ASSERT_GT(bmp.width, 0, "Rendered bitmap width");
    TEST_ASSERT_GT(bmp.height, 0, "Rendered bitmap height");

    // Save
    TEST_ASSERT_TRUE(bmp.savePNG("pipeline_test_output.png"), "Save pipeline output");
}

// ── Main ───────────────────────────────────────────────────────
int main(int argc, char* argv[])
{
    std::cout << "========================================" << std::endl;
    std::cout << "  DOCX Core Pipeline — Test Suite" << std::endl;
    std::cout << "========================================" << std::endl;

    // Find test file
    std::string testFile = findTestFile();
    if (testFile.empty() && argc >= 2)
    {
        testFile = argv[1];
    }

    if (testFile.empty())
    {
        std::cerr << "WARNING: Test file not found. Running basic tests only." << std::endl;
        std::cerr << "  Pass the path to 'WPS Docs Quick Start Guide.docx' as argument."
                  << std::endl;
    }
    else
    {
        std::cout << "Test file: " << testFile << std::endl;
    }
    std::cout << std::endl;

    // Run tests
    test_document_model();
    std::cout << std::endl;

    test_font_engine();
    std::cout << std::endl;

    if (!testFile.empty())
    {
        test_docx_parsing(testFile);
        std::cout << std::endl;

        test_frame_tree(testFile);
        std::cout << std::endl;

        test_layout(testFile);
        std::cout << std::endl;

        test_renderer(testFile);
        std::cout << std::endl;

        test_full_pipeline(testFile);
        std::cout << std::endl;
    }

    // Summary
    std::cout << "========================================" << std::endl;
    std::cout << "  Results: " << g_passed << "/" << g_tests << " passed";
    if (g_failed > 0)
    {
        std::cout << ", " << g_failed << " FAILED";
    }
    std::cout << std::endl;
    std::cout << "========================================" << std::endl;

    return g_failed > 0 ? 1 : 0;
}
