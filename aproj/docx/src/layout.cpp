#include "layout.h"
#include <algorithm>
#include <sstream>
#include <cstring>
#include <cmath>
#include <functional>

namespace docx
{
// ── Helpers ────────────────────────────────────────────────────
float LayoutEngine::halfPtToPx(int halfPt)
{
    // halfPt is in half-points (22 = 11pt)
    // At 96 DPI: px = pt * 96/72 = pt * 4/3
    return (halfPt / 2.0f) * dpi_ / 72.0f;
}

std::string LayoutEngine::getRunFont(const RunProps& rp)
{
    if (!rp.fontName.empty())
        return rp.fontName;
    return "Calibri";
}

ParagraphProps LayoutEngine::resolveParaProps(const Paragraph& para)
{
    return doc_->resolveParaProps(para);
}

RunProps LayoutEngine::resolveRunProps(const Paragraph& para, const TextRun& run)
{
    return doc_->resolveRunProps(para, run);
}

float LayoutEngine::getParaLineHeight(const Paragraph& para)
{
    // LO uses the paragraph's font metrics for line height, not the run's.
    // For empty paragraphs, LO uses the paragraph style's rPr font.
    // For non-empty paragraphs, LO uses the max of all run fonts.

    // First, try the paragraph's own rPr font (from <w:pPr><w:rPr>)
    auto resolvedParaProps = doc_->resolveParaProps(para);
    if (!resolvedParaProps.paraRunProps.fontName.empty()
        || resolvedParaProps.paraRunProps.fontSize > 0)
    {
        std::string font = resolvedParaProps.paraRunProps.fontName;
        int fontSize = resolvedParaProps.paraRunProps.fontSize;
        if (font.empty())
            font = "Calibri";
        if (fontSize <= 0)
            fontSize = 22;
        float h = fonts_->getLineHeight(font, fontSize);
        if (h > 0)
            return h;
    }

    // Fallback: use max of all run fonts
    float maxH = 0;
    for (auto& run : para.runs)
    {
        auto rp = resolveRunProps(para, run);
        std::string font = getRunFont(rp);
        float h = fonts_->getLineHeight(font, rp.fontSize);
        if (h > maxH)
            maxH = h;
    }
    if (maxH <= 0)
        maxH = halfPtToPx(22) * 1.2f; // fallback 11pt
    return maxH;
}

// ── Page Management ────────────────────────────────────────────
PageFrame& LayoutEngine::currentPage() { return root_->pages[currentPage_]; }

void LayoutEngine::addNewPage()
{
    PageFrame newPage;
    newPage.pageIndex = (int)root_->pages.size();
    // Use same dimensions as first page (simplified)
    if (!root_->pages.empty())
    {
        auto& first = root_->pages[0];
        newPage.width = first.width;
        newPage.height = first.height;
        newPage.marginTop = first.marginTop;
        newPage.marginBottom = first.marginBottom;
        newPage.marginLeft = first.marginLeft;
        newPage.marginRight = first.marginRight;
        newPage.body.x = first.body.x;
        newPage.body.y = first.body.y;
        newPage.body.width = first.body.width;
        newPage.body.height = first.body.height;
    }
    root_->pages.push_back(std::move(newPage));
    currentPage_ = (int)root_->pages.size() - 1;
    currentY_ = 0;
}

bool LayoutEngine::ensureSpace(float neededHeight)
{
    float available = currentPage().body.height - currentY_;
    if (neededHeight <= available)
        return true;

    // Need a new page
    addNewPage();
    return true;
}

// ── Text Layout ────────────────────────────────────────────────
std::vector<Line> LayoutEngine::breakIntoLines(const Paragraph& para, float bodyWidth,
                                               const ParagraphProps& resolvedProps)
{
    std::vector<Line> lines;
    Line currentLine;
    float remainingWidth = bodyWidth;
    float lineHeight = 0;
    float maxAscent = 0;
    float maxDescent = 0;

    // First line indent
    float firstIndent = 0;
    if (resolvedProps.indentFirstLine > 0)
    {
        firstIndent = (float)resolvedProps.indentFirstLine * dpi_ / 1440.0f;
    }

    bool isFirstLine = true;

    for (size_t ri = 0; ri < para.runs.size(); ri++)
    {
        auto& run = para.runs[ri];
        if (run.isDrawing())
        {
            // Image run - add as a special line run
            LineRun lr;
            lr.text = "";
            lr.sourceRun = &run;
            lr.fontSize = 0;

            float imgW = run.drawingWidthEMU / 635.0f * dpi_ / 1440.0f;
            float imgH = run.drawingHeightEMU / 635.0f * dpi_ / 1440.0f;

            if (imgW > remainingWidth && !currentLine.runs.empty())
            {
                // Start new line
                currentLine.height = lineHeight > 0 ? lineHeight : 10;
                currentLine.ascent = maxAscent;
                currentLine.descent = maxDescent;
                currentLine.totalWidth = bodyWidth - remainingWidth;
                lines.push_back(currentLine);
                currentLine = Line();
                remainingWidth = bodyWidth;
                lineHeight = 0;
                maxAscent = 0;
                maxDescent = 0;
                isFirstLine = false;
            }

            lr.x = bodyWidth - remainingWidth;
            lr.width = imgW;
            currentLine.runs.push_back(lr);
            remainingWidth -= imgW;
            if (imgH > lineHeight)
                lineHeight = imgH;
            continue;
        }

        auto rp = resolveRunProps(para, run);
        std::string font = getRunFont(rp);
        float runLineHeight = fonts_->getLineHeight(font, rp.fontSize);
        float runAscent = fonts_->getAscent(font, rp.fontSize);
        float runDescent = runLineHeight - runAscent;

        if (runLineHeight > lineHeight)
            lineHeight = runLineHeight;
        if (runAscent > maxAscent)
            maxAscent = runAscent;
        if (runDescent > maxDescent)
            maxDescent = runDescent;

        // Split text into words
        const char* p = run.text.c_str();
        std::string word;
        auto flushWord = [&]() {
            if (word.empty())
                return;

            float wordWidth = fonts_->getStringWidth(word, font, rp.fontSize);
            float indent = isFirstLine ? firstIndent : 0;
            float avail = remainingWidth - indent;

            if (wordWidth > avail && !currentLine.runs.empty())
            {
                // Line break before this word
                currentLine.height = lineHeight;
                currentLine.ascent = maxAscent;
                currentLine.descent = maxDescent;
                currentLine.totalWidth = bodyWidth - remainingWidth;
                lines.push_back(currentLine);
                currentLine = Line();
                remainingWidth = bodyWidth;
                indent = 0;
                isFirstLine = false;
            }

            LineRun lr;
            lr.text = word;
            lr.sourceRun = &run;
            lr.fontSize = rp.fontSize;
            lr.fontName = font;
            lr.color = rp.color.valid ? rp.color : Color::black();
            lr.bold = rp.bold;
            lr.italic = rp.italic;
            lr.x = bodyWidth - remainingWidth + indent;
            lr.width = wordWidth;
            currentLine.runs.push_back(lr);
            remainingWidth -= (wordWidth + indent);
            word.clear();
        };

        while (*p)
        {
            unsigned char c = (unsigned char)*p;
            if (c == '\f' || c == '\v')
            {
                // Page/column break
                flushWord();
                if (!currentLine.runs.empty())
                {
                    currentLine.height = lineHeight;
                    currentLine.ascent = maxAscent;
                    currentLine.descent = maxDescent;
                    currentLine.totalWidth = bodyWidth - remainingWidth;
                    lines.push_back(currentLine);
                    currentLine = Line();
                    remainingWidth = bodyWidth;
                    lineHeight = 0;
                    maxAscent = 0;
                    maxDescent = 0;
                    isFirstLine = false;
                }
                // Mark as page break in the line
                Line breakLine;
                breakLine.height = 0;
                breakLine.alignment = resolvedProps.alignment;
                lines.push_back(breakLine);
                p++;
            }
            else if (c == '\n' || c == '\r')
            {
                flushWord();
                if (!currentLine.runs.empty())
                {
                    currentLine.height = lineHeight;
                    currentLine.ascent = maxAscent;
                    currentLine.descent = maxDescent;
                    currentLine.totalWidth = bodyWidth - remainingWidth;
                    currentLine.isLastInPara = true;
                    lines.push_back(currentLine);
                    currentLine = Line();
                    remainingWidth = bodyWidth;
                    lineHeight = 0;
                    maxAscent = 0;
                    maxDescent = 0;
                    isFirstLine = false;
                }
                p++;
            }
            else if (c == '\t')
            {
                flushWord();
                // Tab: advance to next tab stop (every 0.5 inch = 720 twips = ~48px at 96dpi)
                float tabWidth = 720.0f * dpi_ / 1440.0f;
                float nextTab = (floorf((bodyWidth - remainingWidth) / tabWidth) + 1) * tabWidth;
                float advance = nextTab - (bodyWidth - remainingWidth);
                if (advance > 0 && advance < remainingWidth)
                {
                    LineRun lr;
                    lr.text = "\t";
                    lr.sourceRun = &run;
                    lr.x = bodyWidth - remainingWidth;
                    lr.width = advance;
                    currentLine.runs.push_back(lr);
                    remainingWidth -= advance;
                }
                p++;
            }
            else if (c == ' ')
            {
                flushWord();
                // Space: add width
                float spaceWidth = fonts_->getStringWidth(" ", font, rp.fontSize);
                if (spaceWidth > remainingWidth && !currentLine.runs.empty())
                {
                    currentLine.height = lineHeight;
                    currentLine.ascent = maxAscent;
                    currentLine.descent = maxDescent;
                    currentLine.totalWidth = bodyWidth - remainingWidth;
                    lines.push_back(currentLine);
                    currentLine = Line();
                    remainingWidth = bodyWidth;
                    isFirstLine = false;
                }
                LineRun lr;
                lr.text = " ";
                lr.sourceRun = &run;
                lr.x = bodyWidth - remainingWidth;
                lr.width = spaceWidth;
                currentLine.runs.push_back(lr);
                remainingWidth -= spaceWidth;
                p++;
            }
            else
            {
                // UTF-8 multi-byte character
                if (c < 0x80)
                {
                    word += *p++;
                }
                else if (c < 0xE0)
                {
                    word += *p++;
                    if (*p)
                        word += *p++;
                }
                else if (c < 0xF0)
                {
                    word += *p++;
                    if (*p)
                        word += *p++;
                    if (*p)
                        word += *p++;
                }
                else
                {
                    word += *p++;
                    if (*p)
                        word += *p++;
                    if (*p)
                        word += *p++;
                    if (*p)
                        word += *p++;
                }
            }
        }
        flushWord();
    }

    // Flush last line
    if (!currentLine.runs.empty())
    {
        currentLine.height = lineHeight > 0 ? lineHeight : halfPtToPx(22) * 1.2f;
        currentLine.ascent = maxAscent;
        currentLine.descent = maxDescent;
        currentLine.totalWidth = bodyWidth - remainingWidth;
        currentLine.isLastInPara = true;
        lines.push_back(currentLine);
    }

    // Ensure at least one line with minimum height
    float minHeight = halfPtToPx(22) * 1.2f;
    if (lines.empty())
    {
        Line emptyLine;
        emptyLine.height = minHeight;
        emptyLine.ascent = emptyLine.height * 0.8f;
        emptyLine.descent = emptyLine.height * 0.2f;
        emptyLine.isLastInPara = true;
        lines.push_back(emptyLine);
    }

    // Ensure all lines have minimum height
    for (auto& line : lines)
    {
        if (line.height <= 0)
        {
            line.height = minHeight;
            line.ascent = minHeight * 0.8f;
            line.descent = minHeight * 0.2f;
        }
    }

    // Set alignment
    for (auto& line : lines)
    {
        line.alignment = resolvedProps.alignment;
    }

    return lines;
}

TextFrame LayoutEngine::layoutParagraph(const Paragraph& para, int paraIndex, float bodyWidth,
                                        const std::string& numPrefix)
{
    TextFrame frame;
    frame.paragraph = &para;
    frame.paragraphIndex = paraIndex;

    auto resolvedProps = resolveParaProps(para);

    // If we have a numbering prefix, prepend it to the first run
    Paragraph modifiedPara = para;
    if (!numPrefix.empty() && !modifiedPara.runs.empty())
    {
        // Find the number font from the numbering definition
        std::string numFont;
        if (resolvedProps.numId >= 0 && doc_)
        {
            const NumLevelDef* level
                = doc_->resolveNumbering(resolvedProps.numId, resolvedProps.numLevel);
            if (level && !level->numFont.empty())
            {
                numFont = level->numFont;
            }
        }

        // Prepend number text to the first run
        TextRun numRun;
        numRun.text = numPrefix;
        numRun.props = modifiedPara.runs[0].props; // inherit first run's props
        if (!numFont.empty())
        {
            numRun.props.fontName = numFont;
        }
        modifiedPara.runs.insert(modifiedPara.runs.begin(), numRun);
    }

    // Space before/after in pixels
    frame.spaceBeforePx = (float)resolvedProps.spaceBefore * dpi_ / 1440.0f;
    frame.spaceAfterPx = (float)resolvedProps.spaceAfter * dpi_ / 1440.0f;

    // Indent
    float leftIndent = (float)resolvedProps.indentLeft * dpi_ / 1440.0f;
    float rightIndent = (float)resolvedProps.indentRight * dpi_ / 1440.0f;
    float effectiveWidth = bodyWidth - leftIndent - rightIndent;
    if (effectiveWidth < 50)
        effectiveWidth = 50; // minimum width

    frame.lines = breakIntoLines(modifiedPara, effectiveWidth, resolvedProps);
    frame.x = leftIndent;

    // ── Line spacing (ported from LO: SwTextFormatter::CalcRealHeight) ──
    // OOXML w:line values: 240 = single (100%), 360 = 150%, 480 = double (200%)
    // LO converts during import: propLineSpace = round(w:line * 100 / 240)
    // We keep raw value and convert here.

    int propLineSpace = resolvedProps.lineSpacing; // raw OOXML value (240=single)
    bool isExact = resolvedProps.lineSpacingExact;

    // Convert to LO's percentage: 240 -> 100, 360 -> 150, 480 -> 200
    int propPercent = (int)round(propLineSpace * 100.0 / 240.0);

    // Get text height for proportional spacing calculation
    // (LO uses m_pCurr->GetTextHeight() which is the font's natural height)
    float textHeightPx = 0;
    if (!modifiedPara.runs.empty())
    {
        auto rp = resolveRunProps(modifiedPara, modifiedPara.runs[0]);
        std::string font = getRunFont(rp);
        textHeightPx = fonts_->getLineHeight(font, rp.fontSize);
    }
    if (textHeightPx <= 0)
        textHeightPx = halfPtToPx(22) * 1.2f; // fallback

    for (size_t i = 0; i < frame.lines.size(); i++)
    {
        float nLineHeight = frame.lines[i].height;
        float nAscent = frame.lines[i].ascent;

        if (isExact)
        {
            // SvxLineSpaceRule::Fix: exact line height
            nLineHeight = (float)resolvedProps.lineSpacing * dpi_ / 1440.0f;
            nAscent = nLineHeight * 0.8f; // LO uses 80% for ascent
        }
        else
        {
            // SvxLineSpaceRule::Auto (default)
            // No additional LineSpaceRule applied unless Min/Fix

            // InterLineSpaceRule::Prop (proportional spacing)
            // LO formula: nTmp = propPercent; nTmp -= 100; nTmp *= textHeight; nTmp /= 100; nTmp += nLineHeight;
            if (propPercent != 100 && i > 0)
            {
                // Only apply to non-first lines (LO: !IsParaLine())
                float nTmp = (float)(propPercent - 100);
                nTmp *= textHeightPx;
                nTmp /= 100.0f;
                nTmp += nLineHeight;
                if (nTmp < 1)
                    nTmp = 1;
                nLineHeight = nTmp;
            }
        }

        frame.lines[i].height = nLineHeight;
        frame.lines[i].ascent = nAscent;
    }

    // Calculate total height and set Y positions
    float totalH = 0;
    for (size_t i = 0; i < frame.lines.size(); i++)
    {
        frame.lines[i].y = totalH;
        totalH += frame.lines[i].height;
    }

    frame.height = totalH;

    // Apply horizontal alignment to lines
    for (auto& line : frame.lines)
    {
        if (line.alignment == TextAlign::Center)
        {
            float offset = (effectiveWidth - line.totalWidth) / 2.0f;
            for (auto& lr : line.runs)
            {
                lr.x += offset;
            }
        }
        else if (line.alignment == TextAlign::Right)
        {
            float offset = effectiveWidth - line.totalWidth;
            for (auto& lr : line.runs)
            {
                lr.x += offset;
            }
        }
        else if (line.alignment == TextAlign::Justify && !line.isLastInPara)
        {
            // Justify: distribute extra space across words
            float extra = effectiveWidth - line.totalWidth;
            int spaceCount = 0;
            for (auto& lr : line.runs)
            {
                if (lr.text == " " || lr.text == "\t")
                    spaceCount++;
            }
            if (spaceCount > 0 && extra > 0)
            {
                float extraPerSpace = extra / spaceCount;
                float accumulated = 0;
                for (size_t i = 0; i < line.runs.size(); i++)
                {
                    line.runs[i].x += accumulated;
                    if (line.runs[i].text == " " || line.runs[i].text == "\t")
                    {
                        accumulated += extraPerSpace;
                    }
                }
            }
        }
    }

    return frame;
}

// ── Table Layout ───────────────────────────────────────────────
void LayoutEngine::layoutCell(TableCellFrame& cellFrame, const TableCell& cell, float cellWidth)
{
    float y = 0;
    for (auto& para : cell.paragraphs)
    {
        TextFrame tf = layoutParagraph(para, -1, cellWidth);
        tf.x = 2; // small padding
        tf.y = y;
        y += tf.height + tf.spaceAfterPx;
        cellFrame.textFrames.push_back(std::move(tf));
    }
    cellFrame.height = y;
}

TableFrame LayoutEngine::layoutTable(const Table& tbl, int tblIndex, float bodyWidth)
{
    TableFrame frame;
    frame.sourceTable = &tbl;
    frame.tableIndex = tblIndex;
    frame.width = bodyWidth;

    // Calculate column widths
    int numCols = 0;
    for (auto& row : tbl.rows)
    {
        int cols = 0;
        for (auto& cell : row.cells)
            cols += cell.gridSpan;
        if (cols > numCols)
            numCols = cols;
    }
    if (numCols == 0)
        numCols = 1;

    // Use grid columns if available, otherwise equal width
    std::vector<float> colWidths(numCols, bodyWidth / numCols);
    if (!tbl.gridColumns.empty())
    {
        float totalGrid = 0;
        for (auto gc : tbl.gridColumns)
            totalGrid += gc;
        if (totalGrid > 0)
        {
            for (size_t i = 0; i < tbl.gridColumns.size() && i < colWidths.size(); i++)
            {
                colWidths[i] = (float)tbl.gridColumns[i] / totalGrid * bodyWidth;
            }
        }
    }

    float tableY = 0;
    for (size_t ri = 0; ri < tbl.rows.size(); ri++)
    {
        auto& row = tbl.rows[ri];
        TableRowFrame rowFrame;
        rowFrame.y = tableY;

        float x = 0;
        float maxRowHeight = 0;

        for (size_t ci = 0; ci < row.cells.size(); ci++)
        {
            auto& cell = row.cells[ci];
            TableCellFrame cellFrame;
            cellFrame.x = x;

            float cellW = 0;
            for (int gs = 0; gs < cell.gridSpan && (ci * 1 + gs) < numCols; gs++)
            {
                cellW += colWidths[ci + gs];
            }
            cellFrame.width = cellW;

            layoutCell(cellFrame, cell, cellW - 4); // -4 for padding

            if (cellFrame.height > maxRowHeight)
                maxRowHeight = cellFrame.height;
            x += cellW;
            rowFrame.cells.push_back(std::move(cellFrame));
        }

        // Apply row height
        float rowH = maxRowHeight;
        if (row.heightTwips > 0)
        {
            float specified = (float)row.heightTwips * dpi_ / 1440.0f;
            if (row.heightExact)
                rowH = specified;
            else if (specified > rowH)
                rowH = specified;
        }
        rowFrame.height = rowH;

        // Expand cells to row height
        for (auto& cell : rowFrame.cells)
        {
            cell.height = rowH;
        }

        tableY += rowH;
        frame.rows.push_back(std::move(rowFrame));
    }

    frame.height = tableY;
    return frame;
}

// ── Header/Footer Layout ───────────────────────────────────────
void LayoutEngine::layoutHeaderFooter(PageFrame& page, const SectionProps& section,
                                      const std::vector<Paragraph>& paragraphs, bool isHeader)
{
    if (paragraphs.empty())
        return;

    HeaderFooterFrame& hfFrame = isHeader ? page.header : page.footer;
    float bodyWidth = page.body.width;
    float y = 0;

    for (auto& para : paragraphs)
    {
        TextFrame tf = layoutParagraph(para, -1, bodyWidth);
        tf.x = page.body.x;
        tf.y = y;
        y += tf.height + tf.spaceAfterPx;
        hfFrame.textFrames.push_back(std::move(tf));
    }

    hfFrame.x = page.body.x;
    hfFrame.width = bodyWidth;
    hfFrame.height = y;

    if (isHeader)
    {
        page.headerHeight = y;
        // Adjust body to start below header
        page.body.y = page.marginTop + y;
        page.body.height = page.height - page.marginTop - page.marginBottom - y - page.footerHeight;
    }
    else
    {
        page.footerHeight = y;
        // Adjust body to end before footer
        page.body.height = page.height - page.marginTop - page.marginBottom - page.headerHeight - y;
    }
}

// ── Main Layout ────────────────────────────────────────────────
void LayoutEngine::layout(const Document& doc, RootFrame& root, FontEngine& fonts)
{
    doc_ = &doc;
    root_ = &root;
    fonts_ = &fonts;
    dpi_ = root.dpi;

    // The first page uses the section properties from the first section break.
    // In OOXML, <w:sectPr> at paragraph N defines properties for paragraphs 0..N.
    // So the first section break's properties apply to the first page.
    const SectionProps* firstSection = &doc.sectionProps;
    if (!doc.sectionBreakMap.empty())
    {
        firstSection = &doc.sectionBreakMap.begin()->second;
    }

    if (root.pages.empty())
    {
        root.pages.push_back(FrameBuilder::createPage(*firstSection, 0, dpi_));
    }
    else
    {
        // Update the first page with the correct section properties
        root.pages[0] = FrameBuilder::createPage(*firstSection, 0, dpi_);
    }

    currentPage_ = 0;
    currentY_ = 0;

    // Layout headers and footers on the first page
    auto& firstPage = currentPage();
    for (auto& ref : doc.sectionProps.headerRefs)
    {
        if (ref.type == HeaderFooterType::Default && !ref.target.empty())
        {
            auto it = doc.headers.find(ref.target);
            if (it != doc.headers.end())
            {
                layoutHeaderFooter(firstPage, doc.sectionProps, it->second, true);
            }
        }
    }
    for (auto& ref : doc.sectionProps.footerRefs)
    {
        if (ref.type == HeaderFooterType::Default && !ref.target.empty())
        {
            auto it = doc.footers.find(ref.target);
            if (it != doc.footers.end())
            {
                layoutHeaderFooter(firstPage, doc.sectionProps, it->second, false);
            }
        }
    }

    float bodyWidth = currentPage().body.width;

    // Pre-load fonts
    for (auto & [ id, style ] : doc.styles)
    {
        if (!style.runProps.fontName.empty())
        {
            fonts.loadFont(style.runProps.fontName);
        }
    }
    fonts.loadFont("Calibri");
    fonts.loadFont("Arial");
    fonts.loadFont("Times New Roman");

    // Layout paragraphs
    int tableIdx = 0;
    int paraIdx = 0;

    // Reset numbering counters
    numCounters_.clear();

    // Track previous paragraph for keep-with-next
    int prevParaIndex = -1;
    bool prevKeepNext = false;

    // Use std::function to allow recursive lambda calls
    std::function<void(TextFrame&)> addTextFrame;
    addTextFrame = [&](TextFrame& tf) {
        float totalH = tf.spaceBeforePx + tf.height + tf.spaceAfterPx;
        float available = currentPage().body.height - currentY_;

        // Keep-with-next: if previous paragraph has keepNext, try to fit both on same page
        if (prevKeepNext && prevParaIndex >= 0)
        {
            // Check if we can fit both on the current page
            if (totalH > available && currentY_ > 0)
            {
                // Not enough space - move to next page
                // But first check if the previous paragraph is already on this page
                // If so, we need to move it too (simplified: just add new page)
                addNewPage();
                available = currentPage().body.height - currentY_;
            }
        }

        // Widow-orphan control: ensure at least 2 lines on each page
        if (tf.lines.size() >= 3 && totalH > available)
        {
            // Check if only 1 line would fit (orphan)
            float usedH = tf.spaceBeforePx;
            int linesThatFit = 0;
            for (size_t i = 0; i < tf.lines.size(); i++)
            {
                if (usedH + tf.lines[i].height + tf.spaceAfterPx > available)
                    break;
                usedH += tf.lines[i].height;
                linesThatFit++;
            }

            // If only 1 line fits, move to next page (orphan prevention)
            if (linesThatFit <= 1 && currentY_ > 0)
            {
                addNewPage();
                available = currentPage().body.height - currentY_;
            }
        }

        // Check if we need to split the text frame
        if (totalH > available && tf.lines.size() > 1)
        {
            // Split the frame: find how many lines fit on current page
            float usedH = tf.spaceBeforePx;
            int splitLine = 0;
            for (size_t i = 0; i < tf.lines.size(); i++)
            {
                float lineH = tf.lines[i].height;
                if (usedH + lineH + tf.spaceAfterPx > available)
                {
                    break;
                }
                usedH += lineH;
                splitLine = (int)i + 1;
            }

            if (splitLine > 0 && splitLine < (int)tf.lines.size())
            {
                // Create master frame with lines that fit
                TextFrame master;
                master.paragraph = tf.paragraph;
                master.paragraphIndex = tf.paragraphIndex;
                master.spaceBeforePx = tf.spaceBeforePx;
                master.spaceAfterPx = 0; // no space after for master
                master.x = tf.x;
                master.width = tf.width;
                master.isFollow = false;
                master.startLine = 0;
                master.endLine = splitLine;
                master.lines.assign(tf.lines.begin(), tf.lines.begin() + splitLine);

                // Calculate master height
                float masterH = 0;
                for (auto& line : master.lines)
                    masterH += line.height;
                master.height = masterH;

                // Create follow frame with remaining lines
                TextFrame follow;
                follow.paragraph = tf.paragraph;
                follow.paragraphIndex = tf.paragraphIndex;
                follow.spaceBeforePx = 0; // no space before for follow
                follow.spaceAfterPx = tf.spaceAfterPx;
                follow.x = tf.x;
                follow.width = tf.width;
                follow.isFollow = true;
                follow.startLine = splitLine;
                follow.endLine = (int)tf.lines.size();
                follow.lines.assign(tf.lines.begin() + splitLine, tf.lines.end());

                // Calculate follow height
                float followH = 0;
                for (auto& line : follow.lines)
                    followH += line.height;
                follow.height = followH;

                // Add master to current page
                master.y = currentY_ + master.spaceBeforePx;
                master.x += currentPage().body.x;

                auto& body = currentPage().body;
                BodyFrame::ContentItem item;
                item.type = BodyFrame::ContentType::Text;
                item.index = body.textFrames.size();
                body.items.push_back(item);
                body.textFrames.push_back(std::move(master));

                currentY_ += masterH + master.spaceBeforePx;

                // Add new page and recursively add follow frame
                addNewPage();
                addTextFrame(follow);
                return;
            }
        }

        // Normal case: fits on current page
        ensureSpace(totalH);

        tf.y = currentY_ + tf.spaceBeforePx;
        tf.x += currentPage().body.x;
        tf.width = bodyWidth;

        auto& body = currentPage().body;
        BodyFrame::ContentItem item;
        item.type = BodyFrame::ContentType::Text;
        item.index = body.textFrames.size();
        body.items.push_back(item);
        body.textFrames.push_back(std::move(tf));

        currentY_ += totalH;
    };

    // Build section boundaries: find which section each paragraph belongs to.
    // In OOXML, <w:sectPr> at paragraph N defines properties for paragraphs BEFORE it.
    // So section 1 = paragraphs 0..break1, section 2 = break1+1..break2, etc.
    const SectionProps* currentSection = nullptr;
    int nextSectionBreak = -1;
    auto sectionIt = doc.sectionBreakMap.begin();

    // Find the first section break
    if (sectionIt != doc.sectionBreakMap.end())
    {
        nextSectionBreak = sectionIt->first;
        currentSection = &sectionIt->second;
    }

    for (size_t pi = 0; pi < doc.paragraphs.size(); pi++)
    {
        auto& para = doc.paragraphs[pi];
        auto resolvedProps = resolveParaProps(para);

        // Check if we've crossed into a new section
        if (nextSectionBreak >= 0 && (int)pi > nextSectionBreak)
        {
            // Move to next section
            ++sectionIt;
            if (sectionIt != doc.sectionBreakMap.end())
            {
                nextSectionBreak = sectionIt->first;
                currentSection = &sectionIt->second;
            }
            else
            {
                // Past all section breaks, use body-level
                nextSectionBreak = -1;
                currentSection = &doc.sectionProps;
            }
        }

        // Check for section break transition (create new page at section boundary)
        if (nextSectionBreak >= 0 && (int)pi == nextSectionBreak + 1)
        {
            // Create new page with the NEW section's properties
            addNewPage();
            auto& newPage = currentPage();
            const SectionProps& newSect
                = (sectionIt != doc.sectionBreakMap.end()) ? sectionIt->second : doc.sectionProps;
            newPage = FrameBuilder::createPage(newSect, (int)root_->pages.size() - 1, dpi_);

            // Layout headers/footers for new section
            for (auto& ref : newSect.headerRefs)
            {
                if (ref.type == HeaderFooterType::Default && !ref.target.empty())
                {
                    auto it = doc.headers.find(ref.target);
                    if (it != doc.headers.end())
                    {
                        layoutHeaderFooter(newPage, newSect, it->second, true);
                    }
                }
            }
            for (auto& ref : newSect.footerRefs)
            {
                if (ref.type == HeaderFooterType::Default && !ref.target.empty())
                {
                    auto it = doc.footers.find(ref.target);
                    if (it != doc.footers.end())
                    {
                        layoutHeaderFooter(newPage, newSect, it->second, false);
                    }
                }
            }
        }

        // Page break before
        if (resolvedProps.pageBreakBefore && currentY_ > 0)
        {
            addNewPage();
        }

        // Handle numbering: increment counter and format number text
        std::string numPrefix;
        if (resolvedProps.numId >= 0)
        {
            int numId = resolvedProps.numId;
            int ilvl = resolvedProps.numLevel;

            // Ensure counter vector exists
            if (numCounters_.find(numId) == numCounters_.end())
            {
                numCounters_[numId] = std::vector<int>(10, 0);
            }
            auto& counters = numCounters_[numId];

            // Increment counter for this level
            counters[ilvl]++;

            // Reset deeper levels
            for (int i = ilvl + 1; i < 10; i++)
            {
                counters[i] = 0;
            }

            // Resolve the numbering definition
            const NumLevelDef* level = doc.resolveNumbering(numId, ilvl);
            if (level)
            {
                numPrefix = Document::formatNumText(*level, counters);
                if (!numPrefix.empty())
                {
                    numPrefix += " "; // space after number
                }
            }
        }

        // Check for page break characters in text
        std::string fullText = para.fullText();
        bool hasPageBreak = false;
        for (char c : fullText)
        {
            if (c == '\f')
            {
                hasPageBreak = true;
                break;
            }
        }

        TextFrame tf = layoutParagraph(para, (int)pi, bodyWidth, numPrefix);

        // Handle page breaks within the paragraph
        // If the paragraph contains page breaks, split it
        if (hasPageBreak)
        {
            // Find page break lines and split
            std::vector<TextFrame> splitFrames;
            TextFrame current;
            current.paragraph = tf.paragraph;
            current.paragraphIndex = tf.paragraphIndex;
            current.spaceBeforePx = tf.spaceBeforePx;
            current.spaceAfterPx = tf.spaceAfterPx;
            current.x = tf.x;

            for (auto& line : tf.lines)
            {
                bool isBreak = (line.height == 0 && line.runs.empty());
                if (isBreak)
                {
                    if (!current.lines.empty())
                    {
                        float h = 0;
                        for (auto& l : current.lines)
                            h += l.height;
                        current.height = h;
                        splitFrames.push_back(std::move(current));
                        current = TextFrame();
                        current.paragraph = tf.paragraph;
                        current.paragraphIndex = tf.paragraphIndex;
                        current.x = tf.x;
                    }
                    // Add new page
                    addNewPage();
                }
                else
                {
                    current.lines.push_back(line);
                }
            }
            if (!current.lines.empty())
            {
                float h = 0;
                for (auto& l : current.lines)
                    h += l.height;
                current.height = h;
                splitFrames.push_back(std::move(current));
            }

            for (auto& sf : splitFrames)
            {
                addTextFrame(sf);
            }
        }
        else
        {
            addTextFrame(tf);
        }

        // Track for keep-with-next
        prevParaIndex = (int)pi;
        prevKeepNext = resolvedProps.keepNext;

        paraIdx++;
    }

    // Layout tables
    for (size_t ti = 0; ti < doc.tables.size(); ti++)
    {
        auto& tbl = doc.tables[ti];
        TableFrame tf = layoutTable(tbl, (int)ti, bodyWidth);

        float totalH = tf.height;
        ensureSpace(totalH);

        tf.x = currentPage().body.x;
        tf.y = currentY_;

        auto& body = currentPage().body;
        BodyFrame::ContentItem item;
        item.type = BodyFrame::ContentType::Table;
        item.index = body.tableFrames.size();
        body.items.push_back(item);
        body.tableFrames.push_back(std::move(tf));

        currentY_ += totalH;
    }
}

} // namespace docx
