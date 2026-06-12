#pragma once
// 共享渲染指令定义 — LibreOffice 和 aproj/docx 共用
// 零外部依赖，纯 POD 结构
// 此文件应被 LibreOffice sw/source/core/inc/ 和 aproj/docx/src/render/ 同时包含

#include <cstdint>
#include <cstddef>
#include <cstring> // memset

// 渲染指令类型
enum class RenderCmdType : uint8_t
{
    PAGE_START = 1, // 页面开始
    PAGE_END = 2, // 页面结束
    TEXT_FRAME = 10, // 文本段落 (整段, frame 语义层)
    TEXT_LINE = 11, // 单行文本
    TEXT_RUN = 12, // 文本片段 (同一字体/样式的连续文本)
    TABLE_FRAME = 20, // 表格
    TABLE_ROW = 21, // 表格行
    TABLE_CELL = 22, // 表格单元格
    IMAGE_FRAME = 30, // 图片
    SECTION_FRAME = 40, // 节
    RECT = 50, // 矩形 (背景/边框)
    LINE = 51, // 线段 (分隔线)

    // VCL 绘制层指令 (由 GDIMetaFile 转换生成)
    POLYGON = 60, // DrawPolygon / DrawPolyPolygon
    BITMAP = 61, // DrawBitmapEx
    ELLIPSE = 62, // DrawEllipse
    POLYLINE = 63, // DrawPolyLine

    // 状态变更指令 (用于重建绘制上下文)
    SET_FONT = 80, // SetFont()
    SET_LINE_COLOR = 81, // SetLineColor()
    SET_FILL_COLOR = 82, // SetFillColor()
    SET_TEXT_COLOR = 83, // SetTextColor()
    SET_CLIP_REGION = 84, // SetClipRegion()
    PUSH = 85, // Push() 状态保存
    POP = 86, // Pop() 状态恢复
};

// 渲染指令数据 — 纯 POD，可直接序列化
struct RenderInstruction
{
    RenderCmdType type;
    int pageNum; // 页码 (从 1 开始)
    int x, y; // 位置 (twips)
    int width, height; // 尺寸 (twips)

    // 文本相关
    const char* text; // 文本内容 (UTF-8, 可能为 nullptr)
    int textLen; // 文本长度 (不含 \0)
    const char* fontName; // 字体名 (可能为 nullptr)
    int fontSize; // 字号 (半点, 22 = 11pt)
    uint32_t fontColor; // 字体颜色 (0xRRGGBB)
    uint8_t fontWeight; // 字重 (100-900, 400=normal, 700=bold)
    uint8_t fontItalic; // 斜体 (0/1)
    uint8_t underline; // 下划线类型 (0=无, 1=单线, 2=双线)
    uint8_t strikeout; // 删除线类型 (0=无, 1=单线)
    uint8_t reserved[4]; // 对齐/保留

    // 段落相关
    const char* styleName; // 段落样式名 (可能为 nullptr)
};

// 清零初始化辅助
inline void RenderInstruction_clear(RenderInstruction* p)
{
    // NOLINTNEXTLINE - intentional memset for POD zero-init
    memset(p, 0, sizeof(*p));
}

// 渲染指令接收接口 (纯虚类)
class RenderInstructionSink
{
public:
    virtual ~RenderInstructionSink() = default;
    virtual void OnInstruction(const RenderInstruction& inst) = 0;
};

// 指令类型名称 (用于文本输出)
inline const char* RenderCmdTypeName(RenderCmdType t)
{
    switch (t)
    {
        case RenderCmdType::PAGE_START:
            return "PAGE_START";
        case RenderCmdType::PAGE_END:
            return "PAGE_END";
        case RenderCmdType::TEXT_FRAME:
            return "TEXT_FRAME";
        case RenderCmdType::TEXT_LINE:
            return "TEXT_LINE";
        case RenderCmdType::TEXT_RUN:
            return "TEXT_RUN";
        case RenderCmdType::TABLE_FRAME:
            return "TABLE_FRAME";
        case RenderCmdType::TABLE_ROW:
            return "TABLE_ROW";
        case RenderCmdType::TABLE_CELL:
            return "TABLE_CELL";
        case RenderCmdType::IMAGE_FRAME:
            return "IMAGE_FRAME";
        case RenderCmdType::SECTION_FRAME:
            return "SECTION_FRAME";
        case RenderCmdType::RECT:
            return "RECT";
        case RenderCmdType::LINE:
            return "LINE";
        case RenderCmdType::POLYGON:
            return "POLYGON";
        case RenderCmdType::BITMAP:
            return "BITMAP";
        case RenderCmdType::ELLIPSE:
            return "ELLIPSE";
        case RenderCmdType::POLYLINE:
            return "POLYLINE";
        case RenderCmdType::SET_FONT:
            return "SET_FONT";
        case RenderCmdType::SET_LINE_COLOR:
            return "SET_LINE_COLOR";
        case RenderCmdType::SET_FILL_COLOR:
            return "SET_FILL_COLOR";
        case RenderCmdType::SET_TEXT_COLOR:
            return "SET_TEXT_COLOR";
        case RenderCmdType::SET_CLIP_REGION:
            return "SET_CLIP_REGION";
        case RenderCmdType::PUSH:
            return "PUSH";
        case RenderCmdType::POP:
            return "POP";
        default:
            return "UNKNOWN";
    }
}

// 指令类型解析 (从文本解析)
inline RenderCmdType RenderCmdTypeFromName(const char* name)
{
    if (!name)
        return RenderCmdType::PAGE_START;
    switch (name[0])
    {
        case 'P':
            if (name[1] == 'A')
            {
                if (name[5] == 'S')
                    return RenderCmdType::PAGE_START;
                return RenderCmdType::PAGE_END;
            }
            if (name[1] == 'O')
            {
                if (name[4] == 'G')
                    return RenderCmdType::POLYGON;
                return RenderCmdType::POLYLINE;
            }
            return RenderCmdType::POP;
        case 'T':
            switch (name[5])
            {
                case 'F':
                    return RenderCmdType::TEXT_FRAME;
                case 'L':
                    return RenderCmdType::TEXT_LINE;
                case 'R':
                    return RenderCmdType::TEXT_RUN;
                case 'A':
                    return RenderCmdType::TABLE_FRAME;
                case 'O':
                    return RenderCmdType::TABLE_ROW;
                case 'C':
                    return RenderCmdType::TABLE_CELL;
                default:
                    break;
            }
            break;
        case 'I':
            return RenderCmdType::IMAGE_FRAME;
        case 'S':
            if (name[1] == 'E')
            {
                if (name[4] == 'F')
                    return RenderCmdType::SET_FONT;
                if (name[4] == 'L')
                    return RenderCmdType::SET_LINE_COLOR;
                if (name[4] == 'F' && name[5] == 'I')
                    return RenderCmdType::SET_FILL_COLOR;
                if (name[4] == 'T')
                    return RenderCmdType::SET_TEXT_COLOR;
                if (name[4] == 'C')
                    return RenderCmdType::SET_CLIP_REGION;
            }
            return RenderCmdType::SECTION_FRAME;
        case 'R':
            return RenderCmdType::RECT;
        case 'L':
            return RenderCmdType::LINE;
        case 'B':
            return RenderCmdType::BITMAP;
        case 'E':
            return RenderCmdType::ELLIPSE;
        default:
            break;
    }
    return RenderCmdType::PAGE_START;
}
