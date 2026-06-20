#pragma once
// 简化版 SwNode 层级，对应 LibreOffice 的 sw/inc/node.hxx
// 保留核心结构，去掉 UNO/SwClient/SfxBroadcaster 等重型依赖

#include "types.h"
#include "bparr.h"
#include <string>
#include <vector>
#include <memory>
#include <map>

// 前向声明
class SwNodes;
class SwDoc;
class SwFrame;
class SwContentFrame;
class SwTextFrame;
class SwTabFrame; // 新增
class SwSectionFrame; // 新增
class SwLayoutFrame; // 新增
class SwSection; // 新增
class SwStartNode;
class SwEndNode;
class SwTextNode;
class SwGrfNode;
class SwTableNode;
class SwSectionNode;
class SwTextFormatColl;
class SwFrameFormat;

// 节点类型枚举，对应 LibreOffice 的 SwNodeType
enum class SwNodeType : sal_uInt8
{
    // Start 类型（用于位运算判断）
    Start = 0x01, // SwStartNode
    End = 0x02, // SwEndNode
    Text = 0x04, // SwTextNode
    Grf = 0x08, // SwGrfNode (图片)
    Ole = 0x10, // SwOLENode
    Table = 0x20, // SwTableNode (继承 SwStartNode)
    Section = 0x40, // SwSectionNode (继承 SwStartNode)
    Placeholder = 0x80,
};

// 位运算符
inline SwNodeType operator|(SwNodeType a, SwNodeType b)
{
    return static_cast<SwNodeType>(static_cast<sal_uInt8>(a) | static_cast<sal_uInt8>(b));
}
inline SwNodeType operator&(SwNodeType a, SwNodeType b)
{
    return static_cast<SwNodeType>(static_cast<sal_uInt8>(a) & static_cast<sal_uInt8>(b));
}

// 位掩码常量（用枚举值直接计算）
constexpr sal_uInt8 SwNodeType_StartMaskVal = 0x01 | 0x20 | 0x40; // Start | Table | Section
constexpr sal_uInt8 SwNodeType_ContentMaskVal = 0x04 | 0x08 | 0x10; // Text | Grf | Ole
constexpr sal_uInt8 SwNodeType_NoTextMaskVal = 0x08 | 0x10; // Grf | Ole

// StartNode 子类型
enum SwStartNodeType
{
    SwNormalStartNode = 0,
    SwTableBoxStartNode,
    SwFlyStartNode,
    SwFootnoteStartNode,
    SwHeaderStartNode,
    SwFooterStartNode,
    SwNumRuleStartNode,
};

// SwNode: 文档模型中所有元素的基类
// 对应 LibreOffice 的 SwNode (简化版，去掉 SwClient/BroadcastingModify)
class SwNode : public BigPtrEntry
{
    friend class SwNodes;
    friend class SwStartNode;
    friend class SwEndNode;
    friend class SwContentNode;
    friend class SwTableNode;
    friend class SwSectionNode;

public:
    SwNode(const SwNode&) = delete;
    SwNode& operator=(const SwNode&) = delete;
    virtual ~SwNode() = 0;

    // 类型查询
    SwNodeType GetNodeType() const { return m_nNodeType; }
    bool IsStartNode() const
    {
        return (static_cast<sal_uInt8>(m_nNodeType) & SwNodeType_StartMaskVal) != 0;
    }
    bool IsContentNode() const
    {
        return (static_cast<sal_uInt8>(m_nNodeType) & SwNodeType_ContentMaskVal) != 0;
    }
    bool IsEndNode() const { return m_nNodeType == SwNodeType::End; }
    bool IsTextNode() const { return m_nNodeType == SwNodeType::Text; }
    bool IsTableNode() const { return m_nNodeType == SwNodeType::Table; }
    bool IsSectionNode() const { return m_nNodeType == SwNodeType::Section; }
    bool IsGrfNode() const { return m_nNodeType == SwNodeType::Grf; }
    bool IsOLENode() const { return m_nNodeType == SwNodeType::Ole; }

    // 安全向下转换
    SwStartNode* GetStartNode();
    const SwStartNode* GetStartNode() const;
    SwContentNode* GetContentNode();
    const SwContentNode* GetContentNode() const;
    SwEndNode* GetEndNode();
    const SwEndNode* GetEndNode() const;
    SwTextNode* GetTextNode();
    const SwTextNode* GetTextNode() const;
    SwGrfNode* GetGrfNode();
    const SwGrfNode* GetGrfNode() const;
    SwTableNode* GetTableNode();
    const SwTableNode* GetTableNode() const;
    SwSectionNode* GetSectionNode();
    const SwSectionNode* GetSectionNode() const;

