#pragma once
// 新版 DOCX 解析器，输出 SwDoc（替代现有 DocxReader）
// 对应 LibreOffice 的 WriterFilter → DomainMapper 管线

#include "../core/types.h"
#include "../core/doc.h"
#include "../core/node.h"
#include "../core/format.h"
#include <string>
#include <map>
#include <vector>
#include <cstdint>
#include "pugixml.hpp"

// DocxParser: 解析 .docx 文件，填充 SwDoc
class DocxParser
{
public:
    DocxParser() = default;
    ~DocxParser() = default;

    // 读取 .docx 文件，填充 SwDoc
    // 返回 true 表示成功
    bool Read(const std::string& filePath, SwDoc& doc);

private:
    // ZIP 条目数据
    struct ZipEntry
    {
        std::string name;
        std::vector<uint8_t> data;
    };

    // 解析阶段
    bool ExtractZip(const std::string& filePath);
    void ParseContentTypes();
    void ParseStyles(SwDoc& doc);
    void ParseFontTable(SwDoc& doc);
    void ParseNumbering(SwDoc& doc);
    void ParseDocument(SwDoc& doc);
    void ParseHeaderFooter(const std::string& xml, SwDoc& doc);

    // 文档解析子方法
    void ParseBody(pugi::xml_node bodyNode, SwDoc& doc);
    void ParseParagraph(pugi::xml_node pNode, SwDoc& doc);
    void ParseTable(pugi::xml_node tblNode, SwDoc& doc);
    void ParseSdt(pugi::xml_node sdtNode, SwDoc& doc);
    void ParseSectionProps(pugi::xml_node sectPrNode, SwDoc& doc);

    // 段落/文本解析
    std::string ParseRunText(pugi::xml_node rNode);
    void ParseRunProps(pugi::xml_node rPrNode, SwTextNode* pNode, bool bSkipColor = false,
                       bool bSkipSize = false);
    void ParseParagraphProps(pugi::xml_node pPrNode, SwTextNode* pNode);

    // 属性解析辅助
    std::string GetAttr(pugi::xml_node node, const char* name, const char* ns = nullptr);
    int GetAttrInt(pugi::xml_node node, const char* name, int def = 0);
    bool GetAttrBool(pugi::xml_node node, const char* name, bool def = false);

    // 关系解析
    std::map<std::string, std::string> ParseRelationships(const std::string& xml);
    std::string ResolveImage(const std::string& relId);

    // 样式继承解析
    struct StyleDef
    {
        std::string name;
        std::string type; // "paragraph", "character", "table", "numbering"
        std::string basedOn;
        std::string nextStyle;
        std::string linkStyle;
        bool isDefault = false;
        // 段落属性
        std::string alignment;
        int spacingBefore = 0;
        int spacingAfter = 0;
        int spacingLine = 240;
        int indentLeft = 0;
        int indentRight = 0;
        int indentFirstLine = 0;
        bool pageBreakBefore = false;
        bool keepNext = false;
        bool keepLines = false;
        // 字符属性
        std::string fontName;
        int fontSize = 22; // 半点
        bool bold = false;
        bool italic = false;
        std::string color;
    };

    // 编号定义
    struct NumLevelDef
    {
        int numFmt = 0; // 0=none, 1=decimal, 2=roman, 3=letter, 4=bullet
        std::string lvlText;
        int startVal = 1;
        std::string bulletChar;
        int indentLeft = 0;
        int hangingIndent = 0;
        std::string fontName;
    };

    struct NumDef
    {
        int abstractNumId = -1;
        std::vector<NumLevelDef> levels;
    };

    // 内部状态
    std::map<std::string, ZipEntry> entries_;
    std::map<std::string, std::string> rels_;
    std::map<std::string, StyleDef> styles_;
    std::map<int, NumDef> numDefs_; // numId → NumDef
    std::map<int, std::vector<NumLevelDef>> abstractNumDefs_; // abstractNumId → levels
    std::string docDir_;

    // 主题字体映射：minorHAnsi → "Calibri" 等
    std::map<std::string, std::string> themeFonts_;

    // 解析主题文件
    void ParseTheme();

    // 当前解析状态
    SwTextNode* curTextNode_ = nullptr;
    SwTextFormatColl* curStyle_ = nullptr;
};
