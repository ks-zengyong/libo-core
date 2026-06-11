#pragma once
// 渲染指令记录器 — 实现共享 RenderInstructionSink 接口
// 与 LibreOffice 侧 SwPaintEventListener 使用同一套 render_instruction.h 定义

#include "render_instruction.h"
#include "../core/types.h"
#include "../frame/frame.h"
#include <string>
#include <vector>
#include <fstream>

// 前向声明
class SwDoc;

// RenderLogger: 渲染指令记录器
class RenderLogger : public RenderInstructionSink
{
public:
    RenderLogger();
    ~RenderLogger() override;

    // ── RenderInstructionSink 接口 ──
    void OnInstruction(const RenderInstruction& inst) override;

    // ── 记录控制 ──
    void StartLog(const std::string& filePath);
    void EndLog();
    bool IsLogging() const { return m_bLogging; }

    // ── 高级接口 — 由调用方构建指令后提交 ──
    void LogPageStart(int pageNum, int width, int height);
    void LogPageEnd(int pageNum);
    void LogTextFrame(int pageNum, int x, int y, int width, int height, const char* text,
                      int textLen, const char* fontName, int fontSize, uint32_t fontColor,
                      uint8_t fontWeight, uint8_t fontItalic, const char* styleName);
    void LogTextLine(int pageNum, int x, int y, int width, int height, const char* text,
                     int textLen, const char* fontName, int fontSize, uint32_t fontColor,
                     uint8_t fontWeight, uint8_t fontItalic, const char* styleName);
    void LogTextRun(int pageNum, int x, int y, int width, int height, const char* text, int textLen,
                    const char* fontName, int fontSize, uint32_t fontColor, uint8_t fontWeight,
                    uint8_t fontItalic);
    void LogTableFrame(int pageNum, int x, int y, int width, int height);
    void LogTableRow(int pageNum, int x, int y, int width, int height);
    void LogTableCell(int pageNum, int x, int y, int width, int height);
    void LogImageFrame(int pageNum, int x, int y, int width, int height);
    void LogRect(int pageNum, int x, int y, int width, int height);
    void LogLine(int pageNum, int x1, int y1, int x2, int y2);

    // ── Frame 树遍历 ──
    // 遍历整个 Frame 树，生成渲染指令
    void LogFrameTree(SwRootFrame* pRoot);

    // ── 输出 ──
    // 获取所有记录的指令
    const std::vector<RenderInstruction>& GetInstructions() const { return m_aInstructions; }
    // 将指令写入文件 (与 LibreOffice 相同的 TSV 格式)
    void WriteToFile(const std::string& filePath);

    // ── 格式化工具 (静态) ──
    // 将单条指令格式化为 TSV 行 (与 LibreOffice 完全相同的格式)
    static void WriteInstructionToStream(std::ostream& out, const RenderInstruction& inst);

private:
    void LogFrameRecursive(SwFrame* pFrame, int pageNum);

    std::vector<RenderInstruction> m_aInstructions;
    std::ofstream m_File;
    bool m_bLogging;
};

// DumpFrameTree: 将 Frame 树转储为 XML (调试用)
void DumpFrameTreeXml(SwRootFrame* pRoot, const std::string& filePath);

// DumpNodesXml: 将 SwNodes 转储为 XML (调试用)
void DumpNodesXml(SwDoc& doc, const std::string& filePath);
