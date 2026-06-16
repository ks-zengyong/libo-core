/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * 渲染指令记录器实现 — 与 aproj/docx 的 RenderLogger 输出完全一致的 TSV 格式
 */

#include "paint_listener.hxx"
#include <txtfrm.hxx>
#include <tabfrm.hxx>
#include <pagefrm.hxx>
#include <rootfrm.hxx>
#include <frame.hxx>
#include <ftnfrm.hxx>
#include <flyfrm.hxx>
#include <sectfrm.hxx>
#include <colfrm.hxx>
#include <hffrm.hxx>
#include <notxtfrm.hxx>
#include <ndtxt.hxx>
#include <swrect.hxx>
#include <hintids.hxx>
#include <charatr.hxx>
#include <fmtcol.hxx>
#include <names.hxx>
#include <osl/process.h>
#include <rtl/strbuf.hxx>
#include <tools/color.hxx>

#include "../../../../render_common/render_format.h"
#include "../../../../render_common/frame_tree_walker.h"

#include <iostream>
#include <sstream>
#include <cstring>
#include <deque>

// ── IFrameNode 包装器：将 LO 的 SwFrame 适配为共享遍历接口 ──
//
// 主要变化:
//   - 新增对 SwSortedObjs (浮动对象) 的遍历能力：LoFrameNode 在它的
//     GetFirstFly() / GetNextSiblingFly() 中访问 SwPageFrame::GetSortedObjs
//     (或 SwLayoutFrame::GetDrawObjs) 作为"浮动对象链"，
//     每一项对应 SwFlyFrame 也会以 Fly 容器的形式递归进入其中的 SwNoTextFrame /
//     SwTextFrame，从而在 frame 树输出中还原图片等浮动内容。
//   - 浮动对象链使用专用子 LoFlySiblingNode 包装——因为它的兄弟关系不是
//     GetNext()，而是 SwSortedObjs::operator[] (i+1)，所以我们单独用一个
//     "index-in-sorted-objs 的节点来表示。

namespace
{
// 将 SwFrame* 包装成统一的 IFrameNode 实现（主链遍历）
class LoFrameNode : public IFrameNode
{
public:
    // 主链节点 (page body section column text tab flycell ...)
    LoFrameNode(const SwFrame* pFrame, int pageNum)
        : m_pFrame(pFrame)
        , m_pageNum(pageNum)
        , m_bIsFlySibling(false)
        , m_pFlyContainer(nullptr)
        , m_flyIndex(0)
        , m_pAnchorFrame(nullptr)
    {
        if (pFrame && pFrame->IsTextFrame())
            ExtractTextInfo();
    }

    // 浮动对象链节点: 挂在某个页面的 SwSortedObjs 上的第 n 个，锚定到 pAnchorFrame
    LoFrameNode(const SwFrame* pFrame, int pageNum, const SwSortedObjs* pContainer, size_t index,
                const SwFrame* pAnchorFrame)
        : m_pFrame(pFrame)
        , m_pageNum(pageNum)
        , m_bIsFlySibling(true)
        , m_pFlyContainer(pContainer)
        , m_flyIndex(index)
        , m_pAnchorFrame(pAnchorFrame)
    {
        if (pFrame && pFrame->IsTextFrame())
            ExtractTextInfo();
    }

    FrameNodeType GetNodeType() const override
    {
        if (!m_pFrame)
            return FrameNodeType::Unknown;
        if (m_pFrame->IsPageFrame())
            return FrameNodeType::Page;
        if (m_pFrame->IsBodyFrame())
            return FrameNodeType::Body;
        if (m_pFrame->IsHeaderFrame())
            return FrameNodeType::Header;
        if (m_pFrame->IsFooterFrame())
            return FrameNodeType::Footer;
        if (m_pFrame->IsSctFrame())
            return FrameNodeType::Section;
        if (m_pFrame->IsColumnFrame())
            return FrameNodeType::Column;
        if (m_pFrame->IsFlyFrame())
            return FrameNodeType::Fly;
        if (m_pFrame->IsTextFrame())
            return FrameNodeType::Text;
        if (m_pFrame->IsTabFrame())
            return FrameNodeType::Table;
        if (m_pFrame->IsRowFrame())
            return FrameNodeType::TabRow;
        if (m_pFrame->IsCellFrame())
            return FrameNodeType::TabCell;
        if (m_pFrame->IsFootnoteContFrame())
            return FrameNodeType::FootnoteCont;
        if (m_pFrame->IsFootnoteFrame())
            return FrameNodeType::Footnote;
        if (m_pFrame->IsNoTextFrame())
            return FrameNodeType::NoText;
        return FrameNodeType::Unknown;
    }

