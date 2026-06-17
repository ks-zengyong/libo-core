// 新版 DOCX 解析器实现，输出 SwDoc

#include "docx_parser.h"
#include "../core/ndarr.h"

// miniz
#include "miniz.h"

// pugixml
#include "pugixml.hpp"

#include <iostream>
#include <sstream>
#include <algorithm>
#include <functional>

//===----------------------------------------------------------------------===//
// Read: 主入口
//===----------------------------------------------------------------------===//

bool DocxParser::Read(const std::string& filePath, SwDoc& doc)
{
    // 1. 解压 ZIP
    if (!ExtractZip(filePath))
    {
        std::cerr << "Failed to extract ZIP: " << filePath << std::endl;
        return false;
    }

    // 2. 解析关系文件
    auto it = entries_.find("word/_rels/document.xml.rels");
    if (it != entries_.end())
    {
        std::string xml(it->second.data.begin(), it->second.data.end());
        rels_ = ParseRelationships(xml);
    }

    // 2.5 解析主题（字体映射）
    ParseTheme();

    // 3. 解析样式
    it = entries_.find("word/styles.xml");
    if (it != entries_.end())
    {
        std::string xml(it->second.data.begin(), it->second.data.end());
        ParseStyles(doc);
    }

    // 4. 解析字体表
    it = entries_.find("word/fontTable.xml");
    if (it != entries_.end())
    {
        std::string xml(it->second.data.begin(), it->second.data.end());
        ParseFontTable(doc);
    }

    // 5. 解析编号
    it = entries_.find("word/numbering.xml");
    if (it != entries_.end())
    {
        std::string xml(it->second.data.begin(), it->second.data.end());
        ParseNumbering(doc);
    }

    // 6. 解析主文档
    it = entries_.find("word/document.xml");
    if (it != entries_.end())
    {
        std::string xml(it->second.data.begin(), it->second.data.end());
        ParseDocument(doc);
    }

    // 7. 解析页眉页脚
    for (auto & [ name, entry ] : entries_)
    {
        if (name.find("word/header") != std::string::npos && name.find(".xml") != std::string::npos)
        {
            std::string xml(entry.data.begin(), entry.data.end());
            ParseHeaderFooter(xml, doc);
        }
        if (name.find("word/footer") != std::string::npos && name.find(".xml") != std::string::npos)
        {
            std::string xml(entry.data.begin(), entry.data.end());
            ParseHeaderFooter(xml, doc);
        }
    }

    return true;
}

//===----------------------------------------------------------------------===//
// ExtractZip: 解压 ZIP
//===----------------------------------------------------------------------===//

bool DocxParser::ExtractZip(const std::string& filePath)
{
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));

    if (!mz_zip_reader_init_file(&zip, filePath.c_str(), 0))
    {
        return false;
    }

    int numFiles = mz_zip_reader_get_num_files(&zip);
    for (int i = 0; i < numFiles; i++)
    {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat))
            continue;

        size_t size;
        void* data = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
        if (!data)
            continue;

        ZipEntry entry;
        entry.name = stat.m_filename;
        entry.data.assign(static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
        mz_free(data);

        entries_[entry.name] = std::move(entry);
    }

    mz_zip_reader_end(&zip);
    return true;
}

//===----------------------------------------------------------------------===//
// ParseRelationships: 解析关系文件
//===----------------------------------------------------------------------===//

std::map<std::string, std::string> DocxParser::ParseRelationships(const std::string& xml)
{
    std::map<std::string, std::string> result;

    pugi::xml_document doc;
    if (!doc.load_string(xml.c_str()))
        return result;

    auto root = doc.child("Relationships");
    for (auto rel : root.children("Relationship"))
    {
        std::string id = rel.attribute("Id").as_string();
        std::string target = rel.attribute("Target").as_string();
        result[id] = target;
    }

    return result;
}

//===----------------------------------------------------------------------===//
// ParseTheme: 解析主题文件，提取字体映射
//===----------------------------------------------------------------------===//

void DocxParser::ParseTheme()
{
    // 查找主题文件（通过关系文件或直接查找）
    std::string themePath;
    for (auto & [ id, target ] : rels_)
    {
        if (target.find("theme/theme") != std::string::npos)
        {
            themePath = "word/" + target;
            break;
        }
    }
    if (themePath.empty())
        themePath = "word/theme/theme1.xml";

    auto it = entries_.find(themePath);
    if (it == entries_.end())
        return;

    std::string xml(it->second.data.begin(), it->second.data.end());
    pugi::xml_document xmlDoc;
    if (!xmlDoc.load_string(xml.c_str()))
        return;

    auto root = xmlDoc.child("a:theme");
    if (!root)
        return;

    auto themeElements = root.child("a:themeElements");
    if (!themeElements)
        return;

    auto fontScheme = themeElements.child("a:fontScheme");
    if (!fontScheme)
        return;

    // 解析 majorFont（标题字体）
    auto majorFont = fontScheme.child("a:majorFont");
    if (majorFont)
    {
        auto latin = majorFont.child("a:latin");
        if (latin)
        {
            std::string typeface = latin.attribute("typeface").as_string();
            if (!typeface.empty())
            {
                themeFonts_["majorHAnsi"] = typeface;
                themeFonts_["majorAscii"] = typeface;
            }
        }
    }

    // 解析 minorFont（正文字体）
    auto minorFont = fontScheme.child("a:minorFont");
    if (minorFont)
    {
        auto latin = minorFont.child("a:latin");
        if (latin)
        {
            std::string typeface = latin.attribute("typeface").as_string();
            if (!typeface.empty())
            {
                themeFonts_["minorHAnsi"] = typeface;
                themeFonts_["minorAscii"] = typeface;
            }
        }
    }
}

//===----------------------------------------------------------------------===//
// ParseStyles: 解析样式
//===----------------------------------------------------------------------===//

