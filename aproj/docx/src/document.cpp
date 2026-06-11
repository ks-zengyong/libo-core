#include "document.h"
#include <algorithm>
#include <sstream>
#include <cstring>

namespace docx
{
// ── Color ──────────────────────────────────────────────────────
Color Color::fromHex(const std::string& hex)
{
    Color c;
    std::string h = hex;
    // Strip leading '#'
    if (!h.empty() && h[0] == '#')
        h = h.substr(1);
    // Handle "auto" → black
    if (h == "auto" || h.empty())
    {
        c.r = 0;
        c.g = 0;
        c.b = 0;
        c.valid = true;
        return c;
    }
    if (h.size() >= 6)
    {
        auto hexval = [](char ch) -> uint8_t {
            if (ch >= '0' && ch <= '9')
                return ch - '0';
            if (ch >= 'a' && ch <= 'f')
                return ch - 'a' + 10;
            if (ch >= 'A' && ch <= 'F')
                return ch - 'A' + 10;
            return 0;
        };
        c.r = (hexval(h[0]) << 4) | hexval(h[1]);
        c.g = (hexval(h[2]) << 4) | hexval(h[3]);
        c.b = (hexval(h[4]) << 4) | hexval(h[5]);
        c.valid = true;
    }
    return c;
}

// ── RunProps ───────────────────────────────────────────────────
void RunProps::mergeFrom(const RunProps& other)
{
    if (!other.fontName.empty())
        fontName = other.fontName;
    if (!other.eastAsiaFont.empty())
        eastAsiaFont = other.eastAsiaFont;
    if (other.fontSize != 22)
        fontSize = other.fontSize;
    if (other.fontSizeCs != 22)
        fontSizeCs = other.fontSizeCs;
    if (other.bold)
        bold = true;
    if (other.italic)
        italic = true;
    if (other.underline)
        underline = true;
    if (other.strike)
        strike = true;
    if (other.color.valid)
        color = other.color;
    if (other.highlight.valid)
        highlight = other.highlight;
    if (other.superscript != 0)
        superscript = other.superscript;
    if (!other.language.empty())
        language = other.language;
}

// ── ParagraphProps ─────────────────────────────────────────────
void ParagraphProps::mergeFrom(const ParagraphProps& other)
{
    if (!other.styleName.empty())
        styleName = other.styleName;
    if (other.alignment != TextAlign::Left)
        alignment = other.alignment;
    if (other.spaceBefore != 0)
        spaceBefore = other.spaceBefore;
    if (other.spaceAfter != 0)
        spaceAfter = other.spaceAfter;
    if (other.lineSpacing != 240)
        lineSpacing = other.lineSpacing;
    if (other.lineSpacingExact)
        lineSpacingExact = true;
    if (other.indentLeft != 0)
        indentLeft = other.indentLeft;
    if (other.indentRight != 0)
        indentRight = other.indentRight;
    if (other.indentFirstLine != 0)
        indentFirstLine = other.indentFirstLine;
    if (other.pageBreakBefore)
        pageBreakBefore = true;
    if (other.keepNext)
        keepNext = true;
    if (other.keepLines)
        keepLines = true;
    if (!other.widowControl)
        widowControl = false;
    if (other.outlineLevel >= 0)
        outlineLevel = other.outlineLevel;
    paraRunProps.mergeFrom(other.paraRunProps);
}

// ── Paragraph ──────────────────────────────────────────────────
std::string Paragraph::fullText() const
{
    std::string result;
    for (auto& run : runs)
    {
        result += run.text;
    }
    return result;
}

// ── Document ───────────────────────────────────────────────────
const StyleDef* Document::findStyle(const std::string& name) const
{
    // Search by ID first
    auto it = styles.find(name);
    if (it != styles.end())
        return &it->second;
    // Search by name
    for (auto & [ id, s ] : styles)
    {
        if (s.name == name)
            return &s;
    }
    return nullptr;
}

ParagraphProps Document::resolveParaProps(const Paragraph& para) const
{
    ParagraphProps result;

    // 1. Start with Normal style (if exists)
    const StyleDef* normal = findStyle("Normal");
    if (normal)
    {
        result.mergeFrom(normal->paraProps);
    }

    // 2. Apply named style chain
    if (!para.props.styleName.empty())
    {
        const StyleDef* style = findStyle(para.props.styleName);
        if (style)
        {
            // Walk parent chain
            std::vector<const StyleDef*> chain;
            const StyleDef* s = style;
            while (s && !s->parentId.empty())
            {
                chain.push_back(s);
                s = findStyle(s->parentId);
                if (s == style)
                    break; // prevent cycle
            }
            // Apply from root to leaf
            for (auto it = chain.rbegin(); it != chain.rend(); ++it)
            {
                result.mergeFrom((*it)->paraProps);
            }
        }
    }

    // 3. Apply direct formatting (highest priority)
    result.mergeFrom(para.props);

    return result;
}

RunProps Document::resolveRunProps(const Paragraph& para, const TextRun& run) const
{
    RunProps result;

    // 1. Start with Normal style's run props
    const StyleDef* normal = findStyle("Normal");
    if (normal)
    {
        result.mergeFrom(normal->runProps);
    }

    // 2. Apply paragraph style's run props (rPr in style definition)
    if (!para.props.styleName.empty())
    {
        const StyleDef* style = findStyle(para.props.styleName);
        if (style)
        {
            result.mergeFrom(style->runProps);
        }
    }

    // 3. Apply paragraph-level run props (from <w:pPr><w:rPr>)
    result.mergeFrom(para.props.paraRunProps);

    // 4. Apply direct run formatting (highest priority)
    result.mergeFrom(run.props);

    // Defaults
    if (result.fontName.empty())
        result.fontName = "Calibri";
    if (result.fontSize <= 0)
        result.fontSize = 22; // 11pt

    return result;
}

// ── Numbering Helpers ──────────────────────────────────────────
static std::string toRoman(int num, bool upper)
{
    if (num <= 0)
        return "";
    static const char* rm[]
        = { "m", "cm", "d", "cd", "c", "xc", "l", "xl", "x", "ix", "v", "iv", "i" };
    static const int rv[] = { 1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1 };
    std::string result;
    for (int i = 0; i < 13; i++)
    {
        while (num >= rv[i])
        {
            result += rm[i];
            num -= rv[i];
        }
    }
    if (upper)
    {
        for (auto& c : result)
            c = toupper(c);
    }
    return result;
}

static std::string toAlpha(int num, bool upper)
{
    if (num <= 0)
        return "";
    std::string result;
    while (num > 0)
    {
        num--;
        result += (char)((upper ? 'A' : 'a') + (num % 26));
        num /= 26;
    }
    std::reverse(result.begin(), result.end());
    return result;
}

// ── Numbering Resolution ───────────────────────────────────────
const NumLevelDef* Document::resolveNumbering(int numId, int ilvl) const
{
    if (numId < 0 || ilvl < 0 || ilvl > 9)
        return nullptr;

    // Find by index (numId is 1-based in OOXML)
    int idx = numId - 1;
    if (idx < 0 || idx >= (int)numDefs.size())
        return nullptr;

    auto& num = numDefs[idx];
    int absId = num.abstractNumId;
    if (absId < 0 || absId >= (int)abstractNums.size())
        return nullptr;

    auto& absNum = abstractNums[absId];
    if (ilvl >= (int)absNum.levels.size())
        return nullptr;

    return &absNum.levels[ilvl];
}

std::string Document::formatNumText(const NumLevelDef& level, const std::vector<int>& counters)
{
    if (level.numFmt == NumFormat::Bullet)
    {
        return level.bulletChar.empty() ? "•" : level.bulletChar;
    }
    if (level.numFmt == NumFormat::None)
        return "";

    std::string result = level.lvlText;
    if (result.empty())
        result = "%1.";

    // Replace %1, %2, etc. with formatted numbers
    for (int i = 0; i < 10; i++)
    {
        std::string placeholder = "%" + std::to_string(i + 1);
        size_t pos = result.find(placeholder);
        if (pos == std::string::npos)
            continue;

        int val = (i < (int)counters.size()) ? counters[i] : 1;
        std::string formatted;
        switch (level.numFmt)
        {
            case NumFormat::Decimal:
                formatted = std::to_string(val);
                break;
            case NumFormat::UpperRoman:
                formatted = toRoman(val, true);
                break;
            case NumFormat::LowerRoman:
                formatted = toRoman(val, false);
                break;
            case NumFormat::UpperLetter:
                formatted = toAlpha(val, true);
                break;
            case NumFormat::LowerLetter:
                formatted = toAlpha(val, false);
                break;
            default:
                formatted = std::to_string(val);
                break;
        }
        result.replace(pos, placeholder.size(), formatted);
    }

    return result;
}

} // namespace docx
