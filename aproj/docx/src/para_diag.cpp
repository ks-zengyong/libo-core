// 调试：打印特定段落的所有属性
#include <iostream>
#include <windows.h>
#include <string>
#include <vector>
#include "../src/core/node.h"
#include "../src/core/format.h"
#include "../src/filter/docx_parser.h"
#include "../src/core/doc.h"

int main(int argc, char* argv[]) {
    const char* path = (argc > 1) ? argv[1] : "e:/lo/libo-core/aproj/docx/samples/sample0.docx";
    std::cout << "Parsing: " << path << std::endl;

    SwDoc doc;
    DocxParser parser;
    if (!parser.Read(path, doc)) {
        std::cout << "Parse failed!" << std::endl;
        return 1;
    }

    // 查找包含 "Share" 的段落
    SwNodeOffset idx(0);
    SwNodes& nodes = doc.GetNodes();
    int paraCount = 0;
    for (SwNode* nd = nodes[idx]; nd != nullptr; nd = nodes[++idx]) {
        if (!nd->IsTextNode()) continue;
        SwTextNode* tn = nd->GetTextNode();
        const std::string& text = tn->GetText();
        if (text.find("Share") != std::string::npos || text.find("Find") != std::string::npos ||
            (!text.empty() && text[0] == '\n')) {
            paraCount++;
            std::cout << "\n=== Paragraph " << paraCount << " ===" << std::endl;
            std::cout << "Text: " << text.substr(0, 80) << std::endl;
            std::cout << "Style: " << tn->GetStyleName() << std::endl;
            std::cout << "Font: " << (tn->GetAttr(RES_CHRATR_FONT) ? tn->GetAttr(RES_CHRATR_FONT)->c_str() : "(null)") << std::endl;
            std::cout << "ParaMarkFont: " << (tn->GetAttr(RES_CHRATR_FONT_PARA_MARK) ? tn->GetAttr(RES_CHRATR_FONT_PARA_MARK)->c_str() : "(null)") << std::endl;
            std::cout << "Size: " << (tn->GetAttr(RES_CHRATR_FONTSIZE) ? tn->GetAttr(RES_CHRATR_FONTSIZE)->c_str() : "(null)") << std::endl;
            std::cout << "CJKFont: " << (tn->GetAttr(RES_CHRATR_CJK_FONT) ? tn->GetAttr(RES_CHRATR_CJK_FONT)->c_str() : "(null)") << std::endl;
            std::cout << "Indent: " << (tn->GetAttr(RES_PARATR_INDENT) ? tn->GetAttr(RES_PARATR_INDENT)->c_str() : "(null)") << std::endl;
            std::cout << "RightIndent: " << (tn->GetAttr(RES_PARATR_RIGHT_INDENT) ? tn->GetAttr(RES_PARATR_RIGHT_INDENT)->c_str() : "(null)") << std::endl;
            std::cout << "FirstLine: " << (tn->GetAttr(RES_PARATR_FIRSTLINE) ? tn->GetAttr(RES_PARATR_FIRSTLINE)->c_str() : "(null)") << std::endl;
            std::cout << "Hanging: " << (tn->GetAttr(RES_PARATR_HANGING) ? tn->GetAttr(RES_PARATR_HANGING)->c_str() : "(null)") << std::endl;
            std::cout << "LineSpacing: " << (tn->GetAttr(RES_PARATR_LINESPACING) ? tn->GetAttr(RES_PARATR_LINESPACING)->c_str() : "(null)") << std::endl;
            std::cout << "LineRule: " << (tn->GetAttr(RES_PARATR_LINE_RULE) ? tn->GetAttr(RES_PARATR_LINE_RULE)->c_str() : "(null)") << std::endl;
            std::cout << "SpaceBefore: " << (tn->GetAttr(RES_UL_SPACE) ? tn->GetAttr(RES_UL_SPACE)->c_str() : "(null)") << std::endl;
            std::cout << "SpaceAfter: " << (tn->GetAttr(RES_UL_SPACE_AFTER) ? tn->GetAttr(RES_UL_SPACE_AFTER)->c_str() : "(null)") << std::endl;
            std::cout << "ParaMarkSize: " << (tn->GetAttr(RES_CHRATR_FONTSIZE_PARA_MARK) ? tn->GetAttr(RES_CHRATR_FONTSIZE_PARA_MARK)->c_str() : "(null)") << std::endl;
            std::cout << "StyleSize: " << (tn->GetAttr(RES_CHRATR_FONTSIZE_STYLE) ? tn->GetAttr(RES_CHRATR_FONTSIZE_STYLE)->c_str() : "(null)") << std::endl;
        }
    }
    return 0;
}
