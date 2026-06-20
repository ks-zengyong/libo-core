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
class SwFootnoteFrame;
class SwFlyFrame;
class SwNode;
class SwContentNode;
class SwFrameFormat;
class SwSection;
class SwSectionFrame;  // 移动到前向声明部分
class SwSortedObjs;    // 新增：浮动对象管理类
class OutputDevice;
class SwTabFrame;      // 新增：表格 Frame 前向声明
class SwRowFrame;      // 新增：表格行 Frame 前向声明
class SwCellFrame;     // 新增：表格单元格 Frame 前向声明
class SwTextFootnote;  // 新增：脚注属性类前向声明
class SwFootnoteBossFrame; // 新增：脚注 Boss Frame 前向声明

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
    // 综合有效性（对应 LO isFrameAreaDefinitionValid）
    bool isFrameAreaDefinitionValid() const { return mbFrameAreaPositionValid && mbFrameAreaSizeValid && mbFramePrintAreaValid; }

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
    // SwFlowFrame 需要访问 protected 成员
    friend class SwFlowFrame;

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
    SwPageFrame* FindPageFrame() const; // const 版本
    SwLayoutFrame* FindTabFrame();
    SwLayoutFrame* FindFlyFrame();
    SwSectionFrame* FindSctFrame();  // 新增：查找 SectionFrame
    SwFootnoteFrame* FindFootnoteFrame();  // 新增：查找 FootnoteFrame
    const SwFootnoteFrame* FindFootnoteFrame() const; // const 版本

    // === 布局树查找 ===
    // 迁移自 LO frame.hxx: FindPrev/FindNext
    SwFrame* FindPrev();
    SwFrame* FindNext();
    const SwFrame* FindPrev() const;
    const SwFrame* FindNext() const;

    // Section 相关查询（新增）
    bool IsInSct() const { return mbInfSct; }
    bool IsHiddenNow() const { return false; } // 简化版：暂不实现隐藏逻辑

    // === 正文/表格/脚注/浮动框 上下文查询 ===
    // 迁移自 LO frame.hxx: IsInDocBody, IsInTab, IsInFootnote, IsInFly
    bool IsInDocBody() const { return mbInfBody; }
    bool IsInTab() const { return mbInfTab; }
    bool IsInFootnote() const { return mbInfFootnote; }
    bool IsInFly() const { return mbInfFly; }

    // 信息标志更新（新增）
    void InvalidateInfFlags() 
    {
        mbInfBody = false;
        mbInfTab = false;
        mbInfFly = false;
        mbInfFootnote = false;
        mbInfSct = false;
    }

    // === 独立前驱/后继（用于分页流动） ===
    // 迁移自 LO frame.hxx: GetIndPrev/GetIndNext
    // 获取独立前驱（跳过 Follow 链中的非独立 Frame）
    SwFrame* GetIndPrev() const;
    // 获取独立后继（跳过 Follow 链中的非独立 Frame）
    SwFrame* GetIndNext() const;

    // === 内容 Frame 查找 ===
    // 迁移自 LO frame.hxx: FindNextCnt/FindPrevCnt
    SwContentFrame* FindNextCnt();
    const SwContentFrame* FindNextCnt() const;
    SwContentFrame* FindPrevCnt();
    const SwContentFrame* FindPrevCnt() const;

    // === 列 Frame 查找 ===
    // 迁移自 LO frame.hxx: FindColFrame
    SwLayoutFrame* FindColFrame();
    const SwLayoutFrame* FindColFrame() const;

    // === 脚注 Boss Frame 查找 ===
    // 迁移自 LO frame.hxx: FindFootnoteBossFrame
    SwLayoutFrame* FindFootnoteBossFrame(bool bFootnoteOnly = false);
    const SwLayoutFrame* FindFootnoteBossFrame(bool bFootnoteOnly = false) const;

    // === 移动性检查 ===
    // 迁移自 LO frame.hxx: IsMoveable
    bool IsMoveable() const;

    // === 页面描述项 ===
    // 简化版：返回空描述
    // 迁移自 LO frame.hxx: GetPageDescItem
    struct SwFormatPageDesc { void* GetPageDesc() const { return nullptr; } };
    SwFormatPageDesc GetPageDescItem() const { return SwFormatPageDesc(); }

    // === 分隔项 ===
    // 简化版：返回默认分隔
    // 迁移自 LO frame.hxx: GetBreakItem
    struct SvxBreak { enum Type { None, ColumnBefore, ColumnAfter, ColumnBoth, PageBefore, PageAfter, PageBoth }; };
    struct SwFormatBreakItem { SvxBreak::Type GetBreak() const { return SvxBreak::None; } };
    SwFormatBreakItem GetBreakItem() const { return SwFormatBreakItem(); }

    // === 属性集 ===
    // 简化版：返回空属性集
    // 迁移自 LO frame.hxx: GetAttrSet
    struct SwAttrSet { 
        struct KeepItem { bool GetValue() const { return false; } };
        KeepItem GetKeep() const { return KeepItem(); }
    };
    SwAttrSet GetAttrSet() const { return SwAttrSet(); }

    // === Leaf 获取 ===
    // 迁移自 LO frame.hxx: GetLeaf (行 884-962)
    enum MakePageType { MAKEPAGE_NONE, MAKEPAGE_APPEND, MAKEPAGE_INSERT, MAKEPAGE_NOSECTION, MAKEPAGE_FTN };
    SwLayoutFrame* GetLeaf(MakePageType eMakePage, bool bFwd);
    const SwLayoutFrame* GetLeaf(MakePageType eMakePage, bool bFwd, const SwFrame* pAnch) const;

    // === Layout Leaf 获取 ===
    // 迁移自 LO frame.hxx: GetNextLayoutLeaf/GetPrevLayoutLeaf
    SwLayoutFrame* GetNextLayoutLeaf();
    SwLayoutFrame* GetPrevLayoutLeaf();
    const SwLayoutFrame* GetNextLayoutLeaf() const;
    const SwLayoutFrame* GetPrevLayoutLeaf() const;

    // === 页面描述检查 ===
    // 迁移自 LO flowfrm.cxx: WrongPageDesc (行 984-1054)
    bool WrongPageDesc(SwPageFrame* pNew);

    // === 页面描述验证 ===
    // 迁移自 LO pagechg.cxx: CheckPageDescs (行 1274-1514)
    // 遍历页面链，检查页面描述符/格式/奇偶页是否匹配，必要时插入/删除空页
    static void CheckPageDescs(SwPageFrame* pStart, bool bNotifyFields = false,
                               SwPageFrame** ppPrev = nullptr);

    // 树操作
    void InsertBefore(SwLayoutFrame* pParent, SwFrame* pSibling);
    void InsertBehind(SwLayoutFrame* pParent, SwFrame* pSibling);
    void RemoveFromLayout();

    // 格式化（纯虚函数）
    virtual void Format() = 0;
    virtual void MakeAll() = 0;
    virtual void Calc() { Format(); }

    // === 位置计算（对应 LO frame.hxx: MakePos） ===
    // 计算并设置 Frame 的位置
    void MakePos();

    // 尺寸调整
    virtual SwTwips Grow(SwTwips nDiff);  // 返回实际增长的量
    virtual SwTwips Shrink(SwTwips nDiff); // 返回实际收缩的量
    virtual void ChgSize(const SwRect& rNewSize);

    // 使无效
    void InvalidateSize();
    void InvalidatePrt();
    void InvalidatePos();
    void InvalidateAll();
    void InvalidateNextPos();

    // === 页面无效化（对应 LO InvalidatePage） ===
    void InvalidatePage(SwPageFrame* pPage = nullptr);
    static void InvalidatePage(const SwPageFrame* pPage);

    // 绘制（通过 OutputDevice 接口，与 VCL 的 PaintSwFrame 流程对称）
    virtual void PaintSwFrame(OutputDevice* pOutDev) { (void)pOutDev; }

    // === 无效状态管理（对应 LO SwFrame 无效标志） ===
    // 页面无效标志（简化版：使用单一标志）
    bool IsInvalid() const { return !isFrameAreaDefinitionValid(); }
    bool IsInvalidLayout() const { return !isFrameAreaSizeValid(); }
    bool IsInvalidContent() const { return !isFramePrintAreaValid(); }
    bool IsInvalidFly() const { return false; } // 简化版

    // 验证方法
    void ValidateLayout() { setFrameAreaSizeValid(true); }
    void ValidateContent() { setFramePrintAreaValid(true); }
    void ValidateFly() { } // 简化版
    void Validate() { setFrameAreaPositionValid(true); setFrameAreaSizeValid(true); setFramePrintAreaValid(true); }

    // 无效化方法
    void InvalidateLayout() { setFrameAreaSizeValid(false); }
    void InvalidateContent() { setFramePrintAreaValid(false); }

    // === Invalidate（对应 LO） ===
    void Invalidate() { InvalidateAll(); }

    // === CompletePaint 标志（对应 LO） ===
    bool IsCompletePaint() const { return mbCompletePaint; }
    void SetCompletePaint() { mbCompletePaint = true; }
    void ResetCompletePaint() { mbCompletePaint = false; }

    // === Retouche 标志（对应 LO） ===
    bool IsRetouche() const { return mbRetouche; }
    void SetRetouche() { mbRetouche = true; }
    void ResetRetouche() { mbRetouche = false; }

    // === UnionFrame（对应 LO） ===
    SwRect UnionFrame() const { return getFrameArea(); } // 简化版

    // === OptCalc（对应 LO） ===
    void OptCalc() const; // 声明为 const，在 cpp 中实现

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

    // === 浮动对象管理（ObjectFormatter 支持） ===
    // 对应 LO frame.hxx: GetDrawObjs
    // 简化版：返回 nullptr（实际实现需要 SwSortedObjs 支持）
    SwSortedObjs* GetDrawObjs() { return nullptr; }
    const SwSortedObjs* GetDrawObjs() const { return nullptr; }

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

    // 子 Frame 链表 (主链)
    SwFrame* GetLower() const { return m_pLower; }
    SwFrame* Lower() const { return m_pLower; }
    void SetLower(SwFrame* p) { m_pLower = p; }

    // 浮动对象链 (与主链独立，对应 LO 的 SwSortedObjs)
    SwFlyFrame* GetFirstFly() const { return m_pFirstFly; }
    void SetFirstFly(SwFlyFrame* p) { m_pFirstFly = p; }
    void AppendFly(SwFlyFrame* pFly);

    // 内容查找
    const SwContentFrame* ContainsContent() const;
    SwContentFrame* ContainsContent();

    // === 第一个/最后一个内容 Frame ===
    // 迁移自 LO layfrm.hxx: GetFirstContent/GetLastContent
    SwContentFrame* GetFirstContent() { return ContainsContent(); }
    const SwContentFrame* GetFirstContent() const { return ContainsContent(); }
    SwContentFrame* GetLastContent();
    const SwContentFrame* GetLastContent() const;

    // 是否是某个 Frame 的下级
    bool IsAnLower(const SwFrame* pFrame) const;

    // === 删除禁止检查（对应 LO IsDeleteForbidden） ===
    bool IsDeleteForbidden() const { return false; } // 简化版：默认允许删除

    // 格式化
    virtual void Format() override;
    virtual void MakeAll() override;

    // 绘制
    virtual void PaintSwFrame(OutputDevice* pOutDev) override;

    // 树操作
    virtual void Cut() override;
    virtual void Paste(SwLayoutFrame* pParent, SwFrame* pSibling) override;

    // === Leaf 获取 ===
    // 迁移自 LO frame.hxx: GetNextLayoutLeaf/GetPrevLayoutLeaf
    SwLayoutFrame* GetNextLayoutLeaf();
    SwLayoutFrame* GetPrevLayoutLeaf();
    const SwLayoutFrame* GetNextLayoutLeaf() const;
    const SwLayoutFrame* GetPrevLayoutLeaf() const;

