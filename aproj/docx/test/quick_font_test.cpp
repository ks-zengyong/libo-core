#include <iostream>
#include "../src/font/font_engine.h"

int main() {
    FontEngine& fe = FontEngine::Instance();
    std::string text = "Share your documents with others.";
    int size = 24;
    
    std::cout << "Text: " << text << " (" << text.size() << " chars)" << std::endl;
    std::cout << "Font size: " << size << " half pt" << std::endl << std::endl;
    
    std::string fonts[] = {"Poppins", "Segoe UI Emoji", "Calibri", "DejaVu Sans"};
    for (const std::string& font : fonts) {
        int w = fe.MeasureTextWidth(font, size, text);
        int h = fe.MeasureTextHeight(font, size);
        std::cout << font << ": width=" << w << " height=" << h << std::endl;
    }
    
    std::cout << std::endl;
    std::cout << "Effective width for this paragraph (colWidth-leftIndent):" << std::endl;
    std::cout << "  11906 - 8600 = 3306 twips" << std::endl << std::endl;
    
    // 换行测试
    int effW = 3306;
    for (const std::string& font : fonts) {
        std::cout << font << ": ";
        int pos = 0;
        int lines = 0;
        while (pos < (int)text.size()) {
            int brk = fe.FindLineBreak(font, size, text.substr(pos), effW);
            if (brk < 0 || brk >= (int)text.size() - pos) {
                lines++;
                break;
            }
            if (brk == 0) brk = 1;
            pos += brk;
            lines++;
        }
        std::cout << lines << " lines" << std::endl;
    }
    
    return 0;
}
