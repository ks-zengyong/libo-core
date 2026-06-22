#pragma once
// 字体引擎：独立模块，提供字体度量和文本测量
// 对应 LibreOffice 的 VCL 字体子系统 + sw 的 SwFntObj/SwFont
// 使用 stb_truetype 进行精确字形宽度测量

#include "../core/types.h"
#include <string>
#include <map>
#include <vector>
#include <memory>

//===----------------------------------------------------------------------===//
// FontMetric: 字体度量信息
// 对应 VCL 的 FontMetricData（ascent, descent, leading）
//===----------------------------------------------------------------------===//

struct FontMetric
{
    int ascent = 0; // 基线到最高字形顶部的距离 (twips)
    int descent = 0; // 基线到最低字形底部的距离 (twips, 正值)
    int internalLeading = 0; // em 方形内部的间距
    int externalLeading = 0; // 推荐的行间距
    int lineHeight = 0; // ascent + descent
    int height = 0; // 字体高度 (像素)
};

//===----------------------------------------------------------------------===//
// FontInstance: 字体实例
// 对应 VCL 的 LogicalFontInstance + sw 的 SwFntObj
//===----------------------------------------------------------------------===//

// 前向声明 HarfBuzz 类型
struct hb_font_t;
struct hb_face_t;
struct hb_blob_t;

class FontInstance
{
public:
    FontInstance() = default;
    ~FontInstance();

    // 加载字体文件
    bool LoadFromFile(const std::string& path, int fontIndex = 0);

    // 字体是否有效
    bool IsValid() const { return m_valid; }

    // 获取字体度量（指定像素高度）
    FontMetric GetMetric(float pixelHeight) const;

    // 测量文本宽度（返回 twips）
    // 对应 VCL 的 OutputDevice::GetTextWidth（使用 HarfBuzz 字形测量）
    SwTwips GetTextWidth(const std::string& text, int fontSizeHalfPt) const;

    // 测量单个字符宽度（返回 twips）
    SwTwips GetCharWidth(char c, int fontSizeHalfPt) const;

    // 找到在给定宽度内能容纳的最多字符数
    // 对应 VCL 的 OutputDevice::GetTextBreak（使用 HarfBuzz 字形测量）
    // 返回 -1: 全部容纳; 正数: 断点位置（UTF-8 字节偏移）
    int GetTextBreak(const std::string& text, int fontSizeHalfPt, SwTwips maxWidth) const;

    // 获取行高（ascent + descent, twips）
    // 对应 VCL 的 OutputDevice::GetTextHeight
    int GetTextHeight(int fontSizeHalfPt) const;

    // 设置逻辑字体名（用于 Windows API）
    void SetFontName(const std::string& name) { m_fontName = name; }

private:
    bool m_valid = false;
    struct stbtt_fontinfo* m_info = nullptr;
    std::vector<unsigned char> m_data;
    std::string m_fontName; // 逻辑字体名（用于 Windows API）

    // HarfBuzz 字体缓存（懒加载，与 LO LogicalFontInstance::GetHbFont 对应）
    // 使用 mutable 允许在 const 方法中懒加载
    mutable hb_font_t* m_hbFont = nullptr;
    mutable hb_face_t* m_hbFace = nullptr;
    mutable hb_blob_t* m_hbBlob = nullptr;

    // 创建/获取 HarfBuzz 字体（对应 LO LogicalFontInstance::GetHbFont）
    hb_font_t* GetHbFont() const;
    void ClearHbFont();

    // 获取 stbtt_fontinfo（懒加载）
    struct stbtt_fontinfo* GetInfo() const;
};

//===----------------------------------------------------------------------===//
// FontEngine: 字体引擎（单例）
// 对应 VCL 的 PhysicalFontCollection + ImplFontCache
// 对应 sw 的 SwFntCache + SwFntAccess
//===----------------------------------------------------------------------===//

class FontEngine
{
public:
    static FontEngine& Instance();

    // 获取字体实例（带缓存）
    // fontName: 字体名（如 "Calibri", "Poppins"）
    // 自动搜索系统字体目录
    FontInstance* GetFont(const std::string& fontName);

    // 测量文本宽度（便捷方法）
    SwTwips MeasureTextWidth(const std::string& fontName, int fontSizeHalfPt,
                             const std::string& text);

    // 测量行高（便捷方法）
    int MeasureTextHeight(const std::string& fontName, int fontSizeHalfPt);

    // fontTable altName 映射（OOXML 嵌入/链接字体）
    bool HasAltName(const std::string& fontName);
    void RegisterAltName(const std::string& fontName, const std::string& altName);

    // 找到换行位置（便捷方法）
    int FindLineBreak(const std::string& fontName, int fontSizeHalfPt, const std::string& text,
                      SwTwips maxWidth);

    // 文档兼容性设置（对应 LO DocumentSettingId）
    // MS_WORD_COMP_GRID_METRICS: <w:useFELayout/> 触发，CJK 字体高度 *127/100
    static void SetMsWordCompGridMetrics(bool bSet);
    static bool GetMsWordCompGridMetrics();
    // ADD_EXT_LEADING: DOCX 默认 true，ext leading 加入行高
    static void SetAddExtLeading(bool bSet);

private:
    FontEngine() = default;
    ~FontEngine() = default;
    FontEngine(const FontEngine&) = delete;
    FontEngine& operator=(const FontEngine&) = delete;

    // 字体名 → 文件路径映射
    std::string ResolveFontPath(const std::string& fontName);

    // 字体缓存
    std::map<std::string, std::unique_ptr<FontInstance>> m_cache;

    // 字体名 → 文件路径缓存
    std::map<std::string, std::string> m_pathCache;
    // OOXML fontTable altName → substitute font for GDI measurement
    std::map<std::string, std::string> m_altNameCache;
    bool m_pathCacheInitialized = false;

    void InitPathCache();
};
