#pragma once
// Frame tree — mirrors LibreOffice's SwFrame hierarchy.
// Represents the layout structure: pages → bodies → text frames / table frames.
//
// SwRootFrame → SwPageFrame → SwBodyFrame → SwTextFrame / SwTabFrame

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <iostream>
#include "document.h"

namespace docx
{
// ── Line Run ───────────────────────────────────────────────────
// A segment of text within a line, with position info.
struct LineRun
{
    std::string text;
    const TextRun* sourceRun = nullptr;
    float x = 0; // horizontal position within the line (pixels)
    float width = 0; // advance width in pixels
    int fontSize = 22; // half-points
    std::string fontName;
    Color color;
    bool bold = false;
    bool italic = false;
};

// ── Line ───────────────────────────────────────────────────────
// A single line of text within a TextFrame.
struct Line
{
    std::vector<LineRun> runs;
    float y = 0; // vertical position within the frame (pixels)
    float height = 0; // line height in pixels
    float ascent = 0; // max ascent in pixels
    float descent = 0; // max descent in pixels
    float totalWidth = 0; // total width of all runs
    TextAlign alignment = TextAlign::Left;
    bool isLastInPara = false; // last line of paragraph (don't justify)
};

// ── Text Frame ─────────────────────────────────────────────────
// Represents the layout of a single paragraph (or part of it if split).
struct TextFrame
{
    const Paragraph* paragraph = nullptr;
    int paragraphIndex = -1; // index in Document.paragraphs
    std::vector<Line> lines;
    float x = 0, y = 0; // position on page (pixels)
    float width = 0, height = 0;
    bool isFollow = false; // true if this is a continuation frame
    int startLine = 0; // first line index in this frame
    int endLine = 0; // last line index (exclusive)

    // Space before/after in pixels (resolved from paragraph props)
    float spaceBeforePx = 0;
    float spaceAfterPx = 0;

    // Link to follow frame (for master frames)
    TextFrame* followFrame = nullptr;
};

// ── Image Frame ────────────────────────────────────────────────
struct ImageFrame
{
    const ImageData* image = nullptr;
    int imageIndex = -1;
    float x = 0, y = 0;
    float width = 0, height = 0; // in pixels
    bool isAnchor = false;
};

// ── Table Frame ────────────────────────────────────────────────
struct TableCellFrame
{
    float x = 0, y = 0;
    float width = 0, height = 0;
    std::vector<TextFrame> textFrames;
    std::string verticalAlign;
};

struct TableRowFrame
{
    float y = 0;
    float height = 0;
    std::vector<TableCellFrame> cells;
};

struct TableFrame
{
    const Table* sourceTable = nullptr;
    float x = 0, y = 0;
    float width = 0, height = 0;
    std::vector<TableRowFrame> rows;
    int tableIndex = -1;
};

// ── Body Frame ─────────────────────────────────────────────────
struct BodyFrame
{
    float x = 0, y = 0;
    float width = 0, height = 0;
    // Content in order: text frames, table frames, image frames
    // Using variant-like approach with tagged union
    enum class ContentType
    {
        Text,
        Table,
        Image
    };
    struct ContentItem
    {
        ContentType type;
        size_t index; // index into the corresponding vector
    };
    std::vector<ContentItem> items;
    std::vector<TextFrame> textFrames;
    std::vector<TableFrame> tableFrames;
    std::vector<ImageFrame> imageFrames;
};

// ── Header/Footer Frame ────────────────────────────────────────
struct HeaderFooterFrame
{
    std::vector<TextFrame> textFrames;
    float x = 0, y = 0;
    float width = 0, height = 0;
};

// ── Page Frame ─────────────────────────────────────────────────
struct PageFrame
{
    int pageIndex = 0;
    float width = 0, height = 0; // in pixels
    float marginTop = 0, marginBottom = 0, marginLeft = 0, marginRight = 0;
    float headerHeight = 0; // height of header content
    float footerHeight = 0; // height of footer content
    HeaderFooterFrame header;
    HeaderFooterFrame footer;
    BodyFrame body;
};

// ── Root Frame ─────────────────────────────────────────────────
struct RootFrame
{
    std::vector<PageFrame> pages;
    float dpi = 96.0f;

    // Total rendered height in pixels
    float totalHeight() const;
};

// ── Frame Builder ──────────────────────────────────────────────
// Builds the frame tree from the Document model.
class FrameBuilder
{
public:
    // Build frame tree from document. Creates initial page and body frames.
    static void build(const Document& doc, RootFrame& root, float dpi = 96.0f);

    // Create a page frame from section properties.
    static PageFrame createPage(const SectionProps& section, int pageIndex, float dpi);
    static float twipsToPixels(int twips, float dpi);
};

// ── Dump Utilities ─────────────────────────────────────────────
// Output layout structure as XML, similar to LibreOffice's dumpAsXml.

// Dump the entire frame tree to an output stream
void dumpLayoutXml(const RootFrame& root, std::ostream& out);

// Dump document model (paragraphs, tables, styles) to an output stream
void dumpDocumentXml(const docx::Document& doc, std::ostream& out);

// Helper: indent string
inline std::string indent(int level) { return std::string(level * 2, ' '); }

} // namespace docx
