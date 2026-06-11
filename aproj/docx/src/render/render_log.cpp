// 渲染指令记录器实现 — 使用共享 render_instruction.h 格式
// 与 LibreOffice 侧 SwPaintEventListener 输出完全相同的 TSV 格式

#include "render_log.h"
#include "../core/node.h"
#include "../core/ndarr.h"
#include "../core/doc.h"
#include "../core/format.h"
#include <iostream>
#include <sstream>
#include <cstring>

//===----------------------------------------------------------------------===//
// 格式化工具 — 与 LibreOffice 完全一致的 TSV 输出
//===----------------------------------------------------------------------===//

void RenderLogger::WriteInstructionToStream(std::ostream& out, const RenderInstruction& inst)
{
    out << RenderCmdTypeName(inst.type);
    switch (inst.type)
    {
        case RenderCmdType::PAGE_START:
            out << "\t" << inst.pageNum << "\t" << inst.width << "\t" << inst.height;
            break;
        case RenderCmdType::PAGE_END:
            out << "\t" << inst.pageNum;
            break;
        case RenderCmdType::TEXT_FRAME:
        case RenderCmdType::TEXT_LINE:
        case RenderCmdType::TEXT_RUN:
            out << "\t" << inst.pageNum << "\t" << inst.x << "\t" << inst.y << "\t" << inst.width
                << "\t" << inst.height << "\t\"" << (inst.text ? inst.text : "") << "\""
                << "\t" << (inst.fontName ? inst.fontName : "") << "\t" << inst.fontSize << "\t"
                << inst.fontColor << "\t" << static_cast<int>(inst.fontWeight) << "\t"
                << static_cast<int>(inst.fontItalic) << "\t" << static_cast<int>(inst.underline)
                << "\t" << static_cast<int>(inst.strikeout) << "\t"
                << (inst.styleName ? inst.styleName : "");
            break;
        case RenderCmdType::TABLE_FRAME:
        case RenderCmdType::TABLE_ROW:
        case RenderCmdType::TABLE_CELL:
        case RenderCmdType::IMAGE_FRAME:
        case RenderCmdType::SECTION_FRAME:
        case RenderCmdType::RECT:
            out << "\t" << inst.pageNum << "\t" << inst.x << "\t" << inst.y << "\t" << inst.width
                << "\t" << inst.height;
            break;
        case RenderCmdType::LINE:
            out << "\t" << inst.pageNum << "\t" << inst.x << "\t" << inst.y << "\t"
                << inst.width // x2
                << "\t" << inst.height; // y2
            break;
    }
    out << "\n";
}

//===----------------------------------------------------------------------===//
// RenderLogger
//===----------------------------------------------------------------------===//

RenderLogger::RenderLogger()
    : m_bLogging(false)
{
}

RenderLogger::~RenderLogger() { EndLog(); }

void RenderLogger::StartLog(const std::string& filePath)
{
    m_File.open(filePath);
    m_bLogging = true;
    m_aInstructions.clear();
}

void RenderLogger::EndLog()
{
    if (m_File.is_open())
    {
        m_File.close();
    }
    m_bLogging = false;
}

void RenderLogger::OnInstruction(const RenderInstruction& inst)
{
    m_aInstructions.push_back(inst);
    if (m_bLogging && m_File.is_open())
    {
        WriteInstructionToStream(m_File, inst);
    }
}

//===----------------------------------------------------------------------===//
// 高级接口
//===----------------------------------------------------------------------===//

void RenderLogger::LogPageStart(int pageNum, int width, int height)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::PAGE_START;
    inst.pageNum = pageNum;
    inst.width = width;
    inst.height = height;
    OnInstruction(inst);
}

void RenderLogger::LogPageEnd(int pageNum)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::PAGE_END;
    inst.pageNum = pageNum;
    OnInstruction(inst);
}

void RenderLogger::LogTextFrame(int pageNum, int x, int y, int width, int height, const char* text,
                                int textLen, const char* fontName, int fontSize, uint32_t fontColor,
                                uint8_t fontWeight, uint8_t fontItalic, const char* styleName)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::TEXT_FRAME;
    inst.pageNum = pageNum;
    inst.x = x;
    inst.y = y;
    inst.width = width;
    inst.height = height;
    inst.text = text;
    inst.textLen = textLen;
    inst.fontName = fontName;
    inst.fontSize = fontSize;
    inst.fontColor = fontColor;
    inst.fontWeight = fontWeight;
    inst.fontItalic = fontItalic;
    inst.styleName = styleName;
    OnInstruction(inst);
}

void RenderLogger::LogTextLine(int pageNum, int x, int y, int width, int height, const char* text,
                               int textLen, const char* fontName, int fontSize, uint32_t fontColor,
                               uint8_t fontWeight, uint8_t fontItalic, const char* styleName)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::TEXT_LINE;
    inst.pageNum = pageNum;
    inst.x = x;
    inst.y = y;
    inst.width = width;
    inst.height = height;
    inst.text = text;
    inst.textLen = textLen;
    inst.fontName = fontName;
    inst.fontSize = fontSize;
    inst.fontColor = fontColor;
    inst.fontWeight = fontWeight;
    inst.fontItalic = fontItalic;
    inst.styleName = styleName;
    OnInstruction(inst);
}

