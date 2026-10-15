///////////////////////////////////////////////////////////////////////////
// wirenet.cpp -- orthogonal wire geometry + electrical net extraction.
///////////////////////////////////////////////////////////////////////////

#include "wirenet.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace
{
    // Directions leaving a grid point.  Three or more of them mean the point
    // is a real junction (T or cross) and deserves a dot.
    enum DirBit { D_RIGHT = 1, D_LEFT = 2, D_DOWN = 4, D_UP = 8 };

    int DirMask(const wxPoint& from, const wxPoint& to)
    {
        if (to.x > from.x) return D_RIGHT;
        if (to.x < from.x) return D_LEFT;
        if (to.y > from.y) return D_DOWN;
        if (to.y < from.y) return D_UP;
        return 0;
    }

    int HorizontalDirs() { return D_LEFT | D_RIGHT; }
    int VerticalDirs()   { return D_UP | D_DOWN; }

    int PopCount(int m)
    {
        int n = 0;
        while (m) { n += (m & 1); m >>= 1; }
        return n;
    }

    struct PointCmp
    {
        bool operator()(const wxPoint& a, const wxPoint& b) const
        {
            if (a.x != b.x) return a.x < b.x;
            return a.y < b.y;
        }
    };

    struct DSU
    {
        std::vector<int> p;
        void Init(int n)
        {
            p.resize(n);
            for (int i = 0; i < n; ++i) p[i] = i;
        }
        int Find(int x)
        {
            while (p[x] != x) { p[x] = p[p[x]]; x = p[x]; }
            return x;
        }
        void Unite(int a, int b)
        {
            a = Find(a); b = Find(b);
            if (a != b) p[b] = a;
        }
    };

    // Does the point sit strictly inside the segment (endpoints excluded)?
    bool StrictlyInside(const wxPoint& p, const wxPoint& a, const wxPoint& b)
    {
        return p != a && p != b && wnPointOnSeg(p, a, b);
    }

    bool RangesOverlap(int a1, int a2, int b1, int b2)
    {
        int lo1 = std::min(a1, a2), hi1 = std::max(a1, a2);
        int lo2 = std::min(b1, b2), hi2 = std::max(b1, b2);
        return std::max(lo1, lo2) <= std::min(hi1, hi2);
    }

    // Do the two segments touch / cross, and where?
    bool SegmentsTouch(const wxPoint& a1, const wxPoint& a2,
                       const wxPoint& b1, const wxPoint& b2,
                       std::vector<wxPoint>& contacts)
    {
        const bool aHoriz = (a1.y == a2.y);
        const bool bHoriz = (b1.y == b2.y);
        const bool aVert  = (a1.x == a2.x);
        const bool bVert  = (b1.x == b2.x);

        if (aHoriz && bHoriz)
        {
            if (a1.y != b1.y) return false;
            if (!RangesOverlap(a1.x, a2.x, b1.x, b2.x)) return false;
        }
        else if (aVert && bVert)
        {
            if (a1.x != b1.x) return false;
            if (!RangesOverlap(a1.y, a2.y, b1.y, b2.y)) return false;
        }
        else if (aHoriz && bVert)
        {
            const wxPoint c(b1.x, a1.y);
            if (!wnPointOnSeg(c, a1, a2) || !wnPointOnSeg(c, b1, b2)) return false;
            contacts.push_back(c);
            return true;
        }
        else if (aVert && bHoriz)
        {
            const wxPoint c(a1.x, b1.y);
            if (!wnPointOnSeg(c, a1, a2) || !wnPointOnSeg(c, b1, b2)) return false;
            contacts.push_back(c);
            return true;
        }
        else
        {
            return false;   // degenerate input
        }

        // Parallel and overlapping: every endpoint that lies on the other
        // segment is a contact point.
        if (wnPointOnSeg(a1, b1, b2)) contacts.push_back(a1);
        if (wnPointOnSeg(a2, b1, b2)) contacts.push_back(a2);
        if (wnPointOnSeg(b1, a1, a2)) contacts.push_back(b1);
        if (wnPointOnSeg(b2, a1, a2)) contacts.push_back(b2);
        return true;
    }
}

