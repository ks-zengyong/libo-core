// 渲染指令记录器实现 — 使用共享 render_instruction.h 格式
// 与 LibreOffice 侧 SwPaintEventListener 输出完全相同的 TSV 格式

#include "render_log.h"
#include "render_output_device.h"
#include "../core/node.h"
#include "../core/ndarr.h"
#include "../core/doc.h"
#include "../core/format.h"
#include "../frame/frame.h"
#include "../frame/sortedobjs.h"
#include "instruction_builder.h"
#include "../../../../render_common/render_format.h"
#include "../../../../render_common/frame_tree_walker.h"
#include <functional>
#include <iostream>
#include <sstream>
#include <cstring>
#include <functional>

//===----------------------------------------------------------------------===//
// IFrameNode 包装器：将 aproj 的 SwFrame 适配为共享遍历接口
//===----------------------------------------------------------------------===//

namespace
{
class AprojFrameNode : public IFrameNode
{
public:
    AprojFrameNode(SwFrame* pFrame, int pageNum)
        : m_pFrame(pFrame)
        , m_pageNum(pageNum)
    {
        if (pFrame && pFrame->IsTextFrame())
            ExtractTextInfo();
    }

    AprojFrameNode(SwFrame* pFrame, int pageNum, SwPageFrame* pPage, size_t flyIndex,
                   SwFrame* pAnchor, bool bFlySibling)
        : m_pFrame(pFrame)
        , m_pageNum(pageNum)
        , m_pPageFrame(pPage)
        , m_flyIndex(flyIndex)
        , m_pAnchorFrame(pAnchor)
        , m_bIsFlySibling(bFlySibling)
    {
        if (pFrame && pFrame->IsTextFrame())
            ExtractTextInfo();
    }

    FrameNodeType GetNodeType() const override
    {
        if (!m_pFrame)
            return FrameNodeType::Unknown;
        switch (m_pFrame->GetType())
        {
            case SwFrameType::Page:
                return FrameNodeType::Page;
            case SwFrameType::Body:
                return FrameNodeType::Body;
            case SwFrameType::Header:
                return FrameNodeType::Header;
            case SwFrameType::Footer:
                return FrameNodeType::Footer;
            case SwFrameType::Section:
                return FrameNodeType::Section;
            case SwFrameType::Column:
                return FrameNodeType::Column;
            case SwFrameType::Txt:
                return FrameNodeType::Text;
            case SwFrameType::Tab:
                return FrameNodeType::Table;
            case SwFrameType::Row:
                return FrameNodeType::TabRow;
            case SwFrameType::Cell:
                return FrameNodeType::TabCell;
            case SwFrameType::FootnoteCont:
                return FrameNodeType::FootnoteCont;
            case SwFrameType::Footnote:
                return FrameNodeType::Footnote;
            case SwFrameType::Fly:
                return FrameNodeType::Fly;
            case SwFrameType::NoTxt:
                return FrameNodeType::NoText;
            default:
                return FrameNodeType::Unknown;
        }
    }

    int GetPageNum() const override { return m_pageNum; }

    void GetRect(int& x, int& y, int& w, int& h) const override
    {
        if (!m_pFrame)
            return;
        const SwRect& r = m_pFrame->getFrameArea();
        x = r.Left();
        y = r.Top();
        w = r.Width();
        h = r.Height();
    }

    const char* GetText() const override { return m_textBuf.empty() ? nullptr : m_textBuf.c_str(); }
    int GetTextLen() const override { return static_cast<int>(m_textBuf.size()); }
    const char* GetFontName() const override
    {
        return m_fontBuf.empty() ? nullptr : m_fontBuf.c_str();
    }
    int GetFontSize() const override { return m_fontSize; }
    uint32_t GetFontColor() const override { return m_fontColor; }
    uint8_t GetFontWeight() const override { return m_fontWeight; }
    uint8_t GetFontItalic() const override { return m_fontItalic; }
    const char* GetStyleName() const override
    {
        return m_styleBuf.empty() ? nullptr : m_styleBuf.c_str();
    }

