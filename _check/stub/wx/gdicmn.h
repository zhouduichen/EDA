#pragma once
#include "string.h"

struct wxPoint
{
    int x = 0, y = 0;
    wxPoint() {}
    wxPoint(int x_, int y_) : x(x_), y(y_) {}
    bool operator==(const wxPoint& o) const { return x == o.x && y == o.y; }
    bool operator!=(const wxPoint& o) const { return !(*this == o); }
};

inline wxPoint operator+(const wxPoint& a, const wxPoint& b)
{
    return wxPoint(a.x + b.x, a.y + b.y);
}

inline wxPoint operator-(const wxPoint& a, const wxPoint& b)
{
    return wxPoint(a.x - b.x, a.y - b.y);
}

struct wxRect
{
    int x = 0, y = 0, width = 0, height = 0;
    wxRect() {}
    wxRect(int x_, int y_, int w_, int h_) : x(x_), y(y_), width(w_), height(h_) {}
    wxPoint GetLeftTop() const { return wxPoint(x, y); }
};