    int GetPageNum() const override { return m_pageNum; }

    void GetRect(int& x, int& y, int& w, int& h) const override
    {
        if (!m_pFrame)
            return;
        const SwRect& r = m_pFrame->getFrameArea();
        x = static_cast<int>(r.Left());
        y = static_cast<int>(r.Top());
        w = static_cast<int>(r.Width());
        h = static_cast<int>(r.Height());
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
        const SwFrame* pLower = static_cast<const SwLayoutFrame*>(m_pFrame)->GetLower();
        return pLower ? new LoFrameNode(pLower, m_pageNum) : nullptr;
    }

    IFrameNode* GetNextSibling() const override
    {
        if (!m_pFrame)
            return nullptr;
        const SwFrame* pNext = m_pFrame->GetNext();
        return pNext ? new LoFrameNode(pNext, m_pageNum) : nullptr;
    }

    // 浮动对象链:
    //   - 在 LO 中，锚定对象存储在页面帧的 SwSortedObjs 中
    //   - 需要找到页面帧，遍历其 SwSortedObjs，筛选锚定到当前帧的对象
    IFrameNode* GetFirstFly() const override
    {
        if (!m_pFrame)
            return nullptr;

        // 找到页面帧
        const SwPageFrame* pPageFrame = m_pFrame->FindPageFrame();
        if (!pPageFrame)
            return nullptr;

        // 获取页面的 SwSortedObjs
        const SwSortedObjs* pObjs = pPageFrame->GetSortedObjs();
        if (!pObjs || pObjs->size() == 0)
            return nullptr;

        // 遍历查找第一个锚定到当前帧的 SwFlyFrame
        for (size_t i = 0; i < pObjs->size(); ++i)
        {
            SwAnchoredObject* pA = (*pObjs)[i];
            if (!pA)
                continue;

            // 检查锚定帧是否匹配
            const SwFrame* pAnchorFrame = pA->GetAnchorFrame();
            if (pAnchorFrame != m_pFrame)
                continue;

            // 尝试转换为 SwFlyFrame
            SwFlyFrame* pFly = pA->DynCastFlyFrame();
            if (!pFly)
                continue;

            // 找到第一个匹配的 fly frame，返回一个带有索引信息的节点
            return new LoFrameNode(pFly, m_pageNum, pObjs, i, m_pFrame);
        }

        return nullptr;
    }

