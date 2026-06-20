// 测试 Poppins 字体下的文本宽度和换行行为
#include <iostream>
#include "../src/font/font_engine.h"

int main() {
    FontEngine& fe = FontEngine::Instance();
    
    std::string text = "Share your documents with others.";
    int size = 24;  // half-pt = 12pt
    int effW = 3306;  // effective width
    
    std::cout << "Text: " << text << " (" << text.size() << " chars)" << std::endl;
    std::cout << "Size: " << size << " half-pt = " << size/2 << "pt" << std::endl;
    std::cout << "Effective width: " << effW << " twips" << std::endl << std::endl;
    
    std::string fonts[] = {"Poppins", "Segoe UI Emoji", "Calibri", 
                           "Segoe UI", "DejaVu Sans", "Liberation Serif", "Times New Roman"};
    
    for (const std::string& font : fonts) {
        int w = fe.MeasureTextWidth(font, size, text);
        int h = fe.MeasureTextHeight(font, size);
        std::cout << font << ": width=" << w << " height=" << h;
        
        // 计算行数
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
        std::cout << " lines=" << lines << std::endl;
    }
    
    // 对于 "Find all of Writer's commands." - 这个段落有 hanging indent
    std::cout << std::endl;
    std::string text2 = "\nFind all of Writer's commands.";
    int left = 8598;
    int right = 753;
    int hanging = 198;
    int firstLineW = 11906 - left - right;  // 2555
    int subseqW = 11906 - left - right - hanging;  // 2357
    
    std::cout << "Find all paragraph:" << std::endl;
    std::cout << "Text: " << text2 << " (" << text2.size() << " chars)" << std::endl;
    std::cout << "First line width: " << firstLineW << " twips" << std::endl;
    std::cout << "Subsequent lines width: " << subseqW << " twips" << std::endl << std::endl;
    
    for (const std::string& font : fonts) {
        int w = fe.MeasureTextWidth(font, size, text2);
        int pos = 0;
        int lines = 0;
        int curWidth = firstLineW;
        while (pos < (int)text2.size()) {
            int brk = fe.FindLineBreak(font, size, text2.substr(pos), curWidth);
            curWidth = subseqW;
            if (brk < 0 || brk >= (int)text2.size() - pos) {
                lines++;
                break;
            }
            if (brk == 0) brk = 1;
            pos += brk;
            lines++;
        }
        std::cout << font << ": width=" << w << " lines=" << lines << std::endl;
    }
    
    std::cout << std::endl;
    std::cout << "LO results for comparison:" << std::endl;
    std::cout << "  Share your documents: height=772 (approx 3 lines)" << std::endl;
    std::cout << "  Find all: height=1640 (approx 6 lines)" << std::endl;
    
    return 0;
}
