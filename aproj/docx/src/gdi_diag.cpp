// GDI 字体回退诊断
#include <windows.h>
#include <iostream>
#include <string>
#include <vector>

int main() {
    std::string fonts[] = {
        "Poppins", "Segoe UI Emoji", "DejaVu Sans", "Calibri",
        "Segoe UI", "Times New Roman", "Arial", "Microsoft Sans Serif"
    };
    std::string text = "Share your documents with others.";
    int halfPt = 24;
    int pixels = static_cast<int>(halfPt * 2.0f / 3.0f);
    int maxW = 3306;

    std::cout << "Text: " << text << " (" << text.size() << " chars)" << std::endl;
    std::cout << "Target width: " << maxW << " twips" << std::endl;
    std::cout << std::endl;

    for (const std::string& font : fonts) {
        HDC hdc = CreateCompatibleDC(NULL);
        if (!hdc) continue;

        HFONT hFont = CreateFontA(-pixels, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, font.c_str());

        // 获取实际字体名
        HFONT hOld = (HFONT)SelectObject(hdc, hFont);
        TEXTMETRICA tm;
        GetTextMetricsA(hdc, &tm);
        char actualName[256];
        GetTextFaceA(hdc, 256, actualName);

        // 测量文本宽度
        int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), text.size(), NULL, 0);
        std::vector<wchar_t> wtext(wlen);
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), text.size(), wtext.data(), wlen);
        SIZE size;
        GetTextExtentPoint32W(hdc, wtext.data(), wlen, &size);

        std::cout << "[" << font << "]" << std::endl;
        std::cout << "  actual: " << actualName << std::endl;
        std::cout << "  tmHeight: " << tm.tmHeight << " extLeading: " << tm.tmExternalLeading << std::endl;
        std::cout << "  lineHeight twips: " << (tm.tmHeight + tm.tmExternalLeading) * 15 << std::endl;
        std::cout << "  text width: " << size.cx * 15 << " twips" << std::endl;

        // 二分查找断点
        int lo = 0, hi = wlen + 1;
        int lb = 0;
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (mid > wlen) mid = wlen;
            SIZE s;
            GetTextExtentPoint32W(hdc, wtext.data(), mid, &s);
            if (s.cx * 15 <= maxW) { lb = mid; lo = mid + 1; }
            else hi = mid;
        }

        int wb = lb;
        while (wb > 0 && wtext[wb - 1] != L' ') --wb;
        int finalBreak = wb > 0 ? wb : (lb > 1 ? lb - 1 : 1);

        std::cout << "  break(char): " << lb << " (w/ word-boundary: " << finalBreak << ")" << std::endl;
        std::cout << "  remaining: " << (wlen - finalBreak) << " chars" << std::endl;

        // 对剩余文本继续
        int lineCount = 1;
        int pos = finalBreak;
        while (pos < wlen) {
            lo = pos;
            hi = wlen + 1;
            int lb2 = pos;
            while (lo < hi) {
                int mid = (lo + hi) / 2;
                if (mid > wlen) mid = wlen;
                SIZE s;
                GetTextExtentPoint32W(hdc, wtext.data() + pos, mid - pos, &s);
                if (s.cx * 15 <= maxW) { lb2 = mid; lo = mid + 1; }
                else hi = mid;
            }
            int wb2 = lb2;
            while (wb2 > pos && wtext[wb2 - 1] != L' ') --wb2;
            int fb = wb2 > pos ? wb2 : (lb2 > pos ? lb2 : pos + 1);
            std::cout << "  line " << lineCount << ": break at char " << fb << std::endl;
            pos = fb;
            lineCount++;
        }
        std::cout << "  total lines: " << lineCount << std::endl;
        std::cout << "  est height: " << lineCount * (tm.tmHeight + tm.tmExternalLeading) * 15 << " twips" << std::endl;
        std::cout << std::endl;

        SelectObject(hdc, hOld);
        DeleteObject(hFont);
        DeleteDC(hdc);
    }

    std::cout << "\nLO reference: height=" << 772 << " (~3 lines at 257 twips/line)" << std::endl;
    return 0;
}
