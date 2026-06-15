#pragma once
// 简化版 SwFrame 层级，对应 LibreOffice 的 sw/source/core/inc/frame.hxx
// 保留核心结构，去掉 SwClient/SfxBroadcaster/SwCache 等重型依赖

#include "../core/types.h"
#include "../core/swrect.h"
#include <vector>
#include <memory>

// 前向声明
class SwRootFrame;
class SwLayoutFrame;
class SwPageFrame;
class SwContentFrame;
class SwTextFrame;
class SwFlowFrame;
class SwHeaderFrame;
class SwFooterFrame;
class SwFootnoteContFrame;
class SwNode;
class SwContentNode;
class SwFrameFormat;
class OutputDevice;

// Frame 类型枚举，对应 LibreOffice 的 SwFrameType
enum class SwFrameType : sal_uInt16
{
    None = 0x0000,
    Root = 0x0001,
    Page = 0x0002,
    Column = 0x0004,
    Header = 0x0008,
    Footer = 0x0010,
    FootnoteCont = 0x0020,
    Footnote = 0x0040,
    Body = 0x0080,
    Fly = 0x0100,
    Section = 0x0200,
    Tab = 0x0400,
    Row = 0x0800,
    Cell = 0x1000,
    Txt = 0x2000,
    NoTxt = 0x4000,
    Unknown = 0x8000,
};

// 位运算符
inline SwFrameType operator|(SwFrameType a, SwFrameType b)
{
    return static_cast<SwFrameType>(static_cast<sal_uInt16>(a) | static_cast<sal_uInt16>(b));
}
inline SwFrameType operator&(SwFrameType a, SwFrameType b)
{
    return static_cast<SwFrameType>(static_cast<sal_uInt16>(a) & static_cast<sal_uInt16>(b));
}

// SwFrameAreaDefinition: Frame 几何定义，对应 LibreOffice 的 SwFrameAreaDefinition
class SwFrameAreaDefinition
{
public:
    SwFrameAreaDefinition() = default;
    virtual ~SwFrameAreaDefinition() = default;

    // Frame 区域（绝对位置和大小）
    const SwRect& getFrameArea() const { return m_aFrameArea; }
    void setFrameArea(const SwRect& rRect) { m_aFrameArea = rRect; }

    // 打印区域（相对于 Frame 区域）
    const SwRect& getFramePrintArea() const { return m_aFramePrintArea; }
    void setFramePrintArea(const SwRect& rRect) { m_aFramePrintArea = rRect; }

    // 有效性标志
    bool isFrameAreaPositionValid() const { return mbFrameAreaPositionValid; }
    bool isFrameAreaSizeValid() const { return mbFrameAreaSizeValid; }
    bool isFramePrintAreaValid() const { return mbFramePrintAreaValid; }

    void setFrameAreaPositionValid(bool b) { mbFrameAreaPositionValid = b; }
    void setFrameAreaSizeValid(bool b) { mbFrameAreaSizeValid = b; }
    void setFramePrintAreaValid(bool b) { mbFramePrintAreaValid = b; }

    // Frame ID
    sal_uInt32 getFrameId() const { return mnFrameId; }

protected:
    SwRect m_aFrameArea; // 绝对位置和大小
    SwRect m_aFramePrintArea; // 内容区域（相对于 Frame）
    sal_uInt32 mnFrameId = 0;
    bool mbFrameAreaPositionValid = false;
    bool mbFrameAreaSizeValid = false;
    bool mbFramePrintAreaValid = false;

    static sal_uInt32 snLastFrameId;
};

// SwFrame: 所有布局元素的基类，对应 LibreOffice 的 SwFrame
class SwFrame : public SwFrameAreaDefinition
{
public:
    SwFrame(SwFrameType nType, SwLayoutFrame* pParent = nullptr);
    virtual ~SwFrame();

    // 禁止拷贝
    SwFrame(const SwFrame&) = delete;
    SwFrame& operator=(const SwFrame&) = delete;

