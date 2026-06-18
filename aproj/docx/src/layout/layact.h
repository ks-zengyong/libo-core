#pragma once
// SwLayAction 排版动作编排器，迁移自 LibreOffice sw/source/core/inc/layact.hxx
// 核心逻辑与 LO 一致，简化部分依赖

#include "../core/types.h"
#include "../frame/frame.h"
#include <vector>
#include <memory>
#include <ctime>
#include <string>

// 前向声明
class SwDoc;
class SwRootFrame;
class SwPageFrame;
class SwTextFrame;
class SwTabFrame;
class SwFlyFrame;
class FontEngine;
class OutputDevice;

// 简化版：不使用 SwViewShellImp，用 FontEngine 替代
// 对应 LO 的 SwViewShellImp
class SwViewShellImp
{
public:
    SwViewShellImp(FontEngine* pFontEngine = nullptr)
        : m_pFontEngine(pFontEngine)
    {
    }
    FontEngine* GetFontEngine() const { return m_pFontEngine; }
private:
    FontEngine* m_pFontEngine = nullptr;
};

/**
 * SwLayAction: 排版动作编排器
 * 迁移自 LibreOffice sw/source/core/layout/layact.cxx
 *
 * 使用方式（与 LO 一致）：
 * 1. 创建 SwLayAction 对象
 * 2. 通过 Set 方法设置行为
 * 3. 调用 Action()
 * 4. 销毁对象
 */
class SwLayAction
{
    // === LO 成员变量 ===
    SwRootFrame* m_pRoot;               // 根 Frame
    SwViewShellImp* m_pImp;             // 视图 Shell 实现（简化版）

    // 优化：表格 Frame 指针（对应 LO m_pOptTab）
    const SwTabFrame* m_pOptTab;

    // 帧栈管理（对应 LO m_aFrameStack / m_aFrameDeleteGuards）
    std::vector<SwFrame*> m_aFrameStack;

    // 页面计数
    sal_uInt16 m_nPreInvaPage;          // 无效页面号
    sal_uInt16 m_nEndPage;              // 结束页面号（进度条）
    sal_uInt16 m_nCheckPageNum;         // CheckPageDesc 延迟检查

    // 时间控制
    std::clock_t m_nStartTicks;         // 开始时间

    // === LO 标志位 ===
    bool m_bPaint;                      // 是否绘制
    bool m_bComplete;                   // 是否完整排版
    bool m_bCalcLayout;                 // 是否重新计算布局
    bool m_bAgain;                      // 是否需要重新排版
    bool m_bNextCycle;                  // 是否下一轮循环
    bool m_bInterrupt;                  // 是否中断
    bool m_bIdle;                       // 是否空闲排版
    bool m_bReschedule;                 // 是否调用 Reschedule
    bool m_bCheckPages;                 // 是否检查页面描述
    bool m_bUpdateExpFields;            // 是否更新扩展字段
    bool m_bBrowseActionStop;           // 是否停止浏览动作
    bool m_bWaitAllowed;                // 是否允许等待光标
    bool m_bPaintExtraData;             // 是否绘制额外数据（行号等）
    bool m_bActionInProgress;           // 动作是否进行中
    bool mbFormatContentOnInterrupt;    // 中断时是否继续格式化内容

    // === LO 私有方法 ===
    // 绘制相关（简化版）
    void PaintContent(const SwContentFrame*, const SwPageFrame*, const SwRect& rOldRect, SwTwips nOldBottom);
    bool PaintWithoutFlys(const SwRect&, const SwContentFrame*, const SwPageFrame*);
    bool PaintContent_(const SwContentFrame*, const SwPageFrame*, const SwRect&);

    // 格式化相关
    bool FormatLayout(OutputDevice* pRenderContext, SwLayoutFrame*, bool bAddRect = true);
    bool FormatLayoutTab(SwTabFrame*, bool bAddRect);
    bool FormatContent(SwPageFrame* pPage);
    void FormatContent_(const SwContentFrame* pContent, const SwPageFrame* pPage);
    bool IsShortCut(SwPageFrame*&);

    // Turbo 排版
    bool TurboAction();
    bool TurboAction_(const SwContentFrame*);

    // 内部动作
    void InternalAction(OutputDevice* pRenderContext);

    // 页面检查
    static SwPageFrame* CheckFirstVisPage(SwPageFrame* pPage);

    // 浏览模式页面清理
    bool RemoveEmptyBrowserPages();

    // 帧栈管理
    void PushFormatLayout(SwFrame* pLow);
    void PopFormatLayout();