protected:
    SwFrame* m_pLower = nullptr; // 第一个子 Frame (主链)
    SwFlyFrame* m_pFirstFly = nullptr; // 第一个浮动对象 (子链)
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
    SwPageFrame* mpLastPage = nullptr;
    sal_uInt16 mnPhyPageNums = 0;
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

    // === 页眉页脚准备（对应 LO hffrm.cxx: PrepareHeader/PrepareFooter 行 684-768） ===
    // 创建或移除页眉
    void PrepareHeader();

    // 创建或移除页脚
    void PrepareFooter();

    // === 奇偶页处理 ===
    // 检查是否需要奇偶页不同的页眉页脚
    bool HasOddEvenHeaderFooter() const;

    // === 首页不同处理 ===
    // 检查是否首页页眉页脚不同
    bool HasFirstPageHeaderFooter() const;

    // 获取当前页面的页眉页脚类型（奇数页/偶数页/首页）
    enum PageHeaderFooterType { Normal, First, Even };
    PageHeaderFooterType GetHeaderFooterType() const;

    // 脚注容器
    SwFootnoteContFrame* GetFootnoteCont() const { return m_pFootnoteCont; }
    SwFootnoteContFrame* MakeFootnoteCont();
    void SetFootnoteCont(SwFootnoteContFrame* p) { m_pFootnoteCont = p; }

    // === Body Frame 查找 ===
    // 查找页面内的 Body Frame（对应 LO FindBodyCont）
    SwLayoutFrame* FindBodyFrame();
    const SwLayoutFrame* FindBodyFrame() const;

    // 链表
    SwPageFrame* GetNextPage() const;
    SwPageFrame* GetPrevPage() const;

    // === SwSortedObjs 浮动对象管理（迁移自 LO） ===
    // 获取浮动对象集合（对应 LO SwPageFrame::GetSortedObjs）
    SwSortedObjs* GetSortedObjs() { return m_pSortedObjs; }
    const SwSortedObjs* GetSortedObjs() const { return m_pSortedObjs; }
    
    // 设置浮动对象集合
    void SetSortedObjs(SwSortedObjs* pObjs) { m_pSortedObjs = pObjs; }
    
    // 创建浮动对象集合
    SwSortedObjs* MakeSortedObjs();
    
    // 注册锚定浮动对象（兼容旧接口）
    void RegisterAnchoredFly(SwFlyFrame* pFly, SwFrame* pAnchor);
    
    // 获取锚定浮动对象列表（兼容旧接口，已弃用）
    const std::vector<std::pair<SwFlyFrame*, SwFrame*>>& GetAnchoredFlies() const
    {
        return m_aAnchoredFlies;
    }