// ---------------------------------------------------------------------------
// Basic geometry
// ---------------------------------------------------------------------------

bool wnAligned(const wxPoint& a, const wxPoint& b)
{
    return a.x == b.x || a.y == b.y;
}

bool wnPointOnSeg(const wxPoint& p, const wxPoint& a, const wxPoint& b)
{
    if (a.x == b.x)
        return p.x == a.x && p.y >= std::min(a.y, b.y) && p.y <= std::max(a.y, b.y);
    if (a.y == b.y)
        return p.y == a.y && p.x >= std::min(a.x, b.x) && p.x <= std::max(a.x, b.x);
    return false;
}

bool wnOnWires(const std::vector<std::vector<wxPoint>>& polys, const wxPoint& p)
{
    for (size_t i = 0; i < polys.size(); ++i)
    {
        const std::vector<wxPoint>& poly = polys[i];
        for (size_t j = 0; j + 1 < poly.size(); ++j)
            if (wnPointOnSeg(p, poly[j], poly[j + 1])) return true;
    }
    return false;
}

void wnNormalize(std::vector<wxPoint>& pts)
{
    bool changed = true;
    while (changed)
    {
        changed = false;

        // Drop points that repeat their predecessor.
        for (size_t i = 1; i < pts.size(); ++i)
        {
            if (pts[i] == pts[i - 1])
            {
                pts.erase(pts.begin() + i);
                changed = true;
                break;
            }
        }
        if (changed) continue;

        // Merge a vertex that sits on a straight line.
        for (size_t i = 1; i + 1 < pts.size(); ++i)
        {
            const bool vert = (pts[i - 1].x == pts[i].x && pts[i].x == pts[i + 1].x);
            const bool horz = (pts[i - 1].y == pts[i].y && pts[i].y == pts[i + 1].y);
            if (vert || horz)
            {
                pts.erase(pts.begin() + i);
                changed = true;
                break;
            }
        }
    }
}


void wnExtendPath(std::vector<wxPoint>& path, const wxPoint& g)
{
    if (path.empty()) return;

    if (path.size() == 1)
    {
        if (g != path[0]) path.push_back(g);
        return;
    }

    const size_t n = path.size();
    const wxPoint prev = path[n - 2];
    const wxPoint last = path[n - 1];
    if (g == last) return;

    if (g == prev)
    {
        path.pop_back();                 // pointer came back: drop the corner
        return;
    }
    if (wnAligned(g, prev))
    {
        path.back() = g;                 // same axis: just stretch
        return;
    }
    if (wnAligned(g, last))
    {
        path.push_back(g);               // turn: last becomes the corner
        return;
    }

    // Diagonal jump: keep going along the current direction, then turn.
    const wxPoint c = (prev.y == last.y) ? wxPoint(g.x, last.y) : wxPoint(last.x, g.y);
    if (c == last || c == g)
    {
        path.push_back(g);
        return;
    }
    path.push_back(c);
    path.push_back(g);
}

// ---------------------------------------------------------------------------
// Minimal routing
// ---------------------------------------------------------------------------

wnSide wnAxisSide(const wxPoint& a, const wxPoint& b)
{
    const int dx = std::abs(b.x - a.x);
    const int dy = std::abs(b.y - a.y);
    return (dx >= dy) ? WN_H : WN_V;
}