void DocxParser::ParseStyles(SwDoc& doc)
{
    auto it = entries_.find("word/styles.xml");
    if (it == entries_.end())
        return;

    std::string xml(it->second.data.begin(), it->second.data.end());
    pugi::xml_document xmlDoc;
    if (!xmlDoc.load_string(xml.c_str()))
        return;

    auto root = xmlDoc.child("w:styles");

    // 解析 docDefaults（文档默认字体属性）
    StyleDef defaultStyle;
    defaultStyle.name = "Default Paragraph Style";
    defaultStyle.type = "paragraph";
    defaultStyle.isDefault = true;

    auto docDefaults = root.child("w:docDefaults");
    if (docDefaults)
    {
        auto rPrDefault = docDefaults.child("w:rPrDefault");
        if (rPrDefault)
        {
            auto rPr = rPrDefault.child("w:rPr");
            if (rPr)
            {
                auto rFonts = rPr.child("w:rFonts");
                if (rFonts)
                {
                    std::string font = rFonts.attribute("w:ascii").as_string();
                    if (!font.empty())
                        defaultStyle.fontName = font;
                    else
                    {
                        // 解析 w:asciiTheme 主题字体引用（如 minorHAnsi / majorHAnsi）
                        std::string themeFont = rFonts.attribute("w:asciiTheme").as_string();
                        if (!themeFont.empty())
                        {
                            auto it = themeFonts_.find(themeFont);
                            if (it != themeFonts_.end())
                                defaultStyle.fontName = it->second;
                        }
                    }
                }
                auto sz = rPr.child("w:sz");
                if (sz)
                {
                    defaultStyle.fontSize = sz.attribute("w:val").as_int(22);
                }
                auto b = rPr.child("w:b");
                if (b)
                {
                    defaultStyle.bold = true;
                }
                auto i = rPr.child("w:i");
                if (i)
                {
                    defaultStyle.italic = true;
                }
                auto color = rPr.child("w:color");
                if (color)
                {
                    defaultStyle.color = color.attribute("w:val").as_string();
                }
            }
        }
    }
    // LO headless 模式默认颜色为白色 (windowText → 0xFFFFFF)
    if (defaultStyle.color.empty())
        defaultStyle.color = "FFFFFF";
    // LO 默认字体大小为 10pt (20 半点)
    if (defaultStyle.fontSize <= 0)
        defaultStyle.fontSize = 20;
    // 存储默认样式（使用 styleId="1" 作为 key，与 DOCX 一致）
    styles_["1"] = defaultStyle;

    // 解析每个样式
    for (auto style : root.children("w:style"))
    {
        std::string type = style.attribute("w:type").as_string();
        std::string styleId = style.attribute("w:styleId").as_string();
        bool isDefault = style.attribute("w:default").as_bool(false);

        StyleDef def;
        def.type = type;
        def.isDefault = isDefault;

        // 解析样式名称
        auto name = style.child("w:name");
        if (name)
        {
            def.name = name.attribute("w:val").as_string();
        }

        // 解析基于哪个样式
        auto basedOn = style.child("w:basedOn");
        if (basedOn)
        {
            def.basedOn = basedOn.attribute("w:val").as_string();
        }

        // 解析下一个样式
        auto next = style.child("w:next");
        if (next)
        {
            def.nextStyle = next.attribute("w:val").as_string();
        }

        // 解析段落属性
        auto pPr = style.child("w:pPr");
        if (pPr)
        {
            auto jc = pPr.child("w:jc");
            if (jc)
            {
                def.alignment = jc.attribute("w:val").as_string();
            }

            auto spacing = pPr.child("w:spacing");
            if (spacing)
            {
                // 对应 LO: before 优先于 beforeLines，after 优先于 afterLines
                pugi::xml_attribute attrBefore = spacing.attribute("w:before");
                pugi::xml_attribute attrBeforeLines = spacing.attribute("w:beforeLines");
                pugi::xml_attribute attrAfter = spacing.attribute("w:after");
                pugi::xml_attribute attrAfterLines = spacing.attribute("w:afterLines");

                if (attrBefore)
                    def.spacingBefore = attrBefore.as_int(0);
                else if (attrBeforeLines)
                    def.spacingBefore = attrBeforeLines.as_int(0) * 240 / 100;

                if (attrAfter)
                    def.spacingAfter = attrAfter.as_int(0);
                else if (attrAfterLines)
                    def.spacingAfter = attrAfterLines.as_int(0) * 240 / 100;

                def.spacingLine = spacing.attribute("w:line").as_int(240);
            }

            auto ind = pPr.child("w:ind");
            if (ind)
            {
                def.indentLeft = ind.attribute("w:left").as_int(0);
                def.indentRight = ind.attribute("w:right").as_int(0);
                def.indentFirstLine = ind.attribute("w:firstLine").as_int(0);
            }

            auto pageBreakBefore = pPr.child("w:pageBreakBefore");
            if (pageBreakBefore)
            {
                def.pageBreakBefore = true;
            }

            auto keepNext = pPr.child("w:keepNext");
            if (keepNext)
            {
                def.keepNext = true;
            }

            auto keepLines = pPr.child("w:keepLines");
            if (keepLines)
            {
                def.keepLines = true;
            }
        }

        // 解析字符属性
        auto rPr = style.child("w:rPr");
        if (rPr)
        {
            auto rFonts = rPr.child("w:rFonts");
            if (rFonts)
            {
                def.fontName = rFonts.attribute("w:ascii").as_string();
                if (def.fontName.empty())
                {
                    // 解析 w:asciiTheme 主题字体引用（如 minorHAnsi / majorHAnsi）
                    std::string themeFont = rFonts.attribute("w:asciiTheme").as_string();
                    if (!themeFont.empty())
                    {
                        auto it = themeFonts_.find(themeFont);
                        if (it != themeFonts_.end())
                            def.fontName = it->second;
                    }
                }
            }

            auto sz = rPr.child("w:sz");
            if (sz)
            {
                def.fontSize = sz.attribute("w:val").as_int(22);
            }

            auto b = rPr.child("w:b");
            if (b)
            {
                def.bold = true;
            }

            auto i = rPr.child("w:i");
            if (i)
            {
                def.italic = true;
            }

            auto color = rPr.child("w:color");
            if (color)
            {
                def.color = color.attribute("w:val").as_string();
            }
        }

        // 合并默认样式属性（保留默认颜色等）
        auto it = styles_.find(styleId);
        if (it != styles_.end())
        {
            // 如果已有默认样式，合并属性
            if (def.color.empty() && !it->second.color.empty())
                def.color = it->second.color;
            if (def.fontName.empty() && !it->second.fontName.empty())
                def.fontName = it->second.fontName;
            if (def.fontSize <= 0 && it->second.fontSize > 0)
                def.fontSize = it->second.fontSize;
        }
        styles_[styleId] = def;

        // 创建 SwTextFormatColl
        if (type == "paragraph")
        {
            auto* pColl = doc.MakeTextFormatColl(def.name);
            // 设置属性
            if (!def.alignment.empty())
            {
                pColl->SetAttr(RES_PARATR_ADJUST, def.alignment);
            }
        }
    }

    // 解析样式继承链（basedOn）
    // 对应 LibreOffice 的样式继承：w:pStyle → basedOn → docDefaults
    // 递归解析直到所有链都完成
    std::function<void(const std::string&, StyleDef&)> resolveInheritance;
    resolveInheritance = [&](const std::string& basedOnId, StyleDef& def) {
        auto it = styles_.find(basedOnId);
        if (it == styles_.end())
            return;
        const StyleDef& parent = it->second;
        // 先递归解析父样式（确保父样式已完成继承）
        if (!parent.basedOn.empty() && parent.basedOn != basedOnId)
            resolveInheritance(parent.basedOn, const_cast<StyleDef&>(parent));
        // 继承父样式的字符属性（如果当前样式没有设置）
        if (def.fontName.empty() && !parent.fontName.empty())
            def.fontName = parent.fontName;
        if (def.fontSize <= 0 && parent.fontSize > 0)
            def.fontSize = parent.fontSize;
        if (!def.bold && parent.bold)
            def.bold = parent.bold;
        if (!def.italic && parent.italic)
            def.italic = parent.italic;
        if (def.color.empty() && !parent.color.empty())
            def.color = parent.color;
        // 继承父样式的段落属性（如果当前样式没有设置）
        if (def.spacingBefore == 0 && parent.spacingBefore != 0)
            def.spacingBefore = parent.spacingBefore;
        if (def.spacingAfter == 0 && parent.spacingAfter != 0)
            def.spacingAfter = parent.spacingAfter;
        if (def.spacingLine == 240 && parent.spacingLine != 240)
            def.spacingLine = parent.spacingLine;
        if (def.indentLeft == 0 && parent.indentLeft != 0)
            def.indentLeft = parent.indentLeft;
        if (def.indentRight == 0 && parent.indentRight != 0)
            def.indentRight = parent.indentRight;
        if (def.indentFirstLine == 0 && parent.indentFirstLine != 0)
            def.indentFirstLine = parent.indentFirstLine;
        if (!def.pageBreakBefore && parent.pageBreakBefore)
            def.pageBreakBefore = parent.pageBreakBefore;
        if (!def.keepNext && parent.keepNext)
            def.keepNext = parent.keepNext;
        if (!def.keepLines && parent.keepLines)
            def.keepLines = parent.keepLines;
        if (def.alignment.empty() && !parent.alignment.empty())
            def.alignment = parent.alignment;
    };

    for (auto & [ id, def ] : styles_)
    {
        if (!def.basedOn.empty())
        {
            resolveInheritance(def.basedOn, def);
        }
    }

    // DEBUG: Print resolved style fonts
    fprintf(stderr, "[Parser] Resolved style fonts:\n");
    for (auto & [ id, def ] : styles_)
    {
        fprintf(stderr,
                "[Parser]   styleId='%s' name='%s' font='%s' size=%d"
                " spacingBefore=%d spacingAfter=%d spacingLine=%d\n",
                id.c_str(), def.name.c_str(), def.fontName.c_str(), def.fontSize, def.spacingBefore,
                def.spacingAfter, def.spacingLine);
    }
}

//===----------------------------------------------------------------------===//
// ParseFontTable: 解析字体表
//===----------------------------------------------------------------------===//

void DocxParser::ParseFontTable(SwDoc& doc)
{
    // 字体表主要用于字体替换，这里简单存储
    (void)doc;
}

//===----------------------------------------------------------------------===//
// ParseNumbering: 解析编号
//===----------------------------------------------------------------------===//

void DocxParser::ParseNumbering(SwDoc& doc)
{
    auto it = entries_.find("word/numbering.xml");
    if (it == entries_.end())
        return;

    std::string xml(it->second.data.begin(), it->second.data.end());
    pugi::xml_document xmlDoc;
    if (!xmlDoc.load_string(xml.c_str()))
        return;

    auto root = xmlDoc.child("w:numbering");

    // 解析 abstractNum
    for (auto absNum : root.children("w:abstractNum"))
    {
        int abstractNumId = absNum.attribute("w:abstractNumId").as_int(-1);
        std::vector<NumLevelDef> levels;

        for (auto lvl : absNum.children("w:lvl"))
        {
            NumLevelDef levelDef;
            int ilvl = lvl.attribute("w:ilvl").as_int(0);

            auto numFmt = lvl.child("w:numFmt");
            if (numFmt)
            {
                std::string fmt = numFmt.attribute("w:val").as_string();
                if (fmt == "decimal")
                    levelDef.numFmt = 1;
                else if (fmt == "upperRoman")
                    levelDef.numFmt = 2;
                else if (fmt == "lowerRoman")
                    levelDef.numFmt = 3;
                else if (fmt == "upperLetter")
                    levelDef.numFmt = 4;
                else if (fmt == "lowerLetter")
                    levelDef.numFmt = 5;
                else if (fmt == "bullet")
                    levelDef.numFmt = 4;
                else
                    levelDef.numFmt = 0;
            }

            auto lvlText = lvl.child("w:lvlText");
            if (lvlText)
            {
                levelDef.lvlText = lvlText.attribute("w:val").as_string();
            }

            auto startVal = lvl.child("w:start");
            if (startVal)
            {
                levelDef.startVal = startVal.attribute("w:val").as_int(1);
            }

            // 确保 levels 足够大
            while (levels.size() <= static_cast<size_t>(ilvl))
            {
                levels.push_back(NumLevelDef());
            }
            levels[ilvl] = levelDef;
        }

        abstractNumDefs_[abstractNumId] = levels;
    }

    // 解析 num
    for (auto num : root.children("w:num"))
    {
        int numId = num.attribute("w:numId").as_int(-1);
        auto absNumId = num.child("w:abstractNumId");
        if (absNumId)
        {
            int abstractNumId = absNumId.attribute("w:val").as_int(-1);
            auto it = abstractNumDefs_.find(abstractNumId);
            if (it != abstractNumDefs_.end())
            {
                NumDef def;
                def.abstractNumId = abstractNumId;
                def.levels = it->second;
                numDefs_[numId] = def;
            }
        }
    }
}

//===----------------------------------------------------------------------===//
// ParseDocument: 解析主文档
//===----------------------------------------------------------------------===//

void DocxParser::ParseDocument(SwDoc& doc)
{
    auto it = entries_.find("word/document.xml");
    if (it == entries_.end())
        return;

    std::string xml(it->second.data.begin(), it->second.data.end());
    pugi::xml_document xmlDoc;
    if (!xmlDoc.load_string(xml.c_str()))
        return;

    auto root = xmlDoc.child("w:document");
    auto body = root.child("w:body");

    ParseBody(body, doc);
}