void RenderLogger::LogTextRun(int pageNum, int x, int y, int width, int height, const char* text,
                              int textLen, const char* fontName, int fontSize, uint32_t fontColor,
                              uint8_t fontWeight, uint8_t fontItalic)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::TEXT_RUN;
    inst.pageNum = pageNum;
    inst.x = x;
    inst.y = y;
    inst.width = width;
    inst.height = height;
    inst.text = text;
    inst.textLen = textLen;
    inst.fontName = fontName;
    inst.fontSize = fontSize;
    inst.fontColor = fontColor;
    inst.fontWeight = fontWeight;
    inst.fontItalic = fontItalic;
    OnInstruction(inst);
}

void RenderLogger::LogTableFrame(int pageNum, int x, int y, int width, int height)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::TABLE_FRAME;
    inst.pageNum = pageNum;
    inst.x = x;
    inst.y = y;
    inst.width = width;
    inst.height = height;
    OnInstruction(inst);
}

void RenderLogger::LogTableRow(int pageNum, int x, int y, int width, int height)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::TABLE_ROW;
    inst.pageNum = pageNum;
    inst.x = x;
    inst.y = y;
    inst.width = width;
    inst.height = height;
    OnInstruction(inst);
}

void RenderLogger::LogTableCell(int pageNum, int x, int y, int width, int height)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::TABLE_CELL;
    inst.pageNum = pageNum;
    inst.x = x;
    inst.y = y;
    inst.width = width;
    inst.height = height;
    OnInstruction(inst);
}

void RenderLogger::LogImageFrame(int pageNum, int x, int y, int width, int height)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::IMAGE_FRAME;
    inst.pageNum = pageNum;
    inst.x = x;
    inst.y = y;
    inst.width = width;
    inst.height = height;
    OnInstruction(inst);
}

void RenderLogger::LogRect(int pageNum, int x, int y, int width, int height)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::RECT;
    inst.pageNum = pageNum;
    inst.x = x;
    inst.y = y;
    inst.width = width;
    inst.height = height;
    OnInstruction(inst);
}

void RenderLogger::LogLine(int pageNum, int x1, int y1, int x2, int y2)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::LINE;
    inst.pageNum = pageNum;
    inst.x = x1;
    inst.y = y1;
    inst.width = x2;
    inst.height = y2;
    OnInstruction(inst);
}

//===----------------------------------------------------------------------===//
// Frame 树遍历
//===----------------------------------------------------------------------===//

void RenderLogger::LogFrameTree(SwRootFrame* pRoot)
{
    if (!pRoot)
        return;

    int pageNum = 1;
    // 遍历页面 (从最后一个页面开始，与 LibreOffice 的遍历方向一致)
    SwPageFrame* pPage = pRoot->GetLastPage();

    // 收集所有页面到数组，以便正序遍历
    std::vector<SwPageFrame*> pages;
    while (pPage)
    {
        pages.push_back(pPage);
        pPage = pPage->GetPrevPage();
    }
    // 反转为正序 (第 1 页在前)
    for (size_t i = 0; i < pages.size() / 2; ++i)
    {
        std::swap(pages[i], pages[pages.size() - 1 - i]);
    }

    for (size_t i = 0; i < pages.size(); ++i)
    {
        pPage = pages[i];
        int pn = static_cast<int>(i) + 1;

        LogPageStart(pn, pPage->getFrameArea().Width(), pPage->getFrameArea().Height());

        // 遍历页面内容
        SwFrame* pFrame = pPage->GetLower();
        while (pFrame)
        {
            LogFrameRecursive(pFrame, pn);
            pFrame = pFrame->GetNext();
        }

        LogPageEnd(pn);
    }
}