void wnRoute(const wxPoint& a, const wxPoint& b, wnSide aSide, wnSide bSide,
             std::vector<wxPoint>& out)
{
    out.clear();
    out.push_back(a);
    if (a == b) return;

    // Sharing an axis always wins: one straight segment, nothing else to say.
    if (a.x == b.x || a.y == b.y) { out.push_back(b); return; }

    // Both ends want the same axis.  A single bend cannot satisfy that, so the
    // route goes out, across and back in again -- the classic Z.  It needs room
    // on the axis, otherwise the single bend below has to do.
    if (aSide == WN_H && bSide == WN_H && b.x - a.x >= 3)
    {
        const int mx = a.x + (b.x - a.x) / 2;
        out.push_back(wxPoint(mx, a.y));
        out.push_back(wxPoint(mx, b.y));
        out.push_back(b);
        return;
    }
    if (aSide == WN_V && bSide == WN_V && b.y - a.y >= 3)
    {
        const int my = a.y + (b.y - a.y) / 2;
        out.push_back(wxPoint(a.x, my));
        out.push_back(wxPoint(b.x, my));
        out.push_back(b);
        return;
    }

    // One bend.  The arrival matters most, so the target side picks the corner;
    // a target that does not care falls in line with the source.
    bool cornerAtTargetX = (bSide == WN_V);
    if (bSide == WN_ANY) cornerAtTargetX = (aSide == WN_H);
    out.push_back(cornerAtTargetX ? wxPoint(b.x, a.y) : wxPoint(a.x, b.y));
    out.push_back(b);
}

void wnJoinRun(std::vector<wxPoint>& run, const std::vector<wxPoint>& leg)
{
    for (size_t i = 0; i < leg.size(); ++i)
    {
        if (!run.empty() && run.back() == leg[i]) continue;   // shared joint
        run.push_back(leg[i]);
    }
}

bool wnInsertJog(std::vector<wxPoint>& pts, int seg)
{
    if (seg < 0 || seg + 1 >= (int)pts.size()) return false;
    wxPoint a = pts[seg];
    wxPoint b = pts[seg + 1];
    if (a == b) return false;

    std::vector<wxPoint> extra;
    if (a.y == b.y)
    {
        if (std::abs(b.x - a.x) < 2) return false;   // too short to jog
        int mx = (a.x + b.x) / 2;
        if (mx == a.x || mx == b.x) return false;
        int ny = a.y + 1;
        extra.push_back(wxPoint(mx, a.y));
        extra.push_back(wxPoint(mx, ny));
        extra.push_back(wxPoint(b.x, ny));
    }
    else if (a.x == b.x)
    {
        if (std::abs(b.y - a.y) < 2) return false;
        int my = (a.y + b.y) / 2;
        if (my == a.y || my == b.y) return false;
        int nx = a.x + 1;
        extra.push_back(wxPoint(a.x, my));
        extra.push_back(wxPoint(nx, my));
        extra.push_back(wxPoint(nx, b.y));
    }
    else
    {
        return false;
    }

    pts.insert(pts.begin() + seg + 1, extra.begin(), extra.end());
    return true;
}

// ---------------------------------------------------------------------------
// Net extraction
// ---------------------------------------------------------------------------

