#pragma once
// SwObjectFormatter - 浮动对象格式化器
// 迁移自 LibreOffice sw/source/core/inc/objectformatter.hxx
// 简化版：使用 SwFlyFrame* 代替 SwAnchoredObject*

#include "../core/types.h"
#include <memory>
#include <vector>

// 前向声明
class SwFrame;
class SwTextFrame;
class SwLayoutFrame;
class SwPageFrame;
class SwFlyFrame;
class SwLayAction;
class SwSortedObjs;

/**
 * SwObjectFormatter - 浮动对象格式化器抽象基类
 * 
 * 迁移自 LO sw/source/core/inc/objectformatter.hxx
 * 
 * 负责格式化锚定到给定锚点 Frame 的浮动屏幕对象，
 * 并注册到给定的页面 Frame。
 */
class SwObjectFormatter
{
private:
    // 页面 Frame，浮动对象注册于此
    const SwPageFrame& m_rPageFrame;

    // 文档兼容选项：是否在对象定位时考虑环绕样式
    // 简化版：默认 false
    const bool mbConsiderWrapOnObjPos;

    // 调用浮动对象格式化的布局动作
    SwLayAction* mpLayAction;

    // 用于收集对象锚点页码的数据结构
    // 简化版：使用简单 vector
    std::vector<SwFlyFrame*> m_aCollectedObjs;
    std::vector<sal_uInt32> m_aPageNumsOfAnchor;
    std::vector<bool> m_aAnchoredAtMaster;

    /** 辅助方法 - 执行给定布局 Frame 及其所有下级布局 Frame 的内在格式化
        对应 LO FormatLayout_(..)
    */
    void FormatLayout_(SwLayoutFrame& rLayoutFrame);

    /** 辅助方法 - 执行给定浮动对象的内在内容格式化
        对应 LO FormatObjContent(..)
    */
    void FormatObjContent(SwFlyFrame& rFly);

protected:
    SwObjectFormatter(const SwPageFrame& rPageFrame,
                      SwLayAction* pLayAction,
                      bool bCollectPgNumOfAnchors = false);

    /** 创建对应的对象格式化器（工厂方法）
        对应 LO CreateObjFormatter(..)
    */
    static std::unique_ptr<SwObjectFormatter> CreateObjFormatter(
        SwFrame& rAnchorFrame,
        const SwPageFrame& rPageFrame,
        SwLayAction* pLayAction);

    /** 获取锚点 Frame（纯虚函数）
        对应 LO GetAnchorFrame()
    */
    virtual SwFrame& GetAnchorFrame() = 0;

    /** 获取页面 Frame
        对应 LO GetPageFrame()
    */
    const SwPageFrame& GetPageFrame() const { return m_rPageFrame; }

    /** 是否在对象定位时考虑环绕
        对应 LO ConsiderWrapOnObjPos()
    */
    bool ConsiderWrapOnObjPos() const { return mbConsiderWrapOnObjPos; }

    /** 获取布局动作
        对应 LO GetLayAction()
    */
    SwLayAction* GetLayAction() { return mpLayAction; }

    /** 执行给定浮动对象及其内容的内在格式化
        对应 LO FormatObj_(..)
    */
    void FormatObj_(SwFlyFrame& rFly);

    /** 调用锚定在锚点 Frame 上的所有浮动对象的内在格式化方法
        对应 LO FormatObjsAtFrame_(..)
    */
    bool FormatObjsAtFrame_(SwTextFrame* pMasterTextFrame = nullptr);

    /** 访问收集的锚定对象
        对应 LO GetCollectedObj(..)
    */
    SwFlyFrame* GetCollectedObj(sal_uInt32 nIndex);

    /** 访问收集对象的锚点页码
        对应 LO GetPgNumOfCollected(..)
    */
    sal_uInt32 GetPgNumOfCollected(sal_uInt32 nIndex);

    /** 访问收集对象的锚点类型（是否锚定在 master）
        对应 LO IsCollectedAnchoredAtMaster(..)
    */
    bool IsCollectedAnchoredAtMaster(sal_uInt32 nIndex);

    /** 访问收集的锚定对象总数
        对应 LO CountOfCollected()
    */
    sal_uInt32 CountOfCollected();

public:
    virtual ~SwObjectFormatter();

    /** 格式化特定浮动对象的内在方法
        对应 LO DoFormatObj(..)
        
        @param rFly
        输入参数 - 需要格式化的浮动对象
        
        @param bCheckForMovedFwd
        输入参数 - 指示在成功格式化后是否需要检查锚点 Frame 是否前移
        默认值：false
    */
    virtual bool DoFormatObj(SwFlyFrame& rFly, bool bCheckForMovedFwd = false) = 0;

    /** 格式化所有浮动对象的内在方法
        对应 LO DoFormatObjs()
    */
    virtual bool DoFormatObjs() = 0;

    /** 格式化给定锚点 Frame 上的所有浮动对象
        对应 LO FormatObjsAtFrame(..) - 静态方法
    */
    static bool FormatObjsAtFrame(SwFrame& rAnchorFrame,
                                  const SwPageFrame& rPageFrame,
                                  SwLayAction* pLayAction = nullptr);