    // 节区导航
    SwStartNode* StartOfSectionNode() { return m_pStartOfSection; }
    const SwStartNode* StartOfSectionNode() const { return m_pStartOfSection; }
    SwEndNode* EndOfSectionNode();
    const SwEndNode* EndOfSectionNode() const;

    // 查找包含此节点的表格/节/浮动框架
    SwTableNode* FindTableNode();
    SwSectionNode* FindSectionNode();
    SwStartNode* FindStartNodeByType(SwStartNodeType eTyp);
    const SwStartNode* FindStartNodeByType(SwStartNodeType eTyp) const;
    const SwStartNode* FindFlyStartNode() const { return FindStartNodeByType(SwFlyStartNode); }
    const SwStartNode* FindTableBoxStartNode() const
    {
        return FindStartNodeByType(SwTableBoxStartNode);
    }

    // 节点索引
    SwNodeOffset GetIndex() const { return SwNodeOffset(GetPos()); }

    // 设置节区起始节点（用于修正表格后插入节点的节区归属）
    void SetStartOfSection(SwStartNode* p) { m_pStartOfSection = p; }

    // 所属节点数组
    SwNodes& GetNodes();
    const SwNodes& GetNodes() const;

    // 所属文档
    SwDoc& GetDoc();
    const SwDoc& GetDoc() const;

    // 获取格式集合
    SwTextFormatColl* GetFormatColl() const { return m_pFormatColl; }
    void ChgFormatColl(SwTextFormatColl* pNew) { m_pFormatColl = pNew; }

    // 比较运算符
    bool operator==(const SwNode& r) const { return this == &r; }
    bool operator!=(const SwNode& r) const { return this != &r; }

protected:
    // 用于初始节点的构造
    SwNode(SwNodes& rNodes, SwNodeOffset nPos, SwNodeType nType);
    // 用于在已有节点旁创建
    SwNode(const SwNode& rWhere, SwNodeType nType);

private:
    SwNodeType m_nNodeType;
    SwStartNode* m_pStartOfSection; // 所属节区的起始节点
    SwTextFormatColl* m_pFormatColl = nullptr; // 格式集合
};

// SwStartNode: 节区开始标记
class SwStartNode : public SwNode
{
    friend class SwNodes;
    friend class SwEndNode;
    friend class SwTableNode;
    friend class SwSectionNode;

public:
    SwStartNodeType GetStartNodeType() const { return m_eStartNodeType; }
    SwEndNode* GetEndOfSection() { return m_pEndOfSection; }
    const SwEndNode* GetEndOfSection() const { return m_pEndOfSection; }
    void SetEndOfSection(SwEndNode* pEnd) { m_pEndOfSection = pEnd; }

    // Fly 节区锚点节点索引（仅对 SwFlyStartNode 有效）
    int GetAnchorNodeIndex() const { return m_nAnchorNodeIndex; }
    void SetAnchorNodeIndex(int nIndex) { m_nAnchorNodeIndex = nIndex; }

protected:
    SwStartNode(SwNodes& rNodes, SwNodeOffset nPos, SwStartNodeType eType = SwNormalStartNode);
    SwStartNode(const SwNode& rWhere, SwStartNodeType eType = SwNormalStartNode);

private:
    SwEndNode* m_pEndOfSection;
    SwStartNodeType m_eStartNodeType;
    int m_nAnchorNodeIndex = -1; // Fly 节区的锚点节点索引，-1 表示无锚点
};

// SwEndNode: 节区结束标记
class SwEndNode final : public SwNode
{
    friend class SwNodes;

public:
    SwStartNode* GetStartNode() { return StartOfSectionNode(); }
    const SwStartNode* GetStartNode() const { return StartOfSectionNode(); }

private:
    SwEndNode(SwNodes& rNodes, SwNodeOffset nPos, SwStartNode& rSttNd);
    SwEndNode(const SwNode& rWhere, SwStartNode& rSttNd);
};

// SwContentNode: 内容节点的抽象基类
class SwContentNode : public SwNode
{
    friend class SwNodes;

public:
    virtual ~SwContentNode() = 0;

    // 创建对应的 Frame（纯虚函数）
    virtual SwContentFrame* MakeFrame(SwFrame* pSib) = 0;

