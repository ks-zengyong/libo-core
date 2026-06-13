#pragma once
// 简化版样式系统，对应 LibreOffice 的 sw/inc/format.hxx, frmfmt.hxx, fmtcol.hxx, pagedesc.hxx
// 保留核心结构，去掉 SfxItemSet/SwClient/BroadcastingModify 等重型依赖

#include "types.h"
#include "swrect.h"
#include <string>
#include <map>
#include <vector>
#include <memory>

// 前向声明
class SwDoc;
class SwFrame;
class SwContentFrame;

// 属性值类型（简化版，用 string 存储所有属性值）
using AttrValue = std::string;
using AttrMap = std::map<sal_uInt16, AttrValue>;

// SwFormat: 样式基类，对应 LibreOffice 的 SwFormat
class SwFormat
{
public:
    SwFormat(const std::string& rName, sal_uInt16 nWhichId = 0);
    virtual ~SwFormat() = default;

    // 名称
    const std::string& GetName() const { return m_sName; }
    void SetName(const std::string& rName) { m_sName = rName; }

    // Which ID
    sal_uInt16 GetWhich() const { return m_nWhichId; }

    // 属性访问
    void SetAttr(sal_uInt16 nWhich, const AttrValue& rValue);
    const AttrValue* GetAttr(sal_uInt16 nWhich) const;
    const AttrMap& GetAttrs() const { return m_aAttrs; }

    // 父样式
    SwFormat* GetDerivedFrom() const { return m_pDerivedFrom; }
    void SetDerivedFrom(SwFormat* pFmt) { m_pDerivedFrom = pFmt; }

    // 属性继承查找（沿父链查找）
    const AttrValue* ResolveAttr(sal_uInt16 nWhich) const;

    // 自动格式
    bool IsAutoFormat() const { return m_bAutoFormat; }
    void SetAutoFormat(bool b) { m_bAutoFormat = b; }

protected:
    std::string m_sName;
    sal_uInt16 m_nWhichId;
    AttrMap m_aAttrs;
    SwFormat* m_pDerivedFrom = nullptr;
    bool m_bAutoFormat = true;
};

// SwFrameFormat: 布局元素样式，对应 LibreOffice 的 SwFrameFormat
class SwFrameFormat : public SwFormat
{
public:
    SwFrameFormat(const std::string& rName = "");
    virtual ~SwFrameFormat() = default;

    // 创建/删除 Frame
    virtual void MakeFrames();
    virtual void DelFrames();

    // 格式集合工厂
    static SwFrameFormat* GetDefault();
};

// SwTextFormatColl: 段落样式，对应 LibreOffice 的 SwTextFormatColl
class SwTextFormatColl : public SwFrameFormat
{
public:
    SwTextFormatColl(const std::string& rName = "");
    virtual ~SwTextFormatColl() = default;

    // 池格式 ID
    sal_uInt16 GetPoolFormatId() const { return m_nPoolFormatId; }
    void SetPoolFormatId(sal_uInt16 nId) { m_nPoolFormatId = nId; }

private:
    sal_uInt16 m_nPoolFormatId = 0;
};

// SwPageDesc: 页面样式描述符，对应 LibreOffice 的 SwPageDesc
class SwPageDesc
{
public:
    SwPageDesc(const std::string& rName = "");
    ~SwPageDesc() = default;

    // 名称
    const std::string& GetName() const { return m_sName; }
    void SetName(const std::string& rName) { m_sName = rName; }

    // 页面尺寸（twips）
    SwTwips GetPageWidth() const { return m_nPageWidth; }
    SwTwips GetPageHeight() const { return m_nPageHeight; }
    void SetPageWidth(SwTwips w) { m_nPageWidth = w; }
    void SetPageHeight(SwTwips h) { m_nPageHeight = h; }

    // 页面边距（twips）
    SwTwips GetLeftMargin() const { return m_nLeftMargin; }
    SwTwips GetRightMargin() const { return m_nRightMargin; }
    SwTwips GetTopMargin() const { return m_nTopMargin; }
    SwTwips GetBottomMargin() const { return m_nBottomMargin; }
    void SetLeftMargin(SwTwips v) { m_nLeftMargin = v; }
    void SetRightMargin(SwTwips v) { m_nRightMargin = v; }
    void SetTopMargin(SwTwips v) { m_nTopMargin = v; }
    void SetBottomMargin(SwTwips v) { m_nBottomMargin = v; }