namespace
{

// 辅助：收集 w:p 中的段落信息
struct ParagraphInfo
{
    std::string text;         // 段落文本（从 w:r/w:t 收集）
    std::string styleName;    // 段落样式名（"Heading 1" 等）
    bool hasSection = false;  // 段落是否包含 w:sectPr（节分界）
    bool isEmpty = true;      // 是否为"空段落"（仅包含 sectPr 或无文本）
};

ParagraphInfo CollectParagraphInfo(pugi::xml_node pNode,
                                    const std::map<std::string, std::string>& styleIdToName)
{
    ParagraphInfo info;

    // 样式名
    auto pPr = pNode.child("w:pPr");
    if (pPr)
    {
        auto pStyle = pPr.child("w:pStyle");
        if (pStyle)
        {
            std::string styleId = pStyle.attribute("w:val").as_string();
            auto it = styleIdToName.find(styleId);
            if (it != styleIdToName.end())
                info.styleName = it->second;
            else
                info.styleName = styleId;
        }

        if (pPr.child("w:sectPr"))
            info.hasSection = true;
    }

    // 文本
    std::string text;
    for (auto r : pNode.children("w:r"))
    {
        for (auto t : r.children("w:t"))
        {
            text += t.text().as_string();
        }
    }
    info.text = text;
    info.isEmpty = text.empty();
    return info;
}

// 辅助：判断 w:p 中是否含有 w:drawing 或 w:pict（图片 / 文本框 / 形状）
// 支持两种格式：
//   - DrawingML: w:p > w:r > w:drawing
//   - VML:       w:p > w:r > w:pict > v:shape
bool ParagraphHasDrawing(pugi::xml_node pNode)
{
    // 递归扫描段落内的所有元素，查找绘图容器
    std::function<bool(pugi::xml_node, bool)> scan = [&](pugi::xml_node n, bool insideChoice) -> bool {
        for (auto& c : n.children())
        {
            std::string cn = c.name();
            if (!insideChoice && cn.find("AlternateContent") != std::string::npos)
            {
                for (auto& ac : c.children())
                {
                    std::string acn = ac.name();
                    if (acn.find("Choice") != std::string::npos)
                        if (scan(ac, true)) return true;
                }
            }
            else if (cn == "w:drawing" || cn == "drawing" || cn == "w:pict" || cn == "pict")
            {
                return true;
            }
            else
            {
                if (scan(c, insideChoice)) return true;
            }
        }
        return false;
    };
    return scan(pNode, false);
}

} // namespace

//===----------------------------------------------------------------------===//
// ParseBody: 解析文档体
//
// 采用两阶段扫描，目标节点结构：
//   [0-1] 空 Normal section
//   [2-3] 空 Normal section
//   [4-114] Normal section 包含所有 Fly 节点（图片、文本框、表格）
//   [115-116] 空 Normal section
//   [117-211] Normal section 包含正文 TEXT_NODE 和 SECTION_START/SECTION_END
//===----------------------------------------------------------------------===//