    // 文本长度
    virtual sal_Int32 Len() const = 0;

    // 获取格式集合
    SwTextFormatColl* GetFormatColl() const { return SwNode::GetFormatColl(); }
    void ChgFormatColl(SwTextFormatColl* pNew) { SwNode::ChgFormatColl(pNew); }

protected:
    SwContentNode(const SwNode& rWhere, SwNodeType nType, SwTextFormatColl* pFormatColl);
    SwContentNode(SwNodes& rNodes, SwNodeOffset nPos, SwNodeType nType,
                  SwTextFormatColl* pFormatColl);
};

// SwTextNode: 段落节点
class SwTextNode : public SwContentNode
{
    friend class SwNodes;
    friend class SwDoc;

public:
    virtual ~SwTextNode() override;

    // 文本内容
    const std::string& GetText() const { return m_Text; }
    void SetText(const std::string& rText) { m_Text = rText; }
    sal_Int32 Len() const override { return static_cast<sal_Int32>(m_Text.size()); }

    // 创建 Frame
    SwContentFrame* MakeFrame(SwFrame* pSib) override;

    // 属性存储（简化版：用 map 替代 SfxItemSet）
    using AttrMap = std::map<sal_uInt16, std::string>;
    void SetAttr(sal_uInt16 nWhich, const std::string& rValue);
    void ClearAttr(sal_uInt16 nWhich) { m_aAttrs.erase(nWhich); }
    const std::string* GetAttr(sal_uInt16 nWhich) const;
    const AttrMap& GetAttrs() const { return m_aAttrs; }

    // 段落样式名
    std::string GetStyleName() const { return m_sStyleName; }
    void SetStyleName(const std::string& rName) { m_sStyleName = rName; }

private:
    SwTextNode(const SwNode& rWhere, SwTextFormatColl* pFormatColl);
    SwTextNode(SwNodes& rNodes, SwNodeOffset nPos, SwTextFormatColl* pFormatColl);

    std::string m_Text; // 段落文本
    std::string m_sStyleName; // 段落样式名
    AttrMap m_aAttrs; // 段落属性
};

// SwGrfNode: 图片节点（继承 SwContentNode）
class SwGrfNode : public SwContentNode
{
    friend class SwNodes;

public:
    virtual ~SwGrfNode() override;

    // 图片路径
    const std::string& GetImagePath() const { return m_sImagePath; }
    void SetImagePath(const std::string& rPath) { m_sImagePath = rPath; }

    // 创建 Frame
    SwContentFrame* MakeFrame(SwFrame* pSib) override;

    // 图片尺寸（twips）
    sal_Int32 GetWidth() const { return m_nWidth; }
    sal_Int32 GetHeight() const { return m_nHeight; }
    void SetSize(sal_Int32 nW, sal_Int32 nH)
    {
        m_nWidth = nW;
        m_nHeight = nH;
    }

    sal_Int32 Len() const override { return 0; }

private:
    SwGrfNode(const SwNode& rWhere);
    SwGrfNode(SwNodes& rNodes, SwNodeOffset nPos);

    std::string m_sImagePath; // 图片路径
    sal_Int32 m_nWidth = 0; // 图片宽度
    sal_Int32 m_nHeight = 0; // 图片高度
};

// SwTableNode: 表格节点（继承 SwStartNode）
class SwTableNode : public SwStartNode
{
    friend class SwNodes;

public:
    virtual ~SwTableNode() override;

    // 表格数据（简化版）
    struct ParagraphInfo
    {
        std::string text;
        std::string fontName = "Calibri";
        int fontSizeHalfPt = 20; // 半磅（对应 w:sz w:val）
    };
    struct CellData
    {
        std::string text;
        std::vector<ParagraphInfo> paragraphs; // 单元格内所有段落（每段一个 TextFrame）
        sal_Int32 gridSpan = 1;
        sal_Int32 width = 0; // twips
    };
    struct RowData
    {
        std::vector<CellData> cells;
        sal_Int32 height = 0; // twips
    };
    using TableData = std::vector<RowData>;

    void SetTableData(const TableData& rData) { m_aTableData = rData; }
    const TableData& GetTableData() const { return m_aTableData; }

    // 表格网格列宽
    void SetGridCols(const std::vector<sal_Int32>& rCols) { m_aGridCols = rCols; }
    const std::vector<sal_Int32>& GetGridCols() const { return m_aGridCols; }