private:
    sal_uInt16 m_nPhyPageNum = 0;
    SwFrameFormat* m_pDesc = nullptr;
    SwFootnoteContFrame* m_pFootnoteCont = nullptr;
    SwSortedObjs* m_pSortedObjs = nullptr;  // 新增：浮动对象管理
    std::vector<std::pair<SwFlyFrame*, SwFrame*>> m_aAnchoredFlies; // 保留兼容
};

// SwFlyFrame: 浮动框 Frame，对应 LibreOffice 的 SwFlyFrame
class SwFlyFrame : public SwLayoutFrame
{
public:
    SwFlyFrame(SwLayoutFrame* pParent);
    ~SwFlyFrame() override;

    // 浮动对象链 (由 SwLayoutFrame::AppendFly 维护)
    SwFlyFrame* GetNextFly() const { return m_pNextFly; }
    void SetNextFly(SwFlyFrame* p) { m_pNextFly = p; }

    void Format() override;

private:
    SwFlyFrame* m_pNextFly = nullptr; // 浮动链的下一个
};

// SwBodyFrame: 正文容器，对应 LibreOffice 的 SwBodyFrame
class SwBodyFrame : public SwLayoutFrame
{
public:
    SwBodyFrame(SwPageFrame* pParent);
    SwBodyFrame(SwLayoutFrame* pParent);
    ~SwBodyFrame() override;
};

// SwFlowFrame: 分页混入类，对应 LibreOffice 的 SwFlowFrame
// 注意：这不是 SwFrame 的子类，而是一个混入类
// 迁移自 LO sw/source/core/inc/flowfrm.hxx
// 必须在 SwContentFrame 之前定义，因为 SwContentFrame 继承 SwFlowFrame
class SwFlowFrame
{
public:
    SwFlowFrame(SwFrame& rThis);
    virtual ~SwFlowFrame();

    // === Frame 引用 ===
    const SwFrame& GetFrame() const { return m_rThis; }
    SwFrame& GetFrame() { return m_rThis; }

    // === Follow 链 ===
    bool HasFollow() const { return m_pFollow != nullptr; }
    bool IsFollow() const { return m_pPrecede != nullptr; }
    SwFlowFrame* GetFollow() const { return m_pFollow; }
    SwFlowFrame* GetPrecede() const { return m_pPrecede; }
    void SetFollow(SwFlowFrame* pFollow);

    // === 静态标志 MoveBwdJump ===
    // 迁移自 LO flowfrm.hxx: s_bMoveBwdJump
    static bool IsMoveBwdJump() { return s_bMoveBwdJump; }
    static void SetMoveBwdJump(bool bNew) { s_bMoveBwdJump = bNew; }

    // === Undersized 标志 ===
    void SetUndersized(bool bNew) { m_bUndersized = bNew; }
    bool IsUndersized() const { return m_bUndersized; }

    // === FlyLock 标志 ===
    void SetFlyLock(bool bNew) { m_bFlyLock = bNew; }
    bool IsFlyLock() const { return m_bFlyLock; }

    // === 锁定 ===
    bool IsJoinLocked() const { return m_bLockJoin; }
    bool IsAnyJoinLocked() const { return m_bLockJoin || HasLockedFollow(); }
    void LockJoin() { m_bLockJoin = true; }
    void UnlockJoin() { m_bLockJoin = false; }

    // === 分页检查 ===
    // 迁移自 LO flowfrm.cxx: IsPageBreak (行 1304-1351)
    bool IsPageBreak(bool bAct = false) const;
    // 迁移自 LO flowfrm.cxx: IsColBreak (行 1366-1405)
    bool IsColBreak(bool bAct = false) const;