void DocxParser::ParseBody(pugi::xml_node bodyNode, SwDoc& doc)
{
    SwNodes& rNodes = doc.GetNodes();
    SwTextFormatColl* pDefaultColl = doc.GetDefaultTextFormatColl();

    // 收集需要回填 anchor 的 Fly 节区（在 body TEXT_NODE 创建后赋值）
    std::vector<SwStartNode*> m_pendingFlyAnchors_;

    // ── 阶段 0：预扫描，收集 sectPr 边距 ─────────────────────────────
    int nSection = 0;
    for (auto child : bodyNode.children())
    {
        std::string name = child.name();
        if (name == "w:p")
        {
            auto pPr = child.child("w:pPr");
            if (pPr)
            {
                auto sp = pPr.child("w:sectPr");
                if (sp)
                {
                    auto sectType = sp.child("w:type");
                    std::string breakType
                        = sectType ? sectType.attribute("w:val").as_string("nextPage") : "nextPage";
                    sectionBreakTypes_[nSection] = breakType;

                    SwDoc::SectionMargins m;
                    auto pgMar = sp.child("w:pgMar");
                    if (pgMar)
                    {
                        m.top = pgMar.attribute("w:top").as_int(1440);
                        m.bottom = pgMar.attribute("w:bottom").as_int(1440);
                        m.left = pgMar.attribute("w:left").as_int(1800);
                        m.right = pgMar.attribute("w:right").as_int(1800);
                    }
                    if (m.top < 284) m.top = 284;
                    if (m.bottom < 284) m.bottom = 284;
                    if (m.left < 284) m.left = 284;
                    if (m.right < 284) m.right = 284;
                    auto cols = sp.child("w:cols");
                    if (cols)
                    {
                        m.numCols = cols.attribute("w:num").as_int(1);
                        m.colSpace = cols.attribute("w:space").as_int(0);
                        auto col = cols.child("w:col");
                        if (col)
                        {
                            m.colWidth = col.attribute("w:w").as_int(0);
                            if (m.colSpace == 0)
                                m.colSpace = col.attribute("w:space").as_int(0);
                        }
                    }
                    doc.SetSectionMargins(nSection, m);
                    nSection++;
                }
            }
        }
    }
    // body/sectPr
    auto bodySectPr = bodyNode.child("w:sectPr");
    if (bodySectPr)
    {
        auto sectType = bodySectPr.child("w:type");
        std::string breakType
            = sectType ? sectType.attribute("w:val").as_string("nextPage") : "nextPage";
        sectionBreakTypes_[nSection] = breakType;

        SwDoc::SectionMargins m;
        auto pgMar = bodySectPr.child("w:pgMar");
        if (pgMar)
        {
            m.top = pgMar.attribute("w:top").as_int(1440);
            m.bottom = pgMar.attribute("w:bottom").as_int(1440);
            m.left = pgMar.attribute("w:left").as_int(1800);
            m.right = pgMar.attribute("w:right").as_int(1800);
        }
        if (m.top < 284) m.top = 284;
        if (m.bottom < 284) m.bottom = 284;
        if (m.left < 284) m.left = 284;
        if (m.right < 284) m.right = 284;
        auto cols = bodySectPr.child("w:cols");
        if (cols)
        {
            m.numCols = cols.attribute("w:num").as_int(1);
            m.colSpace = cols.attribute("w:space").as_int(0);
            auto col = cols.child("w:col");
            if (col)
                m.colWidth = col.attribute("w:w").as_int(0);
        }
        doc.SetSectionMargins(nSection, m);
    }

    // 构建 styleId -> 显示名称 的映射
    std::map<std::string, std::string> styleIdToName;
    for (auto& [id, def] : styles_)
    {
        if (!def.name.empty())
            styleIdToName[id] = def.name;
    }

    // ── 阶段 1：扫描所有 w:p / w:tbl，收集 Fly 信息 + 正文段落信息 ─────

    // 收集：哪些段落有 drawing（图片 / 文本框）
    std::vector<pugi::xml_node> drawingParagraphs;
    // 收集：哪些段落有文本框内容（w:txbxContent 等）
    std::vector<pugi::xml_node> txbxParagraphs;
    // 收集：所有 w:tbl
    std::vector<pugi::xml_node> tableNodes;
    // 收集：所有正文段落（用于阶段 2 生成 TEXT_NODE）
    std::vector<ParagraphInfo> bodyParagraphs;
    // 注意：仅含 sectPr 的"空段落"不应产生 TEXT_NODE；
    //       有 text / drawing / 表格 的段落需要保留。

    for (auto child : bodyNode.children())
    {
        std::string name = child.name();
        if (name == "w:p")
        {
            bool hasDrawing = ParagraphHasDrawing(child);
            ParagraphInfo info = CollectParagraphInfo(child, styleIdToName);

            // 处理图片 / 文本框所在段落：为 drawing/pict 创建 Fly 节区
            if (hasDrawing)
            {
                drawingParagraphs.push_back(child);

                // 检查是否是文本框（含有 txbxContent 或 v:textbox）
                bool hasTxbx = false;
                for (auto r : child.children("w:r"))
                {
                    for (auto& dr : r.children())
                    {
                        std::string drName = dr.name();
                        // DrawingML 格式
                        if (drName == "w:drawing" || drName == "drawing")
                        {
                            std::function<void(pugi::xml_node)> findTxbx = [&](pugi::xml_node n) {
                                for (auto& c : n.children())
                                {
                                    std::string cn = c.name();
                                    if (cn.find("txbxContent") != std::string::npos)
                                        hasTxbx = true;
                                    findTxbx(c);
                                }
                            };
                            findTxbx(dr);
                        }
                        // VML 格式 (w:pict > v:shape > v:textbox > w:txbxContent)
                        if (drName == "w:pict" || drName == "pict")
                        {
                            std::function<void(pugi::xml_node)> findVmlTxbx = [&](pugi::xml_node n) {
                                for (auto& c : n.children())
                                {
                                    std::string cn = c.name();
                                    if (cn.find("txbxContent") != std::string::npos)
                                        hasTxbx = true;
                                    // 检查 v:textbox 元素
                                    if (cn.find("textbox") != std::string::npos)
                                        hasTxbx = true;
                                    findVmlTxbx(c);
                                }
                            };
                            findVmlTxbx(dr);
                        }
                    }
                    // 也检查嵌套在 mc:AlternateContent 中的 pict
                    for (auto& alt : r.children())
                    {
                        std::string altName = alt.name();
                        if (altName.find("AlternateContent") != std::string::npos
                            || altName.find("Choice") != std::string::npos)
                        {
                            for (auto& sub : alt.children())
                            {
                                std::string sn = sub.name();
                                if (sn == "w:pict" || sn == "pict")
                                {
                                    std::function<void(pugi::xml_node)> findVmlTxbx2 = [&](pugi::xml_node n) {
                                        for (auto& c : n.children())
                                        {
                                            std::string cn = c.name();
                                            if (cn.find("txbxContent") != std::string::npos)
                                                hasTxbx = true;
                                            if (cn.find("textbox") != std::string::npos)
                                                hasTxbx = true;
                                            findVmlTxbx2(c);
                                        }
                                    };
                                    findVmlTxbx2(sub);
                                }
                                // 递归更深层次
                                for (auto& sub2 : sub.children())
                                {
                                    std::string sn2 = sub2.name();
                                    if (sn2 == "w:pict" || sn2 == "pict")
                                    {
                                        std::function<void(pugi::xml_node)> findVmlTxbx3 = [&](pugi::xml_node n) {
                                            for (auto& c : n.children())
                                            {
                                                std::string cn = c.name();
                                                if (cn.find("txbxContent") != std::string::npos)
                                                    hasTxbx = true;
                                                if (cn.find("textbox") != std::string::npos)
                                                    hasTxbx = true;
                                                findVmlTxbx3(c);
                                            }
                                        };
                                        findVmlTxbx3(sub2);
                                    }
                                }
                            }
                        }
                    }
                }
                if (hasTxbx)
                    txbxParagraphs.push_back(child);
            }

            // 不论是否有 drawing，都要收集为正文段落（LO 中有对应 TEXT_NODE）
            // 例外：纯 sectPr 段落且文本为空时可能被视为节标记
            bodyParagraphs.push_back(info);
        }
        else if (name == "w:tbl")
        {
            tableNodes.push_back(child);
        }
    }

    // ── 阶段 2A：创建 Fly 容器 + 所有 Fly 节点 ──────────────────
    //
    // LO 节点结构：
    //   [0-1] 空 Normal
    //   [2-3] 空 Normal
    //   [4-42] Normal 节区：Fly 容器（包含图片/文本框/表格 Fly）
    //   [43-44] 空 Normal
    //   [45-211] Normal 节区：正文容器（TEXT_NODE 和 SECTION）
    //
    // 实现：
    //   1. 在 [3] 后追加 Fly 容器节区（AppendNormalSection）
    //   2. 设置 m_pEndOfAutotext = Fly 容器的 EndNode
    //   3. 循环调用 InsertFlySection（在 m_pEndOfAutotext 之前插入）
    //      每个 Fly 子节区：Fly StartNode + (GRF_NODE 或 TEXT_NODE 或 Table) + Fly EndNode

    // 步骤 1-2：创建 Fly 容器节区
    SwStartNode* pFlyContainerStt = rNodes.AppendNormalSection();
    // 设置 Fly Container StartNode（用于按顺序追加 Fly）
    rNodes.SetFlyContainerStart(pFlyContainerStt);
    // pFlyContainerStt 的下一个节点是 EndNode（由 AppendNormalSection 创建）
    SwNode* pFlyContainerEnd = rNodes[pFlyContainerStt->GetIndex() + SwNodeOffset(1)];
    rNodes.SetEndOfAutotext(pFlyContainerEnd);

    // 收集 body 中按顺序的所有"Fly 节点"（每个 v:shape 或 drawing 创建一个 Fly）
    // 按文档顺序遍历：
    //   - w:p 中的每个 w:pict/v:shape 或 w:drawing → 图片/文本框 Fly
    //   - w:tbl → 表格 Fly
    int anchorCounter = 0;  // 简单地递增生成锚点索引
    for (auto child : bodyNode.children())
    {
        std::string name = child.name();
        if (name == "w:p")
        {
            if (ParagraphHasDrawing(child))
            {
                // 递归扫描段落内的所有绘制容器（w:drawing / w:pict）
                // 处理：w:p > w:r > (w:drawing|w:pict) 以及嵌套结构
                //   （如 w:hyperlink > w:r > w:drawing，或 mc:AlternateContent > mc:Choice > w:drawing）
                // 在 mc:AlternateContent 中，只处理 mc:Choice，忽略 mc:Fallback
                std::vector<pugi::xml_node> drawContainers;

                std::function<void(pugi::xml_node, bool)> scanAll = [&](pugi::xml_node n, bool insideChoice) {
                    for (auto& c : n.children())
                    {
                        std::string cn = c.name();
                        if (!insideChoice && cn.find("AlternateContent") != std::string::npos)
                        {
                            // mc:AlternateContent：只扫描 Choice，跳过 Fallback
                            for (auto& ac : c.children())
                            {
                                std::string acn = ac.name();
                                if (acn.find("Choice") != std::string::npos)
                                    scanAll(ac, true);  // 递归扫描 Choice 内部
                                // mc:Fallback 忽略
                            }
                        }
                        else if (cn == "w:drawing" || cn == "drawing" || cn == "w:pict" || cn == "pict")
                        {
                            drawContainers.push_back(c);
                        }
                        else
                        {
                            scanAll(c, insideChoice);  // 继续递归
                        }
                    }
                };
                scanAll(child, false);

                // 为每个容器内的内容形状创建一个 Fly。
                // 关键点：递归扫描容器内的所有形状元素（pic:pic、wps:wsp、v:shape），
                // 对每个形状检测：有 txbxContent/textbox → 文本框，有 pic:pic/imagedata → 图片，
                // 否则是装饰形状跳过。
                for (auto& container : drawContainers)
                {
                    // 递归收集容器内的所有形状元素
                    std::vector<pugi::xml_node> shapeNodes;

                    std::function<void(pugi::xml_node)> collectShapes = [&](pugi::xml_node n) {
                        for (auto& c : n.children())
                        {
                            std::string cn = c.name();
                            if (cn.find("pic:pic") != std::string::npos)
                            {
                                shapeNodes.push_back(c);
                            }
                            else if (cn.find("wps:wsp") != std::string::npos ||
                                     (cn == "wsp") || cn.find(":wsp") != std::string::npos)
                            {
                                shapeNodes.push_back(c);
                            }
                            else if (cn.find("v:shape") != std::string::npos ||
                                     (cn.find("shape") != std::string::npos && cn.size() >= 2 && cn.substr(0,2) == "v:"))
                            {
                                shapeNodes.push_back(c);
                            }
                            else
                            {
                                collectShapes(c);  // 递归进入非形状元素
                            }
                        }
                    };
                    collectShapes(container);

                    // 为每个形状创建 Fly 节点
                    for (auto& shape : shapeNodes)
                    {
                        // 检查形状是否是图片：
                        //   - 名称包含 "pic:pic" → DrawingML 图片
                        //   - 名称包含 "v:shape" 且子节点含 imagedata → VML 图片
                        //   - 内部含 pic:pic / imagedata 子元素
                        std::function<bool(pugi::xml_node)> checkPicture = [&](pugi::xml_node n) {
                            std::string nn = n.name();
                            if (nn.find("pic:pic") != std::string::npos)
                                return true;
                            for (auto& c : n.children())
                            {
                                std::string cn = c.name();
                                if (cn.find("pic:pic") != std::string::npos ||
                                    cn.find("imagedata") != std::string::npos ||
                                    cn.find("imageData") != std::string::npos)
                                    return true;
                                if (checkPicture(c)) return true;
                            }
                            return false;
                        };

                        // 检查形状是否包含文本框内容（txbxContent 内有 w:p 段落）
                        bool hasTxbxContent = false;
                        std::function<void(pugi::xml_node, bool&)> scanTxbx = [&](pugi::xml_node n, bool& inside) {
                            for (auto& c : n.children())
                            {
                                std::string cn = c.name();
                                if (cn.find("txbxContent") != std::string::npos ||
                                    cn.find("textbox") != std::string::npos)
                                    inside = true;
                                if (inside && cn == "w:p")
                                    hasTxbxContent = true;
                                scanTxbx(c, inside);
                            }
                        };
                        bool insideTmp = false;
                        scanTxbx(shape, insideTmp);

                        bool isTextbox = false;
                        bool isPicture = false;
                        if (hasTxbxContent)
                            isTextbox = true;
                        else if (checkPicture(shape))
                            isPicture = true;
                        else
                            continue;  // 装饰形状（线条、矩形等），跳过

                        // 创建 Fly
                        if (isTextbox)
                        {
                            SwStartNode* pFlyStt = rNodes.InsertFlySection(SwFlyStartNode, -1);
                            SwNode& flyAnchor = *pFlyStt;

                            std::vector<std::string> txbxText;
                            std::function<void(pugi::xml_node, bool)> extractTxbx = [&](pugi::xml_node n, bool insideTxbx) {
                                for (auto& c : n.children())
                                {
                                    std::string cn = c.name();
                                    if (cn.find("txbxContent") != std::string::npos || cn.find("textbox") != std::string::npos)
                                        insideTxbx = true;
                                    if (insideTxbx && cn == "w:p")
                                        txbxText.push_back("");
                                    if (insideTxbx && !txbxText.empty() && cn == "w:t")
                                        txbxText.back() += c.text().as_string();
                                    extractTxbx(c, insideTxbx);
                                }
                            };
                            extractTxbx(shape, false);

                            if (!txbxText.empty())
                            {
                                for (size_t i = txbxText.size(); i > 0; i--)
                                {
                                    SwTextNode* pTN = rNodes.MakeTextNode(flyAnchor, pDefaultColl);
                                    pTN->SetText(txbxText[i - 1]);
                                }
                            }
                            else
                            {
                                rNodes.InsertGrfNode(flyAnchor);
                            }
                            // 创建 Fly EndNode
                            rNodes.CloseFlySection(*pFlyStt);
                            m_pendingFlyAnchors_.push_back(pFlyStt);
                            anchorCounter++;
                        }
                        else if (isPicture)
                        {
                            SwStartNode* pFlyStt = rNodes.InsertFlySection(SwFlyStartNode, -1);
                            SwNode& flyAnchor = *pFlyStt;
                            rNodes.InsertGrfNode(flyAnchor);
                            // 创建 Fly EndNode
                            rNodes.CloseFlySection(*pFlyStt);
                            m_pendingFlyAnchors_.push_back(pFlyStt);
                            anchorCounter++;
                        }
                    }
                }
            }
        }
        else if (name == "w:tbl")
        {
            // 创建表格 Fly 节区
            SwStartNode* pFlyStt = rNodes.InsertFlySection(SwFlyStartNode, -1);

            // 计算行列
            int nRows = 0;
            int nCols = 0;
            for (auto tr : child.children("w:tr"))
            {
                nRows++;
                int tc = 0;
                for (auto cell : tr.children("w:tc"))
                    tc++;
                if (tc > nCols) nCols = tc;
            }
            if (nRows == 0 || nCols == 0)
                continue;

            // 先收集每个单元格的段落文本（按顺序）
            // 每格可能包含多个段落（每行一个 TEXT_NODE）
            std::vector<std::vector<std::string>> allCellParas;
            for (auto tr : child.children("w:tr"))
            {
                for (auto cell : tr.children("w:tc"))
                {
                    std::vector<std::string> paras;
                    for (auto p : cell.children("w:p"))
                    {
                        std::string paraText;
                        for (auto r : p.children("w:r"))
                        {
                            for (auto t : r.children("w:t"))
                                paraText += t.text().as_string();
                        }
                        paras.push_back(paraText);
                    }
                    allCellParas.push_back(paras);
                }
            }

            // 插入表格结构（每格一个 TEXT_NODE）
            SwTableNode* pTableNode = rNodes.InsertTable(*pFlyStt, nCols, pDefaultColl, nRows);

            if (pTableNode)
            {
                // 填充表格单元格内容
                SwNodeOffset cellStart = pTableNode->GetIndex() + 1;
                int totalCells = static_cast<int>(allCellParas.size());

                for (int ci = 0; ci < totalCells; ci++)
                {
                    const auto& paras = allCellParas[ci];
                    if (paras.empty())
                        continue;

                    // 每格初始结构：BoxStart + TEXT_NODE + BoxEnd = 3 nodes
                    SwNodeOffset boxStartIdx = cellStart + SwNodeOffset(ci * 3);
                    SwNodeOffset firstTextIdx = boxStartIdx + SwNodeOffset(1);

                    SwNode* pFirstText = rNodes[firstTextIdx];
                    if (pFirstText && pFirstText->IsTextNode())
                    {
                        static_cast<SwTextNode*>(pFirstText)->SetText(paras[0]);

                        // 在第一个 TEXT_NODE 之后插入额外的段落
                        SwNode* pAnchor = pFirstText;
                        for (size_t pi = 1; pi < paras.size(); pi++)
                        {
                            SwTextNode* pNewTN = rNodes.MakeTextNode(*pAnchor, pDefaultColl);
                            pNewTN->SetText(paras[pi]);
                        }
                    }
                }
            }

            // 创建 Fly EndNode
            rNodes.CloseFlySection(*pFlyStt);
            m_pendingFlyAnchors_.push_back(pFlyStt);
            anchorCounter++;
        }
    }

    // ── 阶段 2B/2C/2D：创建空节区 + 正文容器节区 ─────────────
    //
    // LO 结构：
    //   Fly 容器节区（结束）
    //   空 Normal 节区 (2 个节点)
    //   正文容器节区 (包含 TEXT_NODE + SECTION)
    //
    // 实现：
    //   1. 在 Fly 容器后追加空 Normal 节区
    //   2. 设置 m_pEndOfRedlines = 空节区的 EndNode
    //   3. 追加正文容器节区
    //   4. 设置 m_pEndOfContent = 正文容器的 EndNode
    //   5. 在正文容器的 StartNode 之后插入正文内容

    // 步骤 1-2：追加空 Normal 节区
    SwStartNode* pEmptyStt = rNodes.AppendNormalSection();
    SwNode* pEmptyEnd = rNodes[pEmptyStt->GetIndex() + SwNodeOffset(1)];
    rNodes.SetEndOfRedlines(pEmptyEnd);

    // 步骤 3-4：追加正文容器节区
    SwStartNode* pBodyStt = rNodes.AppendNormalSection();
    SwNode* pBodyEnd = rNodes[pBodyStt->GetIndex() + SwNodeOffset(1)];
    rNodes.SetEndOfContent(pBodyEnd);

    // 记录创建的 body TEXT_NODE 的索引，用于后面设置 Fly anchor
    std::vector<SwNodeOffset> bodyTextNodeIndices;

    // 遍历收集的正文段落，为每个段落创建一个 TEXT_NODE
    // 当遇到 sectPr 段落时：插入 SectionNode(SECTION_START) + 后续 TEXT_NODE + SectionNode 结束
    bool insideSection = false;
    SwSectionNode* pCurrentSection = nullptr;

    // 正文插入位置：在正文容器的 StartNode 之后插入
    SwNode* pLastNode = pBodyStt;

    for (size_t i = 0; i < bodyParagraphs.size(); ++i)
    {
        const auto& info = bodyParagraphs[i];

        // 如果此段落有 sectPr：视为节分界
        if (info.hasSection)
        {
            // 关闭之前的 section
            if (insideSection && pCurrentSection)
            {
                // 为当前 section 插入 EndNode
                SwEndNode* pSectionEnd = rNodes.MakeEndNode(*pLastNode, *pCurrentSection);
                pLastNode = pSectionEnd;
                insideSection = false;
                pCurrentSection = nullptr;
            }

            // 开启一个新的 section（SectionNode）
            SwSectionNode* pSect = rNodes.MakeSectionNode(*pLastNode);
            pLastNode = pSect;
            insideSection = true;
            pCurrentSection = pSect;
            continue;  // sectPr 段落本身不生成 TEXT_NODE
        }

        // 普通段落：创建 TEXT_NODE
        SwTextNode* pTN = rNodes.MakeTextNode(*pLastNode, pDefaultColl);
        pLastNode = pTN;
        pTN->SetText(info.text);
        if (!info.styleName.empty())
            pTN->SetStyleName(info.styleName);

        bodyTextNodeIndices.push_back(pTN->GetIndex());
    }

    // 关闭最后一个未结束的 section
    if (insideSection && pCurrentSection)
    {
        SwEndNode* pSectionEnd = rNodes.MakeEndNode(*pLastNode, *pCurrentSection);
        pLastNode = pSectionEnd;
    }

    // ── 阶段 2E：回填 Fly anchor ───────────────────────────────────
    // 将 Fly 节区按顺序锚定到 body 段落中的不同节点。
    if (!bodyTextNodeIndices.empty() && !m_pendingFlyAnchors_.empty())
    {
        size_t numNodes = bodyTextNodeIndices.size();
        for (size_t k = 0; k < m_pendingFlyAnchors_.size(); ++k)
        {
            SwStartNode* pFlyStt = m_pendingFlyAnchors_[k];
            size_t anchorIdx = std::min(k, numNodes - 1);
            pFlyStt->SetAnchorNodeIndex(static_cast<int>(bodyTextNodeIndices[anchorIdx]));
        }
    }

    // 最终 m_pEndOfContent 仍指向数组末尾的 EndNode（为 doc 级别的 sentinel）
    // walker 会遍历到它（因为 walker 的 bodyEnd = GetEndOfContent().GetIndex()+1）
    // 但对于 LO 输出的目标：索引 [211] 是 END_NODE（正好是最后一个节点），
    // 我们的结构与此一致。
    //
    // walker 逻辑：for (int i = bodyStart; i < bodyEnd; ++i)
    //   GetBodyEndIndex() 返回 m_pEndOfContent 的索引（设为 E），
    //   即 walker 遍历 [0, E)，注意不包括 E。
    // 而我们希望 walker 处理到 [E]（因为 E 是正文节区的 EndNode），
    // 所以需要让 walker 的 bodyEnd = E + 1。
    //
    // 在 nodes_log.cpp 中 GetBodyEndIndex 已经返回 rEndOfContent.GetIndex()，
    // 需要让它返回 Count() 以便遍历所有节点（含最后一个 EndNode）。
    (void)bodyNode;  // bodyNode 已在上面遍历过，这里保留参数以匹配旧签名
}

