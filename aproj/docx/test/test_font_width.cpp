#include <iostream>
#include <string>
#include "../src/font/font_engine.h"

int main() {
    FontEngine& fe = FontEngine::Instance();
    
    std::string text = "Share your documents with others.";
    std::string fonts[] = {"Poppins", "DejaVu Sans", "Segoe UI Emoji", "Calibri", "Segoe UI"};
    int sizes[] = {24, 28, 36};
    
    for (const std::string& font : fonts) {
        for (int sz : sizes) {
            int width = fe.MeasureTextWidth(font, sz, text);
            int height = fe.MeasureTextHeight(font, sz);
            std::cout << "Font: " << font << " size: " << sz 
                      << " width: " << width << " height: " << height << std::endl;
        }
        std::cout << std::endl;
    }
    
    // 计算不同宽度下的行数（第一行有效宽度=3306）
    std::string font = "Poppins";
    int size = 24;
    int colWidth = 11906;
    int effectiveWidth = colWidth - 8600;  // 3306
    
    std::cout << "=== Line break tests ===" << std::endl;
    std::cout << "Text: " << text << " (" << text.size() << " chars)" << std::endl;
    std::cout << "Text width: " << fe.MeasureTextWidth(font, size, text) << " @ " << font << "/" << size << std::endl;
    std::cout << "Effective width: " << effectiveWidth << std::endl;
    
    // 测试 GetTextBreak
    int remain = text.size();
    int pos = 0;
    int lineCount = 0;
    while (remain > 0) {
        int brk = fe.FindLineBreak(font, size, text.substr(pos), effectiveWidth);
        std::cout << "Line " << lineCount << ": pos=" << pos << " break=" << brk << std::endl;
        if (brk < 0 || brk >= remain) {
            lineCount++;
            break;
        }
        if (brk == 0) brk = 1;
        pos += brk;
        remain -= brk;
        lineCount++;
    }
    std::cout << "Total lines: " << lineCount << std::endl;
    
    return 0;
}
