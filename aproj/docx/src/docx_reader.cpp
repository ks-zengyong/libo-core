#include "docx_reader.h"
#include "xml_util.h"
#include "miniz.h"

#include <cstring>
#include <algorithm>
#include <sstream>
#include <iostream>

namespace docx
{
// ── ZIP Extraction ─────────────────────────────────────────────
bool DocxReader::extractZip(const std::string& filePath, std::map<std::string, ZipEntry>& entries)
{
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, filePath.c_str(), 0))
    {
        std::cerr << "Failed to open ZIP: " << filePath << std::endl;
        return false;
    }

    int numFiles = (int)mz_zip_reader_get_num_files(&zip);
    for (int i = 0; i < numFiles; i++)
    {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat))
            continue;
        if (stat.m_is_directory)
            continue;

        size_t size = 0;
        void* data = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
        if (!data)
            continue;

        ZipEntry entry;
        entry.name = stat.m_filename;
        entry.data.assign((uint8_t*)data, (uint8_t*)data + size);
        mz_free(data);

        entries[entry.name] = std::move(entry);
    }

    mz_zip_reader_end(&zip);
    return true;
}

// ── Relationships ──────────────────────────────────────────────
std::map<std::string, std::string> DocxReader::parseRelationships(const std::string& xml)
{
    std::map<std::string, std::string> result;
    pugi::xml_document doc;
    if (!doc.load_string(xml.c_str()))
        return result;

    auto root = doc.first_child();
    for (auto rel = root.first_child(); rel; rel = rel.next_sibling())
    {
        std::string id = xml::attr(rel, xml::ns::rel, "Id");
        std::string target = xml::attr(rel, xml::ns::rel, "Target");
        if (!id.empty() && !target.empty())
        {
            result[id] = target;
        }
    }
    return result;
}

// ── Style Parsing ──────────────────────────────────────────────
void DocxReader::parseStyles(const std::string& xml, Document& doc)
{
    pugi::xml_document xdoc;
    if (!xdoc.load_string(xml.c_str()))
        return;

    auto root = xdoc.first_child(); // <w:styles>

    // Parse docDefaults
    auto docDefaults = xml::child(root, xml::ns::w, "docDefaults");
    if (docDefaults)
    {
        auto rPrDefault = xml::child(docDefaults, xml::ns::w, "rPrDefault");
        if (rPrDefault)
        {
            auto rPr = xml::child(rPrDefault, xml::ns::w, "rPr");
            if (rPr)
            {
                StyleDef normalStyle;
                normalStyle.id = "Normal";
                normalStyle.name = "Normal";
                normalStyle.type = StyleType::Paragraph;
                normalStyle.isDefault = true;
                normalStyle.runProps = parseRunProps(rPr);
                doc.styles["Normal"] = normalStyle;
            }
        }
    }

    // Parse each <w:style>
    for (auto styleNode : xml::children(root, xml::ns::w, "style"))
    {
        StyleDef style;
        style.id = xml::attr(styleNode, xml::ns::w, "styleId");
        std::string type = xml::attr(styleNode, xml::ns::w, "type");

        if (type == "paragraph")
            style.type = StyleType::Paragraph;
        else if (type == "character")
            style.type = StyleType::Character;
        else if (type == "table")
            style.type = StyleType::Table;
        else if (type == "numbering")
            style.type = StyleType::Numbering;

        style.isDefault = xml::attr_bool(styleNode, xml::ns::w, "default");

        auto nameNode = xml::child(styleNode, xml::ns::w, "name");
        if (nameNode)
        {
            style.name = xml::attr(nameNode, xml::ns::w, "val");
        }

        auto basedOn = xml::child(styleNode, xml::ns::w, "basedOn");
        if (basedOn)
        {
            style.parentId = xml::attr(basedOn, xml::ns::w, "val");
        }

        auto pPr = xml::child(styleNode, xml::ns::w, "pPr");
        if (pPr)
        {
            style.paraProps = parseParagraphProps(pPr);
        }

        auto rPr = xml::child(styleNode, xml::ns::w, "rPr");
        if (rPr)
        {
            style.runProps = parseRunProps(rPr);
        }

        if (!style.id.empty())
        {
            doc.styles[style.id] = style;
        }
    }
}

// ── Font Table ─────────────────────────────────────────────────
void DocxReader::parseFontTable(const std::string& xml, Document& doc)
{
    pugi::xml_document xdoc;
    if (!xdoc.load_string(xml.c_str()))
        return;

    auto root = xdoc.first_child();
    for (auto font : xml::children(root, xml::ns::w, "font"))
    {
        std::string name = xml::attr(font, xml::ns::w, "name");
        if (!name.empty())
        {
            doc.fontTable[name] = name;
        }
    }
}

