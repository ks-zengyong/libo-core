#pragma once
// 共享渲染指令定义 — LibreOffice 和 aproj/docx 共用
// 零外部依赖，纯 POD 结构
// 公共模块: render_common/ — sw 和 aproj/docx 都引用此文件
//
// 新的容器语义：
//   每个容器型节点(Page / Section / Column / Table / TabRow / TabCell /
//   Header / Footer / FootnoteCont / Fly) 都会生成一对
//   <NAME>_START / <NAME>_END 指令，中间是它内部的子节点 (文本、
//   图片、嵌套容器、子浮动对象等)。
//   非容器型节点 (TEXT_FRAME / IMAGE_FRAME / FOOTNOTE_FRAME / VCL 指令)
//   仍然是单条指令。
//   每条指令都携带 nestLevel，TSV 输出时会在行首追加对应层级的缩
//   进空格，便于用文本 diff 直接观察树形结构。

#include <cstdint>
#include <cstddef>
#include <cstring> // memset

// 渲染指令类型
enum class RenderCmdType : uint8_t
{
    UNKNOWN = 0,

    // ── 页面 (容器) ──
    PAGE_START = 1,
    PAGE_END = 2,

    // ── 文本 (非容器) ──
    TEXT_FRAME = 10, // 整段文本 (frame 语义层)
    TEXT_LINE = 11, // 单行文本
    TEXT_RUN = 12, // 同字体/样式的文本片段

    // ── 图像 / 脚注 (非容器) ──
    IMAGE_FRAME = 30,
    FOOTNOTE_FRAME = 31,

    // ── 节 / 分栏 (容器) ──
    SECTION_START = 40,
    SECTION_END = 41,
    COLUMN_START = 42,
    COLUMN_END = 43,

    // ── 页眉 / 页脚 (容器) ──
    HEADER_START = 44,
    HEADER_END = 45,
    FOOTER_START = 46,
    FOOTER_END = 47,

    // ── 脚注容器 (容器) ──
    FOOTNOTE_CONT_START = 48,
    FOOTNOTE_CONT_END = 49,

    // ── 表格 / 行 / 单元格 (容器) ──
    TABLE_START = 50,
    TABLE_END = 51,
    TABLEROW_START = 52,
    TABLEROW_END = 53,
    TABLECELL_START = 54,
    TABLECELL_END = 55,

    // ── 浮动对象 (容器) ──
    FLY_START = 60,
    FLY_END = 61,

    // ── VCL 绘制层指令 (由 GDIMetaFile 转换生成，非容器) ──
    RECT = 100,
    LINE = 101,
    POLYGON = 102,
    BITMAP = 103,
    ELLIPSE = 104,
    POLYLINE = 105,

    // ── 状态变更指令 (用于重建绘制上下文，非容器) ──
    SET_FONT = 120,
    SET_LINE_COLOR = 121,
    SET_FILL_COLOR = 122,
    SET_TEXT_COLOR = 123,
    SET_CLIP_REGION = 124,
    PUSH = 125,
    POP = 126,
};

// 辅助判断：某指令类型是否是"容器型 END" (遍历器用来与 START 对齐)
inline bool IsContainerEnd(RenderCmdType t)
{
    switch (t)
    {
        case RenderCmdType::PAGE_END:
        case RenderCmdType::SECTION_END:
        case RenderCmdType::COLUMN_END:
        case RenderCmdType::HEADER_END:
        case RenderCmdType::FOOTER_END:
        case RenderCmdType::FOOTNOTE_CONT_END:
        case RenderCmdType::TABLE_END:
        case RenderCmdType::TABLEROW_END:
        case RenderCmdType::TABLECELL_END:
        case RenderCmdType::FLY_END:
            return true;
        default:
            return false;
    }
}

// 给定一个容器型节点的 FrameNodeType, 分别返回其 START 和 END 指令类型
// (若该节点不是容器，返回 RenderCmdType::TEXT_FRAME —— 调用方应避免)
template <typename /*FrameNodeType*/> struct ContainerCmdTypes; // 声明，按需特化