// ParseParagraph: 解析段落
//===----------------------------------------------------------------------===//

void DocxParser::ParseParagraph(pugi::xml_node pNode, SwDoc& doc)
{
    SwNodes& rNodes = doc.GetNodes();
    SwTextFormatColl* pColl = doc.GetDefaultTextFormatColl();

    // 获取最后一个节点（在它之后插入）
    SwNode& rLastNode = rNodes.GetEndOfContent();
    // 往前找到最后一个内容节点或 StartNode（但不在表格内部）
    SwNode* pInsertAfter = nullptr;
    bool bInsideTable = false;
    SwNodeOffset nIdx = rLastNode.GetIndex() - 1;
    while (nIdx >= 0)
    {
        SwNode* pNd = rNodes[nIdx];
        if (pNd && (pNd->IsContentNode() || pNd->IsStartNode()))
        {
            // 如果找到的节点在表格内部，使用表格 EndNode 作为插入点
            // 后续需要修正新节点的 StartOfSection
            // 使用 FindTableNode() 而非直接检查 StartOfSectionNode()，
            // 因为表格内部的节点其 StartOfSectionNode() 是单元格起始节点，不是表格节点
            SwTableNode* pTableNode = pNd->FindTableNode();
            if (pTableNode)
            {
                SwEndNode* pTableEnd = pTableNode->GetEndOfSection();
                if (pTableEnd)
                {
                    pInsertAfter = pTableEnd;
                    bInsideTable = true;
                }
                else
                {
                    pInsertAfter = pTableNode;
                    bInsideTable = true;
                }
            }
            else
            {
                pInsertAfter = pNd;
            }
            break;
        }
        --nIdx;
    }
    if (!pInsertAfter)
    {
        // 如果没有找到，在 EndOfContent 的 StartNode 之后插入
        pInsertAfter = rLastNode.StartOfSectionNode();
    }

    std::cerr << "[ParseParagraph] InsertAfter idx=" << pInsertAfter->GetIndex()
              << " bInsideTable=" << (bInsideTable ? "yes" : "no") << " type="
              << (pInsertAfter->IsContentNode()
                      ? "content"
                      : pInsertAfter->IsStartNode() ? "start"
                                                    : pInsertAfter->IsEndNode() ? "end" : "other")
              << std::endl;

    // 创建文本节点
    SwTextNode* pTextNode = rNodes.MakeTextNode(*pInsertAfter, pColl);

    // 修正：如果插入点在表格内部，新节点会继承表格的 StartOfSection
    // 需要将其修正为正文节区（EndOfContent 的 StartOfSection）
    if (bInsideTable)
    {
        pTextNode->SetStartOfSection(rLastNode.StartOfSectionNode());
    }

    // 解析段落属性
    std::string sStyleId;
    auto pPr = pNode.child("w:pPr");
    if (pPr)
    {
        ParseParagraphProps(pPr, pTextNode);
        auto pStyle = pPr.child("w:pStyle");
        if (pStyle)
            sStyleId = pStyle.attribute("w:val").as_string();

        // 段落级 w:rPr 是段落标记的字符格式，先全部应用到节点
        // 后续根据是否有文本内容决定是否用样式覆盖
        auto pPrRPr = pPr.child("w:rPr");
        if (pPrRPr)
        {
            ParseRunProps(pPrRPr, pTextNode); // 不跳过任何属性

            // 将段落标记的字体/字号保存到专用属性（用于行高计算）
            // 对应 LO 中段落标记的 ListAutoFormat 属性
            const std::string* pFont = pTextNode->GetAttr(RES_CHRATR_FONT);
            if (pFont && !pFont->empty())
                pTextNode->SetAttr(RES_CHRATR_FONT_PARA_MARK, *pFont);
            const std::string* pSize = pTextNode->GetAttr(RES_CHRATR_FONTSIZE);
            if (pSize && !pSize->empty())
                pTextNode->SetAttr(RES_CHRATR_FONTSIZE_PARA_MARK, *pSize);
        }
    }

    // 查找样式定义（稍后根据文本内容决定是否覆盖）
    const StyleDef* pStyleDef = nullptr;
    if (!sStyleId.empty())
    {
        auto it = styles_.find(sStyleId);
        if (it != styles_.end())
            pStyleDef = &it->second;
        else
            std::cerr << "[DEBUG] Style not found: styleId=" << sStyleId
                      << " mapSize=" << styles_.size() << std::endl;
    }
    if (!pStyleDef)
    {
        int nDefaultCount = 0;
        for (auto & [ id, def ] : styles_)
        {
            if (def.type == "paragraph" && def.isDefault)
            {
                pStyleDef = &def;
                nDefaultCount++;
                std::cerr << "[DEBUG] Fallback default style: id=" << id << " name=" << def.name
                          << std::endl;
            }
        }
        std::cerr << "[DEBUG] sStyleId=" << sStyleId << " foundStyle=" << (pStyleDef ? "yes" : "NO")
                  << " nDefaultCount=" << nDefaultCount << std::endl;
    }
    else
    {
        std::cerr << "[DEBUG] Found style: styleId=" << sStyleId << " name=" << pStyleDef->name
                  << " fontName=" << pStyleDef->fontName << " fontSize=" << pStyleDef->fontSize
                  << " basedOn=" << pStyleDef->basedOn << std::endl;
    }

    // 解析文本内容和 Run 属性
    std::string text;
    bool bRunPropsApplied = false;
    bool bTextRunFound = false;
    for (auto child : pNode.children())
    {
        std::string name = child.name();

        if (name == "w:r")
        {
            std::string runText = ParseRunText(child);
            auto rPr = child.child("w:rPr");

            if (!runText.empty())
            {
                // 文本 Run：不应用其属性到段落级别的 SwAttrSet
                bTextRunFound = true;
            }
            else if (rPr && !bRunPropsApplied && !pPr.child("w:rPr"))
            {
                // 绘图/图片 Run：仅在段落标记没有 rPr 时应用
                ParseRunProps(rPr, pTextNode);
                bRunPropsApplied = true;
            }
            // 检查图片尺寸 (w:drawing/wp:extent)
            {
                auto drawing = child.child("w:drawing");
                if (drawing)
                {
                    for (auto& elem : drawing.children())
                    {
                        for (auto& sub : elem.children())
                        {
                            std::string subName = sub.name();
                            if (subName.find("extent") != std::string::npos)
                            {
                                long long cy = sub.attribute("cy").as_llong(0);
                                if (cy > 0)
                                {
                                    SwTwips nImgHeight = static_cast<SwTwips>(cy * 1440 / 914400);
                                    pTextNode->SetAttr(RES_IMAGE_HEIGHT,
                                                       std::to_string(nImgHeight));
                                }
                            }
                        }
                    }
                }
            }
            text += runText;
        }
        else if (name == "w:hyperlink")
        {
            for (auto r : child.children("w:r"))
            {
                std::string runText = ParseRunText(r);
                auto rPr = r.child("w:rPr");
                if (!runText.empty())
                    bTextRunFound = true;
                else if (rPr && !bRunPropsApplied && !pPr.child("w:rPr"))
                {
                    ParseRunProps(rPr, pTextNode);
                    bRunPropsApplied = true;
                }
                text += runText;
            }
        }
    }

    // 从段落样式继承字体属性
    // LO 行为：有文本的段落使用样式链字体，空段落使用 pPr/rPr 字体
    if (pStyleDef)
    {
        pTextNode->SetStyleName(pStyleDef->name);

        if (bTextRunFound)
        {
            // 有文本的段落：样式属性覆盖 pPr/rPr（段落标记格式不应用于文本内容）
            if (!pStyleDef->fontName.empty())
                pTextNode->SetAttr(RES_CHRATR_FONT, pStyleDef->fontName);
            if (pStyleDef->fontSize > 0)
                pTextNode->SetAttr(RES_CHRATR_FONTSIZE, std::to_string(pStyleDef->fontSize));
            if (pStyleDef->bold)
                pTextNode->SetAttr(RES_CHRATR_WEIGHT, "bold");
            if (pStyleDef->italic)
                pTextNode->SetAttr(RES_CHRATR_POSTURE, "italic");
            if (!pStyleDef->color.empty())
                pTextNode->SetAttr(RES_CHRATR_COLOR, pStyleDef->color);
        }
        else
        {
            // 空段落：保留 pPr/rPr 属性，仅用样式作为后备
            if (!pStyleDef->fontName.empty() && !pTextNode->GetAttr(RES_CHRATR_FONT))
                pTextNode->SetAttr(RES_CHRATR_FONT, pStyleDef->fontName);
            if (pStyleDef->fontSize > 0 && !pTextNode->GetAttr(RES_CHRATR_FONTSIZE))
                pTextNode->SetAttr(RES_CHRATR_FONTSIZE, std::to_string(pStyleDef->fontSize));
            if (pStyleDef->bold && !pTextNode->GetAttr(RES_CHRATR_WEIGHT))
                pTextNode->SetAttr(RES_CHRATR_WEIGHT, "bold");
            if (pStyleDef->italic && !pTextNode->GetAttr(RES_CHRATR_POSTURE))
                pTextNode->SetAttr(RES_CHRATR_POSTURE, "italic");
            if (!pStyleDef->color.empty() && !pTextNode->GetAttr(RES_CHRATR_COLOR))
                pTextNode->SetAttr(RES_CHRATR_COLOR, pStyleDef->color);
        }

        // 从样式继承段落间距（如果段落自身没有设置）
        // LO 的样式级段落间距会被继承到各段落
        if (pStyleDef->spacingBefore != 0 && !pTextNode->GetAttr(RES_UL_SPACE))
            pTextNode->SetAttr(RES_UL_SPACE, std::to_string(pStyleDef->spacingBefore));
        if (pStyleDef->spacingAfter != 0 && !pTextNode->GetAttr(RES_UL_SPACE_AFTER))
            pTextNode->SetAttr(RES_UL_SPACE_AFTER, std::to_string(pStyleDef->spacingAfter));
        if (pStyleDef->spacingLine != 240 && !pTextNode->GetAttr(RES_PARATR_LINESPACING))
            pTextNode->SetAttr(RES_PARATR_LINESPACING, std::to_string(pStyleDef->spacingLine));
    }

    pTextNode->SetText(text);

    // 提取文本框内容 (w:txbxContent inside w:drawing)
    // 文本框中的段落是独立的文本节点，需要单独创建
    // 注意：w:drawing 可能在 mc:AlternateContent/mc:Choice 中嵌套，需要递归查找

    // 递归查找 w:txbxContent（支持 w:txbxContent, wps:txbxContent, txbxContent）
    std::function<void(pugi::xml_node)> findTxbxContent = [&](pugi::xml_node node) {
        for (auto& n : node.children())
        {
            std::string nName = n.name();
            if (nName == "w:txbxContent" || nName == "wps:txbxContent" || nName == "txbxContent")
            {
                // 文本框（w:txbxContent）属于浮动对象
                // 创建 Fly 节区，锚点为当前段落节点
                int anchorIdx = static_cast<int>(pTextNode->GetIndex());
                SwStartNode* pFlyStart = rNodes.InsertFlySection(SwFlyStartNode, anchorIdx);
                std::cerr << "[ParseTxbx] Found txbxContent, creating Fly section at index "
                          << pFlyStart->GetIndex() << " with anchor=" << anchorIdx << std::endl;

                // 解析文本框内容并添加到 Fly 节区
                // TODO: 解析 txbxContent 内的段落并插入到 Fly 节区
                return;
            }
            findTxbxContent(n);
        }
    };

    // 处理单个 w:drawing 节点
    std::function<void(pugi::xml_node)> processDrawing = [&](pugi::xml_node drawing) {
        for (auto& anchor : drawing.children())
        {
            std::string anchorName = anchor.name();
            // wp:anchor 或 wp:inline
            if (anchorName != "wp:anchor" && anchorName != "wp:inline" && anchorName != "anchor"
                && anchorName != "inline")
                continue;

            // 创建 Fly 节区，锚点为当前段落节点
            int anchorIdx = static_cast<int>(pTextNode->GetIndex());
            SwStartNode* pFlyStart = rNodes.InsertFlySection(SwFlyStartNode, anchorIdx);
            std::cerr << "[ParseDrawing] Found " << anchorName << ", creating Fly section at index "
                      << pFlyStart->GetIndex() << " with anchor=" << anchorIdx << std::endl;

            for (auto& graphic : anchor.children())
            {
                if (std::string(graphic.name()) != "a:graphic"
                    && std::string(graphic.name()) != "graphic")
                    continue;
                auto graphicData = graphic.child("a:graphicData");
                if (!graphicData)
                    graphicData = graphic.child("graphicData");
                if (!graphicData)
                    continue;

                for (auto& gdChild : graphicData.children())
                {
                    // 检查是否是图片
                    std::string gdName = gdChild.name();
                    if (gdName == "pic:pic" || gdName == "pic")
                    {
                        // 插入图片节点到 Fly 节区
                        SwNode& rFlyStartNode = *pFlyStart;
                        SwGrfNode* pGrfNode = rNodes.InsertGrfNode(rFlyStartNode);
                        std::cerr << "[ParseDrawing] Inserted GrfNode at index "
                                  << pGrfNode->GetIndex() << std::endl;
                    }
                    // 检查是否是文本框
                    else
                    {
                        findTxbxContent(gdChild);
                    }
                }
            }
        }
    };

    // 递归在 w:r 的子节点中查找 w:drawing
    // w:drawing 可能嵌套在 mc:AlternateContent/mc:Choice 中
    std::function<void(pugi::xml_node)> findDrawingInRun = [&](pugi::xml_node node) {
        for (auto& child : node.children())
        {
            std::string cname = child.name();
            if (cname == "w:drawing" || cname == "drawing")
            {
                std::cerr << "[ParseTxbx] Found drawing in run" << std::endl;
                processDrawing(child);
            }
            else if (cname == "mc:AlternateContent" || cname == "mc:Choice"
                     || cname == "mc:Fallback")
            {
                findDrawingInRun(child);
            }
        }
    };

    for (auto child : pNode.children())
    {
        if (std::string(child.name()) != "w:r")
            continue;
        findDrawingInRun(child);
    }
}