    // === Keep 属性检查 ===
    // 迁移自 LO flowfrm.cxx: IsKeep (行 257-362)
    bool IsKeep(bool bCheckIfLastRowShouldKeep = false) const;
    // 迁移自 LO flowfrm.cxx: IsKeepFwdMoveAllowed (行 124-147)
    bool IsKeepFwdMoveAllowed(bool bIgnoreMyOwnKeepValue = false);
    // 迁移自 LO flowfrm.cxx: CheckKeep (行 149-200)
    void CheckKeep();

    // === 移动 ===
    // 迁移自 LO flowfrm.cxx: MoveFwd (行 2101-2306)
    bool MoveFwd(bool bMakePage = false, bool bPageBreak = false, bool bMoveAlways = false);
    // 迁移自 LO flowfrm.cxx: MoveBwd (行 2314-2895)
    bool MoveBwd(bool& rbReformat);
    // 迁移自 LO flowfrm.cxx: CheckMoveFwd (行 1994-2091)
    bool CheckMoveFwd(bool& rbMakePage, bool bKeep, bool bIgnoreMyOwnKeepValue = false);

    // === Follow 链检查 ===
    // 迁移自 LO flowfrm.cxx: HasLockedFollow (行 112-122)
    bool HasLockedFollow() const;
    // 迁移自 LO flowfrm.cxx: IsAnFollow (行 795-804)
    bool IsAnFollow(const SwFlowFrame* pAssumed) const;

    // === 查找（忽略隐藏） ===
    // 迁移自 LO flowfrm.cxx: FindPrevIgnoreHidden (行 364-372)
    SwFrame* FindPrevIgnoreHidden() const;
    // 迁移自 LO flowfrm.cxx: FindNextIgnoreHidden (行 374-382)
    SwFrame* FindNextIgnoreHidden() const;

    // === 子树移动 ===
    // 迁移自 LO flowfrm.cxx: MoveSubTree (行 690-793)
    void MoveSubTree(SwLayoutFrame* pParent, SwFrame* pSibling = nullptr);

    // === CastFlowFrame ===
    // 迁移自 LO flowfrm.cxx: CastFlowFrame (行 2897-2917)
    static SwFlowFrame* CastFlowFrame(SwFrame* pFrame);
    static const SwFlowFrame* CastFlowFrame(const SwFrame* pFrame);

protected:
    SwFrame& m_rThis;
    SwFlowFrame* m_pFollow;
    SwFlowFrame* m_pPrecede;
    bool m_bLockJoin : 1;      // 禁止 Join（删除）
    bool m_bUndersized : 1;    // 尺寸不足
    bool m_bFlyLock : 1;       // 锁定 Fly 定位

    // 迁移自 LO flowfrm.hxx: s_bMoveBwdJump
    static bool s_bMoveBwdJump;

    // === 内部方法 ===
    // 迁移自 LO flowfrm.hxx: IsFwdMoveAllowed
    bool IsFwdMoveAllowed() const { return m_rThis.GetIndPrev() != nullptr; }

    // 迁移自 LO flowfrm.cxx: CutTree/PasteTree
    static SwLayoutFrame* CutTree(SwFrame* pStart);
    static bool PasteTree(SwFrame* pStart, SwLayoutFrame* pParent, SwFrame* pSibling, SwFrame* pOldParent);

    // 迁移自 LO flowfrm.hxx: ShouldBwdMoved
    virtual bool ShouldBwdMoved(SwLayoutFrame* pNewUpper, bool& rReformat) = 0;

    // 迁移自 LO flowfrm.hxx: BwdMoveNecessary
    sal_uInt8 BwdMoveNecessary(const SwPageFrame* pPage, const SwRect& rRect);
};

// SwContentFrame: 内容 Frame 基类，对应 LibreOffice 的 SwContentFrame
// 迁移自 LO：SwContentFrame 继承 SwFlowFrame
class SwContentFrame : public SwFrame, public SwFlowFrame
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

    // === 内容 Frame 遍历（对应 LO） ===
    // 查找下一个内容 Frame（在当前布局链中）
    SwContentFrame* GetNextContentFrame() const;
    // 查找上一个内容 Frame
    SwContentFrame* GetPrevContentFrame() const;

    // 关联的文档节点
    SwContentNode* GetNode() const { return mpNode; }
    void SetNode(SwContentNode* pNd) { mpNode = pNd; }

    // 格式化
    virtual void Format() override;
    virtual void MakeAll() override;

    // === SwFlowFrame 虚函数实现 ===
    virtual bool ShouldBwdMoved(SwLayoutFrame* pNewUpper, bool& rReformat) override;

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

// SwTabFrame: 表格 Frame，对应 LibreOffice 的 SwTabFrame
// 迁移自 LO：SwTabFrame 继承 SwFlowFrame
class SwTabFrame : public SwLayoutFrame, public SwFlowFrame
{
public:
    SwTabFrame(SwLayoutFrame* pParent);
    ~SwTabFrame() override;

    // 格式化
    void Format() override;
    void MakeAll() override;

    // === SwFlowFrame 虚函数实现 ===
    virtual bool ShouldBwdMoved(SwLayoutFrame* pNewUpper, bool& rReformat) override;

    // === 表格拆分/合并（迁移自 LO tabfrm.cxx） ===
    // 表格拆分（跨页）
    // 对应 LO: SwTabFrame::Split (行 1103-1641)
    bool Split(SwTwips nSplitHeight, bool bTryToSplit = true);

    // 表格合并
    // 对应 LO: SwTabFrame::Join (行 1646-1703)
    void Join();

    // === 高度计算 ===
    // 计算表格高度
    SwTwips CalcHeight() const;

    // 计算第一内容行高度
    // 对应 LO: SwTabFrame::CalcHeightOfFirstContentLine
    SwTwips CalcHeightOfFirstContentLine() const;

    // === 表格属性 ===
    // 获取表格行数
    sal_uInt16 GetRowCount() const;