// ── Numbering Parsing ──────────────────────────────────────────
NumFormat parseNumFormat(const std::string& fmt)
{
    if (fmt == "decimal")
        return NumFormat::Decimal;
    if (fmt == "upperRoman" || fmt == "upperRoman2")
        return NumFormat::UpperRoman;
    if (fmt == "lowerRoman" || fmt == "lowerRoman2")
        return NumFormat::LowerRoman;
    if (fmt == "upperLetter" || fmt == "upperLetter2")
        return NumFormat::UpperLetter;
    if (fmt == "lowerLetter" || fmt == "lowerLetter2")
        return NumFormat::LowerLetter;
    if (fmt == "bullet")
        return NumFormat::Bullet;
    if (fmt == "none")
        return NumFormat::None;
    return NumFormat::Decimal; // default
}

void DocxReader::parseNumbering(const std::string& xml, Document& doc)
{
    pugi::xml_document xdoc;
    if (!xdoc.load_string(xml.c_str()))
        return;

    auto root = xdoc.first_child(); // <w:numbering>

    // Parse <w:abstractNum> entries
    for (auto absNode : xml::children(root, xml::ns::w, "abstractNum"))
    {
        AbstractNumDef absDef;
        absDef.id = xml::attr_int(absNode, xml::ns::w, "abstractNumId", -1);

        for (auto lvlNode : xml::children(absNode, xml::ns::w, "lvl"))
        {
            NumLevelDef level;
            level.ilvl = xml::attr_int(lvlNode, xml::ns::w, "ilvl", 0);

            auto numFmtNode = xml::child(lvlNode, xml::ns::w, "numFmt");
            if (numFmtNode)
            {
                level.numFmt = parseNumFormat(xml::attr(numFmtNode, xml::ns::w, "val"));
            }

            auto lvlTextNode = xml::child(lvlNode, xml::ns::w, "lvlText");
            if (lvlTextNode)
            {
                level.lvlText = xml::attr(lvlTextNode, xml::ns::w, "val");
            }

            auto startNode = xml::child(lvlNode, xml::ns::w, "start");
            if (startNode)
            {
                level.startVal = xml::attr_int(startNode, xml::ns::w, "val", 1);
            }

            auto numFontNode = xml::child(lvlNode, xml::ns::w, "rPr");
            if (numFontNode)
            {
                auto rFonts = xml::child(numFontNode, xml::ns::w, "rFonts");
                if (rFonts)
                {
                    level.numFont = xml::attr(rFonts, xml::ns::w, "ascii");
                }
                auto sz = xml::child(numFontNode, xml::ns::w, "sz");
                if (sz)
                {
                    level.numFontSize = xml::attr_int(sz, xml::ns::w, "val", 22);
                }
            }

            // Bullet character
            if (level.numFmt == NumFormat::Bullet)
            {
                auto lvlText = xml::child(lvlNode, xml::ns::w, "lvlText");
                if (lvlText)
                {
                    level.bulletChar = xml::attr(lvlText, xml::ns::w, "val");
                }
                // Also check for bullet font
                auto rPr = xml::child(lvlNode, xml::ns::w, "rPr");
                if (rPr)
                {
                    auto rFonts = xml::child(rPr, xml::ns::w, "rFonts");
                    if (rFonts)
                    {
                        level.numFont = xml::attr(rFonts, xml::ns::w, "ascii");
                    }
                }
            }

            // Indent
            auto pPr = xml::child(lvlNode, xml::ns::w, "pPr");
            if (pPr)
            {
                auto ind = xml::child(pPr, xml::ns::w, "ind");
                if (ind)
                {
                    level.indentLeft = xml::attr_int(ind, xml::ns::w, "left", 0);
                    level.hangingIndent = xml::attr_int(ind, xml::ns::w, "hanging", 0);
                }
            }

            // Level restart
            auto lvlRestart = xml::child(lvlNode, xml::ns::w, "lvlRestart");
            if (lvlRestart)
            {
                level.lvlRestart = xml::attr_int(lvlRestart, xml::ns::w, "val", -1);
            }

            // Ensure levels vector is large enough
            while ((int)absDef.levels.size() <= level.ilvl)
            {
                absDef.levels.push_back({});
            }
            absDef.levels[level.ilvl] = level;
        }

        // Ensure abstractNums vector is large enough
        while ((int)doc.abstractNums.size() <= absDef.id)
        {
            doc.abstractNums.push_back({});
        }
        doc.abstractNums[absDef.id] = absDef;
    }

    // Parse <w:num> entries
    for (auto numNode : xml::children(root, xml::ns::w, "num"))
    {
        NumDef numDef;
        auto absRef = xml::child(numNode, xml::ns::w, "abstractNumId");
        if (absRef)
        {
            numDef.abstractNumId = xml::attr_int(absRef, xml::ns::w, "val", -1);
        }
        doc.numDefs.push_back(numDef);
    }
}

