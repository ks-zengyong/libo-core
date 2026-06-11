#pragma once
// 简化版 SwRect，对应 LibreOffice 的 tools/rect.hxx + sw/inc/swrect.hxx
// 只保留排版需要的核心功能

#include "types.h"
#include <algorithm>

// 对应 tools::Rectangle，简化为整数坐标矩形
class SwRect
{
public:
    SwRect() = default;
    SwRect(SwTwips x, SwTwips y, SwTwips w, SwTwips h)
        : m_nX(x)
        , m_nY(y)
        , m_nW(w)
        , m_nH(h)
    {
    }

    SwTwips Left() const { return m_nX; }
    SwTwips Top() const { return m_nY; }
    SwTwips Right() const { return m_nX + m_nW; }
    SwTwips Bottom() const { return m_nY + m_nH; }

    SwTwips Width() const { return m_nW; }
    SwTwips Height() const { return m_nH; }

    void SetLeft(SwTwips v)
    {
        m_nW += m_nX - v;
        m_nX = v;
    }
    void SetTop(SwTwips v)
    {
        m_nH += m_nY - v;
        m_nY = v;
    }
    void SetRight(SwTwips v) { m_nW = v - m_nX; }
    void SetBottom(SwTwips v) { m_nH = v - m_nY; }

    void SetWidth(SwTwips w) { m_nW = w; }
    void SetHeight(SwTwips h) { m_nH = h; }

    void SetPos(SwTwips x, SwTwips y)
    {
        m_nX = x;
        m_nY = y;
    }
    void SetSize(SwTwips w, SwTwips h)
    {
        m_nW = w;
        m_nH = h;
    }

    // 向下移动（排版中常用：将 Frame 放到下一页面/栏）
    void AddTop(SwTwips v)
    {
        m_nY += v;
        m_nH -= v;
    }

    // 面积
    SwTwips GetArea() const { return m_nW * m_nH; }

    // 是否为空
    bool IsEmpty() const { return m_nW <= 0 || m_nH <= 0; }

    // 交集
    SwRect Intersection(const SwRect& rOther) const
    {
        SwTwips l = std::max(Left(), rOther.Left());
        SwTwips t = std::max(Top(), rOther.Top());
        SwTwips r = std::min(Right(), rOther.Right());
        SwTwips b = std::min(Bottom(), rOther.Bottom());
        if (l >= r || t >= b)
            return SwRect();
        return SwRect(l, t, r - l, b - t);
    }

    // 并集（包围盒）
    SwRect Union(const SwRect& rOther) const
    {
        if (IsEmpty())
            return rOther;
        if (rOther.IsEmpty())
            return *this;
        SwTwips l = std::min(Left(), rOther.Left());
        SwTwips t = std::min(Top(), rOther.Top());
        SwTwips r = std::max(Right(), rOther.Right());
        SwTwips b = std::max(Bottom(), rOther.Bottom());
        return SwRect(l, t, r - l, b - t);
    }

    // 包含测试
    bool Contains(const SwRect& rOther) const
    {
        return Left() <= rOther.Left() && Top() <= rOther.Top() && Right() >= rOther.Right()
               && Bottom() >= rOther.Bottom();
    }

    bool Contains(SwTwips x, SwTwips y) const
    {
        return x >= Left() && x < Right() && y >= Top() && y < Bottom();
    }

    // 移动
    void Move(SwTwips dx, SwTwips dy)
    {
        m_nX += dx;
        m_nY += dy;
    }

    bool operator==(const SwRect& r) const
    {
        return m_nX == r.m_nX && m_nY == r.m_nY && m_nW == r.m_nW && m_nH == r.m_nH;
    }
    bool operator!=(const SwRect& r) const { return !(*this == r); }

private:
    SwTwips m_nX = 0;
    SwTwips m_nY = 0;
    SwTwips m_nW = 0;
    SwTwips m_nH = 0;
};