    // 对"已经在 SwSortedObjs 上的"节点返回下一个浮动兄弟
    IFrameNode* GetNextSiblingFly() const override
    {
        if (!m_bIsFlySibling || !m_pFlyContainer || !m_pAnchorFrame)
            return nullptr;

        // 从当前索引的下一个开始查找
        for (size_t i = m_flyIndex + 1; i < m_pFlyContainer->size(); ++i)
        {
            SwAnchoredObject* pA = (*m_pFlyContainer)[i];
            if (!pA)
                continue;

            // 检查锚定帧是否匹配
            const SwFrame* pAnchorFrame = pA->GetAnchorFrame();
            if (pAnchorFrame != m_pAnchorFrame)
                continue;

            // 尝试转换为 SwFlyFrame
            SwFlyFrame* pFly = pA->DynCastFlyFrame();
            if (!pFly)
                continue;

            // 找到下一个匹配的 fly frame
            return new LoFrameNode(pFly, m_pageNum, m_pFlyContainer, i, m_pAnchorFrame);
        }

        return nullptr;
    }

private:
    void ExtractTextInfo()
    {
        const SwTextFrame* pTextFrame = static_cast<const SwTextFrame*>(m_pFrame);
        const SwTextNode* pNode = pTextFrame->GetTextNodeFirst();
        if (!pNode)
            return;

        const OUString& rText = pNode->GetText();
        OString utf8 = OUStringToOString(rText, RTL_TEXTENCODING_UTF8);
        m_textBuf = utf8.getStr();

        const SwAttrSet& rAttrSet = pNode->GetSwAttrSet();

        const SvxFontItem& rFont = rAttrSet.GetFont();
        OString fn = OUStringToOString(rFont.GetFamilyName(), RTL_TEXTENCODING_UTF8);
        m_fontBuf = fn.getStr();

        const SvxFontHeightItem& rSize = rAttrSet.GetSize();
        m_fontSize = static_cast<int>(rSize.GetHeight() / 10);

        const SvxWeightItem& rWeight = rAttrSet.GetWeight();
        m_fontWeight = (rWeight.GetWeight() >= WEIGHT_BOLD) ? 700 : 400;

        const SvxPostureItem& rPosture = rAttrSet.GetPosture();
        m_fontItalic = (rPosture.GetPosture() != ITALIC_NONE) ? 1 : 0;

        const SvxColorItem& rColor = rAttrSet.GetColor();
        Color aColor = rColor.GetValue();
        m_fontColor = (static_cast<uint32_t>(aColor.GetRed()) << 16)
                      | (static_cast<uint32_t>(aColor.GetGreen()) << 8)
                      | static_cast<uint32_t>(aColor.GetBlue());

        const SwFormatColl* pColl = pNode->GetFormatColl();
        if (pColl)
        {
            OString sn = OUStringToOString(pColl->GetName().toString(), RTL_TEXTENCODING_UTF8);
            m_styleBuf = sn.getStr();
        }
    }

    const SwFrame* m_pFrame;
    int m_pageNum;
    std::string m_textBuf;
    std::string m_fontBuf;
    std::string m_styleBuf;
    int m_fontSize = 0;
    uint32_t m_fontColor = 0;
    uint8_t m_fontWeight = 0;
    uint8_t m_fontItalic = 0;

    // 浮动对象链状态: 该节点是否来自 SwSortedObjs 的第 m_flyIndex 个
    // (用于 GetNextSiblingFly 的步进)。主链节点该字段为 false/空。
    bool m_bIsFlySibling;
    const SwSortedObjs* m_pFlyContainer;
    size_t m_flyIndex;
    const SwFrame* m_pAnchorFrame; // 锚定到的帧 (用于筛选页面级浮动对象)
};

} // namespace

// ── 单例 ──

SwPaintEventListener& SwPaintEventListener::Get()
{
    static SwPaintEventListener s_Instance;
    return s_Instance;
}

// ── 控制 ──

void SwPaintEventListener::StartLog(const OString& filePath)
{
    m_sFrameLogPath = filePath;
    m_File.open(filePath.getStr(), std::ios::out | std::ios::binary);
    m_bLogging = true;
    m_aInstructions.clear();
    m_aPrevFrameContent.clear();
}

void SwPaintEventListener::EndLog()
{
    Flush();
    m_File.close();
    m_bLogging = false;
}

void SwPaintEventListener::CheckEnvAndStart()
{
    // 检查环境变量 SW_RENDER_LOG (frame 层日志)
    OUString logPath;
    if (osl_getEnvironment(OUString("SW_RENDER_LOG").pData, &logPath.pData) == osl_Process_E_None)
    {
        if (!logPath.isEmpty())
        {
            OString path = OUStringToOString(logPath, RTL_TEXTENCODING_UTF8);
            StartLog(path);
            std::cerr << "SwPaintEventListener: frame-level logging to " << path.getStr()
                      << std::endl;
        }
    }

    // 检查环境变量 SW_VCL_RENDER_LOG (VCL 层日志)
    OUString vclLogPath;
    if (osl_getEnvironment(OUString("SW_VCL_RENDER_LOG").pData, &vclLogPath.pData)
        == osl_Process_E_None)
    {
        if (!vclLogPath.isEmpty())
        {
            OString path = OUStringToOString(vclLogPath, RTL_TEXTENCODING_UTF8);
            StartVclLog(path);
            std::cerr << "SwPaintEventListener: VCL-level logging to " << path.getStr()
                      << std::endl;
        }
    }
}