// ── Section Properties ─────────────────────────────────────────
SectionProps DocxReader::parseSectionProps(pugi::xml_node sectPrNode)
{
    SectionProps sp;

    auto pgSz = xml::child(sectPrNode, xml::ns::w, "pgSz");
    if (pgSz)
    {
        sp.pageWidth = xml::attr_int(pgSz, xml::ns::w, "w", 11906);
        sp.pageHeight = xml::attr_int(pgSz, xml::ns::w, "h", 16838);
        std::string orient = xml::attr(pgSz, xml::ns::w, "orient");
        sp.landscape = (orient == "landscape");
    }

    auto pgMar = xml::child(sectPrNode, xml::ns::w, "pgMar");
    if (pgMar)
    {
        sp.marginTop = xml::attr_int(pgMar, xml::ns::w, "top", 1440);
        sp.marginBottom = xml::attr_int(pgMar, xml::ns::w, "bottom", 1440);
        sp.marginLeft = xml::attr_int(pgMar, xml::ns::w, "left", 1800);
        sp.marginRight = xml::attr_int(pgMar, xml::ns::w, "right", 1800);
        sp.headerMargin = xml::attr_int(pgMar, xml::ns::w, "header", 720);
        sp.footerMargin = xml::attr_int(pgMar, xml::ns::w, "footer", 720);
        sp.gutter = xml::attr_int(pgMar, xml::ns::w, "gutter", 0);
    }

    // Parse header references
    for (auto ref : xml::children(sectPrNode, xml::ns::w, "headerReference"))
    {
        HeaderFooterRef hfRef;
        std::string type = xml::attr(ref, xml::ns::w, "type");
        if (type == "default")
            hfRef.type = HeaderFooterType::Default;
        else if (type == "first")
            hfRef.type = HeaderFooterType::First;
        else if (type == "even")
            hfRef.type = HeaderFooterType::Even;
        hfRef.relId = xml::attr(ref, xml::ns::r, "id");
        sp.headerRefs.push_back(hfRef);
    }

    // Parse footer references
    for (auto ref : xml::children(sectPrNode, xml::ns::w, "footerReference"))
    {
        HeaderFooterRef hfRef;
        std::string type = xml::attr(ref, xml::ns::w, "type");
        if (type == "default")
            hfRef.type = HeaderFooterType::Default;
        else if (type == "first")
            hfRef.type = HeaderFooterType::First;
        else if (type == "even")
            hfRef.type = HeaderFooterType::Even;
        hfRef.relId = xml::attr(ref, xml::ns::r, "id");
        sp.footerRefs.push_back(hfRef);
    }

    return sp;
}

