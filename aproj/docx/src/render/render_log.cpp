// 渲染指令记录器实现 — 使用共享 render_instruction.h 格式
// 与 LibreOffice 侧 SwPaintEventListener 输出完全相同的 TSV 格式

#include "render_log.h"
#include "render_output_device.h"
#include "../core/node.h"
#include "../core/ndarr.h"
#include "../core/doc.h"
#include "../core/format.h"
#include "../frame/frame.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <functional>

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
        case RenderCmdType::POLYGON:
        case RenderCmdType::BITMAP:
        case RenderCmdType::ELLIPSE:
            out << "\t" << inst.pageNum << "\t" << inst.x << "\t" << inst.y << "\t" << inst.width
                << "\t" << inst.height;
            break;
        case RenderCmdType::LINE:
        case RenderCmdType::POLYLINE:
            out << "\t" << inst.pageNum << "\t" << inst.x << "\t" << inst.y << "\t"
                << inst.width // x2
                << "\t" << inst.height; // y2
            break;
        // 状态变更指令
        case RenderCmdType::SET_FONT:
            out << "\t" << inst.pageNum << "\t" << (inst.fontName ? inst.fontName : "") << "\t"
                << inst.fontSize << "\t" << static_cast<int>(inst.fontWeight) << "\t"
                << static_cast<int>(inst.fontItalic);
            break;
        case RenderCmdType::SET_TEXT_COLOR:
        case RenderCmdType::SET_FILL_COLOR:
        case RenderCmdType::SET_LINE_COLOR:
            out << "\t" << inst.pageNum << "\t" << inst.fontColor;
            break;
        case RenderCmdType::SET_CLIP_REGION:
        case RenderCmdType::PUSH:
        case RenderCmdType::POP:
            out << "\t" << inst.pageNum;
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

    // 创建 RenderInstructionOutputDevice — 通过 OutputDevice 接口绘制
    // 与 LibreOffice 的 PaintSwFrame → OutputDevice → GDIMetaFile 路径对称
    RenderInstructionOutputDevice aOutDev(*this, 1);

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
        aOutDev.SetPageNum(pn);

        LogPageStart(pn, pPage->getFrameArea().Width(), pPage->getFrameArea().Height());

        // 通过 OutputDevice 接口绘制页面内容
        // 与 LibreOffice 的 pPage->PaintSwFrame(rRenderContext, aPaintRect) 流程对称
        SwFrame* pFrame = pPage->GetLower();
        while (pFrame)
        {
            pFrame->PaintSwFrame(&aOutDev);
            pFrame = pFrame->GetNext();
        }

        LogPageEnd(pn);
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

            // 递归转储 Frame 树
            std::function<void(SwFrame*, int)> dumpFrame = [&](SwFrame* pF, int indent) {
                while (pF)
                {
                    std::string prefix(indent * 2, ' ');
                    file << prefix << "<frame type=\""
                         << (pF->IsTextFrame() ? "text"
                                               : pF->IsLayoutFrame() ? "layout" : "unknown")
                         << "\"";
                    if (pF->IsTextFrame())
                    {
                        SwContentNode* pCN = static_cast<SwContentFrame*>(pF)->GetNode();
                        if (pCN && pCN->IsTextNode())
                        {
                            SwTextNode* pTN = static_cast<SwTextNode*>(pCN);
                            std::string txt = pTN->GetText();
                            if (txt.size() > 30)
                                txt = txt.substr(0, 30) + "...";
                            file << " text=\"" << txt << "\"";
                        }
                    }
                    if (pF->IsLayoutFrame() && static_cast<SwLayoutFrame*>(pF)->GetLower())
                    {
                        file << ">\n";
                        dumpFrame(static_cast<SwLayoutFrame*>(pF)->GetLower(), indent + 1);
                        file << prefix << "</frame>\n";
                    }
                    else
                    {
                        file << " />\n";
                    }
                    pF = pF->GetNext();
                }
            };
            dumpFrame(pp->GetLower(), 2);

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
