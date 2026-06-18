#pragma once
// Frame 树构建入口，对应 LibreOffice 的 sw/source/core/layout/frmtool.cxx

#include "frame.h"
#include "../core/types.h"

// 前向声明
class SwDoc;
class SwNodes;
class SwNode;
class SwRootFrame;
class SwPageDesc;

// MakeFrames: 为节点范围创建 Frame 树
// 对应 LibreOffice 的 MakeFrames(SwDoc&, SwNode&, SwNode&)
void MakeFrames(SwDoc& rDoc, SwNode& rSttIdx, SwNode& rEndIdx);

// MakeFrames_LO: 使用 LO 架构的 MakeFrames 入口（简化版）
// 对应 LO: frmtool.cxx:2073-2264，调用 InsertCnt_ 创建 Frame
void MakeFrames_LO(SwDoc& rDoc, SwNode& rSttIdx, SwNode& rEndIdx);

// InitLayout: 初始化布局（创建根 Frame 和第一个页面）
SwRootFrame* InitLayout(SwDoc& rDoc);

// MakeFramesForNode: 为单个节点创建 Frame
void MakeFramesForNode(SwNode& rNode, SwLayoutFrame* pParent, SwFrame* pSibling, int nSection = 0,
                       int nCol = 0);

// InsertNewPage: 创建新页面
SwPageFrame* InsertNewPage(SwRootFrame* pRoot, SwPageDesc* pDesc = nullptr);

// MakeFlyFrames: 为 Fly 容器中的浮动对象创建 Frame 并注册锚点
void MakeFlyFrames(SwDoc& rDoc);

// InsertCnt_: 插入内容节点到布局（对应 LO frmtool.cxx:1508-2071）
// 使用 SwLayHelper 进行分页预计算，使用 SwActualSection 管理嵌套 Section
void InsertCnt_(SwLayoutFrame* pLay, SwDoc& rDoc, SwNodeOffset nIndex, SwNodeOffset nEndIndex,
                SwFrame* pPrv, bool bPages = true);

// UpdateSectionFrameArea: 更新 SectionFrame 区域（辅助函数）
void UpdateSectionFrameArea(SwSectionFrame* pSectionFrame);