    // 获取列宽
    const std::vector<SwTwips>& GetColWidths() const { return m_aColWidths; }
    void SetColWidths(const std::vector<SwTwips>& aWidths) { m_aColWidths = aWidths; }

    // === Follow Flow Line（跨页行） ===
    // 对应 LO: HasFollowFlowLine, SetFollowFlowLine
    bool HasFollowFlowLine() const { return m_bHasFollowFlowLine; }
    void SetFollowFlowLine(bool b) { m_bHasFollowFlowLine = b; }

    // 获取第一个非标题行
    // 对应 LO: SwTabFrame::GetFirstNonHeadlineRow
    SwRowFrame* GetFirstNonHeadlineRow();
    const SwRowFrame* GetFirstNonHeadlineRow() const;

    // 获取最后一个行 Frame
    SwFrame* GetLastLower();

    // === 表格拆分属性 ===
    bool IsSplitable() const { return m_bSplitable; }
    void SetSplitable(bool b) { m_bSplitable = b; }

    // === 重复标题行 ===
    // 对应 LO: GetTable()->GetRowsToRepeat()
    sal_uInt16 GetRowsToRepeat() const { return m_nRowsToRepeat; }
    void SetRowsToRepeat(sal_uInt16 n) { m_nRowsToRepeat = n; }

private:
    std::vector<SwTwips> m_aColWidths;  // 列宽数组
    bool m_bSplitable = true;           // 是否允许拆分
    bool m_bHasFollowFlowLine = false;  // 是否有跨页行
    sal_uInt16 m_nRowsToRepeat = 0;     // 重复标题行数
};

// SwRowFrame: 表格行 Frame，对应 LibreOffice 的 SwRowFrame
class SwRowFrame : public SwLayoutFrame
{
public:
    SwRowFrame(SwTabFrame* pParent);
    ~SwRowFrame() override;

    // === 行格式化（迁移自 LO rowfrm.cxx） ===
    void Format() override;
    void MakeAll() override;

    // === 行高度计算 ===
    // 计算行高度
    SwTwips CalcHeight() const;

    // 设置行高度
    void SetHeight(SwTwips nHeight);

    // 获取行高度
    SwTwips GetHeight() const { return m_nHeight; }

    // === 行属性 ===
    // 是否允许行拆分
    // 对应 LO: SwRowFrame::IsRowSplitAllowed
    bool IsRowSplitAllowed() const { return m_bRowSplitAllowed; }
    void SetRowSplitAllowed(bool b) { m_bRowSplitAllowed = b; }

    // 是否是 Follow Flow Row（跨页行）
    // 对应 LO: IsFollowFlowRow, SetFollowFlowRow
    bool IsFollowFlowRow() const { return m_bFollowFlowRow; }
    void SetFollowFlowRow(bool b) { m_bFollowFlowRow = b; }

    // 是否是 Row Span Line（跨行线）
    // 对应 LO: IsRowSpanLine, SetRowSpanLine
    bool IsRowSpanLine() const { return m_bRowSpanLine; }
    void SetRowSpanLine(bool b) { m_bRowSpanLine = b; }

    // === Keep 属性 ===
    // 是否与下一行保持在一起
    // 对应 LO: ShouldRowKeepWithNext
    bool ShouldRowKeepWithNext() const { return m_bKeepWithNext; }
    void SetKeepWithNext(bool b) { m_bKeepWithNext = b; }

    // === 固定高度 ===
    bool HasFixSize() const { return m_bFixSize; }
    void SetFixSize(bool b) { m_bFixSize = b; }

    // === Follow Row（跨页后续行） ===
    // 对应 LO: GetFollowRow, SetFollowRow
    SwRowFrame* GetFollowRow() const { return m_pFollowRow; }
    void SetFollowRow(SwRowFrame* pRow) { m_pFollowRow = pRow; }

private:
    SwTwips m_nHeight = 0;              // 行高度
    bool m_bRowSplitAllowed = true;     // 是否允许行拆分
    bool m_bFollowFlowRow = false;      // 是否是 Follow Flow Row
    bool m_bRowSpanLine = false;        // 是否是 Row Span Line
    bool m_bKeepWithNext = false;       // Keep with next 属性
    bool m_bFixSize = false;            // 固定高度标志
    SwRowFrame* m_pFollowRow = nullptr; // Follow Row（跨页后续行）
};

// SwCellFrame: 表格单元格 Frame，对应 LibreOffice 的 SwCellFrame
class SwCellFrame : public SwLayoutFrame
{
public:
    SwCellFrame(SwRowFrame* pParent);
    ~SwCellFrame() override;

    // === 单元格格式化 ===
    void Format() override;

    // === 跨行跨列支持（迁移自 LO cellfrm.hxx） ===
    // 获取布局行跨度
    // 对应 LO: SwCellFrame::GetLayoutRowSpan
    SwTwips GetLayoutRowSpan() const { return m_nLayoutRowSpan; }
    void SetLayoutRowSpan(SwTwips nSpan) { m_nLayoutRowSpan = nSpan; }

    // 查找行跨度单元格的起始/结束
    // 对应 LO: SwCellFrame::FindStartEndOfRowSpanCell
    const SwCellFrame& FindStartEndOfRowSpanCell(bool bStart) const;

    // 获取 Follow Cell（跨页后续单元格）
    SwCellFrame* GetFollowCell() const { return m_pFollowCell; }
    void SetFollowCell(SwCellFrame* pCell) { m_pFollowCell = pCell; }

    // 获取 Previous Cell（前一个单元格）
    SwCellFrame* GetPreviousCell() const { return m_pPreviousCell; }
    void SetPreviousCell(SwCellFrame* pCell) { m_pPreviousCell = pCell; }

    // === 单元格属性 ===
    // 是否是覆盖单元格（被合并的单元格）
    // 对应 LO: IsCoveredCell
    bool IsCoveredCell() const { return m_nLayoutRowSpan < 0; }

