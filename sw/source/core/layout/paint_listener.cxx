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
#include <frame.hxx>
#include <ndtxt.hxx>
#include <swrect.hxx>
#include <hintids.hxx>
#include <charatr.hxx>
#include <fmtcol.hxx>
#include <names.hxx>
#include <osl/process.h>
#include <rtl/strbuf.hxx>
#include <tools/color.hxx>

#include <iostream>
#include <sstream>
#include <cstring>

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

// ── 高级接口 ──

void SwPaintEventListener::OnPageStart(int pageNum, int width, int height)
{
    if (!m_bLogging)
        return;
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::PAGE_START;
    inst.pageNum = pageNum;
    inst.width = width;
    inst.height = height;
    OnInstruction(inst);
}

void SwPaintEventListener::OnPageEnd(int pageNum)
{
    if (!m_bLogging)
        return;
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::PAGE_END;
    inst.pageNum = pageNum;
    OnInstruction(inst);
}

void SwPaintEventListener::OnTextFrame(const SwTextFrame* pFrame)
{
    if (!m_bLogging || !pFrame)
        return;

    // 获取 Frame 几何
    const SwRect& rArea = pFrame->getFrameArea();

    // 获取文本节点
    const SwTextNode* pNode = pFrame->GetTextNodeFirst();
    if (!pNode)
        return;

    // 获取文本
    const OUString& rText = pNode->GetText();
    OString utf8Text = OUStringToOString(rText, RTL_TEXTENCODING_UTF8);

    // 获取属性
    const SwAttrSet& rAttrSet = pNode->GetSwAttrSet();

    // 字体名
    const SvxFontItem& rFont = rAttrSet.GetFont();
    OString fontName = OUStringToOString(rFont.GetFamilyName(), RTL_TEXTENCODING_UTF8);

    // 字号 (FontHeight 是 twips, 转为半点: twips / 10)
    const SvxFontHeightItem& rSize = rAttrSet.GetSize();
    int fontSize = static_cast<int>(rSize.GetHeight() / 10);

    // 粗体
    const SvxWeightItem& rWeight = rAttrSet.GetWeight();
    uint8_t fontWeight = (rWeight.GetWeight() >= WEIGHT_BOLD) ? 700 : 400;

    // 斜体
    const SvxPostureItem& rPosture = rAttrSet.GetPosture();
    uint8_t fontItalic = (rPosture.GetPosture() != ITALIC_NONE) ? 1 : 0;

    // 颜色
    const SvxColorItem& rColor = rAttrSet.GetColor();
    Color aColor = rColor.GetValue();
    uint32_t fontColor = (static_cast<uint32_t>(aColor.GetRed()) << 16)
                         | (static_cast<uint32_t>(aColor.GetGreen()) << 8)
                         | static_cast<uint32_t>(aColor.GetBlue());

    // 样式名
    const SwFormatColl* pColl = pNode->GetFormatColl();
    OString styleName;
    if (pColl)
        styleName = OUStringToOString(pColl->GetName().toString(), RTL_TEXTENCODING_UTF8);

    // 页码
    const SwPageFrame* pPage = pFrame->FindPageFrame();
    int pageNum = pPage ? pPage->GetPhyPageNum() : 1;

    // 构造指令
    RenderInstruction inst;
    RenderInstruction_clear(&inst);
    inst.type = RenderCmdType::TEXT_FRAME;
    inst.pageNum = pageNum;
    inst.x = static_cast<int>(rArea.Left());
    inst.y = static_cast<int>(rArea.Top());
    inst.width = static_cast<int>(rArea.Width());
    inst.height = static_cast<int>(rArea.Height());

    // 文本需要持久化 (指令存储的是指针)
    // 使用静态缓冲区避免频繁分配
    static thread_local std::string s_textBuf;
    static thread_local std::string s_fontBuf;
    static thread_local std::string s_styleBuf;

    s_textBuf = utf8Text.getStr();
    s_fontBuf = fontName.getStr();
    s_styleBuf = styleName.getStr();

    inst.text = s_textBuf.c_str();
    inst.textLen = static_cast<int>(s_textBuf.size());
    inst.fontName = s_fontBuf.c_str();
    inst.fontSize = fontSize;
    inst.fontColor = fontColor;
    inst.fontWeight = fontWeight;
    inst.fontItalic = fontItalic;
    inst.styleName = s_styleBuf.empty() ? nullptr : s_styleBuf.c_str();

    OnInstruction(inst);
}

// ── 格式化 ──

// 转义字符串中的换行符和制表符，确保 TSV 每条指令一行
static OString EscapeForTsv(const char* s)
{
    if (!s)
        return OString();
    OStringBuffer buf;
    for (; *s; ++s)
    {
        switch (*s)
        {
            case '\n':
                buf.append("\\n");
                break;
            case '\r':
                buf.append("\\r");
                break;
            case '\t':
                buf.append("\\t");
                break;
            case '"':
                buf.append("\\\"");
                break;
            default:
                if (static_cast<unsigned char>(*s) < 0x20)
                    buf.append(' ');
                else
                    buf.append(*s);
                break;
        }
    }
    return buf.makeStringAndClear();
}

void SwPaintEventListener::WriteInstructionToStream(std::ostream& out,
                                                    const RenderInstruction& inst)
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
                << "\t" << inst.height << "\t\"" << EscapeForTsv(inst.text).getStr() << "\""
                << "\t" << EscapeForTsv(inst.fontName).getStr() << "\t" << inst.fontSize << "\t"
                << inst.fontColor << "\t" << static_cast<int>(inst.fontWeight) << "\t"
                << static_cast<int>(inst.fontItalic) << "\t" << static_cast<int>(inst.underline)
                << "\t" << static_cast<int>(inst.strikeout) << "\t"
                << EscapeForTsv(inst.styleName).getStr();
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
            out << "\t" << inst.pageNum << "\t" << inst.x << "\t" << inst.y << "\t" << inst.width
                << "\t" << inst.height;
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

void SwPaintEventListener::Flush()
{
    // 所有指令已实时写入，这里只需 flush 缓冲区
    if (m_File.is_open())
        m_File.flush();
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
