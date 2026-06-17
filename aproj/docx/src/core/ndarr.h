#pragma once
// 简化版 SwNodes，对应 LibreOffice 的 sw/inc/ndarr.hxx
// 保留核心结构，去掉复杂依赖

#include "types.h"
#include "bparr.h"
#include "node.h" // 需要 SwStartNodeType 等定义
#include <vector>
#include <memory>
#include <functional>

// 前向声明
class SwTextFormatColl;
class SwDoc;

// SwNodeIndex: 节点索引，对应 LibreOffice 的 SwNodeIndex
// 简化版：只保留基本功能
class SwNodeIndex
{
public:
    SwNodeIndex()
        : m_pNode(nullptr)
        , m_nOffset(0)
    {
    }
    SwNodeIndex(SwNodes& rNodes, SwNodeOffset nPos = SwNodeOffset(0));
    SwNodeIndex(const SwNode& rNode);
    SwNodeIndex(const SwNodeIndex& rOther);
    ~SwNodeIndex() = default;

    SwNodeIndex& operator=(const SwNodeIndex& rOther);
    SwNodeIndex& operator=(const SwNode& rNode);
    SwNodeIndex& operator=(SwNodeOffset nOffset);

    SwNode* GetNode() const { return m_pNode; }
    SwNodeOffset GetIndex() const { return m_nOffset; }

    // 运算符
    SwNodeIndex& operator++();
    SwNodeIndex& operator--();
    SwNodeIndex operator++(int);
    SwNodeIndex operator--(int);
    SwNodeIndex& operator+=(SwNodeOffset nOffset);
    SwNodeIndex& operator-=(SwNodeOffset nOffset);

    bool operator==(const SwNodeIndex& r) const { return m_nOffset == r.m_nOffset; }
    bool operator!=(const SwNodeIndex& r) const { return m_nOffset != r.m_nOffset; }
    bool operator<(const SwNodeIndex& r) const { return m_nOffset < r.m_nOffset; }
    bool operator<=(const SwNodeIndex& r) const { return m_nOffset <= r.m_nOffset; }
    bool operator>(const SwNodeIndex& r) const { return m_nOffset > r.m_nOffset; }
    bool operator>=(const SwNodeIndex& r) const { return m_nOffset >= r.m_nOffset; }

private:
    SwNode* m_pNode;
    SwNodeOffset m_nOffset;
};

// SwNodes: 文档节点数组，对应 LibreOffice 的 SwNodes
class SwNodes final : private BigPtrArray
{
    friend class SwDoc;
    friend class SwNode;
    friend class SwStartNode;

public:
    SwNodes(SwDoc& rDoc);
    ~SwNodes();

    // 禁止拷贝
    SwNodes(const SwNodes&) = delete;
    SwNodes& operator=(const SwNodes&) = delete;

    // 元素访问
    SwNode* operator[](SwNodeOffset n) const;
    SwNodeOffset Count() const { return SwNodeOffset(BigPtrArray::Count()); }

    // 哨兵节点访问
    SwNode& GetEndOfContent() const { return *m_pEndOfContent; }
    SwNode& GetEndOfPostIts() const { return *m_pEndOfPostIts; }
    SwNode& GetEndOfInserts() const { return *m_pEndOfInserts; }
    SwNode& GetEndOfAutotext() const { return *m_pEndOfAutotext; }
    SwNode& GetEndOfRedlines() const { return *m_pEndOfRedlines; }

    // Fly Container StartNode（用于按顺序追加Fly）
    SwStartNode* GetFlyContainerStart() const { return m_pFlyContainerStart; }
    void SetFlyContainerStart(SwStartNode* pNode) { m_pFlyContainerStart = pNode; }

    // 所属文档
    SwDoc& GetDoc() { return m_rMyDoc; }
    const SwDoc& GetDoc() const { return m_rMyDoc; }