std::vector<wnNet> wnBuildNets(const std::vector<std::vector<wxPoint>>& polys,
                               const std::vector<wxPoint>& portPos,
                               const std::vector<wxPoint>& junctionPos,
                               const std::vector<std::pair<int, int>>& portAlias)
{
    std::vector<wnNet> nets;

    // 1. Flatten every polyline into axis aligned segments.
    std::vector<wnSegRef> segs;
    for (size_t i = 0; i < polys.size(); ++i)
    {
        const std::vector<wxPoint>& p = polys[i];
        for (size_t j = 0; j + 1 < p.size(); ++j)
        {
            const wxPoint a = p[j];
            const wxPoint b = p[j + 1];
            if (a == b) continue;

            auto push = [&](const wxPoint& s, const wxPoint& e)
            {
                wnSegRef r;
                r.poly = (int)i;
                r.seg  = (int)j;
                r.a    = s;
                r.b    = e;
                segs.push_back(r);
            };

            if (wnAligned(a, b)) { push(a, b); continue; }

            // Should not happen, but stay safe and split at the corner.
            wxPoint c(b.x, a.y);
            if (c != a && c != b) { push(a, c); push(c, b); continue; }
            c = wxPoint(a.x, b.y);
            if (c != a && c != b) { push(a, c); push(c, b); continue; }
            push(a, b);
        }
    }

    const int n = (int)segs.size();
    if (n == 0) return nets;

    // 2. Union touching segments and collect directions at every point.
    DSU dsu;
    dsu.Init(n);

    std::map<wxPoint, int, PointCmp> dirs;
    for (int i = 0; i < n; ++i)
    {
        dirs[segs[i].a] |= DirMask(segs[i].a, segs[i].b);
        dirs[segs[i].b] |= DirMask(segs[i].b, segs[i].a);
    }

    auto isEnd = [](const wxPoint& c, const wnSegRef& s)
    {
        return c == s.a || c == s.b;
    };

    for (int i = 0; i < n; ++i)
    {
        for (int j = i + 1; j < n; ++j)
        {
            std::vector<wxPoint> contacts;
            if (!SegmentsTouch(segs[i].a, segs[i].b, segs[j].a, segs[j].b, contacts))
                continue;

            // Two wires only meet where one of them actually ENDS: end against
            // end, an end inside the other wire (T branch), or overlapping
            // collinear wires -- whose overlap always contains an end point.
            // A crossing where both wires merely pass through is not joined.
            bool joined = false;
            for (size_t k = 0; k < contacts.size() && !joined; ++k)
                if (isEnd(contacts[k], segs[i]) || isEnd(contacts[k], segs[j]))
                    joined = true;
            if (!joined) continue;

            dsu.Unite(i, j);
            for (size_t k = 0; k < contacts.size(); ++k)
            {
                const wxPoint& c = contacts[k];
                if (StrictlyInside(c, segs[i].a, segs[i].b))
                    dirs[c] |= (segs[i].a.y == segs[i].b.y) ? HorizontalDirs() : VerticalDirs();
                if (StrictlyInside(c, segs[j].a, segs[j].b))
                    dirs[c] |= (segs[j].a.y == segs[j].b.y) ? HorizontalDirs() : VerticalDirs();
            }
        }
    }

    // Explicit connection points: the user asked for these, so everything that
    // passes through them is joined -- this is how a crossing gets connected.
    for (size_t k = 0; k < junctionPos.size(); ++k)
    {
        const wxPoint& jp = junctionPos[k];
        int first = -1;
        for (int i = 0; i < n; ++i)
        {
            if (!wnPointOnSeg(jp, segs[i].a, segs[i].b)) continue;
            dirs[jp] |= (segs[i].a.y == segs[i].b.y) ? HorizontalDirs() : VerticalDirs();
            if (first < 0) first = i;
            else           dsu.Unite(first, i);
        }
    }

    // 3. Which segment does each port sit on?  A port only connects at an end.
    std::vector<int> portSeg(portPos.size(), -1);
    for (size_t k = 0; k < portPos.size(); ++k)
    {
        for (int i = 0; i < n; ++i)
        {
            if (segs[i].a != portPos[k] && segs[i].b != portPos[k]) continue;
            portSeg[k] = i;
            break;
        }
    }

    // 3b. Ports declared to be one and the same node.  A sub-circuit instance
    // keeps its port on the edge of the box while the pin stays where the
    // definition drew it: the two are electrically identical even though no
    // wire joins them.  Faking that with a hidden wire would risk the stub
    // running over unrelated geometry and shorting two nets.
    for (size_t k = 0; k < portAlias.size(); ++k)
    {
        const int p = portAlias[k].first;
        const int q = portAlias[k].second;
        if (p < 0 || q < 0) continue;
        if (p >= (int)portSeg.size() || q >= (int)portSeg.size()) continue;
        if (portSeg[p] < 0 || portSeg[q] < 0) continue;
        dsu.Unite(portSeg[p], portSeg[q]);
    }

    // 4. Group segments into nets.
    std::map<int, int> rootNet;
    for (int i = 0; i < n; ++i)
    {
        int r = dsu.Find(i);
        std::map<int, int>::iterator it = rootNet.find(r);
        if (it == rootNet.end())
        {
            rootNet[r] = (int)nets.size();
            nets.push_back(wnNet());
            it = rootNet.find(r);
        }
        nets[it->second].segs.push_back(segs[i]);
    }

    // 5. Attach every port to the net of the segment it sits on.
    for (size_t k = 0; k < portPos.size(); ++k)
    {
        if (portSeg[k] < 0) continue;
        const int r = dsu.Find(portSeg[k]);
        std::map<int, int>::iterator it = rootNet.find(r);
        if (it != rootNet.end()) nets[it->second].ports.push_back((int)k);
    }

    // 6. A point with three or more directions is a junction.
    for (std::map<wxPoint, int, PointCmp>::iterator it = dirs.begin(); it != dirs.end(); ++it)
    {
        if (PopCount(it->second) < 3) continue;
        for (int i = 0; i < n; ++i)
        {
            if (!wnPointOnSeg(it->first, segs[i].a, segs[i].b)) continue;
            int r = dsu.Find(i);
            std::map<int, int>::iterator nit = rootNet.find(r);
            if (nit != rootNet.end()) nets[nit->second].junctions.push_back(it->first);
            break;
        }
    }

    return nets;
}

