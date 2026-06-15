// 渲染指令记录器实现 — 使用共享 render_instruction.h 格式
// 与 LibreOffice 侧 SwPaintEventListener 输出完全相同的 TSV 格式

#include "render_log.h"
#include "render_output_device.h"
#include "../core/node.h"
#include "../core/ndarr.h"
#include "../core/doc.h"
#include "../core/format.h"
#include "../frame/frame.h"
#include "instruction_builder.h"
#include <functional>
#include <iostream>
#include <sstream>
#include <cstring>
#include <functional>

//===----------------------------------------------------------------------===//
// 格式化工具 — 与 LibreOffice 完全一致的 TSV 输出
//===----------------------------------------------------------------------===//

// 转义字符串中的换行符和制表符，确保 TSV 每条指令一行
static std::string EscapeForTsv(const char* s)
{
    if (!s)
        return "";
    std::string result;
    for (unsigned char c = *s; *s; c = *(++s))
    {
        switch (c)
        {
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            case '"':
                result += "\\\"";
                break;
            default:
                if (c < 0x20)
                    result += " "; // 替换其他控制字符为空格
                else
                    result += c;
                break;
        }
    }
    return result;
}

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
                << "\t" << inst.height << "\t\"" << EscapeForTsv(inst.text) << "\""
                << "\t" << EscapeForTsv(inst.fontName) << "\t" << inst.fontSize << "\t"
                << inst.fontColor << "\t" << static_cast<int>(inst.fontWeight) << "\t"
                << static_cast<int>(inst.fontItalic) << "\t" << static_cast<int>(inst.underline)
                << "\t" << static_cast<int>(inst.strikeout) << "\t" << EscapeForTsv(inst.styleName);
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

RenderLogger::RenderLogger() {}

RenderLogger::~RenderLogger() {}

const char* RenderLogger::StoreString(const char* s)
{
    if (!s)
        return "";
    // 检查是否为有效字符串（避免存储悬空指针）
    try
    {
        std::string copy(s);
        m_aStrings.push_back(copy);
        return m_aStrings.back().c_str();
    }
    catch (...)
    {
        return "";
    }
}