void RenderLogger::LogFrameRecursive(SwFrame* pFrame, int pageNum)
{
    if (!pFrame)
        return;

    if (pFrame->IsTextFrame())
    {
        SwTextFrame* pTextFrame = static_cast<SwTextFrame*>(pFrame);
        SwContentNode* pNode = pTextFrame->GetNode();

        std::string text;
        std::string fontName = "Arial";
        int fontSize = 22; // 默认 11pt
        uint32_t fontColor = 0;
        uint8_t fontWeight = 400;
        uint8_t fontItalic = 0;
        std::string styleName;

        if (pNode && pNode->IsTextNode())
        {
            SwTextNode* pTextNode = static_cast<SwTextNode*>(pNode);
            text = pTextNode->GetText();

            // 获取字体信息
            const std::string* pFont = pTextNode->GetAttr(RES_CHRATR_FONT);
            if (pFont)
                fontName = *pFont;

            const std::string* pSize = pTextNode->GetAttr(RES_CHRATR_FONTSIZE);
            if (pSize)
                fontSize = std::stoi(*pSize);

            const std::string* pWeight = pTextNode->GetAttr(RES_CHRATR_WEIGHT);
            if (pWeight && *pWeight == "bold")
                fontWeight = 700;

            const std::string* pPosture = pTextNode->GetAttr(RES_CHRATR_POSTURE);
            if (pPosture && *pPosture == "italic")
                fontItalic = 1;

            const std::string* pColor = pTextNode->GetAttr(RES_CHRATR_COLOR);
            if (pColor && !pColor->empty())
            {
                // 解析十六进制颜色
                try
                {
                    fontColor = static_cast<uint32_t>(std::stoul(*pColor, nullptr, 16));
                }
                catch (...)
                {
                }
            }

            styleName = pTextNode->GetStyleName();
        }

        SwRect aRect = pFrame->getFrameArea();
        LogTextFrame(pageNum, aRect.Left(), aRect.Top(), aRect.Width(), aRect.Height(),
                     text.c_str(), static_cast<int>(text.size()), fontName.c_str(), fontSize,
                     fontColor, fontWeight, fontItalic,
                     styleName.empty() ? nullptr : styleName.c_str());
    }
    else if (pFrame->IsLayoutFrame())
    {
        // 递归记录子 Frame
        SwLayoutFrame* pLayout = static_cast<SwLayoutFrame*>(pFrame);
        SwFrame* pChild = pLayout->GetLower();
        while (pChild)
        {
            LogFrameRecursive(pChild, pageNum);
            pChild = pChild->GetNext();
        }
    }
}

void RenderLogger::WriteToFile(const std::string& filePath)
{
    std::ofstream file(filePath);
    if (!file.is_open())
        return;

    for (const auto& inst : m_aInstructions)
    {
        WriteInstructionToStream(file, inst);
    }

    file.close();
}

//===----------------------------------------------------------------------===//
// DumpFrameTreeXml: 将 Frame 树转储为 XML
//===----------------------------------------------------------------------===//

void DumpFrameTreeXml(SwRootFrame* pRoot, const std::string& filePath)
{
    std::ofstream file(filePath);
    if (!file.is_open())
        return;

    file << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    file << "<layout>\n";

    if (pRoot)
    {
        int pageNum = 1;
        // 收集页面 (正序)
        std::vector<SwPageFrame*> pages;
        SwPageFrame* pPage = pRoot->GetLastPage();
        while (pPage)
        {
            pages.push_back(pPage);
            pPage = pPage->GetPrevPage();
        }
        for (size_t i = 0; i < pages.size() / 2; ++i)
            std::swap(pages[i], pages[pages.size() - 1 - i]);

        for (auto* pp : pages)
        {
            file << "  <page num=\"" << pageNum << "\" "
                 << "width=\"" << pp->getFrameArea().Width() << "\" "
                 << "height=\"" << pp->getFrameArea().Height() << "\">\n";

            SwFrame* pFrame = pp->GetLower();
            while (pFrame)
            {
                file << "    <frame type=\""
                     << (pFrame->IsTextFrame() ? "text"
                                               : pFrame->IsLayoutFrame() ? "layout" : "unknown")
                     << "\" />\n";
                pFrame = pFrame->GetNext();
            }

            file << "  </page>\n";
            ++pageNum;
        }
    }

    file << "</layout>\n";
    file.close();
}

//===----------------------------------------------------------------------===//
// DumpNodesXml: 将 SwNodes 转储为 XML
//===----------------------------------------------------------------------===//

void DumpNodesXml(SwDoc& doc, const std::string& filePath)
{
    std::ofstream file(filePath);
    if (!file.is_open())
        return;

    file << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    file << "<nodes>\n";

    SwNodes& rNodes = doc.GetNodes();
    for (SwNodeOffset i = 0; i < rNodes.Count(); ++i)
    {
        SwNode* pNode = rNodes[i];
        if (!pNode)
            continue;

        file << "  <node index=\"" << i << "\" type=\"";
        switch (pNode->GetNodeType())
        {
            case SwNodeType::Start:
                file << "start";
                break;
            case SwNodeType::End:
                file << "end";
                break;
            case SwNodeType::Text:
                file << "text";
                break;
            case SwNodeType::Table:
                file << "table";
                break;
            case SwNodeType::Section:
                file << "section";
                break;
            case SwNodeType::Grf:
                file << "grf";
                break;
            case SwNodeType::Ole:
                file << "ole";
                break;
            default:
                file << "unknown";
                break;
        }
        file << "\"";

        if (pNode->IsTextNode())
        {
            SwTextNode* pTextNode = static_cast<SwTextNode*>(pNode);
            file << " text=\"" << pTextNode->GetText() << "\"";
        }

        file << " />\n";
    }

    file << "</nodes>\n";
    file.close();
}