// ---------------------------------------------------------------------------
// Move repair
// ---------------------------------------------------------------------------

std::map<wxPoint, wxPoint, wnPtLess>
wnBuildDisplacement(const std::vector<std::pair<wxPoint, wxPoint>>& segs,
                    const std::vector<size_t>& dragIdx,
                    const std::vector<std::pair<wxPoint, wxPoint>>& portMove,
                    const wxPoint& delta)
{
    std::map<wxPoint, wxPoint, wnPtLess> disp;

    // 1. Ports that moved.  They own their location: nothing may overwrite it.
    for (size_t i = 0; i < portMove.size(); ++i)
    {
        if (portMove[i].first == portMove[i].second) continue;
        disp[portMove[i].first] = portMove[i].second;
    }

    if (delta == wxPoint(0, 0)) return disp;

    // 2. Everything that sits on a dragged segment travels with it.
    std::vector<wxPoint> ends;
    ends.reserve(segs.size() * 2);
    for (size_t i = 0; i < segs.size(); ++i)
    {
        ends.push_back(segs[i].first);
        ends.push_back(segs[i].second);
    }

    for (size_t k = 0; k < dragIdx.size(); ++k)
    {
        if (dragIdx[k] >= segs.size()) continue;
        const wxPoint a = segs[dragIdx[k]].first;
        const wxPoint b = segs[dragIdx[k]].second;
        for (size_t i = 0; i < ends.size(); ++i)
        {
            const wxPoint& p = ends[i];
            if (disp.find(p) != disp.end()) continue;      // a port owns it
            if (wnPointOnSeg(p, a, b)) disp[p] = p + delta;
        }
    }
    return disp;
}

std::vector<wnRelocPiece> wnRelocate(const wxPoint& oldA, const wxPoint& oldB,
                                     const wxPoint& newA, const wxPoint& newB)
{
    std::vector<wnRelocPiece> out;
    if (newA == newB) return out;                          // collapsed

    wnRelocPiece one;
    one.a = newA;
    one.b = newB;
    one.srcA = 0;
    one.srcB = 1;
    if (wnAligned(newA, newB)) { out.push_back(one); return out; }

    // The two ends are no longer on one axis: bend the segment once.  The
    // piece that touches the *moved* end keeps the direction the wire had,
    // exactly like Logisim, so a wire keeps leaving a port along the side it
    // used to leave.
    const bool wasHoriz = (oldA.y == oldB.y);
    const bool aMoved   = (newA != oldA);
    const bool bMoved   = (newB != oldB);

    wxPoint mv = newB, fx = newA;
    int     mvSrc = 1, fxSrc = 0;
    if (aMoved && !bMoved) { mv = newA; mvSrc = 0; fx = newB; fxSrc = 1; }

    const wxPoint jog = wasHoriz ? wxPoint(fx.x, mv.y) : wxPoint(mv.x, fx.y);
    if (jog == mv || jog == fx) { out.push_back(one); return out; }

    wnRelocPiece p1;
    p1.a    = mv;
    p1.b    = jog;
    p1.srcA = mvSrc;
    p1.srcB = -1;

    wnRelocPiece p2;
    p2.a    = jog;
    p2.b    = fx;
    p2.srcA = -1;
    p2.srcB = fxSrc;

    out.push_back(p1);
    out.push_back(p2);
    return out;
}