    // 类型查询
    SwFrameType GetType() const { return mnFrameType; }
    bool IsLayoutFrame() const;
    bool IsRootFrame() const { return mnFrameType == SwFrameType::Root; }
    bool IsPageFrame() const { return mnFrameType == SwFrameType::Page; }
    bool IsBodyFrame() const { return mnFrameType == SwFrameType::Body; }
    bool IsContentFrame() const
    {
        return mnFrameType == SwFrameType::Txt || mnFrameType == SwFrameType::NoTxt;
    }
    bool IsTextFrame() const { return mnFrameType == SwFrameType::Txt; }
    bool IsTabFrame() const { return mnFrameType == SwFrameType::Tab; }
    bool IsRowFrame() const { return mnFrameType == SwFrameType::Row; }
    bool IsCellFrame() const { return mnFrameType == SwFrameType::Cell; }
    bool IsHeaderFrame() const { return mnFrameType == SwFrameType::Header; }
    bool IsFooterFrame() const { return mnFrameType == SwFrameType::Footer; }
    bool IsColumnFrame() const { return mnFrameType == SwFrameType::Column; }
    bool IsFootnoteContFrame() const { return mnFrameType == SwFrameType::FootnoteCont; }
    bool IsFootnoteFrame() const { return mnFrameType == SwFrameType::Footnote; }
    bool IsFlyFrame() const { return mnFrameType == SwFrameType::Fly; }
    bool IsNoTextFrame() const { return mnFrameType == SwFrameType::NoTxt; }
    bool IsSctFrame() const { return mnFrameType == SwFrameType::Section; }

    // 树导航
    SwFrame* GetNext() const { return mpNext; }
    SwFrame* GetPrev() const { return mpPrev; }
    SwLayoutFrame* GetUpper() const { return mpUpper; }
    SwRootFrame* getRootFrame();

    // 查找
    SwPageFrame* FindPageFrame();
    SwLayoutFrame* FindTabFrame();
    SwLayoutFrame* FindFlyFrame();

    // 树操作
    void InsertBefore(SwLayoutFrame* pParent, SwFrame* pSibling);
    void InsertBehind(SwLayoutFrame* pParent, SwFrame* pSibling);
    void RemoveFromLayout();

    // 格式化（纯虚函数）
    virtual void Format() = 0;
    virtual void MakeAll() = 0;
    virtual void Calc() { Format(); }

    // 尺寸调整
    virtual void Grow(SwTwips nDiff);
    virtual void Shrink(SwTwips nDiff);
    virtual void ChgSize(const SwRect& rNewSize);

    // 使无效
    void InvalidateSize();
    void InvalidatePrt();
    void InvalidatePos();
    void InvalidateAll();
    void InvalidateNextPos();

    // 绘制（通过 OutputDevice 接口，与 VCL 的 PaintSwFrame 流程对称）
    virtual void PaintSwFrame(OutputDevice* pOutDev) { (void)pOutDev; }

    // 获取关联的文档节点
    SwContentNode* GetNode() const;

    // 获取格式
    SwFrameFormat* GetFormat() const { return mpFormat; }
    void SetFormat(SwFrameFormat* pFmt) { mpFormat = pFmt; }

    // 方向支持（简化版）
    bool IsVertical() const { return mbVertical; }
    bool IsRightToLeft() const { return mbRightToLeft; }
    void SetVertical(bool b) { mbVertical = b; }
    void SetRightToLeft(bool b) { mbRightToLeft = b; }

protected:
    SwFrameType mnFrameType;
    SwRootFrame* mpRoot;
    SwLayoutFrame* mpUpper;
    SwFrame* mpNext;
    SwFrame* mpPrev;
    SwFrameFormat* mpFormat;

    // 标志位
    bool mbVertical : 1;
    bool mbRightToLeft : 1;
    bool mbFixSize : 1;
    bool mbCompletePaint : 1;
    bool mbRetouche : 1;

    // 信息标志
    bool mbInfBody : 1;
    bool mbInfTab : 1;
    bool mbInfFly : 1;
    bool mbInfFootnote : 1;
    bool mbInfSct : 1;

    // 内部方法
    virtual void Cut();
    virtual void Paste(SwLayoutFrame* pParent, SwFrame* pSibling);
};

// SwLayoutFrame: 容器 Frame，对应 LibreOffice 的 SwLayoutFrame
class SwLayoutFrame : public SwFrame
{
public:
    SwLayoutFrame(SwFrameType nType, SwLayoutFrame* pParent = nullptr);
    virtual ~SwLayoutFrame();

    // 子 Frame 链表
    SwFrame* GetLower() const { return m_pLower; }
    SwFrame* Lower() const { return m_pLower; }
    void SetLower(SwFrame* p) { m_pLower = p; }

    // 内容查找
    const SwContentFrame* ContainsContent() const;
    SwContentFrame* ContainsContent();

    // 是否是某个 Frame 的下级
    bool IsAnLower(const SwFrame* pFrame) const;

    // 格式化
    virtual void Format() override;
    virtual void MakeAll() override;

    // 绘制
    virtual void PaintSwFrame(OutputDevice* pOutDev) override;

    // 树操作
    virtual void Cut() override;
    virtual void Paste(SwLayoutFrame* pParent, SwFrame* pSibling) override;

protected:
    SwFrame* m_pLower; // 第一个子 Frame
};