    /** 格式化给定浮动对象
        对应 LO FormatObj(..) - 静态方法
    */
    static bool FormatObj(SwFlyFrame& rFly,
                          SwFrame* pAnchorFrame = nullptr,
                          const SwPageFrame* pPageFrame = nullptr,
                          SwLayAction* pLayAction = nullptr);
};

/**
 * SwObjectFormatterTextFrame - 文本 Frame 的对象格式化器
 * 
 * 迁移自 LO sw/source/core/layout/objectformattertxtfrm.hxx
 * 
 * 格式化锚定到给定文本 Frame 并注册到给定页面 Frame 的浮动对象。
 */
class SwObjectFormatterTextFrame : public SwObjectFormatter
{
private:
    // 锚点文本 Frame
    SwTextFrame& m_rAnchorTextFrame;

    // 'master' 锚点文本 Frame（用于 follow 文本 Frame）
    SwTextFrame* mpMasterAnchorTextFrame;

    SwObjectFormatterTextFrame(SwTextFrame& rAnchorTextFrame,
                               const SwPageFrame& rPageFrame,
                               SwTextFrame* pMasterAnchorTextFrame,
                               SwLayAction* pLayAction);

    /** 使给定对象之前的锚定对象无效
        对应 LO InvalidatePrevObjs(..)
    */
    void InvalidatePrevObjs(SwFlyFrame& rFly);

    /** 使给定对象之后的锚定对象无效
        对应 LO InvalidateFollowObjs(..)
    */
    void InvalidateFollowObjs(SwFlyFrame& rFly);

    /** 确定第一个锚点前移的对象
        对应 LO GetFirstObjWithMovedFwdAnchor(..)
        
        简化版：返回 nullptr
    */
    SwFlyFrame* GetFirstObjWithMovedFwdAnchor(sal_Int16 nWrapInfluenceOnPosition,
                                               sal_uInt32& noToPageNum,
                                               bool& boInFollow);

    /** 格式化锚点 Frame 以检查前移条件
        对应 LO FormatAnchorFrameForCheckMoveFwd()
    */
    void FormatAnchorFrameForCheckMoveFwd();

    /** 确定是否至少有一个对象设置了临时环绕影响
        对应 LO AtLeastOneObjIsTmpConsiderWrapInfluence()
    */
    bool AtLeastOneObjIsTmpConsiderWrapInfluence();

protected:
    virtual SwFrame& GetAnchorFrame() override;

public:
    virtual ~SwObjectFormatterTextFrame() override;

    virtual bool DoFormatObj(SwFlyFrame& rFly, bool bCheckForMovedFwd = false) override;
    virtual bool DoFormatObjs() override;

    /** 创建 SwObjectFormatterTextFrame 实例
        对应 LO CreateObjFormatter(..)
    */
    static std::unique_ptr<SwObjectFormatterTextFrame> CreateObjFormatter(
        SwTextFrame& rAnchorTextFrame,
        const SwPageFrame& rPageFrame,
        SwLayAction* pLayAction);

    /** 格式化给定锚点文本 Frame 及其之前的 Frame
        对应 LO FormatAnchorFrameAndItsPrevs(..)
    */
    static void FormatAnchorFrameAndItsPrevs(SwTextFrame& rAnchorTextFrame);

    /** 检查锚点前移条件
        对应 LO CheckMovedFwdCondition(..)
        
        简化版：返回 false
    */
    static bool CheckMovedFwdCondition(SwFlyFrame& rFly,
                                       SwPageFrame const& rFromPageFrame,
                                       bool bAnchoredAtMasterBeforeFormatAnchor,
                                       sal_uInt32& noToPageNum,
                                       bool& boInFollow);
};

/**
 * SwObjectFormatterLayFrame - 布局 Frame 的对象格式化器
 * 
 * 迁移自 LO sw/source/core/layout/objectformatterlayfrm.hxx
 * 
 * 格式化锚定到给定布局 Frame 并注册到给定页面 Frame 的浮动对象。
 */
class SwObjectFormatterLayFrame : public SwObjectFormatter
{
private:
    // 锚点布局 Frame
    SwLayoutFrame& m_rAnchorLayFrame;

    SwObjectFormatterLayFrame(SwLayoutFrame& rAnchorLayFrame,
                              const SwPageFrame& rPageFrame,
                              SwLayAction* pLayAction);

    /** 格式化注册在页面 Frame 上的所有锚定对象
        对应 LO AdditionalFormatObjsOnPage()
    */
    bool AdditionalFormatObjsOnPage();

protected:
    virtual SwFrame& GetAnchorFrame() override;

public:
    virtual ~SwObjectFormatterLayFrame() override;

    // 对于锚定到布局 Frame 的对象，bCheckForMovedFwd 参数不相关
    virtual bool DoFormatObj(SwFlyFrame& rFly, bool bCheckForMovedFwd = false) override;
    virtual bool DoFormatObjs() override;

    /** 创建 SwObjectFormatterLayFrame 实例
        对应 LO CreateObjFormatter(..)
    */
    static std::unique_ptr<SwObjectFormatterLayFrame> CreateObjFormatter(
        SwLayoutFrame& rAnchorLayFrame,
        const SwPageFrame& rPageFrame,
        SwLayAction* pLayAction);
};