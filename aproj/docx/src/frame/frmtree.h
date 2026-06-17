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

// InitLayout: 初始化布局（创建根 Frame 和第一个页面）
SwRootFrame* InitLayout(SwDoc& rDoc);

// MakeFramesForNode: 为单个节点创建 Frame
void MakeFramesForNode(SwNode& rNode, SwLayoutFrame* pParent, SwFrame* pSibling, int nSection = 0,
                       int nCol = 0);

// InsertNewPage: 创建新页面
SwPageFrame* InsertNewPage(SwRootFrame* pRoot, SwPageDesc* pDesc = nullptr);

// MakeFlyFrames: 为 Fly 容器中的浮动对象创建 Frame 并注册锚点
void MakeFlyFrames(SwDoc& rDoc);
