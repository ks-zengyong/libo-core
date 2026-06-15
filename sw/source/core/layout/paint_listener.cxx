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

namespace
{
class LoFrameNode : public IFrameNode
{
public:
    LoFrameNode(const SwFrame* pFrame, int pageNum)
        : m_pFrame(pFrame)
        , m_pageNum(pageNum)
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
        if (m_pFrame->IsFlyFrame())
            return FrameNodeType::Fly;
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
    m_File.open(filePath.getStr(), std::ios::out | std::ios::binary);
    m_bLogging = true;
    m_aInstructions.clear();
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
    if (m_bConvertingVcl)
    {
        // VCL 层转换 → 写入 VCL 日志文件
        if (m_bVclLogging && m_vclFile.is_open())
            WriteInstructionToStream(m_vclFile, inst);
    }
    else
    {
        // Frame 层事件 → 写入 frame 日志文件
        if (m_bLogging && m_File.is_open())
            WriteInstructionToStream(m_File, inst);
    }
}

void SwPaintEventListener::Flush()
{
    // 所有指令已实时写入，这里只需 flush 缓冲区
    if (m_File.is_open())
        m_File.flush();
}

// ── Frame 树遍历（使用 render_common 共享遍历器） ──

void SwPaintEventListener::LogFrameTree(SwRootFrame* pRoot)
{
    if (!m_bLogging || !pRoot)
        return;

    // 收集所有页面
    SwPageFrame* pPage = pRoot->GetLastPage();
    std::vector<SwPageFrame*> pages;
    while (pPage)
    {
        pages.push_back(pPage);
        pPage = pPage->GetPrevPage();
    }
    // 反转为正序
    for (size_t i = 0; i < pages.size() / 2; ++i)
        std::swap(pages[i], pages[pages.size() - 1 - i]);

    for (size_t i = 0; i < pages.size(); ++i)
    {
        int pageNum = static_cast<int>(i) + 1;
        LoFrameNode rootNode(pages[i], pageNum);
        WalkFrameTreeAndLog(&rootNode, *this);
    }
}

// ── VCL 层录制 (GDIMetaFile 方式) ──

void SwPaintEventListener::StartVclLog(const OString& filePath)
{
    m_vclFile.open(filePath.getStr(), std::ios::out | std::ios::binary);
    m_bVclLogging = true;
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

    // 设置标志，使 OnInstruction 将指令写入 VCL 文件
    m_bConvertingVcl = true;

    // 将 MetaAction 序列转换为 RenderInstruction 并输出
    m_aConverter.Convert(m_aMetaFile, *this, pageNum);

    m_bConvertingVcl = false;

    // 清空 MetaFile 准备下一页
    m_aMetaFile.Clear();
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
