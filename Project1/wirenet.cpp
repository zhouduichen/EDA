///////////////////////////////////////////////////////////////////////////
// wirenet.cpp -- orthogonal wire geometry + electrical net extraction.
///////////////////////////////////////////////////////////////////////////

#include "wirenet.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <functional>
#include <map>
#include <queue>

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

namespace
{
    // The directional sides are refinements of the two axis-only ones, so a
    // shape that only cares about the axis can treat them the same way.
    bool SideIsH(wnSide s) { return s == WN_H || s == WN_LEFT  || s == WN_RIGHT; }
    bool SideIsV(wnSide s) { return s == WN_V || s == WN_UP    || s == WN_DOWN;  }

    // Step directions: 0 = +x, 1 = -x, 2 = +y, 3 = -y.
    const int kStepX[4] = {  1, -1,  0,  0 };
    const int kStepY[4] = {  0,  0,  1, -1 };

    int ReverseDir(int d) { return d ^ 1; }          // 0<->1, 2<->3

    // May a wire whose neighbour lies in direction d leave / arrive here?
    bool SideAllowsDir(wnSide s, int d)
    {
        switch (s)
        {
            case WN_H:     return d == 0 || d == 1;
            case WN_V:     return d == 2 || d == 3;
            case WN_LEFT:  return d == 1;
            case WN_RIGHT: return d == 0;
            case WN_UP:    return d == 3;
            case WN_DOWN:  return d == 2;
            default:       return true;              // WN_ANY
        }
    }

    // Which of the four directions takes `from` one step towards `to`?  The
    // points are usually adjacent; when they are not, the dominant axis wins,
    // which is the direction the first step of the route would take anyway.
    int DirTowards(const wxPoint& from, const wxPoint& to)
    {
        for (int d = 0; d < 4; ++d)
            if (from.x + kStepX[d] == to.x && from.y + kStepY[d] == to.y) return d;
        if (to.x > from.x) return 0;
        if (to.x < from.x) return 1;
        if (to.y > from.y) return 2;
        if (to.y < from.y) return 3;
        return -1;
    }

    // Is the single piece p->q free of every blocked rectangle?  The two ends
    // of the whole route are allowed to sit inside one: that is precisely how
    // a wire is able to touch the pin on the edge of its own component.
    bool LegClear(const std::vector<wnRect>& blocks,
                  const wxPoint& p, const wxPoint& q,
                  const wxPoint& ex1, const wxPoint& ex2)
    {
        const bool horiz = (p.y == q.y);
        const bool vert  = (p.x == q.x);
        if (!horiz && !vert) return false;           // not a legal wire piece

        for (size_t i = 0; i < blocks.size(); ++i)
        {
            const wnRect& r = blocks[i];
            if (r.Empty()) continue;

            int lo, hi, fixedAxis;                   // overlap along the run
            if (horiz)
            {
                if (p.y < r.y0 || p.y > r.y1) continue;
                lo    = std::max(std::min(p.x, q.x), r.x0);
                hi    = std::min(std::max(p.x, q.x), r.x1);
                fixedAxis = p.y;
            }
            else
            {
                if (p.x < r.x0 || p.x > r.x1) continue;
                lo    = std::max(std::min(p.y, q.y), r.y0);
                hi    = std::min(std::max(p.y, q.y), r.y1);
                fixedAxis = p.x;
            }
            if (lo > hi) continue;

            int free_ = hi - lo + 1;                 // cells of the overlap
            for (int t = 0; t < 2; ++t)
            {
                const wxPoint& e = (t == 0) ? ex1 : ex2;
                if (t == 1 && ex2 == ex1) break;     // do not exempt twice
                const int along = horiz ? e.x : e.y;
                const int other = horiz ? e.y : e.x;
                if (other != fixedAxis) continue;
                if (along < lo || along > hi) continue;
                --free_;
            }
            if (free_ > 0) return false;
        }
        return true;
    }