    // 属性
    using AttrMap = std::map<sal_uInt16, std::string>;
    void SetAttr(sal_uInt16 nWhich, const std::string& rValue);
    const std::string* GetAttr(sal_uInt16 nWhich) const;

    // 新增：创建表格 Frame（对应 LO SwTableNode::MakeFrame）
    SwTabFrame* MakeFrame(SwLayoutFrame* pLay);

    // 新增：获取 EndNode 索引（对应 LO EndOfSectionIndex）
    SwNodeOffset EndOfSectionIndex() const;

private:
    SwTableNode(const SwNode& rWhere);
    SwTableNode(SwNodes& rNodes, SwNodeOffset nPos);

    TableData m_aTableData;
    std::vector<sal_Int32> m_aGridCols;
    AttrMap m_aAttrs;
};

// SwSection: 节对象（简化版）
// 对应 LO sw/inc/section.hxx
class SwSection
{
public:
    SwSection() = default;
    virtual ~SwSection() = default;

    // === 隐藏标志（对应 LO SwSection::CalcHiddenFlag） ===
    // 计算节是否隐藏
    bool CalcHiddenFlag() const { return m_bHidden; }

    // 设置隐藏标志
    void SetHidden(bool bHidden) { m_bHidden = bHidden; }

    // === 获取格式（简化版） ===
    SwFrameFormat* GetFormat() const { return nullptr; }

private:
    bool m_bHidden = false; // 隐藏标志
};

// SwSectionNode: 节节点（继承 SwStartNode）
class SwSectionNode : public SwStartNode
{
    friend class SwNodes;

public:
    virtual ~SwSectionNode() override;

    // 新增：创建 Section Frame（对应 LO SwSectionNode::MakeFrame）
    SwSectionFrame* MakeFrame(SwLayoutFrame* pLay, bool bHidden = false);

    // 新增：获取关联的 Section 格式（简化版）
    SwSection* GetSection() const { return nullptr; }

    // 新增：获取 EndNode 索引（对应 LO EndOfSectionIndex）
    SwNodeOffset EndOfSectionIndex() const;

private:
    SwSectionNode(const SwNode& rWhere);
    SwSectionNode(SwNodes& rNodes, SwNodeOffset nPos);
};

// 内联实现
inline SwStartNode* SwNode::GetStartNode()
{
    return IsStartNode() ? static_cast<SwStartNode*>(this) : nullptr;
}
inline const SwStartNode* SwNode::GetStartNode() const
{
    return IsStartNode() ? static_cast<const SwStartNode*>(this) : nullptr;
}
inline SwEndNode* SwNode::GetEndNode()
{
    return IsEndNode() ? static_cast<SwEndNode*>(this) : nullptr;
}
inline const SwEndNode* SwNode::GetEndNode() const
{
    return IsEndNode() ? static_cast<const SwEndNode*>(this) : nullptr;
}
inline SwContentNode* SwNode::GetContentNode()
{
    return IsContentNode() ? static_cast<SwContentNode*>(this) : nullptr;
}
inline const SwContentNode* SwNode::GetContentNode() const
{
    return IsContentNode() ? static_cast<const SwContentNode*>(this) : nullptr;
}
inline SwTextNode* SwNode::GetTextNode()
{
    return IsTextNode() ? static_cast<SwTextNode*>(this) : nullptr;
}
inline const SwTextNode* SwNode::GetTextNode() const
{
    return IsTextNode() ? static_cast<const SwTextNode*>(this) : nullptr;
}
inline SwGrfNode* SwNode::GetGrfNode()
{
    return IsGrfNode() ? static_cast<SwGrfNode*>(this) : nullptr;
}
inline const SwGrfNode* SwNode::GetGrfNode() const
{
    return IsGrfNode() ? static_cast<const SwGrfNode*>(this) : nullptr;
}
inline SwTableNode* SwNode::GetTableNode()
{
    return IsTableNode() ? static_cast<SwTableNode*>(this) : nullptr;
}
inline const SwTableNode* SwNode::GetTableNode() const
{
    return IsTableNode() ? static_cast<const SwTableNode*>(this) : nullptr;
}
inline SwSectionNode* SwNode::GetSectionNode()
{
    return IsSectionNode() ? static_cast<SwSectionNode*>(this) : nullptr;
}
inline const SwSectionNode* SwNode::GetSectionNode() const
{
    return IsSectionNode() ? static_cast<const SwSectionNode*>(this) : nullptr;
}