    IFrameNode* GetFirstChild() const override
    {
        if (!m_pFrame || !m_pFrame->IsLayoutFrame())
            return nullptr;
        SwFrame* pLower = static_cast<SwLayoutFrame*>(m_pFrame)->GetLower();
        return pLower ? new AprojFrameNode(pLower, m_pageNum) : nullptr;
    }

    IFrameNode* GetNextSibling() const override
    {
        if (!m_pFrame)
            return nullptr;
        SwFrame* pNext = m_pFrame->GetNext();
        return pNext ? new AprojFrameNode(pNext, m_pageNum) : nullptr;
    }

    // 浮动对象链: 对应 LO paint_listener.cxx LoFrameNode::GetFirstFly
    // 使用 SwSortedObjs 接口查找锚定到当前帧的 Fly
    IFrameNode* GetFirstFly() const override
    {
        if (!m_pFrame)
            return nullptr;

        SwPageFrame* pPageFrame = m_pFrame->FindPageFrame();
        if (!pPageFrame)
            return nullptr;

        // 使用 SwSortedObjs 接口
        const SwSortedObjs* pSortedObjs = pPageFrame->GetSortedObjs();
        if (!pSortedObjs)
            return nullptr;

        for (size_t i = 0; i < pSortedObjs->size(); ++i)
        {
            SwFlyFrame* pFly = (*pSortedObjs)[i];
            SwFrame* pAnchor = pSortedObjs->GetAnchorFrame(i);
            if (pAnchor == m_pFrame && pFly)
                return new AprojFrameNode(pFly, m_pageNum, pPageFrame, i, m_pFrame, true);
        }
        return nullptr;
    }

    IFrameNode* GetNextSiblingFly() const override
    {
        if (!m_bIsFlySibling || !m_pPageFrame || !m_pAnchorFrame)
            return nullptr;

        const SwSortedObjs* pSortedObjs = m_pPageFrame->GetSortedObjs();
        if (!pSortedObjs)
            return nullptr;

        for (size_t i = m_flyIndex + 1; i < pSortedObjs->size(); ++i)
        {
            SwFrame* pAnchor = pSortedObjs->GetAnchorFrame(i);
            if (pAnchor == m_pAnchorFrame)
            {
                SwFlyFrame* pFly = (*pSortedObjs)[i];
                if (pFly)
                    return new AprojFrameNode(pFly, m_pageNum, m_pPageFrame, i, m_pAnchorFrame, true);
            }
        }
        return nullptr;
    }

private:
    void ExtractTextInfo()
    {
        SwTextFrame* pTextFrame = static_cast<SwTextFrame*>(m_pFrame);
        SwContentNode* pContentNode = pTextFrame->GetNode();
        if (!pContentNode || !pContentNode->IsTextNode())
            return;

        SwTextNode* pTextNode = static_cast<SwTextNode*>(pContentNode);
        m_textBuf = pTextNode->GetText();

        const std::string* pFont = nullptr;
        const std::string* pSize = nullptr;
        if (m_textBuf.empty())
        {
            pFont = pTextNode->GetAttr(RES_CHRATR_FONT_PARA_MARK);
            pSize = pTextNode->GetAttr(RES_CHRATR_FONTSIZE_PARA_MARK);
        }
        if (!pFont)
            pFont = pTextNode->GetAttr(RES_CHRATR_FONT);
        if (!pSize)
            pSize = pTextNode->GetAttr(RES_CHRATR_FONTSIZE);
        const std::string* pWeight = pTextNode->GetAttr(RES_CHRATR_WEIGHT);
        const std::string* pItalic = pTextNode->GetAttr(RES_CHRATR_POSTURE);

        m_fontBuf = (pFont && !pFont->empty()) ? *pFont : "Calibri";
        m_fontSize = pSize ? std::stoi(*pSize) : 20;
        m_fontWeight = (pWeight && *pWeight == "bold") ? 700 : 400;
        m_fontItalic = (pItalic && *pItalic == "italic") ? 1 : 0;
        m_fontColor = 0xFFFFFF; // 无头模式默认白色

        m_styleBuf = pTextNode->GetStyleName();
        if (m_styleBuf.empty() || m_styleBuf == "Normal" || m_styleBuf == "1")
            m_styleBuf = "Default Paragraph Style";
    }