    // 是否允许离开上方
    // 对应 LO: IsLeaveUpperAllowed
    bool IsLeaveUpperAllowed() const { return m_bLeaveUpperAllowed; }

private:
    SwTwips m_nLayoutRowSpan = 1;   // 布局行跨度（正数=起始，负数=覆盖）
    SwCellFrame* m_pFollowCell = nullptr;  // Follow Cell
    SwCellFrame* m_pPreviousCell = nullptr; // Previous Cell
    bool m_bLeaveUpperAllowed = false;  // 是否允许离开上方
};

// SwSectionFrame: 节 Frame，对应 LibreOffice 的 SwSectionFrame
// 迁移自 LO：SwSectionFrame 继承 SwFlowFrame
// 核心功能：节格式化、节拆分、节合并、多列布局管理
class SwSectionFrame : public SwLayoutFrame, public SwFlowFrame
{
public:
    SwSectionFrame(SwLayoutFrame* pParent);
    ~SwSectionFrame() override;
    void Format() override;
    void MakeAll() override;

    // === 初始化（对应 LO SwSectionFrame::Init 行 125-182） ===
    void Init();

    // === 获取关联的 Section（简化版） ===
    SwSection* GetSection() const { return m_pSection; }
    void SetSection(SwSection* pSect) { m_pSection = pSect; }

    // === 列锁定（对应 LO ColLock/ColUnlock） ===
    bool IsColLocked() const { return m_bColLocked; }
    void ColLock() { m_bColLocked = true; }
    void ColUnlock() { m_bColLocked = false; }

    // === 内容锁定（对应 LO ContentLock） ===
    bool IsContentLocked() const { return m_bContentLock; }
    void LockContent() { m_bContentLock = true; }
    void UnlockContent() { m_bContentLock = false; }

    // === 节拆分（对应 LO SwSectionFrame::SplitSect 行 559-616） ===
    // 将节拆分为两部分，第二部分从 pFrameStartAfter 之后开始
    // 新创建的节 Frame 放在 pFramePutAfter 之后
    // 如果 pFrameStartAfter 为 nullptr，拆分发生在开始位置
    SwSectionFrame* SplitSect(SwFrame* pFrameStartAfter, SwFrame* pFramePutAfter);

    // === 节合并（对应 LO SwSectionFrame::MergeNext 行 512-547） ===
    // 合合两个属于同一节的 SectionFrame
    void MergeNext(SwSectionFrame* pNxt);

    // === 查找最后内容（对应 LO SwSectionFrame::FindLastContent 行 1029-1064） ===
    // 查找节中的最后一个内容 Frame
    SwContentFrame* FindLastContent();
    const SwContentFrame* FindLastContent() const;

    // === 可增长检查（对应 LO SwSectionFrame::Growable 行 2309-2317） ===
    // 检查节是否还能增长
    bool Growable() const;

    // === 获取外层节（对应 LO GetOuterSection） ===
    // 查找包含此节的外层节
    SwSectionFrame* GetOuterSection();
    const SwSectionFrame* GetOuterSection() const;

    // === 隐藏检查（对应 LO SwSectionFrame::IsHiddenNow 行 222-226） ===
    // 检查节是否当前隐藏
    bool IsHidden() const;

    // === 超额检查（对应 LO SwSectionFrame::IsSuperfluous） ===
    // 检查节是否是多余的（空节）
    bool IsSuperfluous() const;

    // === 节属性 ===
    // 是否是隐藏节
    bool IsHiddenSection() const { return m_bHidden; }
    void SetHidden(bool b) { m_bHidden = b; }

    // 是否已拆分
    bool IsSplit() const { return m_bSplit; }
    void SetSplit(bool b) { m_bSplit = b; }

    // === 简单格式化（对应 LO SwSectionFrame::SimpleFormat 行 1291-1323） ===
    // 快速格式化节，不进行完整排版
    void SimpleFormat();

    // === 内容移动和删除（对应 LO SwSectionFrame::MoveContentAndDelete 行 731-835） ===
    // 移动节内容并删除节 Frame
    static void MoveContentAndDelete(SwSectionFrame* pDel, bool bSave);

    // === 查找 Master/Follow（对应 LO） ===
    SwSectionFrame* FindMaster();
    SwSectionFrame* GetFollow() const;

    // === SwFlowFrame 虚函数实现 ===
    virtual bool ShouldBwdMoved(SwLayoutFrame* pNewUpper, bool& rReformat) override;

    // === 增长/收缩（对应 LO Grow_/Shrink_） ===
    virtual SwTwips Grow_(SwTwips nDist, bool bTst);
    virtual SwTwips Shrink_(SwTwips nDist, bool bTst);

    // === 列数检查 ===
    // 检查节是否有多个列
    bool HasMultiColumns() const;

    // === 节描述检查（对应 LO ToMaximize） ===
    // 检查节是否需要最大化
    bool ToMaximize(bool bCheckFollow = true) const;

private:
    SwSection* m_pSection = nullptr;    // 关联的 Section 对象
    bool m_bColLocked = false;          // 列锁定标志
    bool m_bContentLock = false;        // 内容锁定标志
    bool m_bSplit = false;              // 是否已拆分
    bool m_bHidden = false;             // 是否隐藏

    // === 内部方法 ===
    // 检查是否需要打破另一个节（对应 LO HasToBreak）
    bool HasToBreak(const SwFrame* pFrame) const;

    // 检查剪切限制（对应 LO CheckClipping）
    void CheckClipping(bool bGrow, bool bMaximize);

    // 计算内部高度（对应 LO InnerHeight）
    SwTwips InnerHeight() const;
};

// SwColumnFrame: 分栏 Frame，对应 LibreOffice 的 SwColumnFrame
// 核心功能：列格式化、列宽计算和调整、列内内容排版
// 迁移自 LO colfrm.cxx
class SwColumnFrame : public SwLayoutFrame
{
public:
    SwColumnFrame(SwLayoutFrame* pParent);
    ~SwColumnFrame() override;
    void Format() override;
    void MakeAll() override;

    // === 列宽计算（对应 LO colfrm.cxx） ===
    // 计算列宽度
    SwTwips CalcColWidth() const;

    // 设置列宽度
    void SetColWidth(SwTwips nWidth);