// SwRootFrame: 根 Frame，对应 LibreOffice 的 SwRootFrame
class SwRootFrame final : public SwLayoutFrame
{
public:
    SwRootFrame();
    ~SwRootFrame() override;

    // 页面管理
    SwPageFrame* GetLastPage() const { return mpLastPage; }
    void SetLastPage(SwPageFrame* pPage) { mpLastPage = pPage; }
    sal_uInt16 GetPageNum() const { return mnPhyPageNums; }
    void SetPageNum(sal_uInt16 n) { mnPhyPageNums = n; }

    // 格式化
    void FormatAll();

private:
    SwPageFrame* mpLastPage;
    sal_uInt16 mnPhyPageNums;
};

// SwPageFrame: 页面 Frame，对应 LibreOffice 的 SwPageFrame
class SwPageFrame : public SwLayoutFrame
{
public:
    SwPageFrame(SwRootFrame* pRoot);
    ~SwPageFrame() override;

    // 页面号
    sal_uInt16 GetPhyPageNum() const { return m_nPhyPageNum; }
    void SetPhyPageNum(sal_uInt16 n) { m_nPhyPageNum = n; }

    // 页面描述符
    SwFrameFormat* GetPageDesc() const { return m_pDesc; }
    void SetPageDesc(SwFrameFormat* pDesc) { m_pDesc = pDesc; }

    // 创建页面结构
    void PreparePage();

    // 页眉/页脚
    SwHeaderFrame* FindHeaderFrame() const;
    SwFooterFrame* FindFooterFrame() const;

    // 脚注容器
    SwFootnoteContFrame* GetFootnoteCont() const { return m_pFootnoteCont; }
    SwFootnoteContFrame* MakeFootnoteCont();
    void SetFootnoteCont(SwFootnoteContFrame* p) { m_pFootnoteCont = p; }

    // 链表
    SwPageFrame* GetNextPage() const;
    SwPageFrame* GetPrevPage() const;

private:
    sal_uInt16 m_nPhyPageNum;
    SwFrameFormat* m_pDesc;
    SwFootnoteContFrame* m_pFootnoteCont = nullptr;
};

// SwBodyFrame: 正文容器，对应 LibreOffice 的 SwBodyFrame
class SwBodyFrame : public SwLayoutFrame
{
public:
    SwBodyFrame(SwPageFrame* pParent);
    SwBodyFrame(SwLayoutFrame* pParent);
    ~SwBodyFrame() override;
};

// SwContentFrame: 内容 Frame 基类，对应 LibreOffice 的 SwContentFrame
class SwContentFrame : public SwFrame
{
public:
    SwContentFrame(SwFrameType nType, SwLayoutFrame* pParent);
    virtual ~SwContentFrame();

    // Follow 链（分页用）
    SwContentFrame* GetFollow() const { return mpFollow; }
    void SetFollow(SwContentFrame* pFollow) { mpFollow = pFollow; }
    bool HasFollow() const { return mpFollow != nullptr; }
    bool IsFollow() const { return mpMaster != nullptr; }
    SwContentFrame* GetMaster() const { return mpMaster; }

    // 关联的文档节点
    SwContentNode* GetNode() const { return mpNode; }
    void SetNode(SwContentNode* pNd) { mpNode = pNd; }

    // 格式化
    virtual void Format() override;
    virtual void MakeAll() override;

protected:
    SwContentNode* mpNode;
    SwContentFrame* mpFollow;
    SwContentFrame* mpMaster;
};

// SwTextFrame: 段落 Frame，对应 LibreOffice 的 SwTextFrame
class SwTextFrame : public SwContentFrame
{
public:
    SwTextFrame(SwContentNode* pNode, SwLayoutFrame* pParent);
    ~SwTextFrame() override;

    // 行数
    sal_Int32 GetLines() const { return mnThisLines; }
    void SetLines(sal_Int32 n) { mnThisLines = n; }

    // 偏移量（用于 Follow Frame）
    sal_Int32 GetOffset() const { return mnOffset; }
    void SetOffset(sal_Int32 n) { mnOffset = n; }

    // 格式化
    void Format() override;
    void MakeAll() override;

    // 绘制
    void PaintSwFrame(OutputDevice* pOutDev) override;

    // Follow 链
    SwTextFrame* GetFollow() const
    {
        return static_cast<SwTextFrame*>(SwContentFrame::GetFollow());
    }

private:
    sal_Int32 mnThisLines;
    sal_Int32 mnOffset;
};

// SwFlowFrame: 分页混入类，对应 LibreOffice 的 SwFlowFrame
// 注意：这不是 SwFrame 的子类，而是一个混入类
class SwFlowFrame
{
public:
    SwFlowFrame(SwFrame& rThis);
    virtual ~SwFlowFrame() = default;