//===----------------------------------------------------------------------===//
// ParseRunText: 解析文本 Run
//===----------------------------------------------------------------------===//

std::string DocxParser::ParseRunText(pugi::xml_node rNode)
{
    std::string text;

    for (auto child : rNode.children())
    {
        std::string name = child.name();

        if (name == "w:t")
        {
            text += child.text().as_string();
        }
        else if (name == "w:tab")
        {
            text += "\t";
        }
        else if (name == "w:br")
        {
            std::string brType = child.attribute("w:type").as_string();
            if (brType == "page")
            {
                text += "\f";
            }
            else if (brType == "column")
            {
                text += "\v";
            }
            else
            {
                text += "\n";
            }
        }
        else if (name == "w:cr")
        {
            text += "\n";
        }
        else if (name == "w:sym")
        {
            text += "?"; // 简化处理
        }
    }

    return text;
}

//===----------------------------------------------------------------------===//
// ParseParagraphProps: 解析段落属性
//===----------------------------------------------------------------------===//

void DocxParser::ParseParagraphProps(pugi::xml_node pPrNode, SwTextNode* pNode)
{
    // 样式引用
    auto pStyle = pPrNode.child("w:pStyle");
    if (pStyle)
    {
        std::string styleId = pStyle.attribute("w:val").as_string();
        // 查找样式的显示名（而非 ID）
        // 例如：styleId="Normal" → 显示名 "Default Paragraph Style"
        // 这里先设置 ID，ParseParagraph 中会用显示名覆盖
        pNode->SetStyleName(styleId);
    }

    // 对齐
    auto jc = pPrNode.child("w:jc");
    if (jc)
    {
        std::string align = jc.attribute("w:val").as_string();
        pNode->SetAttr(RES_PARATR_ADJUST, align);
    }

    // 间距
    // 对应 LO 的 DomainMapper.cxx: lcl_attribute() 处理 CT_Spacing
    auto spacing = pPrNode.child("w:spacing");
    if (spacing)
    {
        int line = spacing.attribute("w:line").as_int(240);
        // 只在行间距不是默认值 240 时才设置，否则让样式继承链处理
        // 否则会阻止样式中的行间距被继承
        if (line != 240)
            pNode->SetAttr(RES_PARATR_LINESPACING, std::to_string(line));

        // before / beforeLines / after / afterLines
        // LO: before 优先于 beforeLines，after 优先于 afterLines
        // LO: beforeLines * nSingleLineSpacing(240) / 100 = twips
        // LO: afterLines * nSingleLineSpacing(240) / 100 = twips
        pugi::xml_attribute attrBefore = spacing.attribute("w:before");
        pugi::xml_attribute attrBeforeLines = spacing.attribute("w:beforeLines");
        pugi::xml_attribute attrAfter = spacing.attribute("w:after");
        pugi::xml_attribute attrAfterLines = spacing.attribute("w:afterLines");

        int nBefore = 0;
        if (attrBefore)
            nBefore = attrBefore.as_int(0);
        else if (attrBeforeLines)
            nBefore = attrBeforeLines.as_int(0) * 240 / 100;

        int nAfter = 0;
        if (attrAfter)
            nAfter = attrAfter.as_int(0);
        else if (attrAfterLines)
            nAfter = attrAfterLines.as_int(0) * 240 / 100;

        if (nBefore != 0)
            pNode->SetAttr(RES_UL_SPACE, std::to_string(nBefore));
        if (nAfter != 0)
            pNode->SetAttr(RES_UL_SPACE_AFTER, std::to_string(nAfter));
    }

    // 缩进
    auto ind = pPrNode.child("w:ind");
    if (ind)
    {
        int left = ind.attribute("w:left").as_int(0);
        int right = ind.attribute("w:right").as_int(0);
        int firstLine = ind.attribute("w:firstLine").as_int(0);
        // 存储左缩进
        pNode->SetAttr(RES_PARATR_INDENT, std::to_string(left));
    }

    // 分页（w:val="0" 表示不分页，缺少 w:val 或 w:val="1" 表示分页）
    auto pageBreakBefore = pPrNode.child("w:pageBreakBefore");
    if (pageBreakBefore)
    {
        std::string val = pageBreakBefore.attribute("w:val").as_string("1");
        if (val != "0" && val != "false")
        {
            pNode->SetAttr(RES_BREAK, "page");
        }
    }

    // 与下段同页
    auto keepNext = pPrNode.child("w:keepNext");
    if (keepNext)
    {
        pNode->SetAttr(RES_PARATR_SPLIT, "keepNext");
    }

    // 编号
    auto numPr = pPrNode.child("w:numPr");
    if (numPr)
    {
        auto numId = numPr.child("w:numId");
        auto ilvl = numPr.child("w:ilvl");
        if (numId && ilvl)
        {
            // 存储编号属性
        }
    }

    // 节属性（w:sectPr 在 w:pPr 内定义节的页面设置）
    auto sectPr = pPrNode.child("w:sectPr");
    if (sectPr)
    {
        // OOXML: w:type 在 w:sectPr 中指定该节的起始分隔类型
        // 所以节 N→N+1 的分隔类型应来自节 N+1 的 sectPr，而非节 N 的 sectPr
        // 例如：sectPr_2 在节 2 的最后一个段落中，其 w:type 指定节 2 的起始类型
        // 但节 2→3 的分隔类型应由 sectPr_3 的 w:type 指定（节 3 的起始类型）
        int nextSection = m_nCurrentSection_ + 1;
        auto it = sectionBreakTypes_.find(nextSection);
        std::string breakType = (it != sectionBreakTypes_.end()) ? it->second : "nextPage";
        std::cerr << "[Parser] sectPr currentSection=" << m_nCurrentSection_
                  << " nextSection=" << nextSection << " breakType=" << breakType << std::endl;
        m_nCurrentSection_++;
        if (breakType == "nextPage" || breakType == "evenPage" || breakType == "oddPage")
        {
            // 节分隔 = 分页：在当前段落之前分页
            pNode->SetAttr(RES_BREAK, "section");
        }
        else if (breakType == "continuous")
        {
            // 连续节分隔：不分页，但需要更新节属性（列布局等）
            pNode->SetAttr(RES_BREAK, "continuous");
        }

        // 更新默认页面描述符
        SwPageDesc* pDesc = pNode->GetDoc().GetDefaultPageDesc();
        if (pDesc)
        {
            // 页面尺寸
            auto pgSz = sectPr.child("w:pgSz");
            if (pgSz)
            {
                int w = pgSz.attribute("w:w").as_int(11906);
                int h = pgSz.attribute("w:h").as_int(16838);
                pDesc->SetPageWidth(w);
                pDesc->SetPageHeight(h);
            }

            // 页面边距
            auto pgMar = sectPr.child("w:pgMar");
            if (pgMar)
            {
                pDesc->SetTopMargin(pgMar.attribute("w:top").as_int(1440));
                pDesc->SetBottomMargin(pgMar.attribute("w:bottom").as_int(1440));
                pDesc->SetLeftMargin(pgMar.attribute("w:left").as_int(1800));
                pDesc->SetRightMargin(pgMar.attribute("w:right").as_int(1800));
                pDesc->SetHeaderMargin(pgMar.attribute("w:header").as_int(720));
                pDesc->SetFooterMargin(pgMar.attribute("w:footer").as_int(720));
            }
        }
    }
}