    // 获取列宽度
    SwTwips GetColWidth() const { return m_nColWidth; }

    // === 列属性 ===
    // 获取列索引（在多列布局中的位置）
    sal_uInt16 GetColIndex() const { return m_nColIndex; }
    void SetColIndex(sal_uInt16 nIdx) { m_nColIndex = nIdx; }

    // === 列间距 ===
    // 获取列左间距
    SwTwips GetLeftSpacing() const { return m_nLeftSpacing; }
    void SetLeftSpacing(SwTwips nSpacing) { m_nLeftSpacing = nSpacing; }

    // 获取列右间距
    SwTwips GetRightSpacing() const { return m_nRightSpacing; }
    void SetRightSpacing(SwTwips nSpacing) { m_nRightSpacing = nSpacing; }

    // === Body Frame 查找 ===
    // 查找列内的 Body Frame（对应 LO FindBodyCont）
    SwLayoutFrame* FindBodyCont();
    const SwLayoutFrame* FindBodyCont() const;

    // === 脚注容器查找 ===
    // 查找列内的脚注容器（对应 LO FindFootnoteCont）
    SwFootnoteContFrame* FindFootnoteCont();
    const SwFootnoteContFrame* FindFootnoteCont() const;

    // === 列调整（对应 LO SwLayoutFrame::AdjustColumns） ===
    // 调整列宽以适应容器
    void AdjustColWidth(SwTwips nAvailWidth);

    // === 最大脚注高度（对应 LO） ===
    void SetMaxFootnoteHeight(SwTwips nHeight) { m_nMaxFootnoteHeight = nHeight; }
    SwTwips GetMaxFootnoteHeight() const { return m_nMaxFootnoteHeight; }

private:
    SwTwips m_nColWidth = 0;           // 列宽度
    SwTwips m_nLeftSpacing = 0;        // 列左间距
    SwTwips m_nRightSpacing = 0;       // 列右间距
    SwTwips m_nMaxFootnoteHeight = LONG_MAX; // 最大脚注高度
    sal_uInt16 m_nColIndex = 0;        // 列索引
};

// SwHeadFootFrame: 页眉页脚基类，对应 LibreOffice 的 SwHeadFootFrame
// 迁移自 LO sw/source/core/layout/hffrm.cxx
// 核心功能：页眉页脚格式化、高度计算、动态间距处理、增长/收缩
class SwHeadFootFrame : public SwLayoutFrame
{
public:
    SwHeadFootFrame(SwFrameType nType, SwLayoutFrame* pParent);
    ~SwHeadFootFrame() override;

    // === 格式化（对应 LO hffrm.cxx: Format 行 415-438） ===
    void Format() override;

    // === 打印区域格式化（对应 LO hffrm.cxx: FormatPrt 行 114-218） ===
    void FormatPrt(SwTwips& nUL);

    // === 尺寸格式化（对应 LO hffrm.cxx: FormatSize 行 220-413） ===
    void FormatSize(SwTwips nUL);

    // === 增长/收缩（对应 LO hffrm.cxx: GrowFrame/ShrinkFrame 行 440-652） ===
    virtual SwTwips GrowFrame(SwTwips nDist, bool bTst = false);
    virtual SwTwips ShrinkFrame(SwTwips nDiff, bool bTst = false);

    // === 高度计算 ===
    // 计算页眉页脚高度
    SwTwips CalcHeight() const;

    // === 动态页眉页脚 ===
    // 是否是动态页眉页脚（根据内容自动调整高度）
    bool IsDynamic() const;

    // === 固定尺寸检查 ===
    // 是否有固定尺寸（对应 LO HasFixSize）
    bool HasFixSize() const { return mbFixSize; }
    void SetFixSize(bool b) { mbFixSize = b; }

    // === 间距处理 ===
    // 是否使用间距吸收（对应 LO GetEatSpacing）
    bool GetEatSpacing() const;

    // === 获取格式 ===
    SwFrameFormat* GetHeaderFooterFormat() const;

    // === 列锁定 ===
    bool IsColLocked() const { return mbColLocked; }
    void ColLock() { mbColLocked = true; }
    void ColUnlock() { mbColLocked = false; }

protected:
    bool mbColLocked : 1;      // 列锁定标志
    bool mbFixSize : 1;        // 固定尺寸标志

    // === 辅助方法 ===
    // 计算最小高度（对应 LO lcl_GetFrameMinHeight）
    SwTwips GetMinHeight() const;

    // 计算内容高度（对应 LO lcl_CalcContentHeight）
    SwTwips CalcContentHeight() const;

    // 确保最小高度（对应 LO lcl_LayoutFrameEnsureMinHeight）
    void EnsureMinHeight();

    // 计算最大可吸收间距
    SwTwips CalcMaxEatSpacing() const;
};

// SwHeaderFrame: 页眉 Frame，对应 LibreOffice 的 SwHeaderFrame
// 继承 SwHeadFootFrame，添加页眉特定功能
class SwHeaderFrame : public SwHeadFootFrame
{
public:
    SwHeaderFrame(SwLayoutFrame* pParent);
    ~SwHeaderFrame() override;

    // 页眉格式化
    void Format() override;

    // 获取页眉格式
    SwFrameFormat* GetHeaderFormat() const;

    // 是否是动态页眉
    bool IsDynamicHeader() const;
};

// SwFooterFrame: 页脚 Frame，对应 LibreOffice 的 SwFooterFrame
// 继承 SwHeadFootFrame，添加页脚特定功能
class SwFooterFrame : public SwHeadFootFrame
{
public:
    SwFooterFrame(SwLayoutFrame* pParent);
    ~SwFooterFrame() override;

    // 页脚格式化
    void Format() override;

    // 获取页脚格式
    SwFrameFormat* GetFooterFormat() const;

    // 是否是动态页脚
    bool IsDynamicFooter() const;
};

// SwFootnoteContFrame: 脚注容器 Frame，对应 LibreOffice 的 SwFootnoteContFrame
// 迁移自 LO sw/source/core/inc/ftnfrm.hxx
// 核心功能：脚注容器格式化、高度计算、脚注管理
class SwFootnoteContFrame : public SwLayoutFrame
{
public:
    SwFootnoteContFrame(SwLayoutFrame* pParent);
    ~SwFootnoteContFrame() override;

