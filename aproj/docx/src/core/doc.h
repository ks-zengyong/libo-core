#pragma once
// 简化版 SwDoc，对应 LibreOffice 的 sw/inc/doc.hxx
// 文档容器，持有 SwNodes + 样式集合

#include "types.h"
#include "ndarr.h"
#include "format.h"
#include <map>
#include <string>
#include <memory>

// 前向声明
class SwRootFrame;

// SwDoc: 文档容器，对应 LibreOffice 的 SwDoc
class SwDoc
{
public:
    SwDoc();
    ~SwDoc();

    // 禁止拷贝
    SwDoc(const SwDoc&) = delete;
    SwDoc& operator=(const SwDoc&) = delete;

    // 节点数组
    SwNodes& GetNodes() { return *m_pNodes; }
    const SwNodes& GetNodes() const { return *m_pNodes; }

    // 样式管理
    // 段落样式
    SwTextFormatColl* MakeTextFormatColl(const std::string& rName);
    SwTextFormatColl* FindTextFormatColl(const std::string& rName) const;
    SwTextFormatColl* GetTextFormatColl(sal_uInt16 nPoolId) const;

    // 页面描述符
    SwPageDesc* MakePageDesc(const std::string& rName);
    SwPageDesc* FindPageDesc(const std::string& rName) const;
    SwPageDesc* GetPageDesc(sal_uInt16 nIdx) const;
    sal_uInt16 GetPageDescCount() const { return static_cast<sal_uInt16>(m_aPageDescs.size()); }

    // 默认样式
    SwTextFormatColl* GetDefaultTextFormatColl() const { return m_pDefaultTextFormatColl; }
    SwPageDesc* GetDefaultPageDesc() const { return m_pDefaultPageDesc; }

    // 内容访问
    // 获取第一个内容节点
    SwContentNode* GetContentNode(SwNodeOffset nIdx) const;

    // 文档属性
    void SetAttr(sal_uInt16 nWhich, const AttrValue& rValue);
    const AttrValue* GetAttr(sal_uInt16 nWhich) const;

    // 布局根 Frame
    SwRootFrame* GetRootFrame() const { return m_pRootFrame; }
    void SetRootFrame(SwRootFrame* p) { m_pRootFrame = p; }

private:
    // 初始化默认样式
    void InitDefaultStyles();

    std::unique_ptr<SwNodes> m_pNodes;

    // 段落样式集合
    std::map<std::string, std::unique_ptr<SwTextFormatColl>> m_aTextFormatColls;
    std::map<sal_uInt16, SwTextFormatColl*> m_aPoolFormatColls;

    // 页面描述符集合
    std::vector<std::unique_ptr<SwPageDesc>> m_aPageDescs;

    // 默认样式
    SwTextFormatColl* m_pDefaultTextFormatColl = nullptr;
    SwPageDesc* m_pDefaultPageDesc = nullptr;

    // 文档级属性
    AttrMap m_aAttrs;

    // 布局根 Frame
    SwRootFrame* m_pRootFrame = nullptr;
};
