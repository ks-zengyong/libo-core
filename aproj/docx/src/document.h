#pragma once
// Document model — mirrors LibreOffice's SwDoc/SwNodes.
// A flat representation of the parsed DOCX content.

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <memory>

namespace docx
{
// ── Units ──────────────────────────────────────────────────────
// OOXML uses EMU (English Metric Units): 1 inch = 914400 EMU
// OOXML uses twips for page sizes: 1 inch = 1440 twips
// We work in twips internally for layout.
constexpr double EMU_PER_TWIP = 635.0; // 914400 / 1440
constexpr double TWIP_PER_INCH = 1440.0;
constexpr double TWIP_PER_CM = 567.0; // 1440 / 2.54
constexpr double TWIP_PER_PT = 20.0; // 1440 / 72

// ── Color ──────────────────────────────────────────────────────
struct Color
{
    uint8_t r = 0, g = 0, b = 0;
    bool valid = false;

    static Color fromHex(const std::string& hex);
    static Color black() { return { 0, 0, 0, true }; }
    static Color white() { return { 255, 255, 255, true }; }
};

// ── Run Properties ─────────────────────────────────────────────
struct RunProps
{
    std::string fontName; // <w:rFonts w:ascii="...">
    std::string eastAsiaFont; // <w:rFonts w:eastAsia="...">
    int fontSize = 22; // <w:sz> in half-points (default 11pt = 22)
    int fontSizeCs = 22; // <w:szCs> complex script
    bool bold = false; // <w:b>
    bool italic = false; // <w:i>
    bool underline = false; // <w:u>
    bool strike = false; // <w:strike>
    Color color; // <w:color w:val="...">
    Color highlight; // <w:highlight w:val="...">
    int superscript = 0; // <w:vertAlign val="superscript"> → 1, subscript → -1
    std::string language; // <w:lang w:val="...">

    // Merge: apply 'other' on top of 'this' (non-default values win)
    void mergeFrom(const RunProps& other);
};

// ── Paragraph Properties ───────────────────────────────────────
enum class TextAlign
{
    Left,
    Center,
    Right,
    Justify,
    Distribute
};

struct ParagraphProps
{
    std::string styleName; // <w:pStyle w:val="...">
    TextAlign alignment = TextAlign::Left; // <w:jc>
    int spaceBefore = 0; // <w:spacing w:before="..."> in twips
    int spaceAfter = 0; // <w:spacing w:after="...">
    int lineSpacing = 240; // <w:spacing w:line="..."> 240 = single
    bool lineSpacingExact = false; // <w:spacing w:lineRule="exact">
    int indentLeft = 0; // <w:ind w:left="...">
    int indentRight = 0; // <w:ind w:right="...">
    int indentFirstLine = 0; // <w:ind w:firstLine="...">
    bool pageBreakBefore = false; // <w:pageBreakBefore>
    bool keepNext = false; // <w:keepNext>
    bool keepLines = false; // <w:keepLines>
    bool widowControl = true; // <w:widowControl> default true
    int outlineLevel = -1; // <w:outlineLvl w:val="..."> -1 = none

    // Numbering
    int numId = -1; // <w:numPr><w:numId w:val="..."> -1 = no numbering
    int numLevel = 0; // <w:numPr><w:ilvl w:val="...">

    // Paragraph-level run properties (from <w:pPr><w:rPr>)
    RunProps paraRunProps;

    // Merge: apply 'other' on top of 'this'
    void mergeFrom(const ParagraphProps& other);
};

// ── Content types ──────────────────────────────────────────────
struct TextContent
{
    std::string text;
};

struct DrawingContent
{
    int imageIndex = -1; // index into Document.images
    int widthEMU = 0;
    int heightEMU = 0;
    bool isAnchor = false; // true = anchored (floating), false = inline
};

// ── Text Run ───────────────────────────────────────────────────
struct TextRun
{
    RunProps props;
    // Content can be text or drawing
    std::string text;
    int drawingImageIndex = -1; // >= 0 means this run contains an image
    int drawingWidthEMU = 0;
    int drawingHeightEMU = 0;
    bool isAnchor = false;

    bool isDrawing() const { return drawingImageIndex >= 0; }
};

// ── Paragraph ──────────────────────────────────────────────────
struct Paragraph
{
    ParagraphProps props;
    std::vector<TextRun> runs;

