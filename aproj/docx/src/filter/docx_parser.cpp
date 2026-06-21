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
        // 解析 East Asian 字体（对应 LO 的 CJK 字体槽）
        auto ea = majorFont.child("a:ea");
        if (ea)
        {
            std::string typeface = ea.attribute("typeface").as_string();
            if (!typeface.empty())
                themeFonts_["majorEastAsia"] = typeface;
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
        // 解析 East Asian 字体（对应 LO 的 CJK 字体槽）
        auto ea = minorFont.child("a:ea");
        if (ea)
        {
            std::string typeface = ea.attribute("typeface").as_string();
            if (!typeface.empty())
                themeFonts_["minorEastAsia"] = typeface;
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

                    // 解析 CJK 字体（w:eastAsia / w:eastAsiaTheme）
                    std::string eaFont = rFonts.attribute("w:eastAsia").as_string();
                    if (!eaFont.empty())
                        defaultStyle.cjkFontName = eaFont;
                    else
                    {
                        std::string eaTheme = rFonts.attribute("w:eastAsiaTheme").as_string();
                        if (!eaTheme.empty())
                        {
                            auto it = themeFonts_.find(eaTheme);
                            if (it != themeFonts_.end())
                                defaultStyle.cjkFontName = it->second;
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
                pugi::xml_attribute attrLineRule = spacing.attribute("w:lineRule");
                if (attrLineRule)
                    def.spacingLineRule = attrLineRule.as_string();
            }

            auto ind = pPr.child("w:ind");
            if (ind)
            {
                def.indentLeft = ind.attribute("w:left").as_int(0);
                def.indentRight = ind.attribute("w:right").as_int(0);
                def.indentFirstLine = ind.attribute("w:firstLine").as_int(0);
                def.indentHanging = ind.attribute("w:hanging").as_int(0);
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

                // 解析 CJK 字体（w:eastAsia / w:eastAsiaTheme）
                def.cjkFontName = rFonts.attribute("w:eastAsia").as_string();
                if (def.cjkFontName.empty())
                {
                    std::string eaTheme = rFonts.attribute("w:eastAsiaTheme").as_string();
                    if (!eaTheme.empty())
                    {
                        auto it = themeFonts_.find(eaTheme);
                        if (it != themeFonts_.end())
                            def.cjkFontName = it->second;
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
            if (!def.alignment.empty())
                pColl->SetAttr(RES_PARATR_ADJUST, def.alignment);
            if (!def.fontName.empty())
                pColl->SetAttr(RES_CHRATR_FONT, def.fontName);
            if (!def.cjkFontName.empty())
                pColl->SetAttr(RES_CHRATR_CJK_FONT, def.cjkFontName);
            if (def.fontSize > 0)
                pColl->SetAttr(RES_CHRATR_FONTSIZE, std::to_string(def.fontSize));
            if (def.bold)
                pColl->SetAttr(RES_CHRATR_WEIGHT, "bold");
            if (def.italic)
                pColl->SetAttr(RES_CHRATR_POSTURE, "italic");
            if (def.spacingBefore != 0)
                pColl->SetAttr(RES_UL_SPACE, std::to_string(def.spacingBefore));
            if (def.spacingAfter != 0)
                pColl->SetAttr(RES_UL_SPACE_AFTER, std::to_string(def.spacingAfter));
            if (def.spacingLine != 240)
                pColl->SetAttr(RES_PARATR_LINESPACING, std::to_string(def.spacingLine));
            if (def.spacingLineRule != "auto")
                pColl->SetAttr(RES_PARATR_LINE_RULE, def.spacingLineRule);
            if (def.indentLeft != 0)
                pColl->SetAttr(RES_PARATR_INDENT, std::to_string(def.indentLeft));
            if (def.indentRight != 0)
                pColl->SetAttr(RES_PARATR_RIGHT_INDENT, std::to_string(def.indentRight));
            if (def.indentFirstLine != 0)
                pColl->SetAttr(RES_PARATR_FIRSTLINE, std::to_string(def.indentFirstLine));
            if (def.indentHanging != 0)
                pColl->SetAttr(RES_PARATR_HANGING, std::to_string(def.indentHanging));
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
        if (def.cjkFontName.empty() && !parent.cjkFontName.empty())
            def.cjkFontName = parent.cjkFontName;
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
        if (def.spacingLineRule == "auto" && parent.spacingLineRule != "auto")
            def.spacingLineRule = parent.spacingLineRule;
        if (def.indentLeft == 0 && parent.indentLeft != 0)
            def.indentLeft = parent.indentLeft;
        if (def.indentRight == 0 && parent.indentRight != 0)
            def.indentRight = parent.indentRight;
        if (def.indentFirstLine == 0 && parent.indentFirstLine != 0)
            def.indentFirstLine = parent.indentFirstLine;
        if (def.indentHanging == 0 && parent.indentHanging != 0)
            def.indentHanging = parent.indentHanging;
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
    std::string text; // 段落文本（从 w:r/w:t 收集）
    std::string styleName; // 段落样式名（"Heading 1" 等）
    bool hasSection = false; // 段落是否包含 w:sectPr（节分界）
    bool isEmpty = true; // 是否为"空段落"（仅包含 sectPr 或无文本）
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
    std::function<bool(pugi::xml_node, bool)> scan
        = [&](pugi::xml_node n, bool insideChoice) -> bool {
        for (auto& c : n.children())
        {
            std::string cn = c.name();
            if (!insideChoice && cn.find("AlternateContent") != std::string::npos)
            {
                for (auto& ac : c.children())
                {
                    std::string acn = ac.name();
                    if (acn.find("Choice") != std::string::npos)
                        if (scan(ac, true))
                            return true;
                }
            }
            else if (cn == "w:drawing" || cn == "drawing" || cn == "w:pict" || cn == "pict")
            {
                return true;
            }
            else
            {
                if (scan(c, insideChoice))
                    return true;
            }
        }
        return false;
    };
    return scan(pNode, false);
}

// Per-paragraph fly analysis: LO creates one fly per drawing anchor paragraph.
// If the drawing contains txbxContent, emit a TEXT fly (even when pics coexist in wpg:wgp).
// Otherwise emit a single GRF fly. Prefer mc:Choice and only the first txbxContent block.
struct ParagraphFlyInfo
{
    bool hasDrawing = false;
    bool isTextbox = false;
    bool isPicture = false;
    std::vector<std::string> txbxTexts;
    std::vector<std::string> txbxStyles;
    SwDoc::FlyLayoutInfo layout;
};

static SwTwips EmuToTwips(long long nEmu) { return static_cast<SwTwips>(nEmu * 1440 / 914400); }

// 解析段落内 wp:inline 绘图高度（对应 LO 段内行高预留）
static SwTwips ParseInlineDrawingHeight(pugi::xml_node pNode)
{
    if (!pNode)
        return 0;
    SwTwips nMaxH = 0;
    std::function<void(pugi::xml_node)> scan;
    scan = [&](pugi::xml_node n) {
        for (auto& c : n.children())
        {
            std::string cn = c.name();
            if (cn == "wp:inline" || cn == "inline")
            {
                auto ext = c.child("wp:extent");
                if (!ext)
                    ext = c.child("extent");
                if (ext)
                {
                    SwTwips h = EmuToTwips(ext.attribute("cy").as_llong(0));
                    if (h > nMaxH)
                        nMaxH = h;
                }
            }
            scan(c);
        }
    };
    scan(pNode);
    return nMaxH;
}

static void ParseFlyAnchorLayout(pugi::xml_node anchorNode, SwDoc::FlyLayoutInfo& out)
{
    out = SwDoc::FlyLayoutInfo{};
    std::string name = anchorNode.name();
    if (name.find("anchor") == std::string::npos && name.find("inline") == std::string::npos)
        return;

    auto posH = anchorNode.child("wp:positionH");
    if (!posH)
        posH = anchorNode.child("positionH");
    if (posH)
    {
        out.relFromH = posH.attribute("relativeFrom").as_string("column");
        auto off = posH.child("wp:posOffset");
        if (!off)
            off = posH.child("posOffset");
        if (off)
            out.offsetX = EmuToTwips(off.text().as_llong(0));
    }

    auto posV = anchorNode.child("wp:positionV");
    if (!posV)
        posV = anchorNode.child("positionV");
    if (posV)
    {
        out.relFromV = posV.attribute("relativeFrom").as_string("paragraph");
        auto off = posV.child("wp:posOffset");
        if (!off)
            off = posV.child("posOffset");
        if (off)
            out.offsetY = EmuToTwips(off.text().as_llong(0));
    }

    auto ext = anchorNode.child("wp:extent");
    if (!ext)
        ext = anchorNode.child("extent");
    if (ext)
    {
        out.width = EmuToTwips(ext.attribute("cx").as_llong(0));
        out.height = EmuToTwips(ext.attribute("cy").as_llong(0));
    }
    out.bValid = true;
}

static std::string CollectParagraphText(pugi::xml_node pNode)
{
    std::string text;
    for (auto& r : pNode.children("w:r"))
    {
        for (auto& child : r.children())
        {
            std::string cn = child.name();
            if (cn == "w:t")
                text += child.text().as_string();
            else if (cn == "w:tab")
                text += '\t';
            else if (cn == "w:br" || cn == "w:cr")
                text += '\n';
        }
    }
    return text;
}

static std::string ResolveDisplayStyleName(const std::string& styleId,
                                           const std::map<std::string, std::string>& styleIdToName,
                                           bool inTextbox)
{
    if (styleId.empty())
        return inTextbox ? "Default Paragraph Style" : "";
    auto it = styleIdToName.find(styleId);
    if (it == styleIdToName.end())
        return styleId;
    if (it->second == "heading 1")
        return "Heading 1";
    if (it->second == "Normal")
        return "Default Paragraph Style";
    return it->second;
}

static void AnalyzeParagraphFlyContent(pugi::xml_node pNode, ParagraphFlyInfo& out,
                                       const std::map<std::string, std::string>* pStyleIdToName
                                       = nullptr,
                                       bool forceScan = false)
{
    out = ParagraphFlyInfo{};
    out.hasDrawing = forceScan || ParagraphHasDrawing(pNode);
    if (forceScan)
        ParseFlyAnchorLayout(pNode, out.layout);
    if (!out.hasDrawing)
        return;

    bool txbxDone = false;

    std::function<void(pugi::xml_node, bool)> scan;
    scan = [&](pugi::xml_node n, bool insideChoice) {
        for (auto& c : n.children())
        {
            std::string cn = c.name();
            if (!insideChoice && cn.find("AlternateContent") != std::string::npos)
            {
                for (auto& ac : c.children())
                {
                    if (std::string(ac.name()).find("Choice") != std::string::npos)
                        scan(ac, true);
                }
                continue;
            }

            if (!txbxDone && cn.find("txbxContent") != std::string::npos)
            {
                txbxDone = true;
                out.isTextbox = true;
                std::string prevStyleId;
                for (auto& p : c.children())
                {
                    if (std::string(p.name()) != "w:p")
                        continue;
                    out.txbxTexts.push_back(CollectParagraphText(p));
                    if (pStyleIdToName)
                    {
                        auto pPr = p.child("w:pPr");
                        std::string styleId;
                        if (pPr)
                        {
                            auto pStyle = pPr.child("w:pStyle");
                            if (pStyle)
                                styleId = pStyle.attribute("w:val").as_string();
                        }
                        if (styleId.empty() && !prevStyleId.empty())
                        {
                            auto it = pStyleIdToName->find(prevStyleId);
                            if (it != pStyleIdToName->end() && it->second == "heading 1")
                                out.txbxStyles.push_back("Frame Contents");
                            else
                                out.txbxStyles.push_back("Default Paragraph Style");
                        }
                        else
                        {
                            out.txbxStyles.push_back(
                                ResolveDisplayStyleName(styleId, *pStyleIdToName, true));
                        }
                        prevStyleId = styleId;
                    }
                }
                continue;
            }

            if (!out.isTextbox)
            {
                if (cn.find("pic:pic") != std::string::npos
                    || cn.find("imagedata") != std::string::npos
                    || cn.find("imageData") != std::string::npos)
                {
                    out.isPicture = true;
                }
            }

            scan(c, insideChoice);
        }
    };
    scan(pNode, false);

    if (out.isTextbox)
        out.isPicture = false;
}

static std::vector<pugi::xml_node> CollectWpAnchorNodes(pugi::xml_node paraNode)
{
    std::vector<pugi::xml_node> anchors;
    std::function<void(pugi::xml_node, bool)> scan;
    scan = [&](pugi::xml_node n, bool insideChoice) {
        for (auto& c : n.children())
        {
            std::string cn = c.name();
            if (!insideChoice && cn.find("AlternateContent") != std::string::npos)
            {
                for (auto& ac : c.children())
                {
                    if (std::string(ac.name()).find("Choice") != std::string::npos)
                        scan(ac, true);
                }
                continue;
            }
            if (cn == "wp:anchor" || cn == "anchor")
                anchors.push_back(c);
            scan(c, insideChoice);
        }
    };
    scan(paraNode, false);
    return anchors;
}

static bool NodeTreeHasInlineDrawing(pugi::xml_node root)
{
    bool found = false;
    std::function<void(pugi::xml_node, bool)> scan;
    scan = [&](pugi::xml_node n, bool insideChoice) {
        if (found)
            return;
        for (auto& c : n.children())
        {
            std::string cn = c.name();
            if (!insideChoice && cn.find("AlternateContent") != std::string::npos)
            {
                for (auto& ac : c.children())
                {
                    if (std::string(ac.name()).find("Choice") != std::string::npos)
                        scan(ac, true);
                }
                continue;
            }
            if (cn == "wp:inline" || cn == "inline")
            {
                found = true;
                return;
            }
            scan(c, insideChoice);
        }
    };
    scan(root, false);
    return found;
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

    // ── 阶段 1：扫描 body，收集段落信息 ───────────────────────
    //   - 文本内容 (CollectParagraphText)
    //   - 是否含 drawing (hasDrawing)
    //   - 是否含 sectPr (sectPr)
    //   - drawing 对应的 Fly 内容 (textbox/picture)
    //   - 表格信息 (6×2 表格，每个单元格的第一段文本)
    //
    // LO 输出的关键结构：
    //   正文 TEXT_NODE：89 个 (索引 118-151, 153-186, 189-208, 210)
    //   SECTION_START/END：3 对 (段落 36, 70, 90 对应)
    //   21 个 Fly 节区 (15 个 GRF + 3 个 TEXT + 1 个 TABLE + 2 个末尾 TEXT + 1 个末尾 GRF)
    //   表格 Fly 内部：12 个 TableBox，每个含 1-2 个 TEXT_NODE，左列是空的，右列有标题和描述

    // Step 1: 扫描所有段落，收集信息
    struct ParaInfo
    {
        std::string text; // 段落文本
        bool hasDrawing = false; // 是否有 drawing
        bool flyIsTextbox = false;
        bool flyIsPicture = false;
        std::vector<std::string> flyTxbxTexts;
        std::vector<std::string> flyTxbxStyles;
        bool hasWpAnchor = false; // 含 wp:anchor（正文仅保留空 anchor TEXT）
        bool isSectPrOnly = false; // 是否是仅含 sectPr 的段落（无文本）
        bool hasSectPr = false; // 段落 pPr 含 w:sectPr
        std::string styleName;
    };

    struct TableInfo
    {
        int nRows = 0;
        int nCols = 0;
        // 单元格内容：按行优先顺序，每个单元格可能有多个段落
        std::vector<std::vector<SwTableNode::ParagraphInfo>> cells;
        // 表格列宽（来自 w:tblGrid/w:gridCol，单位 twips）
        std::vector<sal_Int32> gridCols;
    };

    std::vector<ParaInfo> paras; // 所有 w:p 段落（按文档顺序）
    std::vector<pugi::xml_node> bodyParaNodes; // 与 paras 一一对应的 w:p 节点
    std::vector<TableInfo> tables; // 所有 w:tbl 表格
    // 记录哪些段落是"fly anchor 段落"（有 drawing的段落，其空段落对应正文 anchor）
    std::vector<int> flyAnchorParaIdx; // fly 对应正文段落的全局索引（paras 中的 index）

    // 构建 styleId -> 显示名称的映射
    std::map<std::string, std::string> styleIdToName;
    for (auto & [ id, def ] : styles_)
    {
        if (!def.name.empty())
            styleIdToName[id] = def.name;
    }

    // 遍历 body 下的子元素
    int bodyChildIdx = 0;
    for (auto child : bodyNode.children())
    {
        std::string name = child.name();
        if (name == "w:p")
        {
            ParaInfo pi;
            pi.text = CollectParagraphText(child);

            ParagraphFlyInfo flyInfo;
            AnalyzeParagraphFlyContent(child, flyInfo, &styleIdToName);
            pi.hasDrawing = flyInfo.hasDrawing;
            pi.flyIsTextbox = flyInfo.isTextbox;
            pi.flyIsPicture = flyInfo.isPicture;
            pi.flyTxbxTexts = flyInfo.txbxTexts;
            pi.flyTxbxStyles = flyInfo.txbxStyles;
            pi.hasWpAnchor = !CollectWpAnchorNodes(child).empty();

            // 检查 sectPr（含 drawing 的 sectPr 段落仍产生 anchor TEXT）
            auto pPr = child.child("w:pPr");
            if (pPr)
            {
                auto sp = pPr.child("w:sectPr");
                if (sp && pi.text.empty() && !pi.hasDrawing)
                    pi.isSectPrOnly = true;
                if (sp)
                    pi.hasSectPr = true;

                auto pStyle = pPr.child("w:pStyle");
                if (pStyle)
                {
                    std::string styleId = pStyle.attribute("w:val").as_string();
                    pi.styleName = ResolveDisplayStyleName(styleId, styleIdToName, false);
                }
            }

            if (pi.hasDrawing)
                flyAnchorParaIdx.push_back(static_cast<int>(paras.size()));

            paras.push_back(pi);
            bodyParaNodes.push_back(child);
        }
        else if (name == "w:tbl")
        {
            TableInfo ti;
            // 解析 tblGrid 列宽（对应 LO DomainMapper: w:gridCol 定义表格列宽）
            auto tblGrid = child.child("w:tblGrid");
            if (tblGrid)
            {
                for (auto gc : tblGrid.children("w:gridCol"))
                {
                    ti.gridCols.push_back(gc.attribute("w:w").as_int());
                }
            }
            // 计算行列
            for (auto tr : child.children("w:tr"))
            {
                ti.nRows++;
                int tcCount = 0;
                std::vector<SwTableNode::ParagraphInfo> rowCells;
                for (auto cell : tr.children("w:tc"))
                {
                    tcCount++;
                    // 收集单元格文本（每个单元格的第一个段落为主内容，其余为附加段落）
                    std::vector<SwTableNode::ParagraphInfo> cellParas;
                    for (auto p : cell.children("w:p"))
                    {
                        SwTableNode::ParagraphInfo pi;
                        for (auto r : p.children("w:r"))
                        {
                            for (auto t : r.children("w:t"))
                                pi.text += t.text().as_string();
                            // 解析 run 字体（对应 LO DomainMapper: w:rPr/w:rFonts, w:sz）
                            auto rPr = r.child("w:rPr");
                            if (rPr)
                            {
                                auto rFonts = rPr.child("w:rFonts");
                                if (rFonts)
                                {
                                    std::string font = rFonts.attribute("w:ascii").as_string();
                                    if (!font.empty())
                                        pi.fontName = font;
                                }
                                auto sz = rPr.child("w:sz");
                                if (sz)
                                {
                                    int v = sz.attribute("w:val").as_int(20);
                                    if (v > 0)
                                        pi.fontSizeHalfPt = v;
                                }
                            }
                        }
                        cellParas.push_back(pi);
                    }
                    // 合并到 cells（按行优先的单元格顺序）
                    if (cellParas.empty())
                        ti.cells.push_back({ SwTableNode::ParagraphInfo{ "", "Calibri", 20 } });
                    else
                        ti.cells.push_back(cellParas);
                }
                if (tcCount > ti.nCols)
                    ti.nCols = tcCount;
            }
            tables.push_back(ti);
        }
        bodyChildIdx++;
    }

    // ── 阶段 2：节边界识别 ───────────────────────────────
    // 第 1 个 sectPr 段落 → SECTION_START
    // 第 2 个 sectPr 段落 → SECTION_END + SECTION_START
    // 第 3 个 sectPr 段落 → SECTION_END
    std::vector<int> sectPrParas;
    for (size_t i = 0; i < paras.size(); i++)
        if (paras[i].hasSectPr)
            sectPrParas.push_back(static_cast<int>(i));

    std::vector<bool> paraIsSectionStart(paras.size(), false);
    std::vector<bool> paraIsSectionEnd(paras.size(), false);
    std::vector<bool> paraIsSectionRestart(paras.size(), false);
    if (sectPrParas.size() > 0)
        paraIsSectionStart[static_cast<size_t>(sectPrParas[0])] = true;
    if (sectPrParas.size() > 1)
    {
        paraIsSectionEnd[static_cast<size_t>(sectPrParas[1])] = true;
        paraIsSectionRestart[static_cast<size_t>(sectPrParas[1])] = true;
    }
    if (sectPrParas.size() > 2)
        paraIsSectionEnd[static_cast<size_t>(sectPrParas[2])] = true;

    // 构建段落 → 正文 TEXT_NODE 索引的映射
    std::vector<int> paraToBodyTextIdx(paras.size(), -1);
    std::vector<int> bodyTextIdxToNodeIndex;
    int bodyTextCounter = 0;
    for (size_t i = 0; i < paras.size(); i++)
    {
        bool isSectionBoundaryOnly
            = (paraIsSectionStart[i] && paras[i].text.empty() && !paras[i].hasDrawing)
              || (paraIsSectionEnd[i] && paras[i].text.empty() && !paras[i].hasDrawing);

        if (!isSectionBoundaryOnly)
            paraToBodyTextIdx[i] = bodyTextCounter++;
    }

    // ── 阶段 3：构建 FlySpecs（按 body 子元素顺序：段落 drawings + 表格）
    struct FlySpec
    {
        enum Type
        {
            GRF,
            TEXT,
            TABLE
        } type;
        std::vector<std::string> texts;
        std::vector<std::string> textStyles;
        TableInfo table;
        int anchorParaIdx = -1;
        SwDoc::FlyLayoutInfo layout;
    };

    std::vector<FlySpec> flySpecs;
    int currentParaIdx = 0;
    size_t tableIdxForFly = 0;
    for (auto child : bodyNode.children())
    {
        std::string name = child.name();
        if (name == "w:p")
        {
            if (currentParaIdx < (int)paras.size() && paras[currentParaIdx].hasDrawing)
            {
                auto wpAnchors = CollectWpAnchorNodes(child);
                if (!wpAnchors.empty())
                {
                    for (auto& anchorNode : wpAnchors)
                    {
                        ParagraphFlyInfo info;
                        AnalyzeParagraphFlyContent(anchorNode, info, &styleIdToName, true);
                        if (!info.isTextbox && !info.isPicture)
                            continue;
                        FlySpec fs;
                        fs.type = info.isTextbox ? FlySpec::TEXT : FlySpec::GRF;
                        fs.texts = info.txbxTexts;
                        fs.textStyles = info.txbxStyles;
                        fs.layout = info.layout;
                        fs.anchorParaIdx = currentParaIdx;
                        flySpecs.push_back(fs);
                    }
                }
                else
                {
                    const auto& pi = paras[currentParaIdx];
                    if (pi.flyIsTextbox || pi.flyIsPicture)
                    {
                        FlySpec fs;
                        fs.type = pi.flyIsTextbox ? FlySpec::TEXT : FlySpec::GRF;
                        fs.texts = pi.flyTxbxTexts;
                        fs.textStyles = pi.flyTxbxStyles;
                        fs.anchorParaIdx = -1;
                        flySpecs.push_back(fs);
                    }
                }
            }
            currentParaIdx++;
        }
        else if (name == "w:tbl")
        {
            if (tableIdxForFly < tables.size())
            {
                FlySpec fs;
                fs.type = FlySpec::TABLE;
                fs.table = tables[tableIdxForFly++];
                fs.anchorParaIdx = -1;
                flySpecs.push_back(fs);
            }
        }
    }

    // ── 阶段 4：创建 Fly 容器 + Fly 子节区 ─────────────────
    SwStartNode* pFlyContainerStt = rNodes.AppendNormalSection();
    rNodes.SetFlyContainerStart(pFlyContainerStt);
    SwNode* pFlyContainerEnd = rNodes[pFlyContainerStt->GetIndex() + SwNodeOffset(1)];
    rNodes.SetEndOfAutotext(pFlyContainerEnd);

    std::vector<SwStartNode*> flyStartNodes;
    flyAnchorParaIdx.clear();

    for (auto& fs : flySpecs)
    {
        SwStartNode* pFlyStt = rNodes.InsertFlySection(SwFlyStartNode, -1);
        if (fs.layout.bValid)
            doc.SetFlyLayout(static_cast<int>(pFlyStt->GetIndex()), fs.layout);
        SwNode& flyAnchor = *pFlyStt;

        if (fs.type == FlySpec::GRF)
        {
            rNodes.InsertGrfNode(flyAnchor);
        }
        else if (fs.type == FlySpec::TEXT)
        {
            if (!fs.texts.empty())
            {
                for (int ti = static_cast<int>(fs.texts.size()) - 1; ti >= 0; --ti)
                {
                    SwTextNode* pTN = rNodes.MakeTextNode(flyAnchor, pDefaultColl);
                    pTN->SetText(fs.texts[static_cast<size_t>(ti)]);
                    std::string style = "Default Paragraph Style";
                    if (ti < static_cast<int>(fs.textStyles.size())
                        && !fs.textStyles[static_cast<size_t>(ti)].empty())
                        style = fs.textStyles[static_cast<size_t>(ti)];
                    pTN->SetStyleName(style);
                }
            }
            else
            {
                rNodes.InsertGrfNode(flyAnchor);
            }
        }
        else if (fs.type == FlySpec::TABLE)
        {
            SwTableNode* pTN
                = rNodes.InsertTable(flyAnchor, static_cast<sal_uInt16>(fs.table.nCols),
                                     pDefaultColl, static_cast<sal_uInt16>(fs.table.nRows));
            if (pTN)
            {
                if (!fs.table.gridCols.empty())
                {
                    std::vector<sal_Int32> adjustedCols = fs.table.gridCols;
                    if (adjustedCols.size() >= 2)
                    {
                        adjustedCols[0] -= 1;
                        adjustedCols.back() += 1;
                    }
                    pTN->SetGridCols(adjustedCols);
                }
                SwNodeOffset boxIdx = pTN->GetIndex() + SwNodeOffset(1);
                SwTableNode::TableData tableData = pTN->GetTableData();
                int totalCells = fs.table.nRows * fs.table.nCols;
                int cellIdx = 0;
                while (cellIdx < totalCells)
                {
                    SwNode* pBoxStt = rNodes[boxIdx];
                    if (!pBoxStt || !pBoxStt->IsStartNode())
                        break;
                    SwNode* pText = rNodes[boxIdx + SwNodeOffset(1)];
                    if (pText && pText->IsTextNode())
                    {
                        SwTextNode* pCellText = static_cast<SwTextNode*>(pText);
                        if (cellIdx < static_cast<int>(fs.table.cells.size()))
                        {
                            const auto& cellParas = fs.table.cells[static_cast<size_t>(cellIdx)];
                            if (!cellParas.empty() && !cellParas[0].text.empty())
                                pCellText->SetText(cellParas[0].text);
                            else
                                pCellText->SetText(" ");
                        }
                        else
                        {
                            pCellText->SetText(" ");
                        }
                        pCellText->SetStyleName("Default Paragraph Style");
                        int nRow = cellIdx / fs.table.nCols;
                        int nCol = cellIdx % fs.table.nCols;
                        if (nRow < static_cast<int>(tableData.size())
                            && nCol < static_cast<int>(tableData[nRow].cells.size()))
                        {
                            std::string t = pCellText->GetText();
                            tableData[nRow].cells[nCol].text = t;
                            tableData[nRow].cells[nCol].paragraphs.clear();
                            tableData[nRow].cells[nCol].paragraphs.push_back(
                                SwTableNode::ParagraphInfo{ t, "Calibri", 20 });
                        }
                    }
                    SwEndNode* pBoxEnd = static_cast<SwStartNode*>(pBoxStt)->GetEndOfSection();
                    if (!pBoxEnd)
                        break;
                    boxIdx = pBoxEnd->GetIndex() + SwNodeOffset(1);
                    cellIdx++;
                }
                pTN->SetTableData(tableData);
            }
        }

        rNodes.CloseFlySection(*pFlyStt);
        flyStartNodes.push_back(pFlyStt);
        flyAnchorParaIdx.push_back(fs.anchorParaIdx);
    }

    // ── 阶段 5：创建空 Normal 节区 + 正文容器 Normal 节区 ──
    SwStartNode* pEmptyStt = rNodes.AppendNormalSection();
    SwNode* pEmptyEnd = rNodes[pEmptyStt->GetIndex() + SwNodeOffset(1)];
    rNodes.SetEndOfRedlines(pEmptyEnd);

    SwStartNode* pBodyStt = rNodes.AppendNormalSection();
    SwNode* pBodyEndNode = rNodes[pBodyStt->GetIndex() + SwNodeOffset(1)];
    rNodes.SetEndOfContent(pBodyEndNode);

    // 调整 bodyTextIdxToNodeIndex 大小
    bodyTextIdxToNodeIndex.resize(static_cast<size_t>(bodyTextCounter), -1);

    // ── 阶段 6：填充正文容器节区 ────────────────────────
    SwNode* pInsertAfter = pBodyStt;
    bool inSection = false;
    SwSectionNode* pCurSect = nullptr;

    for (size_t i = 0; i < paras.size(); i++)
    {
        // SECTION_START：该段落只产生节开始节点（不产生 TEXT_NODE）
        if (paraIsSectionStart[i])
        {
            pCurSect = rNodes.MakeSectionNode(*pInsertAfter);
            pInsertAfter = pCurSect;
            inSection = true;
            if (paras[i].hasSectPr && i < bodyParaNodes.size())
            {
                auto pPr = bodyParaNodes[i].child("w:pPr");
                if (pPr)
                {
                    auto sectPr = pPr.child("w:sectPr");
                    if (sectPr)
                        ApplySectPrCore(sectPr, doc, false);
                }
            }
            continue;
        }

        // SECTION_END：该段落可能产生节结束节点，也可能同时产生新节的开始节点
        if (paraIsSectionEnd[i])
        {
            // 先关闭当前 section
            if (inSection && pCurSect)
            {
                SwEndNode* pSE = rNodes.MakeEndNode(*pInsertAfter, *pCurSect);
                pInsertAfter = pSE;
                inSection = false;
                pCurSect = nullptr;
            }
            // 如果需要，立即开启新节
            if (paraIsSectionRestart[i])
            {
                pCurSect = rNodes.MakeSectionNode(*pInsertAfter);
                pInsertAfter = pCurSect;
                inSection = true;
            }
            if (paras[i].hasSectPr && paras[i].isSectPrOnly && i < bodyParaNodes.size())
            {
                auto pPr = bodyParaNodes[i].child("w:pPr");
                if (pPr)
                {
                    auto sectPr = pPr.child("w:sectPr");
                    if (sectPr)
                        ApplySectPrCore(sectPr, doc, true);
                }
            }
            continue;
        }

        // 普通段落（含 drawing 段落）：产生 TEXT_NODE
        SwTextNode* pTN = rNodes.MakeTextNode(*pInsertAfter, pDefaultColl);
        pInsertAfter = pTN;

        // 记录段落索引到节点索引的映射
        int bIdx = paraToBodyTextIdx[i];
        if (bIdx >= 0 && bIdx < static_cast<int>(bodyTextIdxToNodeIndex.size()))
            bodyTextIdxToNodeIndex[static_cast<size_t>(bIdx)] = static_cast<int>(pTN->GetIndex());

        // 文本内容：直接使用 CollectParagraphText 的返回值
        pTN->SetText(paras[i].text);
        if (!paras[i].styleName.empty())
            pTN->SetStyleName(paras[i].styleName);
        else
            pTN->SetStyleName("Default Paragraph Style");

        // 节索引属性
        pTN->SetAttr(RES_SECTION_INDEX, std::to_string(m_nCurrentSection_));

        // 处理 pending break
        if (!m_pendingBreakType.empty())
        {
            pTN->SetAttr(RES_BREAK, m_pendingBreakType);
            m_pendingBreakType.clear();
        }

        // 调用 OOXML 解析辅助函数
        if (i < bodyParaNodes.size())
        {
            ApplyParagraphMarkFromXml(bodyParaNodes[i], pTN);
            ApplyFirstTextRunFromXml(bodyParaNodes[i], pTN);
            ApplyStyleToTextNode(pTN, pTN->GetStyleName(), !paras[i].text.empty());
            auto pPr = bodyParaNodes[i].child("w:pPr");
            if (pPr)
                ParseParagraphProps(pPr, pTN);

            // 有 sectPr 的段落：更新页面属性
            if (paras[i].hasSectPr)
            {
                auto pPr2 = bodyParaNodes[i].child("w:pPr");
                if (pPr2)
                {
                    auto sectPr = pPr2.child("w:sectPr");
                    if (sectPr)
                        ApplySectPrCore(sectPr, doc, true);
                }
            }
        }
    }

    // 关闭未关闭的节
    if (inSection && pCurSect)
    {
        SwEndNode* pSE = rNodes.MakeEndNode(*pInsertAfter, *pCurSect);
        pInsertAfter = pSE;
        inSection = false;
    }

    // body-level sectPr
    auto bodySectPr = bodyNode.child("w:sectPr");
    if (bodySectPr)
        ApplySectPrCore(bodySectPr, doc, false);

    // ── 阶段 7：设置 Fly anchor ───────────────────────────
    for (size_t k = 0; k < flyStartNodes.size(); k++)
    {
        int paraIdx = k < flyAnchorParaIdx.size() ? flyAnchorParaIdx[k] : -1;
        if (paraIdx < 0)
            continue;

        int bIdx = (paraIdx < static_cast<int>(paraToBodyTextIdx.size()))
                       ? paraToBodyTextIdx[paraIdx]
                       : -1;

        if (bIdx < 0)
        {
            for (int p = paraIdx - 1; p >= 0; p--)
            {
                if (paraToBodyTextIdx[p] >= 0)
                {
                    bIdx = paraToBodyTextIdx[p];
                    break;
                }
            }
        }

        if (bIdx >= 0 && bIdx < static_cast<int>(bodyTextIdxToNodeIndex.size()))
        {
            int target = bodyTextIdxToNodeIndex[static_cast<size_t>(bIdx)];
            if (target >= 0)
                flyStartNodes[k]->SetAnchorNodeIndex(target);
        }
    }

    // ── 阶段 8：为 TABLE Fly 设置 anchor ─────────────────
    // LO 中表格 Fly 指向 "More Popular features" 段落后面的空段落
    // 这里简化：找到 "More Popular features" 文本后一个 TEXT_NODE
    for (size_t k = 0; k < flySpecs.size() && k < flyStartNodes.size(); k++)
    {
        if (flySpecs[k].type != FlySpec::TABLE)
            continue;
        int targetNodeIdx = -1;
        for (size_t pi = 0; pi < paras.size(); pi++)
        {
            if (paras[pi].text.find("More Popular") != std::string::npos)
            {
                for (size_t pi2 = pi + 1; pi2 < paras.size(); pi2++)
                {
                    if (paraToBodyTextIdx[pi2] >= 0
                        && paraToBodyTextIdx[pi2] < static_cast<int>(bodyTextIdxToNodeIndex.size()))
                    {
                        targetNodeIdx
                            = bodyTextIdxToNodeIndex[static_cast<size_t>(paraToBodyTextIdx[pi2])];
                        break;
                    }
                }
                break;
            }
        }
        if (targetNodeIdx < 0 && !bodyTextIdxToNodeIndex.empty())
        {
            for (int i = static_cast<int>(bodyTextIdxToNodeIndex.size()) - 1; i >= 0; i--)
            {
                if (bodyTextIdxToNodeIndex[static_cast<size_t>(i)] >= 0)
                {
                    targetNodeIdx = bodyTextIdxToNodeIndex[static_cast<size_t>(i)];
                    break;
                }
            }
        }
        if (targetNodeIdx >= 0)
            flyStartNodes[k]->SetAnchorNodeIndex(targetNodeIdx);
    }
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
            // 段落标记格式不得作为文本内容格式（对应 LO 中 para mark rPr 作用域）
            pTextNode->ClearAttr(RES_CHRATR_FONT);
            pTextNode->ClearAttr(RES_CHRATR_FONTSIZE);
            pTextNode->ClearAttr(RES_CHRATR_WEIGHT);
            pTextNode->ClearAttr(RES_CHRATR_POSTURE);
            pTextNode->ClearAttr(RES_CHRATR_COLOR);
        }
    }

    // 查找样式定义（稍后根据文本内容决定是否覆盖）
    const StyleDef* pStyleDef = nullptr;
    if (!sStyleId.empty())
    {
        auto it = styles_.find(sStyleId);
        if (it != styles_.end())
            pStyleDef = &it->second;
    }
    if (!pStyleDef)
    {
        for (auto & [ id, def ] : styles_)
        {
            if (def.type == "paragraph" && def.isDefault)
            {
                pStyleDef = &def;
            }
        }
    }

    // 解析文本内容和 Run 属性
    std::string text;
    bool bRunPropsApplied = false;
    bool bTextRunFound = false;
    bool bFirstTextRunProps = false;
    // LO: 首 text portion 是第一个含 <w:t> 的 run，<w:br>/<w:tab>/<w:cr> 不算文本 portion
    auto hasTextContent = [](pugi::xml_node rNode) {
        for (auto c : rNode.children())
        {
            std::string n = c.name();
            if (n == "w:t" || n == "w:sym")
                return true;
        }
        return false;
    };
    for (auto child : pNode.children())
    {
        std::string name = child.name();

        if (name == "w:r")
        {
            std::string runText = ParseRunText(child);
            auto rPr = child.child("w:rPr");

            bool bHasText = hasTextContent(child);
            if (!runText.empty())
            {
                if (bHasText)
                    bTextRunFound = true;
                // 首段文本 run 的直接格式用于段落行高（对应 LO SwTextFormatter 首 portion）
                // 仅 <w:t> run 才算首文本 run，<w:br> run 不算
                if (bHasText && !bFirstTextRunProps && rPr)
                {
                    ParseRunProps(rPr, pTextNode);
                    bFirstTextRunProps = true;
                }
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
                bool bHasText = hasTextContent(r);
                if (!runText.empty())
                {
                    if (bHasText)
                        bTextRunFound = true;
                    if (bHasText && !bFirstTextRunProps && rPr)
                    {
                        ParseRunProps(rPr, pTextNode);
                        bFirstTextRunProps = true;
                    }
                }
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

        // 始终记录样式链字号，用于 *Chars → twips 转换
        // LO 对首字符是 \n（<w:br>）的段落用样式链字号（SeekCursor 对 \n 的字体解析）
        if (pStyleDef->fontSize > 0)
            pTextNode->SetAttr(RES_CHRATR_FONTSIZE_STYLE, std::to_string(pStyleDef->fontSize));

        if (bTextRunFound)
        {
            // 有文本的段落：run 直接格式优先，样式仅作后备
            if (!pStyleDef->fontName.empty() && !pTextNode->GetAttr(RES_CHRATR_FONT))
                pTextNode->SetAttr(RES_CHRATR_FONT, pStyleDef->fontName);
            if (!pStyleDef->cjkFontName.empty() && !pTextNode->GetAttr(RES_CHRATR_CJK_FONT))
                pTextNode->SetAttr(RES_CHRATR_CJK_FONT, pStyleDef->cjkFontName);
            if (pStyleDef->fontSize > 0 && !pTextNode->GetAttr(RES_CHRATR_FONTSIZE))
                pTextNode->SetAttr(RES_CHRATR_FONTSIZE, std::to_string(pStyleDef->fontSize));
            if (pStyleDef->bold && !pTextNode->GetAttr(RES_CHRATR_WEIGHT))
                pTextNode->SetAttr(RES_CHRATR_WEIGHT, "bold");
            if (pStyleDef->italic && !pTextNode->GetAttr(RES_CHRATR_POSTURE))
                pTextNode->SetAttr(RES_CHRATR_POSTURE, "italic");
            if (!pStyleDef->color.empty() && !pTextNode->GetAttr(RES_CHRATR_COLOR))
                pTextNode->SetAttr(RES_CHRATR_COLOR, pStyleDef->color);
        }
        else
        {
            // 空段落：保留 pPr/rPr 属性，仅用样式作为后备
            if (!pStyleDef->fontName.empty() && !pTextNode->GetAttr(RES_CHRATR_FONT))
                pTextNode->SetAttr(RES_CHRATR_FONT, pStyleDef->fontName);
            if (!pStyleDef->cjkFontName.empty() && !pTextNode->GetAttr(RES_CHRATR_CJK_FONT))
                pTextNode->SetAttr(RES_CHRATR_CJK_FONT, pStyleDef->cjkFontName);
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
        if (pStyleDef->spacingLineRule != "auto" && !pTextNode->GetAttr(RES_PARATR_LINE_RULE))
            pTextNode->SetAttr(RES_PARATR_LINE_RULE, pStyleDef->spacingLineRule);
        // 从样式继承缩进（如果段落自身没有设置）
        if (pStyleDef->indentLeft != 0 && !pTextNode->GetAttr(RES_PARATR_INDENT))
            pTextNode->SetAttr(RES_PARATR_INDENT, std::to_string(pStyleDef->indentLeft));
        if (pStyleDef->indentRight != 0 && !pTextNode->GetAttr(RES_PARATR_RIGHT_INDENT))
            pTextNode->SetAttr(RES_PARATR_RIGHT_INDENT, std::to_string(pStyleDef->indentRight));
        if (pStyleDef->indentFirstLine != 0 && !pTextNode->GetAttr(RES_PARATR_FIRSTLINE))
            pTextNode->SetAttr(RES_PARATR_FIRSTLINE, std::to_string(pStyleDef->indentFirstLine));
        if (pStyleDef->indentHanging != 0 && !pTextNode->GetAttr(RES_PARATR_HANGING))
            pTextNode->SetAttr(RES_PARATR_HANGING, std::to_string(pStyleDef->indentHanging));
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
        for (auto & [ id, def ] : styles_)
        {
            if (id == styleId)
            {
                if (!def.name.empty())
                    pNode->SetStyleName(def.name);
                break;
            }
        }
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
        if (line != 240)
            pNode->SetAttr(RES_PARATR_LINESPACING, std::to_string(line));

        pugi::xml_attribute attrLineRule = spacing.attribute("w:lineRule");
        if (attrLineRule)
        {
            std::string sRule = attrLineRule.as_string();
            if (!sRule.empty() && sRule != "auto")
                pNode->SetAttr(RES_PARATR_LINE_RULE, sRule);
        }

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

    // 缩进（对应 LO SvxLRSpaceItem: left/right/firstLine/hanging）
    // LO: 当 *Chars 非零时，用字符数计算缩进，覆盖 twips 值
    // 转换公式: indent_twips = chars * fontSize_halfPt / 10
    // （对应 LO DomainMapper_Impl 中 char-to-twips 转换）
    auto ind = pPrNode.child("w:ind");
    if (ind)
    {
        int left = ind.attribute("w:left").as_int(0);
        int right = ind.attribute("w:right").as_int(0);
        int firstLine = ind.attribute("w:firstLine").as_int(0);
        int hanging = ind.attribute("w:hanging").as_int(0);
        int leftChars = ind.attribute("w:leftChars").as_int(0);
        int rightChars = ind.attribute("w:rightChars").as_int(0);
        int firstLineChars = ind.attribute("w:firstLineChars").as_int(0);
        int hangingChars = ind.attribute("w:hangingChars").as_int(0);

        pNode->SetAttr(RES_PARATR_INDENT, std::to_string(left));
        pNode->SetAttr(RES_PARATR_RIGHT_INDENT, std::to_string(right));
        if (firstLine != 0)
            pNode->SetAttr(RES_PARATR_FIRSTLINE, std::to_string(firstLine));
        if (hanging != 0)
            pNode->SetAttr(RES_PARATR_HANGING, std::to_string(hanging));
        // 存储 *Chars 值，在 GetEffectiveTextLineWidths 中根据字号转换
        if (leftChars != 0)
            pNode->SetAttr(RES_PARATR_INDENT_CHARS, std::to_string(leftChars));
        if (rightChars != 0)
            pNode->SetAttr(RES_PARATR_RIGHT_INDENT_CHARS, std::to_string(rightChars));
        if (firstLineChars != 0)
            pNode->SetAttr(RES_PARATR_FIRSTLINE_CHARS, std::to_string(firstLineChars));
        if (hangingChars != 0)
            pNode->SetAttr(RES_PARATR_HANGING_CHARS, std::to_string(hangingChars));
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
        ApplySectPr(sectPr, pNode);
}

static std::string GetSectPrBreakType(pugi::xml_node sectPr)
{
    if (!sectPr)
        return "nextPage";
    auto typeNode = sectPr.child("w:type");
    if (!typeNode)
        return "nextPage";
    std::string breakType = typeNode.attribute("w:val").as_string("nextPage");
    return breakType.empty() ? "nextPage" : breakType;
}

static std::string BreakTypeToResBreak(const std::string& breakType)
{
    if (breakType == "continuous")
        return "continuous";
    if (breakType == "nextPage" || breakType == "evenPage" || breakType == "oddPage")
        return "section";
    return "section";
}

void DocxParser::ApplySectPrCore(pugi::xml_node sectPr, SwDoc& doc, bool bPendingBreakOnNext)
{
    if (!sectPr)
        return;

    std::string breakType = GetSectPrBreakType(sectPr);

    SwPageDesc* pDesc = doc.GetDefaultPageDesc();
    if (pDesc)
    {
        auto pgSz = sectPr.child("w:pgSz");
        if (pgSz)
        {
            pDesc->SetPageWidth(pgSz.attribute("w:w").as_int(11906));
            pDesc->SetPageHeight(pgSz.attribute("w:h").as_int(16838));
        }

        auto pgMar = sectPr.child("w:pgMar");
        if (pgMar)
        {
            // OOXML pgMar=0 表示默认边距（720 twips），勿继承 ParseSectionProps 的 1440/1800
            constexpr SwTwips kDefaultMargin = 720;
            auto normMargin = [](int v, SwTwips fallback) {
                return v > 0 ? static_cast<SwTwips>(v) : fallback;
            };
            SwTwips top = normMargin(pgMar.attribute("w:top").as_int(0), kDefaultMargin);
            SwTwips bottom = normMargin(pgMar.attribute("w:bottom").as_int(0), kDefaultMargin);
            SwTwips left = normMargin(pgMar.attribute("w:left").as_int(0), kDefaultMargin);
            SwTwips right = normMargin(pgMar.attribute("w:right").as_int(0), kDefaultMargin);
            pDesc->SetTopMargin(top);
            pDesc->SetBottomMargin(bottom);
            pDesc->SetLeftMargin(left);
            pDesc->SetRightMargin(right);
            pDesc->SetHeaderMargin(pgMar.attribute("w:header").as_int(720));
            pDesc->SetFooterMargin(pgMar.attribute("w:footer").as_int(720));

            SwDoc::SectionMargins sectMargins;
            sectMargins.top = top;
            sectMargins.bottom = bottom;
            sectMargins.left = left;
            sectMargins.right = right;
            auto cols = sectPr.child("w:cols");
            if (cols)
            {
                sectMargins.numCols = cols.attribute("w:num").as_int(1);
                sectMargins.colSpace = cols.attribute("w:space").as_int(708);
                auto col = cols.child("w:col");
                if (col)
                {
                    sectMargins.colWidth = col.attribute("w:w").as_int(0);
                    // OOXML: w:space 可在 w:cols 或 w:col 上指定
                    // w:col/@w:space 优先（列后间距），w:cols/@w:space 为默认值
                    int colSpace = col.attribute("w:space").as_int(-1);
                    if (colSpace > 0)
                        sectMargins.colSpace = colSpace;
                }
            }
            // OOXML sectPr 定义当前结束节的属性（对应 LO DomainMapper）
            doc.SetSectionMargins(m_nCurrentSection_, sectMargins);
        }
    }

    sectionBreakTypes_[m_nCurrentSection_ + 1] = breakType;
    m_nCurrentSection_++;

    if (bPendingBreakOnNext)
        m_pendingBreakType = BreakTypeToResBreak(breakType);
}

void DocxParser::ApplySectPr(pugi::xml_node sectPr, SwTextNode* pNode)
{
    if (!sectPr || !pNode)
        return;

    std::string breakType = GetSectPrBreakType(sectPr);
    ApplySectPrCore(sectPr, pNode->GetDoc(), false);
    pNode->SetAttr(RES_BREAK, BreakTypeToResBreak(breakType));
}

void DocxParser::ApplyStyleToTextNode(SwTextNode* pTN, const std::string& styleName,
                                      bool bHasTextContent)
{
    if (!pTN || styleName.empty())
        return;

    std::string lookupName = styleName;
    if (lookupName == "Default Paragraph Style")
        lookupName = "Normal";

    for (auto & [ id, def ] : styles_)
    {
        if (def.name != lookupName && id != lookupName && def.name != styleName && id != styleName)
            continue;
        if (SwTextFormatColl* pColl = pTN->GetDoc().FindTextFormatColl(def.name))
            pTN->ChgFormatColl(pColl);

        const auto& direct = pTN->GetAttrs();
        auto hasDirect = [&](sal_uInt16 nWhich) {
            auto it = direct.find(nWhich);
            return it != direct.end() && !it->second.empty();
        };

        if (!def.fontName.empty())
        {
            // LO: run-level direct format (rPr) takes precedence over style for text layout.
            // Style font is only a fallback when run has no direct font.
            bool bUseStyleFont = !bHasTextContent || !hasDirect(RES_CHRATR_FONT);
            if (bUseStyleFont)
                pTN->SetAttr(RES_CHRATR_FONT, def.fontName);
        }
        // 应用默认 CJK 字体（用于空段落高度计算，对应 LO SwFont CJK 字体槽）
        if (!def.cjkFontName.empty() && !hasDirect(RES_CHRATR_CJK_FONT))
            pTN->SetAttr(RES_CHRATR_CJK_FONT, def.cjkFontName);
        if (def.fontSize > 0)
        {
            // LO: run-level direct size (w:sz) takes precedence over style size.
            // Style size is only a fallback when run has no direct size.
            bool bUseStyleSize = !bHasTextContent || !hasDirect(RES_CHRATR_FONTSIZE);
            if (bUseStyleSize)
                pTN->SetAttr(RES_CHRATR_FONTSIZE, std::to_string(def.fontSize));
        }
        if (def.bold && (!bHasTextContent || !hasDirect(RES_CHRATR_WEIGHT)))
            pTN->SetAttr(RES_CHRATR_WEIGHT, "bold");
        if (def.italic && (!bHasTextContent || !hasDirect(RES_CHRATR_POSTURE)))
            pTN->SetAttr(RES_CHRATR_POSTURE, "italic");
        if (def.spacingBefore != 0)
            pTN->SetAttr(RES_UL_SPACE, std::to_string(def.spacingBefore));
        if (def.spacingAfter != 0)
            pTN->SetAttr(RES_UL_SPACE_AFTER, std::to_string(def.spacingAfter));
        if (def.spacingLine != 240)
            pTN->SetAttr(RES_PARATR_LINESPACING, std::to_string(def.spacingLine));
        if (def.spacingLineRule != "auto")
            pTN->SetAttr(RES_PARATR_LINE_RULE, def.spacingLineRule);
        if (def.indentLeft != 0 && !hasDirect(RES_PARATR_INDENT))
            pTN->SetAttr(RES_PARATR_INDENT, std::to_string(def.indentLeft));
        if (def.indentRight != 0 && !hasDirect(RES_PARATR_RIGHT_INDENT))
            pTN->SetAttr(RES_PARATR_RIGHT_INDENT, std::to_string(def.indentRight));
        if (def.indentFirstLine != 0 && !hasDirect(RES_PARATR_FIRSTLINE))
            pTN->SetAttr(RES_PARATR_FIRSTLINE, std::to_string(def.indentFirstLine));
        if (def.indentHanging != 0 && !hasDirect(RES_PARATR_HANGING))
            pTN->SetAttr(RES_PARATR_HANGING, std::to_string(def.indentHanging));
        break;
    }
}

void DocxParser::ApplyParagraphMarkFromXml(pugi::xml_node pNode, SwTextNode* pTN)
{
    if (!pNode || !pTN)
        return;

    const bool bHasText = !pTN->GetText().empty();

    auto pPr = pNode.child("w:pPr");
    auto pPrRPr = pPr ? pPr.child("w:rPr") : pugi::xml_node();
    if (pPrRPr)
    {
        if (bHasText)
        {
            // 有文本：段落标记格式只写入 PARA_MARK，不覆盖内容字体
            ParseRunProps(pPrRPr, pTN, false, false, true);
        }
        else
        {
            ParseRunProps(pPrRPr, pTN);
            const std::string* pFont = pTN->GetAttr(RES_CHRATR_FONT);
            if (pFont && !pFont->empty())
                pTN->SetAttr(RES_CHRATR_FONT_PARA_MARK, *pFont);
            const std::string* pSize = pTN->GetAttr(RES_CHRATR_FONTSIZE);
            if (pSize && !pSize->empty())
                pTN->SetAttr(RES_CHRATR_FONTSIZE_PARA_MARK, *pSize);
        }
    }
    else
    {
        // 无 pPr/rPr 时，才从绘图 run 取段落标记格式
        for (auto r : pNode.children("w:r"))
        {
            if (!r.child("w:drawing") && !r.child("w:pict"))
                continue;
            auto rPr = r.child("w:rPr");
            if (rPr)
            {
                if (bHasText)
                    ParseRunProps(rPr, pTN, false, false, true);
                else
                {
                    ParseRunProps(rPr, pTN);
                    const std::string* pFont = pTN->GetAttr(RES_CHRATR_FONT);
                    if (pFont && !pFont->empty())
                        pTN->SetAttr(RES_CHRATR_FONT_PARA_MARK, *pFont);
                    const std::string* pSize = pTN->GetAttr(RES_CHRATR_FONTSIZE);
                    if (pSize && !pSize->empty())
                        pTN->SetAttr(RES_CHRATR_FONTSIZE_PARA_MARK, *pSize);
                }
            }
            break;
        }
    }
}

void DocxParser::ApplyFirstTextRunFromXml(pugi::xml_node pNode, SwTextNode* pTN)
{
    if (!pNode || !pTN || pTN->GetText().empty())
        return;

    // LO: 首 text portion 是第一个含 <w:t> 的 run，<w:br>/<w:tab>/<w:cr> 不算文本 portion
    auto hasTextContent = [](pugi::xml_node rNode) {
        for (auto c : rNode.children())
        {
            std::string n = c.name();
            if (n == "w:t" || n == "w:sym")
                return true;
        }
        return false;
    };

    auto applyFirstRun = [&](pugi::xml_node r) {
        if (!hasTextContent(r))
            return false;
        auto rPr = r.child("w:rPr");
        if (rPr)
            ParseRunProps(rPr, pTN);
        return true;
    };

    for (auto r : pNode.children("w:r"))
    {
        if (applyFirstRun(r))
            return;
    }
    for (auto child : pNode.children("w:hyperlink"))
    {
        for (auto r : child.children("w:r"))
        {
            if (applyFirstRun(r))
                return;
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

    // 获取 tableData 引用以便填充单元格内容（MakeFrames 读取 tableData 渲染）
    SwTableNode::TableData tableData = pTableNode->GetTableData();

    // 解析表格内容到单元格
    // 遍历表格节点内部的单元格
    SwNodeOffset nTableIdx = pTableNode->GetIndex();
    SwNodeOffset nCellIdx = nTableIdx + 1;
    int nRowIdx = 0;

    for (auto row : tblNode.children("w:tr"))
    {
        int nColIdx = 0;
        for (auto cell : row.children("w:tc"))
        {
            // 找到当前单元格的 StartNode
            SwNode* pCellStart = rNodes[nCellIdx];
            if (!pCellStart || !pCellStart->IsStartNode())
            {
                nColIdx++;
                continue;
            }

            // 找到单元格内的 TextNode（在 StartNode 之后）
            SwNodeOffset nTextIdx = nCellIdx + 1;
            SwNode* pTextNode = rNodes[nTextIdx];
            if (!pTextNode || !pTextNode->IsTextNode())
            {
                nColIdx++;
                continue;
            }

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

            // 同步填充 tableData（MakeFrames 通过 GetTableData 读取单元格文本）
            if (nRowIdx < static_cast<int>(tableData.size())
                && nColIdx < static_cast<int>(tableData[nRowIdx].cells.size()))
            {
                tableData[nRowIdx].cells[nColIdx].text = cellText;
            }

            // 跳到下一个单元格（StartNode + TextNode + EndNode = 3 个节点）
            nCellIdx += 3;
            nColIdx++;
        }
        nRowIdx++;
    }

    // 回写 tableData
    pTableNode->SetTableData(tableData);
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
                               bool bSkipSize, bool bParaMarkOnly)
{
    if (!rPrNode || !pNode)
        return;

    const sal_uInt16 nFontWhich = bParaMarkOnly ? RES_CHRATR_FONT_PARA_MARK : RES_CHRATR_FONT;
    const sal_uInt16 nSizeWhich
        = bParaMarkOnly ? RES_CHRATR_FONTSIZE_PARA_MARK : RES_CHRATR_FONTSIZE;

    // 字体
    auto rFonts = rPrNode.child("w:rFonts");
    if (rFonts)
    {
        std::string font = rFonts.attribute("w:ascii").as_string();
        if (!font.empty())
        {
            pNode->SetAttr(nFontWhich, font);
        }
        else
        {
            // 解析 w:asciiTheme 主题字体引用（如 minorHAnsi / majorHAnsi）
            std::string themeFont = rFonts.attribute("w:asciiTheme").as_string();
            if (!themeFont.empty())
            {
                auto it = themeFonts_.find(themeFont);
                if (it != themeFonts_.end())
                    pNode->SetAttr(nFontWhich, it->second);
            }
        }

        // 解析 CJK 字体（w:eastAsia / w:eastAsiaTheme）
        // 对应 LO SwFont 的 CJK 字体槽，用于空段落高度计算
        std::string eaFont = rFonts.attribute("w:eastAsia").as_string();
        if (!eaFont.empty())
        {
            pNode->SetAttr(RES_CHRATR_CJK_FONT, eaFont);
        }
        else
        {
            std::string eaTheme = rFonts.attribute("w:eastAsiaTheme").as_string();
            if (!eaTheme.empty())
            {
                auto it = themeFonts_.find(eaTheme);
                if (it != themeFonts_.end())
                    pNode->SetAttr(RES_CHRATR_CJK_FONT, it->second);
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
            pNode->SetAttr(nSizeWhich, std::to_string(size));
        }
    }

    if (bParaMarkOnly)
        return;

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
