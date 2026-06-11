// 简化版 SwLayAction 实现

#include "layact.h"
#include "../core/node.h"
#include "../core/doc.h"
#include "../core/format.h"
#include <algorithm>
#include <iostream>

// FontEngine 前向声明（将在后续版本中集成）
class FontEngine
{
public:
    float getLineHeight(const std::string& fontName, int fontSize) { return fontSize * 1.2f; }
    float getStringWidth(const std::string& text, const std::string& fontName, int fontSize)
    {
        return text.size() * fontSize * 0.6f;
    }
};

//===----------------------------------------------------------------------===//
// SwLayAction
//===----------------------------------------------------------------------===//

SwLayAction::SwLayAction(SwRootFrame& rRoot, FontEngine* pFontEngine)
    : m_rRoot(rRoot)
    , m_pFontEngine(pFontEngine)
    , m_bPaint(false)
    , m_bComplete(true)
    , m_bAgain(false)
{
}

SwLayAction::~SwLayAction() = default;

void SwLayAction::Action()
{
    // 主排版入口
    InternalAction();

    // 如果有页面被删除，重新排版
    while (m_bAgain)
    {
        m_bAgain = false;
        InternalAction();
    }
}

void SwLayAction::InternalAction()
{
    // 格式化所有内容
    FormatAll();
}

void SwLayAction::FormatAll()
{
    // 遍历所有页面
    SwPageFrame* pPage = m_rRoot.GetLastPage();
    while (pPage)
    {
        FormatPage(pPage);
        pPage = pPage->GetPrevPage();
    }
}

void SwLayAction::FormatPage(SwPageFrame* pPage)
{
    if (!pPage)
        return;

    // 格式化页面的所有子 Frame
    SwFrame* pFrame = pPage->GetLower();
    while (pFrame)
    {
        FormatFrame(pFrame);
        pFrame = pFrame->GetNext();
    }
}

void SwLayAction::FormatFrame(SwFrame* pFrame)
{
    if (!pFrame)
        return;

    // 如果是布局 Frame，递归格式化子 Frame
    if (pFrame->IsLayoutFrame())
    {
        FormatLayout(static_cast<SwLayoutFrame*>(pFrame));
    }
    // 如果是文本 Frame，格式化文本
    else if (pFrame->IsTextFrame())
    {
        // 需要格式化文本
        // 简化实现：暂时不做任何事情
    }
}

void SwLayAction::FormatLayout(SwLayoutFrame* pLayout)
{
    if (!pLayout)
        return;

    // 格式化所有子 Frame
    SwFrame* pFrame = pLayout->GetLower();
    while (pFrame)
    {
        FormatFrame(pFrame);
        pFrame = pFrame->GetNext();
    }
}

void SwLayAction::FormatContent(SwLayoutFrame* pLayout)
{
    if (!pLayout)
        return;

    // 查找内容 Frame
    SwContentFrame* pContent = pLayout->ContainsContent();
    while (pContent)
    {
        // 格式化内容
        pContent->Format();
        pContent = pContent->GetFollow();
    }
}

//===----------------------------------------------------------------------===//
// TextFormatter
//===----------------------------------------------------------------------===//

TextFormatter::TextFormatter(FontEngine* pFontEngine)
    : m_pFontEngine(pFontEngine)
{
}

TextFormatter::~TextFormatter() = default;