    SwFrame* m_pFrame;
    int m_pageNum;
    SwPageFrame* m_pPageFrame = nullptr;
    size_t m_flyIndex = 0;
    SwFrame* m_pAnchorFrame = nullptr;
    bool m_bIsFlySibling = false;
    std::string m_textBuf;
    std::string m_fontBuf;
    std::string m_styleBuf;
    int m_fontSize = 0;
    uint32_t m_fontColor = 0;
    uint8_t m_fontWeight = 0;
    uint8_t m_fontItalic = 0;
};

// VCL 层遍历：递归遍历 Frame 树并调用 PaintSwFrame
static void TraverseVclLayer(SwFrame* pFrame, OutputDevice* pOutDev)
{
    while (pFrame)
    {
        if (pFrame->IsPageFrame())
        {
            TraverseVclLayer(static_cast<SwLayoutFrame*>(pFrame)->GetLower(), pOutDev);
        }
        else if (pFrame->IsTextFrame())
        {
            pFrame->PaintSwFrame(pOutDev);
        }
        else if (pFrame->IsLayoutFrame())
        {
            TraverseVclLayer(static_cast<SwLayoutFrame*>(pFrame)->GetLower(), pOutDev);
        }
        pFrame = pFrame->GetNext();
    }
}

} // namespace

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
// Frame 树遍历
//===----------------------------------------------------------------------===//

void RenderLogger::LogFrameTree(SwRootFrame* pRoot)
{
    if (!pRoot)
        return;

    // 创建 RenderInstructionOutputDevice — 通过 OutputDevice 接口绘制（VCL 层）
    RenderInstructionOutputDevice aOutDev(*this, 1);

    // 收集所有页面，反转为正序
    SwPageFrame* pPage = pRoot->GetLastPage();
    std::vector<SwPageFrame*> pages;
    while (pPage)
    {
        pages.push_back(pPage);
        pPage = pPage->GetPrevPage();
    }
    for (size_t i = 0; i < pages.size() / 2; ++i)
        std::swap(pages[i], pages[pages.size() - 1 - i]);

    for (size_t i = 0; i < pages.size(); ++i)
    {
        pPage = pages[i];
        int pn = static_cast<int>(i) + 1;
        aOutDev.SetPageNum(pn);

        // Frame 层：使用 render_common 共享遍历器（WalkFrameTreeAndLog）
        // 生成 PAGE_START + 所有 Frame 类型指令 + PAGE_END
        AprojFrameNode pageNode(pPage, pn);
        WalkFrameTreeAndLog(&pageNode, *this);

        // VCL 层：递归遍历 Frame 树并调用 PaintSwFrame
        // 生成 SET_FONT / TEXT_RUN / RECT 等绘制指令
        TraverseVclLayer(pPage->GetLower(), &aOutDev);
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
        case RenderCmdType::TABLE_START:
        case RenderCmdType::TABLE_END:
        case RenderCmdType::TABLEROW_START:
        case RenderCmdType::TABLEROW_END:
        case RenderCmdType::TABLECELL_START:
        case RenderCmdType::TABLECELL_END:
        case RenderCmdType::IMAGE_FRAME:
        case RenderCmdType::SECTION_START:
        case RenderCmdType::SECTION_END:
        case RenderCmdType::COLUMN_START:
        case RenderCmdType::COLUMN_END:
        case RenderCmdType::HEADER_START:
        case RenderCmdType::HEADER_END:
        case RenderCmdType::FOOTER_START:
        case RenderCmdType::FOOTER_END:
        case RenderCmdType::FOOTNOTE_CONT_START:
        case RenderCmdType::FOOTNOTE_CONT_END:
        case RenderCmdType::FOOTNOTE_FRAME:
        case RenderCmdType::FLY_START:
        case RenderCmdType::FLY_END:
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
