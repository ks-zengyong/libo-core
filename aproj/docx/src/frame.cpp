#include "frame.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace docx
{
float RootFrame::totalHeight() const
{
    float h = 0;
    for (auto& page : pages)
    {
        h += page.height + 20; // 20px gap between pages
    }
    return h;
}

float FrameBuilder::twipsToPixels(int twips, float dpi)
{
    // 1 inch = 1440 twips, 1 inch = dpi pixels
    return (float)twips * dpi / 1440.0f;
}

PageFrame FrameBuilder::createPage(const SectionProps& section, int pageIndex, float dpi)
{
    PageFrame page;
    page.pageIndex = pageIndex;
    page.width = twipsToPixels(section.pageWidth, dpi);
    page.height = twipsToPixels(section.pageHeight, dpi);
    page.marginTop = twipsToPixels(section.marginTop, dpi);
    page.marginBottom = twipsToPixels(section.marginBottom, dpi);
    page.marginLeft = twipsToPixels(section.marginLeft, dpi);
    page.marginRight = twipsToPixels(section.marginRight, dpi);

    // Body frame fills the content area
    page.body.x = page.marginLeft;
    page.body.y = page.marginTop;
    page.body.width = page.width - page.marginLeft - page.marginRight;
    page.body.height = page.height - page.marginTop - page.marginBottom;

    return page;
}

void FrameBuilder::build(const Document& doc, RootFrame& root, float dpi)
{
    root.dpi = dpi;
    root.pages.clear();

    if (doc.paragraphs.empty() && doc.tables.empty())
    {
        // Create one empty page
        root.pages.push_back(createPage(doc.sectionProps, 0, dpi));
        return;
    }

    // Create first page
    root.pages.push_back(createPage(doc.sectionProps, 0, dpi));

    // The actual content (text frames, table frames) is populated during layout.
    // Here we just create the page structure based on section breaks.

    // For documents with section breaks, create additional pages as needed.
    // The layout engine will handle splitting content across pages.
    // For now, we start with one page and let the layout engine add more.
}

// ── XML Dump Implementation ────────────────────────────────────

// Escape XML special characters
static std::string xmlEscape(const std::string& s)
{
    std::string result;
    result.reserve(s.size());
    for (char c : s)
    {
        switch (c)
        {
            case '&':
                result += "&amp;";
                break;
            case '<':
                result += "&lt;";
                break;
            case '>':
                result += "&gt;";
                break;
            case '"':
                result += "&quot;";
                break;
            case '\'':
                result += "&apos;";
                break;
            default:
                if (c >= 0 && c < 0x20 && c != '\n' && c != '\r' && c != '\t')
                {
                    result += '*'; // Replace control chars like LO does
                }
                else
                {
                    result += c;
                }
                break;
        }
    }
    return result;
}

// Dump a single line
static void dumpLine(const Line& line, std::ostream& out, int level)
{
    out << indent(level) << "<SwLineLayout"
        << " width=\"" << line.totalWidth << "\""
        << " height=\"" << line.height << "\""
        << " ascent=\"" << line.ascent << "\""
        << " descent=\"" << line.descent << "\""
        << " type=\"PortionType::Para\""
        << ">\n";

    for (auto& lr : line.runs)
    {
        out << indent(level + 1) << "<SwLinePortion"
            << " width=\"" << lr.width << "\""
            << " height=\"" << line.height << "\""
            << " length=\"" << lr.text.size() << "\""
            << " type=\"PortionType::Text\""
            << " portion=\"" << xmlEscape(lr.text) << "\""
            << " font=\"" << lr.fontName << "\""
            << " fontSize=\"" << lr.fontSize << "\"";
        if (lr.bold)
            out << " bold=\"true\"";
        if (lr.color.valid)
        {
            char colorBuf[8];
            snprintf(colorBuf, sizeof(colorBuf), "#%02x%02x%02x", lr.color.r, lr.color.g,
                     lr.color.b);
            out << " color=\"" << colorBuf << "\"";
        }
        if (lr.sourceRun && lr.sourceRun->drawingImageIndex >= 0)
        {
            out << " drawing=\"true\""
                << " imgIndex=\"" << lr.sourceRun->drawingImageIndex << "\"";
        }
        out << "/>\n";
    }

    out << indent(level) << "</SwLineLayout>\n";
}

// Dump a text frame
static void dumpTextFrame(const TextFrame& tf, std::ostream& out, int level, int id)
{
    out << indent(level) << "<txt"
        << " id=\"" << id << "\""
        << " paraIndex=\"" << tf.paragraphIndex << "\""
        << " x=\"" << tf.x << "\""
        << " y=\"" << tf.y << "\""
        << " width=\"" << tf.width << "\""
        << " height=\"" << tf.height << "\""
        << " isFollow=\"" << (tf.isFollow ? "true" : "false") << "\""
        << ">\n";

    // Bounds (like LO)
    out << indent(level + 1) << "<bounds"
        << " left=\"" << tf.x << "\""
        << " top=\"" << tf.y << "\""
        << " width=\"" << tf.width << "\""
        << " height=\"" << tf.height << "\""
        << "/>\n";

    // Print bounds
    float prtLeft = 0, prtTop = 0;
    if (!tf.lines.empty())
    {
        prtLeft = tf.lines[0].runs.empty() ? 0 : tf.lines[0].runs[0].x;
    }
    out << indent(level + 1) << "<prtBounds"
        << " left=\"" << prtLeft << "\""
        << " top=\"" << prtTop << "\""
        << " width=\"" << tf.width << "\""
        << " height=\"" << tf.height << "\""
        << "/>\n";

    // Lines
    for (auto& line : tf.lines)
    {
        dumpLine(line, out, level + 1);
    }

    out << indent(level) << "</txt>\n";
}

// Dump a table frame
static void dumpTableFrame(const TableFrame& tf, std::ostream& out, int level, int& nextId)
{
    out << indent(level) << "<table"
        << " x=\"" << tf.x << "\""
        << " y=\"" << tf.y << "\""
        << " width=\"" << tf.width << "\""
        << " height=\"" << tf.height << "\""
        << ">\n";

    for (auto& row : tf.rows)
    {
        out << indent(level + 1) << "<row"
            << " y=\"" << row.y << "\""
            << " height=\"" << row.height << "\""
            << ">\n";

        for (auto& cell : row.cells)
        {
            out << indent(level + 2) << "<cell"
                << " x=\"" << cell.x << "\""
                << " y=\"" << cell.y << "\""
                << " width=\"" << cell.width << "\""
                << " height=\"" << cell.height << "\""
                << ">\n";

            for (auto& ctf : cell.textFrames)
            {
                dumpTextFrame(ctf, out, level + 3, nextId++);
            }

            out << indent(level + 2) << "</cell>\n";
        }

        out << indent(level + 1) << "</row>\n";
    }

    out << indent(level) << "</table>\n";
}

// Dump header/footer frame
static void dumpHeaderFooter(const HeaderFooterFrame& hf, const std::string& tag, std::ostream& out,
                             int level, int& nextId)
{
    if (hf.textFrames.empty())
        return;
    out << indent(level) << "<" << tag << " x=\"" << hf.x << "\""
        << " y=\"" << hf.y << "\""
        << " width=\"" << hf.width << "\""
        << " height=\"" << hf.height << "\""
        << ">\n";
    for (auto& tf : hf.textFrames)
    {
        dumpTextFrame(tf, out, level + 1, nextId++);
    }
    out << indent(level) << "</" << tag << ">\n";
}

void dumpLayoutXml(const RootFrame& root, std::ostream& out)
{
    out << "<?xml version=\"1.0\"?>\n";
    out << "<root dpi=\"" << root.dpi << "\">\n";

    int nextId = 1;
    for (auto& page : root.pages)
    {
        out << " <page"
            << " index=\"" << page.pageIndex << "\""
            << " width=\"" << page.width << "\""
            << " height=\"" << page.height << "\""
            << ">\n";

        // Page bounds
        out << "  <bounds"
            << " left=\"" << page.marginLeft << "\""
            << " top=\"" << page.marginTop << "\""
            << " width=\"" << page.width << "\""
            << " height=\"" << page.height << "\""
            << "/>\n";

        // Header
        dumpHeaderFooter(page.header, "header", out, 2, nextId);

        // Body
        out << "  <body"
            << " x=\"" << page.body.x << "\""
            << " y=\"" << page.body.y << "\""
            << " width=\"" << page.body.width << "\""
            << " height=\"" << page.body.height << "\""
            << ">\n";

        for (auto& item : page.body.items)
        {
            switch (item.type)
            {
                case BodyFrame::ContentType::Text:
                    if (item.index < page.body.textFrames.size())
                    {
                        dumpTextFrame(page.body.textFrames[item.index], out, 3, nextId++);
                    }
                    break;
                case BodyFrame::ContentType::Table:
                    if (item.index < page.body.tableFrames.size())
                    {
                        dumpTableFrame(page.body.tableFrames[item.index], out, 3, nextId);
                    }
                    break;
                case BodyFrame::ContentType::Image:
                    if (item.index < page.body.imageFrames.size())
                    {
                        auto& imgf = page.body.imageFrames[item.index];
                        out << "   <image"
                            << " x=\"" << imgf.x << "\""
                            << " y=\"" << imgf.y << "\""
                            << " width=\"" << imgf.width << "\""
                            << " height=\"" << imgf.height << "\""
                            << " imgIndex=\"" << imgf.imageIndex << "\""
                            << "/>\n";
                    }
                    break;
            }
        }

        out << "  </body>\n";

        // Footer
        dumpHeaderFooter(page.footer, "footer", out, 2, nextId);

        out << " </page>\n";
    }

    out << "</root>\n";
}

void dumpDocumentXml(const Document& doc, std::ostream& out)
{
    out << "<?xml version=\"1.0\"?>\n";
    out << "<Document>\n";

    // Styles
    out << " <styles count=\"" << doc.styles.size() << "\">\n";
    for (auto & [ id, style ] : doc.styles)
    {
        out << "  <style id=\"" << xmlEscape(id) << "\""
            << " name=\"" << xmlEscape(style.name) << "\""
            << " type=\"" << static_cast<int>(style.type) << "\"";
        if (!style.parentId.empty())
            out << " parent=\"" << xmlEscape(style.parentId) << "\"";
        if (style.isDefault)
            out << " default=\"true\"";
        out << "/>\n";
    }
    out << " </styles>\n";

    // Numbering
    if (!doc.abstractNums.empty())
    {
        out << " <numbering>\n";
        for (auto& absNum : doc.abstractNums)
        {
            out << "  <abstractNum id=\"" << absNum.id << "\""
                << " levels=\"" << absNum.levels.size() << "\">\n";
            for (auto& lvl : absNum.levels)
            {
                out << "   <lvl ilvl=\"" << lvl.ilvl << "\""
                    << " numFmt=\"" << static_cast<int>(lvl.numFmt) << "\""
                    << " lvlText=\"" << xmlEscape(lvl.lvlText) << "\""
                    << " startVal=\"" << lvl.startVal << "\""
                    << "/>\n";
            }
            out << "  </abstractNum>\n";
        }
        out << " </numbering>\n";
    }

    // Section props
    out << " <section pageWidth=\"" << doc.sectionProps.pageWidth << "\""
        << " pageHeight=\"" << doc.sectionProps.pageHeight << "\""
        << " marginTop=\"" << doc.sectionProps.marginTop << "\""
        << " marginBottom=\"" << doc.sectionProps.marginBottom << "\""
        << " marginLeft=\"" << doc.sectionProps.marginLeft << "\""
        << " marginRight=\"" << doc.sectionProps.marginRight << "\""
        << " headers=\"" << doc.sectionProps.headerRefs.size() << "\""
        << " footers=\"" << doc.sectionProps.footerRefs.size() << "\""
        << "/>\n";

    // Section breaks
    if (!doc.sectionBreakMap.empty())
    {
        out << " <sectionBreaks count=\"" << doc.sectionBreakMap.size() << "\">\n";
        for (auto & [ paraIdx, sp ] : doc.sectionBreakMap)
        {
            out << "  <break paraIndex=\"" << paraIdx << "\""
                << " pageWidth=\"" << sp.pageWidth << "\""
                << " pageHeight=\"" << sp.pageHeight << "\""
                << " marginTop=\"" << sp.marginTop << "\""
                << " marginBottom=\"" << sp.marginBottom << "\""
                << " marginLeft=\"" << sp.marginLeft << "\""
                << " marginRight=\"" << sp.marginRight << "\""
                << "/>\n";
        }
        out << " </sectionBreaks>\n";
    }

    // Paragraphs
    out << " <paragraphs count=\"" << doc.paragraphs.size() << "\">\n";
    for (size_t i = 0; i < doc.paragraphs.size(); i++)
    {
        auto& para = doc.paragraphs[i];
        out << "  <paragraph index=\"" << i << "\"";
        if (!para.props.styleName.empty())
            out << " style=\"" << xmlEscape(para.props.styleName) << "\"";
        if (para.props.numId >= 0)
            out << " numId=\"" << para.props.numId << "\" ilvl=\"" << para.props.numLevel << "\"";
        if (para.props.alignment != TextAlign::Left)
            out << " align=\"" << static_cast<int>(para.props.alignment) << "\"";
        out << ">\n";

        for (auto& run : para.runs)
        {
            out << "   <run";
            if (!run.props.fontName.empty())
                out << " font=\"" << xmlEscape(run.props.fontName) << "\"";
            out << " size=\"" << run.props.fontSize << "\"";
            if (run.props.bold)
                out << " bold=\"true\"";
            if (run.props.italic)
                out << " italic=\"true\"";
            if (run.isDrawing())
            {
                out << " drawing=\"true\" imgIndex=\"" << run.drawingImageIndex << "\"";
            }
            else
            {
                out << " text=\"" << xmlEscape(run.text) << "\"";
            }
            out << "/>\n";
        }

        out << "  </paragraph>\n";
    }
    out << " </paragraphs>\n";

    // Tables
    if (!doc.tables.empty())
    {
        out << " <tables count=\"" << doc.tables.size() << "\">\n";
        for (size_t ti = 0; ti < doc.tables.size(); ti++)
        {
            auto& tbl = doc.tables[ti];
            out << "  <table index=\"" << ti << "\""
                << " rows=\"" << tbl.rows.size() << "\""
                << " cols=\"" << tbl.gridColumns.size() << "\""
                << ">\n";
            for (size_t ri = 0; ri < tbl.rows.size(); ri++)
            {
                auto& row = tbl.rows[ri];
                out << "   <row index=\"" << ri << "\""
                    << " cells=\"" << row.cells.size() << "\""
                    << " height=\"" << row.heightTwips << "\""
                    << ">\n";
                for (size_t ci = 0; ci < row.cells.size(); ci++)
                {
                    auto& cell = row.cells[ci];
                    out << "    <cell index=\"" << ci << "\""
                        << " width=\"" << cell.widthTwips << "\""
                        << " paragraphs=\"" << cell.paragraphs.size() << "\""
                        << "/>\n";
                }
                out << "   </row>\n";
            }
            out << "  </table>\n";
        }
        out << " </tables>\n";
    }

    // Images
    if (!doc.images.empty())
    {
        out << " <images count=\"" << doc.images.size() << "\">\n";
        for (size_t i = 0; i < doc.images.size(); i++)
        {
            out << "  <image index=\"" << i << "\""
                << " file=\"" << xmlEscape(doc.images[i].fileName) << "\""
                << " size=\"" << doc.images[i].data.size() << "\""
                << "/>\n";
        }
        out << " </images>\n";
    }

    // Headers/Footers
    if (!doc.headers.empty() || !doc.footers.empty())
    {
        out << " <headers count=\"" << doc.headers.size() << "\"/>\n";
        out << " <footers count=\"" << doc.footers.size() << "\"/>\n";
    }

    out << "</Document>\n";
}

} // namespace docx
