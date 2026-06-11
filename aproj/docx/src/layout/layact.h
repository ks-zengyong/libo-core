#pragma once
// 简化版 SwLayAction，对应 LibreOffice 的 sw/source/core/layout/layact.cxx
// 排版编排器

#include "../core/types.h"
#include "../frame/frame.h"
#include <vector>
#include <string>

// 前向声明
class SwDoc;
class SwRootFrame;
class SwPageFrame;
class SwTextFrame;
class FontEngine;

// SwLayAction: 排版动作编排器
class SwLayAction
{
public:
    SwLayAction(SwRootFrame& rRoot, FontEngine* pFontEngine = nullptr);
    ~SwLayAction();

    // 执行排版
    void Action();

    // 格式化所有内容
    void FormatAll();

    // 格式化单个页面
    void FormatPage(SwPageFrame* pPage);

    // 格式化单个 Frame
    void FormatFrame(SwFrame* pFrame);

    // 设置/获取标志
    void SetPaint(bool b) { m_bPaint = b; }
    bool IsPaint() const { return m_bPaint; }

    void SetComplete(bool b) { m_bComplete = b; }
    bool IsComplete() const { return m_bComplete; }

private:
    SwRootFrame& m_rRoot;
    FontEngine* m_pFontEngine;
    bool m_bPaint;
    bool m_bComplete;
    bool m_bAgain;

    // 内部方法
    void InternalAction();
    void FormatLayout(SwLayoutFrame* pLayout);
    void FormatContent(SwLayoutFrame* pLayout);
};

// TextFormatter: 文本格式化器
class TextFormatter
{
public:
    TextFormatter(FontEngine* pFontEngine);
    ~TextFormatter();

    // 格式化文本 Frame
    void FormatTextFrame(SwTextFrame* pFrame);

    // 计算行高
    int CalcLineHeight(const std::string& fontName, int fontSize);

    // 计算字符串宽度
    int CalcStringWidth(const std::string& text, const std::string& fontName, int fontSize);

private:
    FontEngine* m_pFontEngine;

    // 换行算法
    struct LineBreak
    {
        int startPos;
        int endPos;
        int width;
        int height;
    };

    std::vector<LineBreak> BreakIntoLines(const std::string& text, const std::string& fontName,
                                          int fontSize, int maxWidth);
};