    // === 格式化（对应 LO ftnfrm.cxx: Format 行 277-365） ===
    void Format() override;

    // === 高度计算 ===
    // 计算最大可用高度（对应 LO ftnfrm.cxx: GrowFrame）
    SwTwips CalcMaxHeight() const;

    // === 脚注管理 ===
    // 获取脚注数量
    sal_uInt16 GetFootnoteCount() const;

    // 获取第一个脚注
    SwFootnoteFrame* GetFirstFootnote();
    const SwFootnoteFrame* GetFirstFootnote() const;

    // 获取最后一个脚注
    SwFootnoteFrame* GetLastFootnote();
    const SwFootnoteFrame* GetLastFootnote() const;

    // === 查找脚注 ===
    // 查找脚注（对应 LO FindFootnote/FindEndNote）
    const SwFootnoteFrame* FindFootNote() const;
    const SwFootnoteFrame* FindEndNote() const;

    // === 链式脚注管理（对应 LO AppendChained/PrependChained） ===
    // 在链末尾添加脚注
    static SwFootnoteFrame* AppendChained(SwFrame* pThis, bool bDefaultFormat);
    // 在链开头添加脚注
    static SwFootnoteFrame* PrependChained(SwFrame* pThis, bool bDefaultFormat);

    // === 尺寸调整（对应 LO GrowFrame/ShrinkFrame） ===
    virtual SwTwips GrowFrame(SwTwips nDist, bool bTst = false);
    virtual SwTwips ShrinkFrame(SwTwips nDiff, bool bTst = false);

    // === 绘制辅助线（对应 LO PaintLine） ===
    void PaintLine(const SwRect& rRect, const SwPageFrame* pPage) const;

private:
    // 内部方法：添加链式脚注
    static SwFootnoteFrame* AddChained(bool bAppend, SwFrame* pNewUpper, bool bDefaultFormat);
};

// SwFootnoteFrame: 脚注 Frame，对应 LibreOffice 的 SwFootnoteFrame
// 迁移自 LO sw/source/core/inc/ftnfrm.hxx
// 核心功能：脚注格式化、Master/Follow 链、引用管理、锁定机制
class SwFootnoteFrame : public SwLayoutFrame
{
public:
    SwFootnoteFrame(SwLayoutFrame* pParent, SwContentFrame* pRef = nullptr, SwTextFootnote* pAttr = nullptr);
    ~SwFootnoteFrame() override;

    // === 格式化 ===
    void Format() override;

    // === 高度计算 ===
    SwTwips CalcHeight() const;

    // === 引用管理（对应 LO GetRef/SetRef） ===
    SwContentFrame* GetRef() const { return m_pReference; }
    void SetRef(SwContentFrame* pNew) { m_pReference = pNew; }

    // 从属性获取引用（对应 LO GetRefFromAttr）
    SwContentFrame* GetRefFromAttr() const;

    // === 脚注属性（对应 LO GetAttr） ===
    SwTextFootnote* GetAttr() const { return m_pAttribute; }
    void SetAttr(SwTextFootnote* pAttr) { m_pAttribute = pAttr; }

    // === Master/Follow 链（对应 LO GetMaster/GetFollow/SetMaster/SetFollow） ===
    SwFootnoteFrame* GetFollow() const { return m_pFollow; }
    SwFootnoteFrame* GetMaster() const { return m_pMaster; }
    void SetFollow(SwFootnoteFrame* pNew) { m_pFollow = pNew; }
    void SetMaster(SwFootnoteFrame* pNew) { m_pMaster = pNew; }

    // === 比较操作（对应 LO operator<） ===
    bool operator<(const SwTextFootnote* pTextFootnote) const;

    // === 锁定机制（对应 LO LockBackMove/UnlockBackMove/IsBackMoveLocked） ===
    void LockBackMove() { m_bBackMoveLocked = true; }
    void UnlockBackMove() { m_bBackMoveLocked = false; }
    bool IsBackMoveLocked() const { return m_bBackMoveLocked; }

    // === 列锁定（对应 LO ColLock/ColUnlock） ===
    void ColLock() { m_bColLocked = true; }
    void ColUnlock() { m_bColLocked = false; }
    bool IsColLocked() const { return m_bColLocked; }

    // === 删除禁止检查（对应 LO IsDeleteForbidden） ===
    bool IsDeleteForbidden() const;

    // === Cut/Paste（对应 LO Cut/Paste） ===
    virtual void Cut() override;
    virtual void Paste(SwLayoutFrame* pParent, SwFrame* pSibling = nullptr) override;

    // === 无效化下一个脚注容器（对应 LO InvalidateNxtFootnoteCnts） ===
    void InvalidateNxtFootnoteCnts(const SwPageFrame* pPage);

    // === 查找最后内容（对应 LO FindLastContent） ===
    SwContentFrame* FindLastContent();
    const SwContentFrame* FindLastContent() const;

    // === 解锁下级对象位置（对应 LO UnlockPosOfLowerObjs） ===
    void UnlockPosOfLowerObjs() { m_bUnlockPosOfLowerObjs = true; }
    void KeepLockPosOfLowerObjs() { m_bUnlockPosOfLowerObjs = false; }
    bool IsUnlockPosOfLowerObjs() const { return m_bUnlockPosOfLowerObjs; }

private:
    // Master/Follow 链
    SwFootnoteFrame* m_pFollow = nullptr;      // 后续脚注（跨页）
    SwFootnoteFrame* m_pMaster = nullptr;      // 前驱脚注（Master）
    SwContentFrame* m_pReference = nullptr;    // 引用的内容 Frame
    SwTextFootnote* m_pAttribute = nullptr;    // 脚注属性

    // 锁定标志
    bool m_bBackMoveLocked : 1;                 // 禁止向后移动
    bool m_bColLocked : 1;                      // 列锁定
    bool m_bUnlockPosOfLowerObjs : 1;           // 解锁下级对象位置
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