    // 中断检查
    inline void CheckIdleEnd();

public:
    // === 构造/析构 ===
    SwLayAction(SwRootFrame* pRoot, SwViewShellImp* pImp);
    SwLayAction(SwRootFrame& rRoot);  // 简化版构造函数（测试用）
    ~SwLayAction();

    // === LO 公共方法 ===
    void Action(OutputDevice* pRenderContext);  // 主入口
    void Action();                              // 简化版（测试用）
    void Reset();                               // 重置到默认值

    // === 设置方法 ===
    void SetIdle(bool bNew) { m_bIdle = bNew; }
    void SetCheckPages(bool bNew) { m_bCheckPages = bNew; }
    void SetBrowseActionStop(bool bNew) { m_bBrowseActionStop = bNew; }
    void SetNextCycle(bool bNew) { m_bNextCycle = bNew; }
    void SetPaint(bool bNew) { m_bPaint = bNew; }
    void SetComplete(bool bNew) { m_bComplete = bNew; }
    void SetCalcLayout(bool bNew) { m_bCalcLayout = bNew; }
    void SetReschedule(bool bNew) { m_bReschedule = bNew; }
    void SetWaitAllowed(bool bNew) { m_bWaitAllowed = bNew; }
    void SetStatBar(bool bNew);
    void SetAgain(bool bAgain);
    void SetUpdateExpFields() { m_bUpdateExpFields = true; }
    void SetCheckPageNum(sal_uInt16 nNew);
    void SetCheckPageNumDirect(sal_uInt16 nNew) { m_nCheckPageNum = nNew; }

    // === 查询方法 ===
    bool IsWaitAllowed() const { return m_bWaitAllowed; }
    bool IsNextCycle() const { return m_bNextCycle; }
    bool IsPaint() const { return m_bPaint; }
    bool IsIdle() const { return m_bIdle; }
    bool IsReschedule() const { return m_bReschedule; }
    bool IsPaintExtraData() const { return m_bPaintExtraData; }
    bool IsInterrupt() const { return m_bInterrupt; }
    bool IsAgain() const { return m_bAgain; }
    bool IsComplete() const { return m_bComplete; }
    bool IsExpFields() const { return m_bUpdateExpFields; }
    bool IsCalcLayout() const { return m_bCalcLayout; }
    bool IsCheckPages() const { return m_bCheckPages; }
    bool IsBrowseActionStop() const { return m_bBrowseActionStop; }
    bool IsActionInProgress() const { return m_bActionInProgress; }
    sal_uInt16 GetCheckPageNum() const { return m_nCheckPageNum; }

    // === 其他公共方法 ===
    void CheckWaitCursor();
    void FormatLayoutFly(SwFlyFrame*);
    void FormatFlyContent(SwFlyFrame*);  // 修改为非 const，支持 ObjectFormatter 调用
    
    // === ObjectFormatter 支持方法 ===
    // 对应 LO layact.hxx 中的相关方法
    void FormatObj_(SwFlyFrame& rFly);   // 格式化单个浮动对象
    bool FormatObjsAtFrame(SwFrame& rAnchorFrame, SwPageFrame& rPageFrame);  // 格式化锚定对象
};

// === 内联方法实现 ===
inline void SwLayAction::CheckIdleEnd()
{
    // 简化版：不检查实际输入，仅基于标志位
    // LO 原版会检查 Application::AnyInput()
    if (!IsInterrupt())
        m_bInterrupt = false; // 简化：不自动设置中断
}

inline void SwLayAction::SetCheckPageNum(sal_uInt16 nNew)
{
    if (nNew < m_nCheckPageNum)
        m_nCheckPageNum = nNew;
}

// === TextFormatter: 文本格式化器（保留原有功能） ===
class TextFormatter
{
public:
    TextFormatter(FontEngine* pFontEngine);
    ~TextFormatter();

    // 格式化文本 Frame
    void FormatTextFrame(SwTextFrame* pFrame);

    // 计算行高
    int CalcLineHeight(const std::string& fontName, int fontSize);

    // 计算字符串宽度
    int CalcStringWidth(const std::string& text, const std::string& fontName, int fontSize);

private:
    FontEngine* m_pFontEngine;

    // 换行算法
    struct LineBreak
    {
        int startPos;
        int endPos;
        int width;
        int height;
    };

    std::vector<LineBreak> BreakIntoLines(const std::string& text, const std::string& fontName,
                                          int fontSize, int maxWidth);
};