// ── RenderInstructionSink ──

void SwPaintEventListener::OnInstruction(const RenderInstruction& inst)
{
    m_aInstructions.push_back(inst);
}

void SwPaintEventListener::Flush()
{
    // 所有指令已实时写入，这里只需 flush 缓冲区
    if (m_File.is_open())
        m_File.flush();
}

// ── Frame 树遍历（使用 render_common 共享遍历器） ──

namespace
{
// TempSink: 边走边序列化，避免 const char* 指针在 LoFrameNode 销毁后失效
struct SerializingSink : RenderInstructionSink
{
    std::string& content;
    explicit SerializingSink(std::string& c)
        : content(c)
    {
    }
    void OnInstruction(const RenderInstruction& inst) override
    {
        // 立即序列化（此时 LoFrameNode 的 buffer 仍然有效）
        std::ostringstream oss;
        WriteInstructionToStream(oss, inst);
        content += oss.str();
    }
};
} // namespace

void SwPaintEventListener::LogFrameTree(SwRootFrame* pRoot)
{
    if (!m_bLogging || !pRoot)
        return;

    // 收集新一帧的 frame 指令，边走边序列化为 TSV
    std::string newContent;

    int pageNum = 1;
    for (SwFrame* pFrame = pRoot->GetLower(); pFrame; pFrame = pFrame->GetNext())
    {
        SwPageFrame* pPage = static_cast<SwPageFrame*>(pFrame);
        LoFrameNode rootNode(pPage, pageNum);
        SerializingSink sink(newContent);
        WalkFrameTreeAndLog(&rootNode, sink);
        ++pageNum;
    }

    // 与上次比较
    if (newContent != m_aPrevFrameContent || m_aPrevFrameContent.empty())
    {
        std::cerr << "[FrameTree] output changed"
                  << " (prev=" << m_aPrevFrameContent.size() << " now=" << newContent.size() << ")"
                  << std::endl;

        // 重写文件（覆盖而非追加）
        m_File.close();
        m_File.open(m_sFrameLogPath.getStr(), std::ios::out | std::ios::trunc | std::ios::binary);
        if (m_File.is_open())
        {
            m_File << newContent;
            m_File.flush();
        }

        m_aPrevFrameContent = std::move(newContent);
    }
}

// ── VCL 层录制 (GDIMetaFile 方式) ──

void SwPaintEventListener::StartVclLog(const OString& filePath)
{
    m_sVclLogPath = filePath;
    m_vclFile.open(filePath.getStr(), std::ios::out | std::ios::binary);
    m_bVclLogging = true;
    m_aPrevVclContent.clear();
}

void SwPaintEventListener::EndVclLog()
{
    if (m_vclFile.is_open())
        m_vclFile.flush();
    m_vclFile.close();
    m_bVclLogging = false;
}

void SwPaintEventListener::StartPageRecord(OutputDevice* pOutDev)
{
    if (!m_bVclLogging || !pOutDev)
        return;

    // 开始录制 OutputDevice 的所有 Draw* 调用
    m_aMetaFile.Record(pOutDev);
}

void SwPaintEventListener::StopPageRecordAndConvert(int pageNum)
{
    if (!m_bVclLogging)
        return;

    // 停止录制
    m_aMetaFile.Stop();

    // 边走边序列化（避免 const char* 指针在 static thread_local buffer 复用后失效）
    std::string pageContent;
    SerializingSink sink(pageContent);
    m_aConverter.Convert(m_aMetaFile, sink, pageNum);

    // 追加到累积的 VCL 内容中
    m_aPrevVclContent += pageContent;

    std::cerr << "[VCL] page " << pageNum << " recorded (" << pageContent.size() << " bytes, total "
              << m_aPrevVclContent.size() << " bytes)" << std::endl;

    // 重写 VCL 文件（覆盖模式）
    m_vclFile.close();
    m_vclFile.open(m_sVclLogPath.getStr(), std::ios::out | std::ios::trunc | std::ios::binary);
    if (m_vclFile.is_open())
    {
        m_vclFile << m_aPrevVclContent;
        m_vclFile.flush();
    }

    // 清空 MetaFile 准备下一页
    m_aMetaFile.Clear();
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