//===----------------------------------------------------------------------===//
// ParseTable: 解析表格
//===----------------------------------------------------------------------===//

void DocxParser::ParseTable(pugi::xml_node tblNode, SwDoc& doc)
{
    // 以 LO 的 nodes 结构为标准：生成 SwTableNode 及嵌套的单元格结构
    // LO 表格节点结构：
    //   SwTableNode
    //     SwStartNode (TableBox) -> Cell 1
    //       SwTextNode
    //       SwEndNode
    //     SwStartNode (TableBox) -> Cell 2
    //       SwTextNode
    //       SwEndNode
    //     ...
    //   SwEndNode (Table)

    SwNodes& rNodes = doc.GetNodes();
    SwNode& rLastNode = rNodes.GetEndOfContent();

    // 找到插入点（在最后一个节点之后）
    SwNode* pInsertAfter = nullptr;
    SwNodeOffset nIdx = rLastNode.GetIndex() - 1;
    while (nIdx >= 0)
    {
        SwNode* pNd = rNodes[nIdx];
        if (pNd && (pNd->IsContentNode() || pNd->IsStartNode()))
        {
            // 如果找到的节点在表格内部，跳到表格 EndNode
            SwTableNode* pTableNode = pNd->FindTableNode();
            if (pTableNode)
            {
                SwEndNode* pTableEnd = pTableNode->GetEndOfSection();
                if (pTableEnd)
                {
                    pInsertAfter = pTableEnd;
                }
                else
                {
                    pInsertAfter = pTableNode;
                }
            }
            else
            {
                pInsertAfter = pNd;
            }
            break;
        }
        --nIdx;
    }
    if (!pInsertAfter)
    {
        pInsertAfter = rLastNode.StartOfSectionNode();
    }

    // 统计行数和列数
    int nRows = 0;
    int nCols = 0;
    for (auto row : tblNode.children("w:tr"))
    {
        nRows++;
        int rowCols = 0;
        for (auto cell : row.children("w:tc"))
        {
            rowCols++;
        }
        if (rowCols > nCols)
            nCols = rowCols;
    }

    if (nRows == 0 || nCols == 0)
        return;

    // 创建表格节点（使用 SwNodes::InsertTable）
    SwTextFormatColl* pColl = doc.GetDefaultTextFormatColl();
    SwTableNode* pTableNode = rNodes.InsertTable(*pInsertAfter, nCols, pColl, nRows);

    // 解析表格内容到单元格
    // 遍历表格节点内部的单元格
    SwNodeOffset nTableIdx = pTableNode->GetIndex();
    SwNodeOffset nCellIdx = nTableIdx + 1;

    for (auto row : tblNode.children("w:tr"))
    {
        for (auto cell : row.children("w:tc"))
        {
            // 找到当前单元格的 StartNode
            SwNode* pCellStart = rNodes[nCellIdx];
            if (!pCellStart || !pCellStart->IsStartNode())
                continue;

            // 找到单元格内的 TextNode（在 StartNode 之后）
            SwNodeOffset nTextIdx = nCellIdx + 1;
            SwNode* pTextNode = rNodes[nTextIdx];
            if (!pTextNode || !pTextNode->IsTextNode())
                continue;

            SwTextNode* pCellText = static_cast<SwTextNode*>(pTextNode);

            // 解析单元格中的段落内容
            // 简化处理：只取第一个段落的文本
            std::string cellText;
            for (auto p : cell.children("w:p"))
            {
                std::string paraText;
                for (auto r : p.children("w:r"))
                {
                    for (auto t : r.children("w:t"))
                    {
                        paraText += t.text().as_string();
                    }
                }
                if (!paraText.empty())
                {
                    if (!cellText.empty())
                        cellText += "\n";
                    cellText += paraText;
                }
            }
            pCellText->SetText(cellText);

            // 跳到下一个单元格（StartNode + TextNode + EndNode = 3 个节点）
            nCellIdx += 3;
        }
    }
}