    // A finished route is acceptable when no piece crosses a component and
    // both ends meet the direction they were asked for.
    bool RouteOK(const std::vector<wnRect>& blocks, const std::vector<wxPoint>& pts,
                 wnSide aSide, wnSide bSide)
    {
        if (pts.size() < 2) return true;
        const wxPoint a = pts.front();
        const wxPoint b = pts.back();

        for (size_t i = 0; i + 1 < pts.size(); ++i)
            if (!LegClear(blocks, pts[i], pts[i + 1], a, b)) return false;

        const int d0 = DirTowards(a, pts[1]);
        if (d0 >= 0 && !SideAllowsDir(aSide, d0)) return false;

        const int dn = DirTowards(b, pts[pts.size() - 2]);
        if (dn >= 0 && !SideAllowsDir(bSide, dn)) return false;
        return true;
    }

    // Bounded grid search.  Returns false when nothing gets there, leaving the
    // caller free to use the plain route instead.
    bool SearchAvoid(const wxPoint& a, const wxPoint& b,
                     wnSide aSide, wnSide bSide,
                     const std::vector<wnRect>& blocks,
                     std::vector<wxPoint>& out)
    {
        // ---- the window: the two ends plus room to get round anything ----
        int xlo = std::min(a.x, b.x) - 4, xhi = std::max(a.x, b.x) + 4;
        int ylo = std::min(a.y, b.y) - 4, yhi = std::max(a.y, b.y) + 4;

        for (int pass = 0; pass < 3; ++pass)
        {
            bool grew = false;
            for (size_t i = 0; i < blocks.size(); ++i)
            {
                const wnRect& r = blocks[i];
                if (r.Empty()) continue;
                if (r.x1 < xlo || r.x0 > xhi || r.y1 < ylo || r.y0 > yhi) continue;
                if (r.x0 - 2 < xlo) { xlo = r.x0 - 2; grew = true; }
                if (r.x1 + 2 > xhi) { xhi = r.x1 + 2; grew = true; }
                if (r.y0 - 2 < ylo) { ylo = r.y0 - 2; grew = true; }
                if (r.y1 + 2 > yhi) { yhi = r.y1 + 2; grew = true; }
            }
            if (!grew) break;
        }

        const int W = xhi - xlo + 1;
        const int H = yhi - ylo + 1;
        if (W < 1 || H < 1 || W > 160 || H > 160) return false;
        if (a.x < xlo || a.x > xhi || a.y < ylo || a.y > yhi) return false;
        if (b.x < xlo || b.x > xhi || b.y < ylo || b.y > yhi) return false;

        // ---- blocked cells (the two ends are always reachable) ------------
        const int N = W * H;
        std::vector<unsigned char> blk(N, 0);
        for (size_t i = 0; i < blocks.size(); ++i)
        {
            const wnRect& r = blocks[i];
            if (r.Empty()) continue;
            const int bx0 = std::max(xlo, r.x0), bx1 = std::min(xhi, r.x1);
            const int by0 = std::max(ylo, r.y0), by1 = std::min(yhi, r.y1);
            for (int x = bx0; x <= bx1; ++x)
                for (int y = by0; y <= by1; ++y)
                    blk[(x - xlo) * H + (y - ylo)] = 1;
        }
        const int idxA = (a.x - xlo) * H + (a.y - ylo);
        const int idxB = (b.x - xlo) * H + (b.y - ylo);
        blk[idxA] = 0;
        blk[idxB] = 0;
        if (idxA == idxB) { out.clear(); out.push_back(a); return true; }

        // ---- cheapest route, bends charged extra --------------------------
        const int kBend = 2;
        std::vector<int> dist(N * 4, INT_MAX);
        std::vector<int> prev(N * 4, -1);
        std::priority_queue<std::pair<int, int>,
                            std::vector<std::pair<int, int>>,
                            std::greater<std::pair<int, int>>> pq;

        for (int d = 0; d < 4; ++d)
        {
            if (!SideAllowsDir(aSide, d)) continue;
            const int nx = a.x + kStepX[d], ny = a.y + kStepY[d];
            if (nx < xlo || nx > xhi || ny < ylo || ny > yhi) continue;
            const int ni = (nx - xlo) * H + (ny - ylo);
            if (blk[ni]) continue;
            const int st = ni * 4 + d;
            dist[st] = 1;
            prev[st] = -2;                       // -2 = "the step before is a"
            pq.push(std::make_pair(1, st));
        }

        int goal = -1;
        while (!pq.empty())
        {
            const std::pair<int, int> top = pq.top();
            pq.pop();
            if (top.first != dist[top.second]) continue;      // stale entry

            const int st   = top.second;
            const int cost = top.first;
            const int node = st / 4;
            const int d    = st % 4;

            if (node == idxB && SideAllowsDir(bSide, ReverseDir(d)))
            {
                goal = st;                       // arrived from the right side
                break;
            }

            const int x = xlo + node / H;
            const int y = ylo + node % H;
            for (int nd = 0; nd < 4; ++nd)
            {
                if (nd == ReverseDir(d)) continue;            // never a U turn
                const int nx = x + kStepX[nd], ny = y + kStepY[nd];
                if (nx < xlo || nx > xhi || ny < ylo || ny > yhi) continue;
                const int ni = (nx - xlo) * H + (ny - ylo);
                if (blk[ni]) continue;
                const int ns = ni * 4 + nd;
                const int nc = cost + 1 + (nd == d ? 0 : kBend);
                if (nc >= dist[ns]) continue;
                dist[ns] = nc;
                prev[ns] = st;
                pq.push(std::make_pair(nc, ns));
            }
        }
        if (goal < 0) return false;

        // ---- walk it back and keep only the corners -----------------------
        std::vector<wxPoint> steps;
        for (int st = goal; st != -2; st = prev[st])
        {
            const int node = st / 4;
            steps.push_back(wxPoint(xlo + node / H, ylo + node % H));
        }
        steps.push_back(a);
        std::reverse(steps.begin(), steps.end());

        out.clear();
        out.push_back(steps.front());
        for (size_t i = 1; i + 1 < steps.size(); ++i)
        {
            const wxPoint in  = steps[i]     - steps[i - 1];
            const wxPoint out_ = steps[i + 1] - steps[i];
            const bool straight = (in.x == 0 && out_.x == 0) || (in.y == 0 && out_.y == 0);
            if (!straight) out.push_back(steps[i]);
        }
        out.push_back(steps.back());
        wnNormalize(out);
        return true;
    }
}

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
    if (SideIsH(aSide) && SideIsH(bSide) && b.x - a.x >= 3)
    {
        const int mx = a.x + (b.x - a.x) / 2;
        out.push_back(wxPoint(mx, a.y));
        out.push_back(wxPoint(mx, b.y));
        out.push_back(b);
        return;
    }
    if (SideIsV(aSide) && SideIsV(bSide) && b.y - a.y >= 3)
    {
        const int my = a.y + (b.y - a.y) / 2;
        out.push_back(wxPoint(a.x, my));
        out.push_back(wxPoint(b.x, my));
        out.push_back(b);
        return;
    }

    // One bend.  The arrival matters most, so the target side picks the corner;
    // a target that does not care falls in line with the source.
    bool cornerAtTargetX = SideIsV(bSide);
    if (bSide == WN_ANY) cornerAtTargetX = SideIsH(aSide);
    out.push_back(cornerAtTargetX ? wxPoint(b.x, a.y) : wxPoint(a.x, b.y));
    out.push_back(b);
}

