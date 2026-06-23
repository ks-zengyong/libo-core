// 字体宽度/高度诊断程序
#include "font_engine.h"
#include <iostream>

int main(int argc, char* argv[])
{
    using namespace std;
    
    cout << "=== 字体诊断工具 ===" << endl;
    
    // 测试字体
    string fonts[] = {"Poppins", "Segoe UI Emoji", "Segoe UI", "Calibri", 
                     "DejaVu Sans", "Liberation Sans", "Times New Roman",
                     "Segoe UI Semibold", "Calibri Light"};
    
    // 测试文本（从实际文档抽取）
    string testTexts[] = {
        "Share your documents with others.",
        "Find all of Writer's commands.",
        "Search the Help center for answers when you get stuck.",
        "Quickly switch views and adjust the scale of the page display.",
    };
    
    // 字号（half-pt）
    int sizes[] = {24, 36, 22};
    
    FontEngine& fe = FontEngine::Instance();
    
    // 实际列宽度和缩进
    int colWidth = 11906;
    int para7Left = 8600;
    int para8Left = 8598;
    int para8Right = 753;
    int para8Hanging = 198;
    
    int para7Effective = colWidth - para7Left;  // = 3306
    int para7Right = 0;
    int para8FirstLine = colWidth - para8Left + para8Hanging - para8Right; // hanging firstLine=-198
    int para8Subseq = colWidth - para8Left - para8Right;
    int para9Left = 8600;
    int para9Right = 93;
    int para9Width = colWidth - para9Left - para9Right;
    int para10Left = 8600;
    int para10Right = 388;
    int para10Width = colWidth - para10Left - para10Right;
    
    cout << "Para7 effective width: " << para7Effective << " twips" << endl;
    cout << "Para8 firstLine width: " << para8FirstLine << " twips" << endl;
    cout << "Para8 subseq width: " << para8Subseq << " twips" << endl;
    cout << "Para9 (Search) width: " << para9Width << " twips" << endl;
    cout << "Para10 (Quickly) width: " << para10Width << " twips" << endl;
    cout << endl;
    
    // 测试每种字体
    for (int s : sizes) {
        cout << "\n=== Font size: " << s << " half-pt (" << s/2.0 << "pt) ===" << endl;
        
        for (const string& f : fonts) {
            cout << "\n[" << f << "]" << endl;
            int h = fe.MeasureTextHeight(f, s);
            cout << "  Height: " << h << " twips" << endl;
            
            for (const string& t : testTexts) {
                int w = fe.MeasureTextWidth(f, s, t);
                cout << "  Width(" << t.substr(0, 30) << "...): " << w << " twips" << endl;
                
                // 计算用 para7Effective 做的行数
                int pos = 0;
                int lines = 0;
                int curW = para7Effective;
                while (pos < (int)t.size()) {
                    int brk = fe.FindLineBreak(f, s, t.substr(pos), curW);
                    curW = para7Effective;
                    if (brk <= 0 || brk >= (int)t.size() - pos) {
                        lines++;
                        break;
                    }
                    pos += brk;
                    lines++;
                }
                cout << "    Lines(para7 effective): " << lines;
                cout << "  EstHeight: " << lines * h << " twips" << endl;
                
                // 用 para8/9/10 宽度计算
                auto countLines = [&](int firstW, int subW) {
                    int pos = 0;
                    int lines = 0;
                    int curW = firstW;
                    while (pos < (int)t.size()) {
                        int brk = fe.FindLineBreak(f, s, t.substr(pos), curW);
                        curW = subW;
                        if (brk <= 0 || brk >= (int)t.size() - pos) {
                            lines++;
                            break;
                        }
                        pos += brk;
                        lines++;
                    }
                    return lines;
                };
                int n8 = countLines(para8FirstLine, para8Subseq);
                cout << "    Lines(para8 widths): " << n8;
                cout << "  EstHeight: " << n8 * h << " twips" << endl;
                if (t.find("Search the Help") == 0) {
                    int n9 = countLines(para9Width, para9Width);
                    cout << "    Lines(para9 widths): " << n9 << endl;
                }
                if (t.find("Quickly switch") == 0) {
                    int n10 = countLines(para10Width, para10Width);
                    cout << "    Lines(para10 widths): " << n10 << endl;
                }
            }
        }
    }
    
    // LO 参考值
    cout << "\n\n=== LO 参考值 ===" << endl;
    cout << "Para7 (Share your documents): height=772, ~3 lines" << endl;
    cout << "Para8 (Find all): height=1640, ~6 lines" << endl;
    
    return 0;
}