//===----------------------------------------------------------------------===//
// ParseSdt: 解析结构化文档标签
//===----------------------------------------------------------------------===//

void DocxParser::ParseSdt(pugi::xml_node sdtNode, SwDoc& doc)
{
    // 以 LO 的 nodes 结构为标准：SDT 内容需要创建嵌套的 START_NODE/END_NODE 对
    // LO 中 SDT 会生成一个 SwStartNode 和对应的 SwEndNode，内容嵌套在其中

    SwNodes& rNodes = doc.GetNodes();
    SwNode& rLastNode = rNodes.GetEndOfContent();

    // 找到插入点（在最后一个节点之后）
    SwNode* pInsertAfter = nullptr;
    SwNodeOffset nIdx = rLastNode.GetIndex() - 1;
    while (nIdx >= 0)
    {
        SwNode* pNd = rNodes[nIdx];
        if (pNd && (pNd->IsContentNode() || pNd->IsStartNode()))
        {
            // 如果找到的节点在表格内部，跳到表格 EndNode
            SwTableNode* pTableNode = pNd->FindTableNode();
            if (pTableNode)
            {
                SwEndNode* pTableEnd = pTableNode->GetEndOfSection();
                if (pTableEnd)
                {
                    pInsertAfter = pTableEnd;
                }
                else
                {
                    pInsertAfter = pTableNode;
                }
            }
            else
            {
                pInsertAfter = pNd;
            }
            break;
        }
        --nIdx;
    }
    if (!pInsertAfter)
    {
        pInsertAfter = rLastNode.StartOfSectionNode();
    }

    // 创建 SDT 对应的 StartNode（使用 SwNormalStartNode）
    SwStartNode* pSdtStart = rNodes.MakeTextSection(*pInsertAfter, SwNormalStartNode);

    // 解析 SDT 内容
    auto sdtContent = sdtNode.child("w:sdtContent");
    if (sdtContent)
    {
        // 获取 SDT 的 EndNode（MakeTextSection 创建的）
        SwEndNode* pSdtEnd = pSdtStart->GetEndOfSection();
        SwNodeOffset nSdtEndIdx = pSdtEnd->GetIndex();

        // 解析 SDT 内容
        // 注意：ParseBody/ParseParagraph 会在 EndOfContent 之前插入节点
        // 我们需要将解析的内容移动到 SDT 节区内部

        // 记录解析前的节点数量
        SwNodeOffset nBeforeCount = rNodes.Count();

        // 解析 SDT 内容
        ParseBody(sdtContent, doc);

        // 记录解析后的节点数量
        SwNodeOffset nAfterCount = rNodes.Count();
        SwNodeOffset nNewNodes = nAfterCount - nBeforeCount;

        // 如果有新节点被插入，需要将它们移动到 SDT 节区内部
        if (nNewNodes > 0)
        {
            // 新节点被插入在 EndOfContent 之前
            // 我们需要将它们移动到 SDT StartNode 之后、SDT EndNode 之前

            // 简化处理：直接在 SDT 节区内重新创建节点
            // 遍历新插入的节点，复制其内容到 SDT 内部

            // 由于节点移动复杂，采用另一种策略：
            // 将 SDT EndNode 移动到最后一个新节点之后

            // 实际上，更简单的方法是：让 ParseBody 在 SDT EndNode 之前插入
            // 但当前 ParseBody 的逻辑是固定在 EndOfContent 之前插入

            // 修正：将新插入节点的 StartOfSection 设置为 SDT StartNode
            // 并将 SDT EndNode 移动到最后一个新节点之后

            SwNodeOffset nEndOfContentIdx = rLastNode.GetIndex();
            SwNodeOffset nFirstNewIdx = nEndOfContentIdx - nNewNodes;

            for (SwNodeOffset i = nFirstNewIdx; i < nEndOfContentIdx; ++i)
            {
                SwNode* pNd = rNodes[i];
                if (pNd && pNd->IsContentNode())
                {
                    pNd->SetStartOfSection(pSdtStart);
                }
            }

            // 注意：这里不移动 EndNode，因为节点物理位置不变
            // 只是逻辑上属于 SDT 节区（通过 StartOfSection 标识）
        }
    }
}

//===----------------------------------------------------------------------===//
// ParseSectionProps: 解析节属性
//===----------------------------------------------------------------------===//

void DocxParser::ParseSectionProps(pugi::xml_node sectPrNode, SwDoc& doc)
{
    // 更新默认页面描述符（而非创建新的）
    SwPageDesc* pDesc = doc.GetDefaultPageDesc();
    if (!pDesc)
        return;

    // 页面尺寸
    auto pgSz = sectPrNode.child("w:pgSz");
    if (pgSz)
    {
        int w = pgSz.attribute("w:w").as_int(11906);
        int h = pgSz.attribute("w:h").as_int(16838);
        pDesc->SetPageWidth(w);
        pDesc->SetPageHeight(h);

        // 横向
        std::string orient = pgSz.attribute("w:orient").as_string();
        if (orient == "landscape")
        {
            pDesc->SetLandscape(true);
        }
    }

    // 页面边距
    auto pgMar = sectPrNode.child("w:pgMar");
    if (pgMar)
    {
        pDesc->SetTopMargin(pgMar.attribute("w:top").as_int(1440));
        pDesc->SetBottomMargin(pgMar.attribute("w:bottom").as_int(1440));
        pDesc->SetLeftMargin(pgMar.attribute("w:left").as_int(1800));
        pDesc->SetRightMargin(pgMar.attribute("w:right").as_int(1800));
        pDesc->SetHeaderMargin(pgMar.attribute("w:header").as_int(720));
        pDesc->SetFooterMargin(pgMar.attribute("w:footer").as_int(720));
    }

    // DEBUG
    std::cout << "[ParseSectionProps] PageDesc: w=" << pDesc->GetPageWidth()
              << " h=" << pDesc->GetPageHeight() << " L=" << pDesc->GetLeftMargin()
              << " R=" << pDesc->GetRightMargin() << " T=" << pDesc->GetTopMargin()
              << " B=" << pDesc->GetBottomMargin() << std::endl;
}

//===----------------------------------------------------------------------===//
// ParseHeaderFooter: 解析页眉页脚
//===----------------------------------------------------------------------===//

void DocxParser::ParseHeaderFooter(const std::string& xml, SwDoc& doc)
{
    pugi::xml_document xmlDoc;
    if (!xmlDoc.load_string(xml.c_str()))
        return;

    auto root = xmlDoc.first_child();
    for (auto p : root.children("w:p"))
    {
        // 解析页眉/页脚段落
        // 简化实现：暂不处理
    }
}

//===----------------------------------------------------------------------===//
// GetAttr: 获取属性值
//===----------------------------------------------------------------------===//

std::string DocxParser::GetAttr(pugi::xml_node node, const char* name, const char* ns)
{
    if (ns)
    {
        return node.attribute((std::string(ns) + ":" + name).c_str()).as_string();
    }
    return node.attribute(name).as_string();
}

int DocxParser::GetAttrInt(pugi::xml_node node, const char* name, int def)
{
    return node.attribute(name).as_int(def);
}

bool DocxParser::GetAttrBool(pugi::xml_node node, const char* name, bool def)
{
    return node.attribute(name).as_bool(def);
}

//===----------------------------------------------------------------------===//
// ResolveImage: 解析图片
//===----------------------------------------------------------------------===//

std::string DocxParser::ResolveImage(const std::string& relId)
{
    auto it = rels_.find(relId);
    if (it == rels_.end())
        return "";

    // 构建完整路径
    std::string target = it->second;
    if (target.find("word/") == 0)
    {
        return target;
    }
    return docDir_ + target;
}

//===----------------------------------------------------------------------===//
// ParseRunProps: 解析文本属性（简化版）
//===----------------------------------------------------------------------===//

void DocxParser::ParseRunProps(pugi::xml_node rPrNode, SwTextNode* pNode, bool bSkipColor,
                               bool bSkipSize)
{
    if (!rPrNode || !pNode)
        return;

    // 字体
    auto rFonts = rPrNode.child("w:rFonts");
    if (rFonts)
    {
        std::string font = rFonts.attribute("w:ascii").as_string();
        if (!font.empty())
        {
            pNode->SetAttr(RES_CHRATR_FONT, font);
        }
        else
        {
            // 解析 w:asciiTheme 主题字体引用（如 minorHAnsi / majorHAnsi）
            std::string themeFont = rFonts.attribute("w:asciiTheme").as_string();
            if (!themeFont.empty())
            {
                auto it = themeFonts_.find(themeFont);
                if (it != themeFonts_.end())
                    pNode->SetAttr(RES_CHRATR_FONT, it->second);
            }
        }
    }

    // 字号（段落标记的字号不应用于文本内容）
    if (!bSkipSize)
    {
        auto sz = rPrNode.child("w:sz");
        if (sz)
        {
            int size = sz.attribute("w:val").as_int(22);
            pNode->SetAttr(RES_CHRATR_FONTSIZE, std::to_string(size));
        }
    }

    // 粗体
    auto b = rPrNode.child("w:b");
    if (b)
    {
        pNode->SetAttr(RES_CHRATR_WEIGHT, "bold");
    }

    // 斜体
    auto i = rPrNode.child("w:i");
    if (i)
    {
        pNode->SetAttr(RES_CHRATR_POSTURE, "italic");
    }

    // 颜色（段落标记的颜色不应用于文本内容）
    if (!bSkipColor)
    {
        auto color = rPrNode.child("w:color");
        if (color)
        {
            pNode->SetAttr(RES_CHRATR_COLOR, color.attribute("w:val").as_string());
        }
    }
}