void wnRouteAvoid(const wxPoint& a, const wxPoint& b, wnSide aSide, wnSide bSide,
                  const std::vector<wnRect>& blocks, std::vector<wxPoint>& out)
{
    // Nothing in the way costs nothing: the answer is byte for byte what
    // wnRoute would have produced, so a wire drawn in open canvas is not
    // "improved" behind the user's back.
    std::vector<wxPoint> plain;
    wnRoute(a, b, aSide, bSide, plain);
    if (plain.size() < 2 || RouteOK(blocks, plain, aSide, bSide))
    {
        out = plain;
        return;
    }

    // Something really is standing in the corridor.  Search with the sides the
    // caller asked for; if even that fails, drop the directional demand and
    // keep only the axis -- a wire that meets a pin from the wrong side still
    // conducts, while a wire that was never laid does not.
    if (SearchAvoid(a, b, aSide, bSide, blocks, out)) return;

    const wnSide relaxA = SideIsH(aSide) ? WN_H : (SideIsV(aSide) ? WN_V : WN_ANY);
    const wnSide relaxB = SideIsH(bSide) ? WN_H : (SideIsV(bSide) ? WN_V : WN_ANY);
    if (SearchAvoid(a, b, relaxA, relaxB, blocks, out)) return;

    std::vector<wxPoint> fallback;
    wnRoute(a, b, relaxA, relaxB, fallback);
    if (RouteOK(blocks, fallback, relaxA, relaxB)) { out = fallback; return; }

    out = plain;
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
