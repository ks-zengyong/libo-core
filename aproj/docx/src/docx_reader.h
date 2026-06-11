#pragma once
// DOCX Reader — parses .docx files into Document model.
// Mirrors LibreOffice's SwDOCXReader → WriterFilter → DomainMapper pipeline.
//
// The reader:
// 1. Extracts the ZIP package (via miniz)
// 2. Parses styles.xml → Document.styles
// 3. Parses document.xml → Document.paragraphs, tables, images

#include <string>
#include <map>
#include <vector>
#include <cstdint>
#include "document.h"
#include "pugixml.hpp"

namespace docx
{
class DocxReader
{
public:
    // Read a .docx file and populate the Document model.
    // Returns true on success.
    bool read(const std::string& filePath, Document& doc);

private:
    // ZIP entry data
    struct ZipEntry
    {
        std::string name;
        std::vector<uint8_t> data;
    };

    // Extract all entries from the ZIP file
    bool extractZip(const std::string& filePath, std::map<std::string, ZipEntry>& entries);

    // Parse [Content_Types].xml to find relationship targets
    void parseContentTypes(const std::map<std::string, ZipEntry>& entries);

    // Parse styles.xml
    void parseStyles(const std::string& xml, Document& doc);

    // Parse fontTable.xml
    void parseFontTable(const std::string& xml, Document& doc);

    // Parse numbering.xml
    void parseNumbering(const std::string& xml, Document& doc);

    // Parse a header or footer XML file
    std::vector<Paragraph> parseHeaderFooter(const std::string& xml,
                                             const std::map<std::string, ZipEntry>& entries,
                                             Document& doc);

    // Parse document.xml — the main content
    void parseDocument(const std::string& xml, const std::map<std::string, ZipEntry>& entries,
                       Document& doc);

    // Parse a <w:p> element into a Paragraph
    Paragraph parseParagraph(pugi::xml_node pNode, const std::map<std::string, ZipEntry>& entries,
                             Document& doc);

    // Parse <w:pPr> into ParagraphProps
    ParagraphProps parseParagraphProps(pugi::xml_node pPrNode);

    // Parse <w:r> into a TextRun
    TextRun parseRun(pugi::xml_node rNode, const std::map<std::string, ZipEntry>& entries,
                     Document& doc);

    // Parse <w:rPr> into RunProps
    RunProps parseRunProps(pugi::xml_node rPrNode);

    // Parse <w:tbl> into a Table
    Table parseTable(pugi::xml_node tblNode, const std::map<std::string, ZipEntry>& entries,
                     Document& doc);

    // Parse <w:drawing> / <w:pict> to extract image info
    void parseDrawing(pugi::xml_node drawingNode, TextRun& run,
                      const std::map<std::string, ZipEntry>& entries, Document& doc);

    // Parse <w:sectPr> into SectionProps
    SectionProps parseSectionProps(pugi::xml_node sectPrNode);

    // Resolve image from relationship ID
    int resolveImage(const std::string& relId, const std::map<std::string, ZipEntry>& entries,
                     const std::string& documentRelsPath, Document& doc);

    // Parse relationships file
    std::map<std::string, std::string> parseRelationships(const std::string& xml);

    // Relationship map (rId → target path)
    std::map<std::string, std::string> rels_;
    std::string docDir_; // directory of document.xml (e.g., "word/")
};

} // namespace docx
