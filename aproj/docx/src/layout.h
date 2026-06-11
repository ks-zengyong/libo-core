#pragma once
// Layout engine — text layout and pagination.
// Mirrors LibreOffice's SwLayAction + SwContentFrame::MakeAll().
//
// Responsibilities:
// 1. Measure text runs and break into lines (word-wrap)
// 2. Position lines within text frames
// 3. Split frames across pages (pagination)
// 4. Layout tables (cell content, row heights)
// 5. Position inline images

#include "document.h"
#include "frame.h"
#include "font_engine.h"

namespace docx
{
class LayoutEngine
{
public:
    // Layout all content in the document into the frame tree.
    // Populates BodyFrame with TextFrames, TableFrames, ImageFrames.
    void layout(const Document& doc, RootFrame& root, FontEngine& fonts);

private:
    const Document* doc_ = nullptr;
    RootFrame* root_ = nullptr;
    FontEngine* fonts_ = nullptr;
    float dpi_ = 96.0f;

    // Current page and position
    int currentPage_ = 0;
    float currentY_ = 0; // Y position within current page body (pixels)

    // Numbering counters: [numId][ilvl] → current count
    std::map<int, std::vector<int>> numCounters_;

    // Layout a single paragraph into a TextFrame
    TextFrame layoutParagraph(const Paragraph& para, int paraIndex, float bodyWidth,
                              const std::string& numPrefix = "");

    // Break a paragraph's text into lines
    std::vector<Line> breakIntoLines(const Paragraph& para, float bodyWidth,
                                     const ParagraphProps& resolvedProps);

    // Layout a table
    TableFrame layoutTable(const Table& tbl, int tblIndex, float bodyWidth);

    // Layout header/footer content
    void layoutHeaderFooter(PageFrame& page, const SectionProps& section,
                            const std::vector<Paragraph>& paragraphs, bool isHeader);

    // Layout cell content (recursive)
    void layoutCell(TableCellFrame& cellFrame, const TableCell& cell, float cellWidth);

    // Check if content fits on current page, add new page if needed
    bool ensureSpace(float neededHeight);

    // Add a new page
    void addNewPage();

    // Get current page
    PageFrame& currentPage();

    // Resolve style properties
    ParagraphProps resolveParaProps(const Paragraph& para);
    RunProps resolveRunProps(const Paragraph& para, const TextRun& run);

    // Convert half-points to pixels
    float halfPtToPx(int halfPt);

    // Get line height for a paragraph (max of all runs)
    float getParaLineHeight(const Paragraph& para);

    // Get font for a run
    std::string getRunFont(const RunProps& rp);
};

} // namespace docx