void RenderLogger::OnInstruction(const RenderInstruction& inst)
{
    // 存储字符串副本，避免指针悬空
    RenderInstruction copy = inst;
    if (copy.fontName)
        copy.fontName = StoreString(copy.fontName);
    if (copy.text)
        copy.text = StoreString(copy.text);
    if (copy.styleName)
        copy.styleName = StoreString(copy.styleName);

    m_aInstructions.push_back(copy);
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

void RenderLogger::LogSectionFrame(int pageNum, int x, int y, int width, int height)
{
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::SECTION_FRAME;
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

        // 遍历 Frame 树，输出 frame 层语义指令 + VCL 层绘制指令
        // 与 LibreOffice 的双层录制架构对称：
        //   frame 层: TEXT_FRAME (语义级，来自 SwTextFrame::Paint)
        //   VCL 层:   SET_FONT / TEXT_RUN (绘制级，来自 OutputDevice)
        std::function<void(SwFrame*)> logFrame = [&](SwFrame* pFrame) {
            while (pFrame)
            {
                if (pFrame->IsTextFrame())
                {
                    // TextFrame: 先输出 frame 层 TEXT_FRAME，再输出 VCL 层指令
                    SwTextFrame* pTextFrame = static_cast<SwTextFrame*>(pFrame);
                    SwContentNode* pContentNode = pTextFrame->GetNode();
                    if (pContentNode && pContentNode->IsTextNode())
                    {
                        SwTextNode* pTextNode = static_cast<SwTextNode*>(pContentNode);
                        const std::string& rText = pTextNode->GetText();

                        const std::string* pFont = pTextNode->GetAttr(RES_CHRATR_FONT);
                        const std::string* pSize = pTextNode->GetAttr(RES_CHRATR_FONTSIZE);
                        const std::string* pColor = pTextNode->GetAttr(RES_CHRATR_COLOR);
                        const std::string* pWeight = pTextNode->GetAttr(RES_CHRATR_WEIGHT);
                        const std::string* pItalic = pTextNode->GetAttr(RES_CHRATR_POSTURE);

                        const char* fontName
                            = (pFont && !pFont->empty()) ? pFont->c_str() : "Calibri";
                        int fontSize = pSize ? std::stoi(*pSize) : 20; // 默认 10pt (20 半点)

                        // 样式名处理
                        std::string sStyleName = pTextNode->GetStyleName();
                        if (sStyleName.empty() || sStyleName == "Normal" || sStyleName == "1")
                        {
                            sStyleName = "Default Paragraph Style";
                        }

                        // LibreOffice 无头模式默认前景色为白色 (0xFFFFFF)
                        uint32_t fontColor = 0xFFFFFF;
                        uint8_t fontWeight = (pWeight && *pWeight == "bold") ? 188 : 144;
                        uint8_t fontItalic = (pItalic && *pItalic == "italic") ? 1 : 0;

                        const char* styleName = StoreString(sStyleName.c_str());

                        SwRect aArea = pTextFrame->getFrameArea();
                        BuildTextFrameInstruction(
                            *this, pn, aArea.Left(), aArea.Top(), aArea.Width(), aArea.Height(),
                            rText.c_str(), static_cast<int>(rText.size()), fontName, fontSize,
                            fontColor, fontWeight, fontItalic, styleName);
                    }

                    // VCL 层：PaintSwFrame 输出 SET_FONT + TEXT_RUN
                    pFrame->PaintSwFrame(&aOutDev);
                }
                else if (pFrame->IsNoTextFrame())
                {
                    // NoTextFrame: 图片/OLE Frame，输出 IMAGE_FRAME
                    const SwRect& aArea = pFrame->getFrameArea();
                    LogImageFrame(pn, aArea.Left(), aArea.Top(), aArea.Width(), aArea.Height());
                }
                else if (pFrame->IsTabFrame())
                {
                    // TabFrame: 输出 TABLE_FRAME，然后递归子 Frame
                    const SwRect& aArea = pFrame->getFrameArea();
                    LogTableFrame(pn, aArea.Left(), aArea.Top(), aArea.Width(), aArea.Height());
                    logFrame(static_cast<SwLayoutFrame*>(pFrame)->GetLower());
                }
                else if (pFrame->IsRowFrame())
                {
                    // RowFrame: 输出 TABLE_ROW，然后递归子 Frame
                    const SwRect& aArea = pFrame->getFrameArea();
                    LogTableRow(pn, aArea.Left(), aArea.Top(), aArea.Width(), aArea.Height());
                    logFrame(static_cast<SwLayoutFrame*>(pFrame)->GetLower());
                }
                else if (pFrame->IsCellFrame())
                {
                    // CellFrame: 输出 TABLE_CELL，然后递归子 Frame
                    const SwRect& aArea = pFrame->getFrameArea();
                    LogTableCell(pn, aArea.Left(), aArea.Top(), aArea.Width(), aArea.Height());
                    logFrame(static_cast<SwLayoutFrame*>(pFrame)->GetLower());
                }
                else if (pFrame->IsSctFrame())
                {
                    // SectionFrame: 输出 SECTION_FRAME，然后递归子 Frame
                    const SwRect& aArea = pFrame->getFrameArea();
                    LogSectionFrame(pn, aArea.Left(), aArea.Top(), aArea.Width(), aArea.Height());
                    logFrame(static_cast<SwLayoutFrame*>(pFrame)->GetLower());
                }
                else if (pFrame->IsLayoutFrame())
                {
                    // 其他 LayoutFrame: 只递归进入子 Frame（Header, Footer, Column, Footnote 等）
                    logFrame(static_cast<SwLayoutFrame*>(pFrame)->GetLower());
                }
                else
                {
                    pFrame->PaintSwFrame(&aOutDev);
                }

                pFrame = pFrame->GetNext();
            }
        };

        logFrame(pPage->GetLower());

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

// 判断是否为 frame 层语义指令
static bool IsFrameLayerInstruction(RenderCmdType type)
{
    switch (type)
    {
        case RenderCmdType::PAGE_START:
        case RenderCmdType::PAGE_END:
        case RenderCmdType::TEXT_FRAME:
        case RenderCmdType::TEXT_LINE:
        case RenderCmdType::TABLE_FRAME:
        case RenderCmdType::TABLE_ROW:
        case RenderCmdType::TABLE_CELL:
        case RenderCmdType::IMAGE_FRAME:
        case RenderCmdType::SECTION_FRAME:
            return true;
        default:
            return false;
    }
}

// 判断是否为 VCL 层绘制指令
static bool IsVclLayerInstruction(RenderCmdType type)
{
    switch (type)
    {
        case RenderCmdType::PAGE_START:
        case RenderCmdType::PAGE_END:
        case RenderCmdType::SET_FONT:
        case RenderCmdType::SET_TEXT_COLOR:
        case RenderCmdType::SET_FILL_COLOR:
        case RenderCmdType::SET_LINE_COLOR:
        case RenderCmdType::SET_CLIP_REGION:
        case RenderCmdType::TEXT_RUN:
        case RenderCmdType::RECT:
        case RenderCmdType::LINE:
        case RenderCmdType::POLYGON:
        case RenderCmdType::ELLIPSE:
        case RenderCmdType::BITMAP:
        case RenderCmdType::POLYLINE:
        case RenderCmdType::PUSH:
        case RenderCmdType::POP:
            return true;
        default:
            return false;
    }
}

void RenderLogger::WriteFrameLayerToFile(const std::string& filePath)
{
    std::ofstream file(filePath);
    if (!file.is_open())
        return;

    for (const auto& inst : m_aInstructions)
    {
        if (IsFrameLayerInstruction(inst.type))
            WriteInstructionToStream(file, inst);
    }

    file.close();
}

void RenderLogger::WriteVclLayerToFile(const std::string& filePath)
{
    std::ofstream file(filePath);
    if (!file.is_open())
        return;

    for (const auto& inst : m_aInstructions)
    {
        if (IsVclLayerInstruction(inst.type))
            WriteInstructionToStream(file, inst);
    }

    file.close();
}

//===----------------------------------------------------------------------===//
// RenderLogger
//===----------------------------------------------------------------------===//