    // Follow 链
    bool HasFollow() const { return m_pFollow != nullptr; }
    bool IsFollow() const { return m_pPrecede != nullptr; }
    SwFlowFrame* GetFollow() const { return m_pFollow; }
    SwFlowFrame* GetPrecede() const { return m_pPrecede; }
    void SetFollow(SwFlowFrame* pFollow);

    // 分页检查
    bool IsPageBreak(bool bAct = false) const;
    bool IsColBreak(bool bAct = false) const;
    bool IsKeep() const;

    // 移动
    bool MoveFwd(bool bMakePage = false, bool bPageBreak = false);
    bool MoveBwd(bool bReformat = false);

    // 锁定
    bool IsJoinLocked() const { return m_bLockJoin; }
    void LockJoin() { m_bLockJoin = true; }
    void UnlockJoin() { m_bLockJoin = false; }

protected:
    SwFrame& m_rThis;
    SwFlowFrame* m_pFollow;
    SwFlowFrame* m_pPrecede;
    bool m_bLockJoin : 1;
    bool m_bUndersized : 1;
    bool m_bFlyLock : 1;

    static bool s_bMoveBwdJump;
};

// SwTabFrame: 表格 Frame，对应 LibreOffice 的 SwTabFrame
class SwTabFrame : public SwLayoutFrame
{
public:
    SwTabFrame(SwLayoutFrame* pParent);
    ~SwTabFrame() override;

    // 格式化
    void Format() override;
    void MakeAll() override;
};

// SwRowFrame: 表格行 Frame，对应 LibreOffice 的 SwRowFrame
class SwRowFrame : public SwLayoutFrame
{
public:
    SwRowFrame(SwTabFrame* pParent);
    ~SwRowFrame() override;
};

// SwCellFrame: 表格单元格 Frame，对应 LibreOffice 的 SwCellFrame
class SwCellFrame : public SwLayoutFrame
{
public:
    SwCellFrame(SwRowFrame* pParent);
    ~SwCellFrame() override;
};

// SwSectionFrame: 节 Frame，对应 LibreOffice 的 SwSectionFrame
class SwSectionFrame : public SwLayoutFrame
{
public:
    SwSectionFrame(SwLayoutFrame* pParent);
    ~SwSectionFrame() override;
    void Format() override;
};

// SwColumnFrame: 分栏 Frame，对应 LibreOffice 的 SwColumnFrame
class SwColumnFrame : public SwLayoutFrame
{
public:
    SwColumnFrame(SwLayoutFrame* pParent);
    ~SwColumnFrame() override;
    void Format() override;
    void MakeAll() override;
};

// SwHeaderFrame: 页眉 Frame，对应 LibreOffice 的 SwHeaderFrame
class SwHeaderFrame : public SwLayoutFrame
{
public:
    SwHeaderFrame(SwLayoutFrame* pParent);
    ~SwHeaderFrame() override;
    void Format() override;
};

// SwFooterFrame: 页脚 Frame，对应 LibreOffice 的 SwFooterFrame
class SwFooterFrame : public SwLayoutFrame
{
public:
    SwFooterFrame(SwLayoutFrame* pParent);
    ~SwFooterFrame() override;
    void Format() override;
};

// SwFootnoteContFrame: 脚注容器 Frame，对应 LibreOffice 的 SwFootnoteContFrame
class SwFootnoteContFrame : public SwLayoutFrame
{
public:
    SwFootnoteContFrame(SwLayoutFrame* pParent);
    ~SwFootnoteContFrame() override;
    void Format() override;
};

// SwFootnoteFrame: 脚注 Frame，对应 LibreOffice 的 SwFootnoteFrame
class SwFootnoteFrame : public SwLayoutFrame
{
public:
    SwFootnoteFrame(SwLayoutFrame* pParent);
    ~SwFootnoteFrame() override;
    void Format() override;
};

// SwFlyFrame: 浮动框 Frame，对应 LibreOffice 的 SwFlyFrame
class SwFlyFrame : public SwLayoutFrame
{
public:
    SwFlyFrame(SwLayoutFrame* pParent);
    ~SwFlyFrame() override;
    void Format() override;
};

// SwNoTextFrame: 非文本内容 Frame (图片/OLE)，对应 LibreOffice 的 SwNoTextFrame
class SwNoTextFrame : public SwContentFrame
{
public:
    SwNoTextFrame(SwContentNode* pNode, SwLayoutFrame* pParent);
    ~SwNoTextFrame() override;
};

// 内联实现
inline bool SwFrame::IsLayoutFrame() const
{
    return mnFrameType != SwFrameType::Txt && mnFrameType != SwFrameType::NoTxt;
}