    // Combined text of all runs
    std::string fullText() const;
};

// ── Table ──────────────────────────────────────────────────────
struct TableCell
{
    int widthTwips = 0;
    std::vector<Paragraph> paragraphs;
    int gridSpan = 1; // <w:tcW w:gridSpan>
    std::string verticalAlign; // <w:vAlign w:val="...">
};

struct TableRow
{
    std::vector<TableCell> cells;
    int heightTwips = 0;
    bool heightExact = false; // <w:trHeight w:hRule="exact">
};

struct TableProps
{
    int widthTwips = 0;
    std::string widthType; // "auto", "pct", "dxa"
    int indentTwips = 0;
    std::string alignment;
    // Borders
    int borderTop = 0, borderBottom = 0, borderLeft = 0, borderRight = 0;
    int borderInsideH = 0, borderInsideV = 0;
};

struct Table
{
    TableProps props;
    std::vector<TableRow> rows;
    std::vector<int> gridColumns; // <w:tblGrid><w:gridCol w:w="...">
};

// ── Header/Footer ──────────────────────────────────────────────
enum class HeaderFooterType
{
    Default,
    First,
    Even
};

struct HeaderFooterRef
{
    HeaderFooterType type = HeaderFooterType::Default;
    std::string relId; // relationship ID (r:id)
    std::string target; // resolved file path (e.g., "word/header1.xml")
};

// ── Section Properties ─────────────────────────────────────────
struct SectionProps
{
    int pageWidth = 11906; // A4 default in twips
    int pageHeight = 16838; // A4 default
    int marginTop = 1440; // 1 inch
    int marginBottom = 1440;
    int marginLeft = 1800; // 1.25 inch
    int marginRight = 1800;
    int headerMargin = 720;
    int footerMargin = 720;
    int gutter = 0;
    bool landscape = false;

    // Header/footer references
    std::vector<HeaderFooterRef> headerRefs;
    std::vector<HeaderFooterRef> footerRefs;
};

// ── Image Data ─────────────────────────────────────────────────
struct ImageData
{
    std::string fileName; // e.g., "image1.png"
    std::vector<uint8_t> data; // raw file bytes
    int widthEMU = 0;
    int heightEMU = 0;
};

// ── Numbering Definitions ──────────────────────────────────────
// OOXML numbering: <w:num> references an <w:abstractNum>.
// Each abstractNum has multiple <w:lvl> (0-9) defining the format.

enum class NumFormat
{
    Decimal, // 1, 2, 3
    UpperRoman, // I, II, III
    LowerRoman, // i, ii, iii
    UpperLetter, // A, B, C
    LowerLetter, // a, b, c
    Bullet, // bullet character
    None // no numbering
};

struct NumLevelDef
{
    int ilvl = 0; // level index (0-9)
    NumFormat numFmt = NumFormat::None;
    std::string lvlText; // e.g. "%1.", "%1.%2.", "•"
    int startVal = 1; // starting value
    std::string numFont; // font for the number text
    int numFontSize = 22; // half-points
    std::string bulletChar; // bullet character (for NumFormat::Bullet)
    // Indent
    int indentLeft = 0; // left indent in twips
    int hangingIndent = 0; // hanging indent in twips
    // Level restart
    int lvlRestart = -1; // restart level (-1 = no restart)
};

struct NumDef
{
    int abstractNumId = -1;
    std::vector<NumLevelDef> levels; // indexed by ilvl
};

struct AbstractNumDef
{
    int id = -1;
    std::vector<NumLevelDef> levels; // indexed by ilvl
};

// ── Style Definition ───────────────────────────────────────────
enum class StyleType
{
    Paragraph,
    Character,
    Table,
    Numbering,
    Unknown
};

struct StyleDef
{
    std::string id;
    std::string name;
    StyleType type = StyleType::Unknown;
    std::string parentId;
    ParagraphProps paraProps;
    RunProps runProps;
    bool isDefault = false;
};

// ── Document ───────────────────────────────────────────────────
// The root document model, analogous to LibreOffice's SwDoc.
struct Document
{
    // Content
    std::vector<Paragraph> paragraphs;
    std::vector<Table> tables;
    std::vector<ImageData> images;

    // Styles
    std::map<std::string, StyleDef> styles; // keyed by style ID

    // Page layout (from last sectPr or body-level sectPr)
    SectionProps sectionProps;
    std::vector<SectionProps> sectionBreaks; // per-section overrides

    // Section break markers: paragraph index → section properties after that paragraph
    std::map<int, SectionProps> sectionBreakMap;

    // Font table (font name → first found family/charset)
    std::map<std::string, std::string> fontTable;

    // Header/footer content (file path → paragraphs)
    std::map<std::string, std::vector<Paragraph>> headers;
    std::map<std::string, std::vector<Paragraph>> footers;

    // Numbering definitions
    std::vector<AbstractNumDef> abstractNums; // from numbering.xml
    std::vector<NumDef> numDefs; // <w:num> entries

    // Resolve numbering for a paragraph: returns the level definition or nullptr
    const NumLevelDef* resolveNumbering(int numId, int ilvl) const;

    // Format number text for a given level and counter values
    static std::string formatNumText(const NumLevelDef& level, const std::vector<int>& counters);

    // Helpers
    const StyleDef* findStyle(const std::string& name) const;
    ParagraphProps resolveParaProps(const Paragraph& para) const;
    RunProps resolveRunProps(const Paragraph& para, const TextRun& run) const;
};

} // namespace docx