// ── Paragraph Properties ───────────────────────────────────────
ParagraphProps DocxReader::parseParagraphProps(pugi::xml_node pPrNode)
{
    ParagraphProps pp;

    auto pStyle = xml::child(pPrNode, xml::ns::w, "pStyle");
    if (pStyle)
    {
        pp.styleName = xml::attr(pStyle, xml::ns::w, "val");
    }

    auto jc = xml::child(pPrNode, xml::ns::w, "jc");
    if (jc)
    {
        std::string val = xml::attr(jc, xml::ns::w, "val");
        if (val == "center")
            pp.alignment = TextAlign::Center;
        else if (val == "right")
            pp.alignment = TextAlign::Right;
        else if (val == "both" || val == "distribute")
            pp.alignment = TextAlign::Justify;
        else
            pp.alignment = TextAlign::Left;
    }

    auto spacing = xml::child(pPrNode, xml::ns::w, "spacing");
    if (spacing)
    {
        pp.spaceBefore = xml::attr_int(spacing, xml::ns::w, "before", 0);
        pp.spaceAfter = xml::attr_int(spacing, xml::ns::w, "after", 0);
        pp.lineSpacing = xml::attr_int(spacing, xml::ns::w, "line", 240);
        std::string rule = xml::attr(spacing, xml::ns::w, "lineRule");
        pp.lineSpacingExact = (rule == "exact");
    }

    auto ind = xml::child(pPrNode, xml::ns::w, "ind");
    if (ind)
    {
        pp.indentLeft = xml::attr_int(ind, xml::ns::w, "left", 0);
        pp.indentRight = xml::attr_int(ind, xml::ns::w, "right", 0);
        pp.indentFirstLine = xml::attr_int(ind, xml::ns::w, "firstLine", 0);
        int chars = xml::attr_int(ind, xml::ns::w, "firstLineChars", 0);
        if (chars > 0 && pp.indentFirstLine == 0)
        {
            pp.indentFirstLine = chars * 10;
        }
    }

    if (xml::has_child(pPrNode, xml::ns::w, "pageBreakBefore"))
    {
        auto pbb = xml::child(pPrNode, xml::ns::w, "pageBreakBefore");
        pp.pageBreakBefore
            = xml::attr_bool(pbb, xml::ns::w, "val") || xml::attr(pbb, xml::ns::w, "val").empty();
    }

    if (xml::has_child(pPrNode, xml::ns::w, "keepNext"))
    {
        auto kn = xml::child(pPrNode, xml::ns::w, "keepNext");
        pp.keepNext
            = xml::attr_bool(kn, xml::ns::w, "val") || xml::attr(kn, xml::ns::w, "val").empty();
    }

    if (xml::has_child(pPrNode, xml::ns::w, "keepLines"))
    {
        auto kl = xml::child(pPrNode, xml::ns::w, "keepLines");
        pp.keepLines
            = xml::attr_bool(kl, xml::ns::w, "val") || xml::attr(kl, xml::ns::w, "val").empty();
    }

    // Numbering
    auto numPr = xml::child(pPrNode, xml::ns::w, "numPr");
    if (numPr)
    {
        auto numId = xml::child(numPr, xml::ns::w, "numId");
        if (numId)
        {
            pp.numId = xml::attr_int(numId, xml::ns::w, "val", -1);
        }
        auto ilvl = xml::child(numPr, xml::ns::w, "ilvl");
        if (ilvl)
        {
            pp.numLevel = xml::attr_int(ilvl, xml::ns::w, "val", 0);
        }
    }

    auto outlineLvl = xml::child(pPrNode, xml::ns::w, "outlineLvl");
    if (outlineLvl)
    {
        pp.outlineLevel = xml::attr_int(outlineLvl, xml::ns::w, "val", 0);
    }

    // Paragraph-level run properties (from <w:pPr><w:rPr>)
    auto pRpr = xml::child(pPrNode, xml::ns::w, "rPr");
    if (pRpr)
    {
        pp.paraRunProps = parseRunProps(pRpr);
    }

    return pp;
}

// ── Run Properties ─────────────────────────────────────────────
RunProps DocxReader::parseRunProps(pugi::xml_node rPrNode)
{
    RunProps rp;

    auto rFonts = xml::child(rPrNode, xml::ns::w, "rFonts");
    if (rFonts)
    {
        rp.fontName = xml::attr(rFonts, xml::ns::w, "ascii");
        if (rp.fontName.empty())
            rp.fontName = xml::attr(rFonts, xml::ns::w, "hAnsi");
        rp.eastAsiaFont = xml::attr(rFonts, xml::ns::w, "eastAsia");
    }

    auto sz = xml::child(rPrNode, xml::ns::w, "sz");
    if (sz)
    {
        rp.fontSize = xml::attr_int(sz, xml::ns::w, "val", 22);
    }

    auto szCs = xml::child(rPrNode, xml::ns::w, "szCs");
    if (szCs)
    {
        rp.fontSizeCs = xml::attr_int(szCs, xml::ns::w, "val", 22);
    }

    if (xml::has_child(rPrNode, xml::ns::w, "b"))
    {
        auto b = xml::child(rPrNode, xml::ns::w, "b");
        rp.bold
            = xml::attr(b, xml::ns::w, "val") != "false" && xml::attr(b, xml::ns::w, "val") != "0";
    }

    if (xml::has_child(rPrNode, xml::ns::w, "i"))
    {
        auto i = xml::child(rPrNode, xml::ns::w, "i");
        rp.italic
            = xml::attr(i, xml::ns::w, "val") != "false" && xml::attr(i, xml::ns::w, "val") != "0";
    }

    if (xml::has_child(rPrNode, xml::ns::w, "u"))
    {
        auto u = xml::child(rPrNode, xml::ns::w, "u");
        rp.underline = xml::attr(u, xml::ns::w, "val") != "none";
    }

    if (xml::has_child(rPrNode, xml::ns::w, "strike"))
    {
        auto s = xml::child(rPrNode, xml::ns::w, "strike");
        rp.strike = xml::attr_bool(s, xml::ns::w, "val");
    }

    auto color = xml::child(rPrNode, xml::ns::w, "color");
    if (color)
    {
        rp.color = Color::fromHex(xml::attr(color, xml::ns::w, "val"));
    }

    auto vertAlign = xml::child(rPrNode, xml::ns::w, "vertAlign");
    if (vertAlign)
    {
        std::string val = xml::attr(vertAlign, xml::ns::w, "val");
        if (val == "superscript")
            rp.superscript = 1;
        else if (val == "subscript")
            rp.superscript = -1;
    }

    return rp;
}

