#pragma once
// 共享节点指令定义 — LibreOffice 和 aproj/docx 共用
// 零外部依赖，纯 POD 结构
// 公共模块: render_common/ — sw 和 aproj/docx 都引用此文件
//
// 节点层语义：
//   遍历 SwNodes 数组，为每个节点生成对应的 NodeInstruction。
//   容器型节点 (StartNode/EndNode/Table/Section) 会生成一对
//   <NAME>_START / <NAME>_END 指令，中间是它内部的子节点。
//   内容型节点 (Text/Grf/Ole) 是单条指令。
//   每条指令都携带 nestLevel，TSV 输出时会在行首追加对应层级的缩
//   进空格，便于用文本 diff 直接观察树形结构。

#include <cstdint>
#include <cstddef>
#include <cstring> // memset

// 节点指令类型
enum class NodeCmdType : uint8_t
{
    UNKNOWN = 0,

    // ── 节区标记 (容器) ──
    START_NODE = 1, // SwStartNode (容器 START)
    END_NODE = 2, // SwEndNode (容器 END)

    // ── 内容节点 (非容器) ──
    TEXT_NODE = 10, // SwTextNode (段落)
    GRF_NODE = 11, // SwGrfNode (图片)
    OLE_NODE = 12, // SwOLENode (OLE 对象)

    // ── 表格节点 (容器) ──
    TABLE_START = 20, // SwTableNode (容器 START)
    TABLE_END = 21, // SwTableNode (容器 END)

    // ── 节节点 (容器) ──
    SECTION_START = 30, // SwSectionNode (容器 START)
    SECTION_END = 31, // SwSectionNode (容器 END)
};

// 辅助判断：某指令类型是否是"容器型 END"
inline bool IsNodeContainerEnd(NodeCmdType t)
{
    switch (t)
    {
        case NodeCmdType::END_NODE:
        case NodeCmdType::TABLE_END:
        case NodeCmdType::SECTION_END:
            return true;
        default:
            return false;
    }
}

// 节点指令数据 — 纯 POD，可直接序列化
struct NodeInstruction
{
    NodeCmdType type;
    int nodeIndex; // 节点在 SwNodes 数组中的索引
    int nestLevel; // 缩进层级 (0 = root, 1 = 第一层子节点, ...)

    // StartNode 子类型 (仅 START_NODE 有效)
    // 0=Normal, 1=TableBox, 2=Fly, 3=Footnote, 4=Header, 5=Footer
    int startNodeType;

    // 文本相关 (仅 TEXT_NODE 有效)
    const char* text; // 文本内容 (UTF-8, 可能为 nullptr)
    int textLen; // 文本长度 (不含 \0)
    const char* styleName; // 段落样式名 (可能为 nullptr)

    // 表格相关 (仅 TABLE_START 有效)
    int tableRows; // 表格行数
    int tableCols; // 表格列数

    // 清零初始化辅助
    void clear() { memset(this, 0, sizeof(*this)); }
};

// 指令接收接口 (纯虚类)
class NodeInstructionSink
{
public:
    virtual ~NodeInstructionSink() = default;
    virtual void OnInstruction(const NodeInstruction& inst) = 0;
};

// 指令类型名称 (用于文本输出)
inline const char* NodeCmdTypeName(NodeCmdType t)
{
    switch (t)
    {
        case NodeCmdType::START_NODE:
            return "START_NODE";
        case NodeCmdType::END_NODE:
            return "END_NODE";
        case NodeCmdType::TEXT_NODE:
            return "TEXT_NODE";
        case NodeCmdType::GRF_NODE:
            return "GRF_NODE";
        case NodeCmdType::OLE_NODE:
            return "OLE_NODE";
        case NodeCmdType::TABLE_START:
            return "TABLE_START";
        case NodeCmdType::TABLE_END:
            return "TABLE_END";
        case NodeCmdType::SECTION_START:
            return "SECTION_START";
        case NodeCmdType::SECTION_END:
            return "SECTION_END";
        default:
            return "UNKNOWN";
    }
}

// StartNode 子类型名称
inline const char* StartNodeTypeName(int type)
{
    switch (type)
    {
        case 0:
            return "Normal";
        case 1:
            return "TableBox";
        case 2:
            return "Fly";
        case 3:
            return "Footnote";
        case 4:
            return "Header";
        case 5:
            return "Footer";
        default:
            return "Unknown";
    }
}