    // 页眉页脚边距
    SwTwips GetHeaderMargin() const { return m_nHeaderMargin; }
    SwTwips GetFooterMargin() const { return m_nFooterMargin; }
    void SetHeaderMargin(SwTwips v) { m_nHeaderMargin = v; }
    void SetFooterMargin(SwTwips v) { m_nFooterMargin = v; }

    // 横向/纵向
    bool IsLandscape() const { return m_bLandscape; }
    void SetLandscape(bool b) { m_bLandscape = b; }

    // 页眉页脚格式
    SwFrameFormat* GetHeaderFormat() const { return m_pHeaderFormat; }
    SwFrameFormat* GetFooterFormat() const { return m_pFooterFormat; }
    void SetHeaderFormat(SwFrameFormat* pFmt) { m_pHeaderFormat = pFmt; }
    void SetFooterFormat(SwFrameFormat* pFmt) { m_pFooterFormat = pFmt; }

    // 属性
    void SetAttr(sal_uInt16 nWhich, const AttrValue& rValue);
    const AttrValue* GetAttr(sal_uInt16 nWhich) const;

    // 默认页面描述符（A4）
    static SwPageDesc& GetDefault();

private:
    std::string m_sName;

    // 默认 A4 尺寸（twips）
    SwTwips m_nPageWidth = 11906; // 210mm
    SwTwips m_nPageHeight = 16838; // 297mm

    // 默认边距
    SwTwips m_nLeftMargin = 1800; // ~31.75mm
    SwTwips m_nRightMargin = 1800;
    SwTwips m_nTopMargin = 1440; // ~25.4mm
    SwTwips m_nBottomMargin = 1440;

    SwTwips m_nHeaderMargin = 720; // ~12.7mm
    SwTwips m_nFooterMargin = 720;

    bool m_bLandscape = false;

    SwFrameFormat* m_pHeaderFormat = nullptr;
    SwFrameFormat* m_pFooterFormat = nullptr;

    AttrMap m_aAttrs;
};

// 属性 Which ID 定义（简化版，只保留常用属性）
// 对应 LibreOffice 的 sw/inc/hintids.hxx
enum SwAttrWhich : sal_uInt16
{
    // 段落属性
    RES_PARATR_BEGIN = 1,
    RES_PARATR_LINESPACING = RES_PARATR_BEGIN,
    RES_PARATR_ADJUST, // 对齐
    RES_PARATR_SPLIT, // 段前分页
    RES_PARATR_ORPHANS, // 孤行控制
    RES_PARATR_WIDOWS, // 寡行控制
    RES_PARATR_TABSTOP,
    RES_PARATR_HYPHENZONE,
    RES_PARATR_DROP, // 首字下沉
    RES_PARATR_INDENT, // 段落缩进（左缩进 twips）
    RES_PARATR_END,

    // 字符属性
    RES_CHRATR_BEGIN = 100,
    RES_CHRATR_FONT = RES_CHRATR_BEGIN,
    RES_CHRATR_FONTSIZE, // 字号（半点）
    RES_CHRATR_WEIGHT, // 粗体
    RES_CHRATR_POSTURE, // 斜体
    RES_CHRATR_UNDERLINE,
    RES_CHRATR_STRIKETHROUGH,
    RES_CHRATR_COLOR, // 颜色
    RES_CHRATR_LANGUAGE, // 语言
    RES_CHRATR_END,

    // 框架属性
    RES_FRM_SIZE = 200,
    RES_LR_SPACE, // 左右边距
    RES_UL_SPACE, // 上下边距
    RES_BACKGROUND, // 背景
    RES_BORDER, // 边框
    RES_IMAGE_HEIGHT, // 图片高度 (twips)

    // 页面属性
    RES_PAGEDESC = 300,
    RES_BREAK, // 分页符

    // 表格属性
    RES_TABLE_WIDTH = 400,
    RES_TABLE_BORDER,
};