// ── Image Resolution ───────────────────────────────────────────
int DocxReader::resolveImage(const std::string& relId,
                             const std::map<std::string, ZipEntry>& entries,
                             const std::string& relsPath, Document& doc)
{
    static std::map<std::string, std::map<std::string, std::string>> relsCache;

    if (!relsCache.count(relsPath))
    {
        auto it = entries.find(relsPath);
        if (it != entries.end())
        {
            std::string xml(it->second.data.begin(), it->second.data.end());
            relsCache[relsPath] = parseRelationships(xml);
        }
    }

    auto& rels = relsCache[relsPath];
    auto targetIt = rels.find(relId);
    if (targetIt == rels.end())
        return -1;

    std::string target = targetIt->second;
    std::string dir = relsPath.substr(0, relsPath.find_last_of('/') + 1);
    std::string fullPath = dir + target;

    // Normalize path
    auto entryIt = entries.find(fullPath);
    if (entryIt == entries.end())
        return -1;

    // Check if already loaded
    for (size_t i = 0; i < doc.images.size(); i++)
    {
        if (doc.images[i].fileName == fullPath)
            return (int)i;
    }

    ImageData img;
    img.fileName = fullPath;
    img.data = entryIt->second.data;

    doc.images.push_back(std::move(img));
    return (int)doc.images.size() - 1;
}

// ── Drawing Parsing ────────────────────────────────────────────
void DocxReader::parseDrawing(pugi::xml_node drawingNode, TextRun& run,
                              const std::map<std::string, ZipEntry>& entries, Document& doc)
{
    auto inlineNode = xml::child(drawingNode, xml::ns::wp, "inline");
    auto anchorNode = xml::child(drawingNode, xml::ns::wp, "anchor");

    pugi::xml_node container = inlineNode ? inlineNode : anchorNode;
    if (!container)
        return;

    run.isAnchor = (anchorNode != pugi::xml_node{});

    auto extent = xml::child(container, xml::ns::wp, "extent");
    if (extent)
    {
        run.drawingWidthEMU = xml::attr_int(extent, xml::ns::wp, "cx", 0);
        run.drawingHeightEMU = xml::attr_int(extent, xml::ns::wp, "cy", 0);
    }

    auto graphic = xml::child(container, xml::ns::a, "graphic");
    if (!graphic)
        return;
    auto graphicData = xml::child(graphic, xml::ns::a, "graphicData");
    if (!graphicData)
        return;
    auto pic = xml::child(graphicData, xml::ns::pic, "pic");
    if (!pic)
        return;
    auto blipFill = xml::child(pic, xml::ns::pic, "blipFill");
    if (!blipFill)
        return;
    auto blip = xml::child(blipFill, xml::ns::a, "blip");
    if (!blip)
        return;

    std::string embed = xml::attr(blip, xml::ns::r, "embed");
    std::string link = xml::attr(blip, xml::ns::r, "link");
    std::string relId = !embed.empty() ? embed : link;
    if (relId.empty())
        return;

    run.drawingImageIndex = resolveImage(relId, entries, docDir_ + "_rels/document.xml.rels", doc);
}

