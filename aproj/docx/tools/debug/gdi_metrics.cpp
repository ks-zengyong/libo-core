#ifdef _WIN32
#include <windows.h>
#include <cstdio>

static void testFont(const char* name, int halfPt)
{
    float px = halfPt * 2.0f / 3.0f;
    int nHeight = -static_cast<int>(px * 72.0 / 96.0 + 0.5);
    HDC hdc = CreateCompatibleDC(NULL);
    HFONT hFont = CreateFontA(nHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH | FF_DONTCARE, name);
    HFONT hOld = (HFONT)SelectObject(hdc, hFont);
    TEXTMETRICA tm{};
    GetTextMetricsA(hdc, &tm);
    int twH = tm.tmHeight * 15;
    int twHExt = (tm.tmHeight + tm.tmExternalLeading) * 15;
    int minLay = halfPt * 14;
    printf("%s halfPt=%d px=%.1f tmH=%d ext=%d asc=%d desc=%d twH=%d twH+ext=%d min=%d\n", name,
           halfPt, px, tm.tmHeight, tm.tmExternalLeading, tm.tmAscent, tm.tmDescent, twH, twHExt,
           minLay);
    SelectObject(hdc, hOld);
    DeleteObject(hFont);
    DeleteDC(hdc);
}

int main()
{
    testFont("Segoe UI Semibold", 36);
    testFont("fony family", 24);
    testFont("Calibri", 20);
    testFont("Segoe UI Emoji", 24);
    return 0;
}
#endif