    // 节区级别
    static sal_uInt16 GetSectionLevel(const SwNode& rIndex);

    // 导航方法
    static SwContentNode* GoNext(SwNodeIndex* pIdx);
    static SwContentNode* GoPrevious(SwNodeIndex* pIdx);

    // 节区导航
    static void GoStartOfSection(SwNodeIndex* pIdx);
    static void GoEndOfSection(SwNodeIndex* pIdx);

    // 工厂方法
    SwTextNode* MakeTextNode(const SwNode& rWhere, SwTextFormatColl* pColl);
    SwTextNode* MakeBodyTextNode(SwTextFormatColl* pColl); // 在正文区末尾插入
    SwStartNode* MakeTextSection(const SwNode& rWhere, SwStartNodeType eSttNdTyp);

    // 在 rWhere 之后插入 EndNode（与 rSttNd 配对），返回 EndNode 指针
    SwEndNode* MakeEndNode(const SwNode& rWhere, SwStartNode& rSttNd);

    // 创建 SwSectionNode（SECTION_START 对应的节点类型）
    SwSectionNode* MakeSectionNode(const SwNode& rWhere);

    // 表格插入（用于在 Fly 节区内创建表格）
    SwTableNode* InsertTable(const SwNode& rNd, sal_uInt16 nBoxes,
                             SwTextFormatColl* pContentTextColl, sal_uInt16 nLines,
                             sal_uInt16 nRepeat = 0, SwTextFormatColl* pHeadlineTextColl = nullptr);

    // Fly 节区创建：在 Fly 区末尾插入 StartNode（不创建 EndNode）
    // 返回创建的 Fly StartNode 指针
    // 调用者需在内容插入后调用 CloseFlySection 创建 EndNode
    SwStartNode* InsertFlySection(SwStartNodeType eType = SwFlyStartNode,
                                  int nAnchorNodeIndex = -1);

    // Fly 节区关闭：在 Fly StartNode 的内容之后创建 EndNode
    SwEndNode* CloseFlySection(SwStartNode& rFlyStt);

    // 图片节点创建：在 rWhere 之后插入
    SwGrfNode* InsertGrfNode(const SwNode& rWhere);

    // 节点删除
    void Delete(const SwNodeIndex& rPos, SwNodeOffset nNodes = SwNodeOffset(1));

    // 遍历
    using ForEachFn = std::function<bool(SwNode*)>;
    void ForEach(SwNodeOffset nStt, SwNodeOffset nEnd, ForEachFn fn);

    // 是否是文档节点数组（而非 Undo 等）
    bool IsDocNodes() const;

    // 动态追加 Normal 节区（StartNode + EndNode 对）
    // 返回 StartNode 指针，EndNode 插入到数组末尾
    SwStartNode* AppendNormalSection();

    // 设置哨兵节点指针（用于动态节区管理）
    void SetEndOfAutotext(SwNode* pNode) { m_pEndOfAutotext = pNode; }
    void SetEndOfRedlines(SwNode* pNode) { m_pEndOfRedlines = pNode; }
    void SetEndOfContent(SwNode* pNode) { m_pEndOfContent.reset(pNode); }

private:

    // 内部插入方法
    void InsertNode(SwNode* pNode, SwNodeOffset nPos);

    // 初始化哨兵节点
    void InitNodes();

    SwDoc& m_rMyDoc;

    // 哨兵节点
    SwNode* m_pEndOfPostIts;     // node[1]
    SwNode* m_pEndOfInserts;     // node[3]
    SwNode* m_pEndOfAutotext;    // Fly 区结束 (node[5] initially)
    SwNode* m_pEndOfRedlines;    // node[7]
    std::unique_ptr<SwNode> m_pEndOfContent;  // 正文区结束 (node[9] initially)

    // Fly Container StartNode（用于按顺序追加Fly）
    SwStartNode* m_pFlyContainerStart = nullptr;
};