// ── Run Parsing ────────────────────────────────────────────────
TextRun DocxReader::parseRun(pugi::xml_node rNode, const std::map<std::string, ZipEntry>& entries,
                             Document& doc)
{
    TextRun run;

    auto rPr = xml::child(rNode, xml::ns::w, "rPr");
    if (rPr)
    {
        run.props = parseRunProps(rPr);
    }

    // Check for drawing
    auto drawing = xml::child(rNode, xml::ns::w, "drawing");
    if (drawing)
    {
        parseDrawing(drawing, run, entries, doc);
        return run;
    }

    // Check for pict (VML)
    auto pict = xml::child(rNode, xml::ns::w, "pict");
    if (pict)
    {
        // Try shape with imagedata
        for (auto child = pict.first_child(); child; child = child.next_sibling())
        {
            auto imageData = xml::child(child, xml::ns::v, "imagedata");
            if (imageData)
            {
                std::string relId = xml::attr(imageData, xml::ns::r, "id");
                if (!relId.empty())
                {
                    run.drawingImageIndex
                        = resolveImage(relId, entries, docDir_ + "_rels/document.xml.rels", doc);
                    // Get size from shape style
                    std::string style = xml::attr(child, xml::ns::w, "style");
                    // Parse width/height from style string (simplified)
                    run.drawingWidthEMU = 914400; // default 1 inch
                    run.drawingHeightEMU = 914400;
                }
                break;
            }
        }
        return run;
    }

    // Collect text from child elements
    std::string text;
    for (auto child = rNode.first_child(); child; child = child.next_sibling())
    {
        const char* name = child.name();
        if (strcmp(name, "w:t") == 0 || strcmp(name, "t") == 0)
        {
            text += xml::text(child);
        }
        else if (strcmp(name, "w:tab") == 0 || strcmp(name, "tab") == 0)
        {
            text += "\t";
        }
        else if (strcmp(name, "w:br") == 0 || strcmp(name, "br") == 0)
        {
            std::string type = xml::attr(child, xml::ns::w, "type");
            if (type == "page")
                text += "\f";
            else if (type == "column")
                text += "\v";
            else
                text += "\n";
        }
        else if (strcmp(name, "w:cr") == 0 || strcmp(name, "cr") == 0)
        {
            text += "\n";
        }
        else if (strcmp(name, "w:sym") == 0 || strcmp(name, "sym") == 0)
        {
            text += "?";
        }
    }

    run.text = text;
    return run;
}

// ── Paragraph Parsing ──────────────────────────────────────────
Paragraph DocxReader::parseParagraph(pugi::xml_node pNode,
                                     const std::map<std::string, ZipEntry>& entries, Document& doc)
{
    Paragraph para;

    auto pPr = xml::child(pNode, xml::ns::w, "pPr");
    if (pPr)
    {
        para.props = parseParagraphProps(pPr);
    }

    for (auto child = pNode.first_child(); child; child = child.next_sibling())
    {
        const char* name = child.name();
        if (strcmp(name, "w:r") == 0 || strcmp(name, "r") == 0)
        {
            para.runs.push_back(parseRun(child, entries, doc));
        }
        else if (strcmp(name, "w:hyperlink") == 0 || strcmp(name, "hyperlink") == 0)
        {
            for (auto r : xml::children(child, xml::ns::w, "r"))
            {
                para.runs.push_back(parseRun(r, entries, doc));
            }
        }
        else if (strcmp(name, "w:pPr") == 0 || strcmp(name, "pPr") == 0)
        {
            // Already handled
        }
    }

    if (para.runs.empty())
    {
        TextRun emptyRun;
        emptyRun.text = "";
        para.runs.push_back(emptyRun);
    }

    return para;
}