// 渲染指令数据 — 纯 POD，可直接序列化
struct RenderInstruction
{
    RenderCmdType type;
    int pageNum; // 页码 (从 1 开始)
    int nestLevel; // 缩进层级 (0 = root, 1 = page 内, 2 = 子容器, ...)
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
        case RenderCmdType::IMAGE_FRAME:
            return "IMAGE_FRAME";
        case RenderCmdType::SECTION_START:
            return "SECTION_START";
        case RenderCmdType::SECTION_END:
            return "SECTION_END";
        case RenderCmdType::COLUMN_START:
            return "COLUMN_START";
        case RenderCmdType::COLUMN_END:
            return "COLUMN_END";
        case RenderCmdType::HEADER_START:
            return "HEADER_START";
        case RenderCmdType::HEADER_END:
            return "HEADER_END";
        case RenderCmdType::FOOTER_START:
            return "FOOTER_START";
        case RenderCmdType::FOOTER_END:
            return "FOOTER_END";
        case RenderCmdType::FOOTNOTE_CONT_START:
            return "FOOTNOTE_CONT_START";
        case RenderCmdType::FOOTNOTE_CONT_END:
            return "FOOTNOTE_CONT_END";
        case RenderCmdType::FOOTNOTE_FRAME:
            return "FOOTNOTE_FRAME";
        case RenderCmdType::TABLE_START:
            return "TABLE_START";
        case RenderCmdType::TABLE_END:
            return "TABLE_END";
        case RenderCmdType::TABLEROW_START:
            return "TABLEROW_START";
        case RenderCmdType::TABLEROW_END:
            return "TABLEROW_END";
        case RenderCmdType::TABLECELL_START:
            return "TABLECELL_START";
        case RenderCmdType::TABLECELL_END:
            return "TABLECELL_END";
        case RenderCmdType::FLY_START:
            return "FLY_START";
        case RenderCmdType::FLY_END:
            return "FLY_END";
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

// 指令名称反解 (用于从 TSV 行字符串解析类型)
inline RenderCmdType RenderCmdTypeFromName(const char* name)
{
    if (!name)
        return RenderCmdType::UNKNOWN;
    // 先跳过开头的空格缩进
    while (*name == ' ' || *name == '\t')
        ++name;
    if (strcmp(name, "PAGE_START") == 0)
        return RenderCmdType::PAGE_START;
    if (strcmp(name, "PAGE_END") == 0)
        return RenderCmdType::PAGE_END;
    if (strcmp(name, "TEXT_FRAME") == 0)
        return RenderCmdType::TEXT_FRAME;
    if (strcmp(name, "TEXT_LINE") == 0)
        return RenderCmdType::TEXT_LINE;
    if (strcmp(name, "TEXT_RUN") == 0)
        return RenderCmdType::TEXT_RUN;
    if (strcmp(name, "IMAGE_FRAME") == 0)
        return RenderCmdType::IMAGE_FRAME;
    if (strcmp(name, "SECTION_START") == 0)
        return RenderCmdType::SECTION_START;
    if (strcmp(name, "SECTION_END") == 0)
        return RenderCmdType::SECTION_END;
    if (strcmp(name, "COLUMN_START") == 0)
        return RenderCmdType::COLUMN_START;
    if (strcmp(name, "COLUMN_END") == 0)
        return RenderCmdType::COLUMN_END;
    if (strcmp(name, "HEADER_START") == 0)
        return RenderCmdType::HEADER_START;
    if (strcmp(name, "HEADER_END") == 0)
        return RenderCmdType::HEADER_END;
    if (strcmp(name, "FOOTER_START") == 0)
        return RenderCmdType::FOOTER_START;
    if (strcmp(name, "FOOTER_END") == 0)
        return RenderCmdType::FOOTER_END;
    if (strcmp(name, "FOOTNOTE_CONT_START") == 0)
        return RenderCmdType::FOOTNOTE_CONT_START;
    if (strcmp(name, "FOOTNOTE_CONT_END") == 0)
        return RenderCmdType::FOOTNOTE_CONT_END;
    if (strcmp(name, "FOOTNOTE_FRAME") == 0)
        return RenderCmdType::FOOTNOTE_FRAME;
    if (strcmp(name, "TABLE_START") == 0)
        return RenderCmdType::TABLE_START;
    if (strcmp(name, "TABLE_END") == 0)
        return RenderCmdType::TABLE_END;
    if (strcmp(name, "TABLEROW_START") == 0)
        return RenderCmdType::TABLEROW_START;
    if (strcmp(name, "TABLEROW_END") == 0)
        return RenderCmdType::TABLEROW_END;
    if (strcmp(name, "TABLECELL_START") == 0)
        return RenderCmdType::TABLECELL_START;
    if (strcmp(name, "TABLECELL_END") == 0)
        return RenderCmdType::TABLECELL_END;
    if (strcmp(name, "FLY_START") == 0)
        return RenderCmdType::FLY_START;
    if (strcmp(name, "FLY_END") == 0)
        return RenderCmdType::FLY_END;
    if (strcmp(name, "RECT") == 0)
        return RenderCmdType::RECT;
    if (strcmp(name, "LINE") == 0)
        return RenderCmdType::LINE;
    if (strcmp(name, "POLYGON") == 0)
        return RenderCmdType::POLYGON;
    if (strcmp(name, "BITMAP") == 0)
        return RenderCmdType::BITMAP;
    if (strcmp(name, "ELLIPSE") == 0)
        return RenderCmdType::ELLIPSE;
    if (strcmp(name, "POLYLINE") == 0)
        return RenderCmdType::POLYLINE;
    if (strcmp(name, "SET_FONT") == 0)
        return RenderCmdType::SET_FONT;
    if (strcmp(name, "SET_LINE_COLOR") == 0)
        return RenderCmdType::SET_LINE_COLOR;
    if (strcmp(name, "SET_FILL_COLOR") == 0)
        return RenderCmdType::SET_FILL_COLOR;
    if (strcmp(name, "SET_TEXT_COLOR") == 0)
        return RenderCmdType::SET_TEXT_COLOR;
    if (strcmp(name, "SET_CLIP_REGION") == 0)
        return RenderCmdType::SET_CLIP_REGION;
    if (strcmp(name, "PUSH") == 0)
        return RenderCmdType::PUSH;
    if (strcmp(name, "POP") == 0)
        return RenderCmdType::POP;
    return RenderCmdType::UNKNOWN;
}