void TextFormatter::FormatTextFrame(SwTextFrame* pFrame)
{
    if (!pFrame || !pFrame->GetNode())
        return;

    // 获取文本内容
    SwTextNode* pNode = static_cast<SwTextNode*>(pFrame->GetNode());
    const std::string& text = pNode->GetText();

    // 获取字体信息
    std::string fontName = "Arial";
    int fontSize = 22; // 默认 11pt (22 半点)

    // 从节点属性获取字体
    const std::string* pFont = pNode->GetAttr(RES_CHRATR_FONT);
    if (pFont)
        fontName = *pFont;

    const std::string* pSize = pNode->GetAttr(RES_CHRATR_FONTSIZE);
    if (pSize)
        fontSize = std::stoi(*pSize);

    // 计算可用宽度
    SwRect aPrtRect = pFrame->getFramePrintArea();
    int maxWidth = static_cast<int>(aPrtRect.Width());

    // 换行
    auto lines = BreakIntoLines(text, fontName, fontSize, maxWidth);

    // 设置行数
    pFrame->SetLines(static_cast<sal_Int32>(lines.size()));

    // 计算 Frame 高度
    int totalHeight = 0;
    for (const auto& line : lines)
    {
        totalHeight += line.height;
    }

    // 更新 Frame 大小
    SwRect aFrameRect = pFrame->getFrameArea();
    aFrameRect.SetHeight(totalHeight);
    pFrame->setFrameArea(aFrameRect);
}

int TextFormatter::CalcLineHeight(const std::string& fontName, int fontSize)
{
    if (m_pFontEngine)
    {
        return static_cast<int>(m_pFontEngine->getLineHeight(fontName, fontSize));
    }
    // 默认行高：fontSize * 1.2
    return static_cast<int>(fontSize * 1.2);
}

int TextFormatter::CalcStringWidth(const std::string& text, const std::string& fontName,
                                   int fontSize)
{
    if (m_pFontEngine)
    {
        return static_cast<int>(m_pFontEngine->getStringWidth(text, fontName, fontSize));
    }
    // 默认宽度：每个字符约 0.6 * fontSize
    return static_cast<int>(text.size() * fontSize * 0.6);
}

std::vector<TextFormatter::LineBreak> TextFormatter::BreakIntoLines(const std::string& text,
                                                                    const std::string& fontName,
                                                                    int fontSize, int maxWidth)
{
    std::vector<LineBreak> lines;

    if (text.empty())
    {
        // 空段落也有一行
        LineBreak line;
        line.startPos = 0;
        line.endPos = 0;
        line.width = 0;
        line.height = CalcLineHeight(fontName, fontSize);
        lines.push_back(line);
        return lines;
    }

    int lineHeight = CalcLineHeight(fontName, fontSize);
    int pos = 0;
    int lineStart = 0;

    while (pos < static_cast<int>(text.size()))
    {
        // 查找行尾
        int lineEnd = pos;
        int lineWidth = 0;

        while (lineEnd < static_cast<int>(text.size()))
        {
            // 查找下一个空格或换行
            int wordEnd = lineEnd;
            while (wordEnd < static_cast<int>(text.size()) && text[wordEnd] != ' '
                   && text[wordEnd] != '\n' && text[wordEnd] != '\t')
            {
                ++wordEnd;
            }

            // 计算单词宽度
            std::string word = text.substr(lineEnd, wordEnd - lineEnd);
            int wordWidth = CalcStringWidth(word, fontName, fontSize);

            // 检查是否超过最大宽度
            if (lineWidth + wordWidth > maxWidth && lineWidth > 0)
            {
                break;
            }

            lineWidth += wordWidth;
            lineEnd = wordEnd;

            // 跳过空格
            if (lineEnd < static_cast<int>(text.size()) && text[lineEnd] == ' ')
            {
                lineWidth += CalcStringWidth(" ", fontName, fontSize);
                ++lineEnd;
            }

            // 处理换行符
            if (lineEnd < static_cast<int>(text.size()) && text[lineEnd] == '\n')
            {
                ++lineEnd;
                break;
            }

            // 处理制表符
            if (lineEnd < static_cast<int>(text.size()) && text[lineEnd] == '\t')
            {
                // 跳到下一个制表位（每 0.5 英寸）
                int tabWidth = 480; // 0.5 英寸 = 720 twips ≈ 480 像素
                lineWidth = (lineWidth / tabWidth + 1) * tabWidth;
                ++lineEnd;
            }
        }

        // 创建行
        LineBreak line;
        line.startPos = lineStart;
        line.endPos = lineEnd;
        line.width = lineWidth;
        line.height = lineHeight;
        lines.push_back(line);

        lineStart = lineEnd;
        pos = lineEnd;
    }

    return lines;
}