// ── Table Parsing ──────────────────────────────────────────────
Table DocxReader::parseTable(pugi::xml_node tblNode, const std::map<std::string, ZipEntry>& entries,
                             Document& doc)
{
    Table table;

    auto tblPr = xml::child(tblNode, xml::ns::w, "tblPr");
    if (tblPr)
    {
        auto tblW = xml::child(tblPr, xml::ns::w, "tblW");
        if (tblW)
        {
            table.props.widthTwips = xml::attr_int(tblW, xml::ns::w, "w", 0);
            table.props.widthType = xml::attr(tblW, xml::ns::w, "type");
        }

        auto tblInd = xml::child(tblPr, xml::ns::w, "tblInd");
        if (tblInd)
        {
            table.props.indentTwips = xml::attr_int(tblInd, xml::ns::w, "w", 0);
        }

        auto jc = xml::child(tblPr, xml::ns::w, "jc");
        if (jc)
        {
            table.props.alignment = xml::attr(jc, xml::ns::w, "val");
        }

        auto tblBorders = xml::child(tblPr, xml::ns::w, "tblBorders");
        if (tblBorders)
        {
            auto parseBorder = [&](const char* name) -> int {
                auto b = xml::child(tblBorders, xml::ns::w, name);
                if (b)
                    return xml::attr_int(b, xml::ns::w, "sz", 0);
                return 0;
            };
            table.props.borderTop = parseBorder("top");
            table.props.borderBottom = parseBorder("bottom");
            table.props.borderLeft = parseBorder("left");
            table.props.borderRight = parseBorder("right");
            table.props.borderInsideH = parseBorder("insideH");
            table.props.borderInsideV = parseBorder("insideV");
        }
    }

    auto tblGrid = xml::child(tblNode, xml::ns::w, "tblGrid");
    if (tblGrid)
    {
        for (auto col : xml::children(tblGrid, xml::ns::w, "gridCol"))
        {
            table.gridColumns.push_back(xml::attr_int(col, xml::ns::w, "w", 0));
        }
    }

    for (auto trNode : xml::children(tblNode, xml::ns::w, "tr"))
    {
        TableRow row;

        auto trPr = xml::child(trNode, xml::ns::w, "trPr");
        if (trPr)
        {
            auto trHeight = xml::child(trPr, xml::ns::w, "trHeight");
            if (trHeight)
            {
                row.heightTwips = xml::attr_int(trHeight, xml::ns::w, "val", 0);
                std::string rule = xml::attr(trHeight, xml::ns::w, "hRule");
                row.heightExact = (rule == "exact");
            }
        }

        for (auto tcNode : xml::children(trNode, xml::ns::w, "tc"))
        {
            TableCell cell;

            auto tcPr = xml::child(tcNode, xml::ns::w, "tcPr");
            if (tcPr)
            {
                auto tcW = xml::child(tcPr, xml::ns::w, "tcW");
                if (tcW)
                {
                    cell.widthTwips = xml::attr_int(tcW, xml::ns::w, "w", 0);
                }
                cell.gridSpan = xml::attr_int(tcPr, xml::ns::w, "gridSpan", 1);

                auto vAlign = xml::child(tcPr, xml::ns::w, "vAlign");
                if (vAlign)
                {
                    cell.verticalAlign = xml::attr(vAlign, xml::ns::w, "val");
                }
            }

            for (auto child = tcNode.first_child(); child; child = child.next_sibling())
            {
                const char* name = child.name();
                if (strcmp(name, "w:p") == 0 || strcmp(name, "p") == 0)
                {
                    cell.paragraphs.push_back(parseParagraph(child, entries, doc));
                }
            }

            row.cells.push_back(std::move(cell));
        }

        table.rows.push_back(std::move(row));
    }

    return table;
}

// ── Header/Footer Parsing ──────────────────────────────────────
std::vector<Paragraph> DocxReader::parseHeaderFooter(const std::string& xml,
                                                     const std::map<std::string, ZipEntry>& entries,
                                                     Document& doc)
{
    std::vector<Paragraph> paragraphs;
    pugi::xml_document xdoc;
    if (!xdoc.load_string(xml.c_str()))
        return paragraphs;

    auto root = xdoc.first_child(); // <w:hdr> or <w:ftr>
    for (auto child = root.first_child(); child; child = child.next_sibling())
    {
        const char* name = child.name();
        if (strcmp(name, "w:p") == 0 || strcmp(name, "p") == 0)
        {
            paragraphs.push_back(parseParagraph(child, entries, doc));
        }
        else if (strcmp(name, "w:tbl") == 0 || strcmp(name, "tbl") == 0)
        {
            // Tables in headers are rare but supported
            // For now, just parse paragraphs within
        }
    }
    return paragraphs;
}

// ── Main Document Parsing ──────────────────────────────────────
void DocxReader::parseDocument(const std::string& xml,
                               const std::map<std::string, ZipEntry>& entries, Document& doc)
{
    pugi::xml_document xdoc;
    if (!xdoc.load_string(xml.c_str()))
    {
        std::cerr << "Failed to parse document.xml" << std::endl;
        return;
    }

    auto root = xdoc.first_child();
    auto body = xml::child(root, xml::ns::w, "body");
    if (!body)
    {
        std::cerr << "No <w:body> found" << std::endl;
        return;
    }

    int paraIndex = 0;
    for (auto child = body.first_child(); child; child = child.next_sibling())
    {
        const char* name = child.name();

        if (strcmp(name, "w:p") == 0 || strcmp(name, "p") == 0)
        {
            Paragraph para = parseParagraph(child, entries, doc);

            // Check for section break in paragraph properties
            auto pPr = xml::child(child, xml::ns::w, "pPr");
            if (pPr)
            {
                auto sectPr = xml::child(pPr, xml::ns::w, "sectPr");
                if (sectPr)
                {
                    SectionProps sp = parseSectionProps(sectPr);
                    doc.sectionBreakMap[paraIndex] = sp;
                }
            }

            doc.paragraphs.push_back(para);
            paraIndex++;
        }
        else if (strcmp(name, "w:tbl") == 0 || strcmp(name, "tbl") == 0)
        {
            doc.tables.push_back(parseTable(child, entries, doc));
        }
        else if (strcmp(name, "w:sdt") == 0 || strcmp(name, "sdt") == 0)
        {
            auto sdtContent = xml::child(child, xml::ns::w, "sdtContent");
            if (sdtContent)
            {
                for (auto sc = sdtContent.first_child(); sc; sc = sc.next_sibling())
                {
                    const char* sname = sc.name();
                    if (strcmp(sname, "w:p") == 0 || strcmp(sname, "p") == 0)
                    {
                        doc.paragraphs.push_back(parseParagraph(sc, entries, doc));
                        paraIndex++;
                    }
                    else if (strcmp(sname, "w:tbl") == 0 || strcmp(sname, "tbl") == 0)
                    {
                        doc.tables.push_back(parseTable(sc, entries, doc));
                    }
                }
            }
        }
        else if (strcmp(name, "w:sectPr") == 0 || strcmp(name, "sectPr") == 0)
        {
            doc.sectionProps = parseSectionProps(child);
        }
    }
}

// ── Main Entry Point ───────────────────────────────────────────
bool DocxReader::read(const std::string& filePath, Document& doc)
{
    std::map<std::string, ZipEntry> entries;
    if (!extractZip(filePath, entries))
    {
        return false;
    }

    docDir_ = "word/";

    // Parse styles.xml
    {
        auto it = entries.find("word/styles.xml");
        if (it != entries.end())
        {
            std::string xml(it->second.data.begin(), it->second.data.end());
            parseStyles(xml, doc);
        }
    }

    // Parse fontTable.xml
    {
        auto it = entries.find("word/fontTable.xml");
        if (it != entries.end())
        {
            std::string xml(it->second.data.begin(), it->second.data.end());
            parseFontTable(xml, doc);
        }
    }

    // Parse numbering.xml
    {
        auto it = entries.find("word/numbering.xml");
        if (it != entries.end())
        {
            std::string xml(it->second.data.begin(), it->second.data.end());
            parseNumbering(xml, doc);
        }
    }

    // Parse document.xml
    {
        auto it = entries.find("word/document.xml");
        if (it == entries.end())
        {
            std::cerr << "word/document.xml not found in ZIP" << std::endl;
            return false;
        }
        std::string xml(it->second.data.begin(), it->second.data.end());
        parseDocument(xml, entries, doc);
    }

    // Parse headers and footers
    {
        // Get relationships for document.xml
        auto relsIt = entries.find("word/_rels/document.xml.rels");
        if (relsIt != entries.end())
        {
            std::string relsXml(relsIt->second.data.begin(), relsIt->second.data.end());
            auto docRels = parseRelationships(relsXml);

            // Resolve header/footer references in section props
            auto resolveRef = [&](HeaderFooterRef& ref) {
                auto it = docRels.find(ref.relId);
                if (it != docRels.end())
                {
                    ref.target = "word/" + it->second;
                }
            };

            for (auto& ref : doc.sectionProps.headerRefs)
                resolveRef(ref);
            for (auto& ref : doc.sectionProps.footerRefs)
                resolveRef(ref);

            // Parse header files
            for (auto& ref : doc.sectionProps.headerRefs)
            {
                if (ref.target.empty())
                    continue;
                auto it = entries.find(ref.target);
                if (it != entries.end())
                {
                    std::string xml(it->second.data.begin(), it->second.data.end());
                    doc.headers[ref.target] = parseHeaderFooter(xml, entries, doc);
                }
            }

            // Parse footer files
            for (auto& ref : doc.sectionProps.footerRefs)
            {
                if (ref.target.empty())
                    continue;
                auto it = entries.find(ref.target);
                if (it != entries.end())
                {
                    std::string xml(it->second.data.begin(), it->second.data.end());
                    doc.footers[ref.target] = parseHeaderFooter(xml, entries, doc);
                }
            }
        }
    }

    // Load all media files
    for (auto & [ name, entry ] : entries)
    {
        if (name.find("word/media/") == 0)
        {
            bool found = false;
            for (auto& img : doc.images)
            {
                if (img.fileName == name)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                ImageData img;
                img.fileName = name;
                img.data = entry.data;
                doc.images.push_back(std::move(img));
            }
        }
    }

    return true;
}

} // namespace docx
