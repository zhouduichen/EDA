// Unit tests for the wire geometry / net extraction module.
// Build: g++ -std=c++17 -I stub -I ../Project1 test_wirenet.cpp ../Project1/wirenet.cpp
#include "wirenet.h"

#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

static int g_fail = 0;
static int g_pass = 0;

static std::string Dump(const std::vector<wxPoint>& v)
{
    std::string s = "[";
    for (size_t i = 0; i < v.size(); ++i)
    {
        char b[64];
        snprintf(b, sizeof(b), "%s(%d,%d)", i ? " " : "", v[i].x, v[i].y);
        s += b;
    }
    return s + "]";
}

static void Check(bool ok, const std::string& what, const std::string& detail = "")
{
    if (ok) { ++g_pass; return; }
    ++g_fail;
    printf("FAIL: %s   %s\n", what.c_str(), detail.c_str());
}

static bool Ortho(const std::vector<wxPoint>& v)
{
    for (size_t i = 0; i + 1 < v.size(); ++i)
        if (v[i].x != v[i + 1].x && v[i].y != v[i + 1].y) return false;
    return true;
}

// ---------------------------------------------------------------------------

static void TestNormalize()
{
    std::vector<wxPoint> v;
    v.push_back(wxPoint(0, 0));
    v.push_back(wxPoint(5, 0));
    v.push_back(wxPoint(5, 0));   // duplicate
    v.push_back(wxPoint(10, 0));  // collinear
    v.push_back(wxPoint(10, 4));
    wnNormalize(v);
    Check(v.size() == 3 && v[0] == wxPoint(0, 0) && v[1] == wxPoint(10, 0) && v[2] == wxPoint(10, 4),
          "normalize merges collinear", Dump(v));

    v.clear();
    v.push_back(wxPoint(0, 0));
    v.push_back(wxPoint(5, 0));
    v.push_back(wxPoint(5, 5));
    wnNormalize(v);
    Check(v.size() == 3, "normalize keeps corners", Dump(v));
}

static void TestExtendPath()
{
    auto P = [](int x, int y) { return wxPoint(x, y); };

    // Drag straight right.
    std::vector<wxPoint> p;
    p.push_back(P(0, 0));
    wnExtendPath(p, P(3, 0));
    wnExtendPath(p, P(5, 0));
    Check(p.size() == 2 && p[1] == P(5, 0), "extend straight", Dump(p));

    // Turn downwards: a corner appears.
    wnExtendPath(p, P(5, 3));
    Check(p.size() == 3 && p[1] == P(5, 0) && p[2] == P(5, 3), "turn adds a corner", Dump(p));

    // Turn right again (Z shape).
    wnExtendPath(p, P(8, 3));
    Check(p.size() == 4 && p[3] == P(8, 3) && Ortho(p), "second turn keeps the shape", Dump(p));

    // Coming back removes the last corner.
    wnExtendPath(p, P(5, 3));
    Check(p.size() == 3, "coming back drops a corner", Dump(p));

    // Back onto the very start leaves a single segment.
    std::vector<wxPoint> q;
    q.push_back(P(0, 0));
    wnExtendPath(q, P(4, 0));
    wnExtendPath(q, P(4, 4));
    wnExtendPath(q, P(4, 0));
    wnExtendPath(q, P(0, 0));
    Check((q.size() == 2 && q[1] == P(0, 0)) || q.size() == 1, "retrace to the start", Dump(q));

    // A diagonal jump still produces an orthogonal path.
    std::vector<wxPoint> r;
    r.push_back(P(0, 0));
    wnExtendPath(r, P(3, 0));
    wnExtendPath(r, P(9, 4));
    Check(Ortho(r) && r.back() == P(9, 4), "diagonal jump stays orthogonal", Dump(r));
}

static void TestInsertJog()
{
    std::vector<wxPoint> v;
    v.push_back(wxPoint(0, 0));
    v.push_back(wxPoint(8, 0));
    bool ok = wnInsertJog(v, 0);
    Check(ok && Ortho(v) && v.front() == wxPoint(0, 0) && v.back() == wxPoint(8, 0) && v.size() == 5,
          "insert jog in a horizontal segment", Dump(v));

    std::vector<wxPoint> s;
    s.push_back(wxPoint(0, 0));
    s.push_back(wxPoint(1, 0));
    Check(!wnInsertJog(s, 0), "jog refused on a 1 unit segment");
}

// ---------------------------------------------------------------------------

static bool HasJunction(const std::vector<wnNet>& nets, const wxPoint& p)
{
    for (size_t i = 0; i < nets.size(); ++i)
        for (size_t j = 0; j < nets[i].junctions.size(); ++j)
            if (nets[i].junctions[j] == p) return true;
    return false;
}

static void TestNets()
{
    std::vector<std::vector<wxPoint>> polys;

    // Wire A: (0,5) -> (10,5) horizontal.
    std::vector<wxPoint> a;
    a.push_back(wxPoint(0, 5));
    a.push_back(wxPoint(10, 5));

    // Wire B: (5,5) -> (5,12) vertical, T branch off A.
    std::vector<wxPoint> b;
    b.push_back(wxPoint(5, 5));
    b.push_back(wxPoint(5, 12));

    // Wire C: crosses A at (8,5) -> real crossing.
    std::vector<wxPoint> c;
    c.push_back(wxPoint(8, 0));
    c.push_back(wxPoint(8, 10));

    // Wire D: far away, own net.
    std::vector<wxPoint> d;
    d.push_back(wxPoint(30, 30));
    d.push_back(wxPoint(35, 30));

    polys.push_back(a); polys.push_back(b); polys.push_back(c); polys.push_back(d);

    std::vector<wxPoint> ports;
    ports.push_back(wxPoint(0, 5));    // port 0 on wire A
    ports.push_back(wxPoint(5, 12));   // port 1 on wire B
    ports.push_back(wxPoint(30, 30));  // port 2 on wire D
    ports.push_back(wxPoint(99, 99));  // port 3 unconnected

    std::vector<wnNet> nets = wnBuildNets(polys, ports);
    // A+B join (B ends on A), C only crosses A so it stays its own net, D is
    // far away: three nets.
    Check(nets.size() == 3, "T branch merges, crossing stays separate",
          "nets=" + std::to_string(nets.size()));

    size_t ab = 0, farIdx = 0;
    for (size_t i = 0; i < nets.size(); ++i)
    {
        for (size_t k = 0; k < nets[i].ports.size(); ++k)
        {
            if (nets[i].ports[k] == 0) ab = i;
            if (nets[i].ports[k] == 2) farIdx = i;
        }
    }
    Check(nets[ab].ports.size() == 2, "two ports on the T branch net",
          std::to_string(nets[ab].ports.size()));
    Check(nets[farIdx].ports.size() == 1 && nets[farIdx].ports[0] == 2,
          "isolated wire keeps its own port");

    Check(HasJunction(nets, wxPoint(5, 5)), "T branch is a junction");
    Check(!HasJunction(nets, wxPoint(8, 5)), "crossing is NOT a junction");
    Check(!HasJunction(nets, wxPoint(5, 12)), "dangling end is not a junction");
    Check(!HasJunction(nets, wxPoint(0, 5)), "wire start is not a junction");

    // A corner of an L must not produce a dot.
    std::vector<std::vector<wxPoint>> lone;
    std::vector<wxPoint> l;
    l.push_back(wxPoint(0, 0));
    l.push_back(wxPoint(4, 0));
    l.push_back(wxPoint(4, 4));
    lone.push_back(l);
    std::vector<wnNet> n2 = wnBuildNets(lone, std::vector<wxPoint>());
    Check(n2.size() == 1 && n2[0].junctions.empty(), "L corner is not a junction");

    // Two collinear overlapping wires: one net, no dot.
    std::vector<std::vector<wxPoint>> col;
    std::vector<wxPoint> p1; p1.push_back(wxPoint(0, 0)); p1.push_back(wxPoint(10, 0));
    std::vector<wxPoint> p2; p2.push_back(wxPoint(5, 0)); p2.push_back(wxPoint(15, 0));
    col.push_back(p1); col.push_back(p2);
    std::vector<wnNet> n3 = wnBuildNets(col, std::vector<wxPoint>());
    Check(n3.size() == 1 && n3[0].junctions.empty(), "collinear overlap: one net, no dot");

    // Parallel wires one grid apart: two nets.
    std::vector<std::vector<wxPoint>> par;
    std::vector<wxPoint> q1; q1.push_back(wxPoint(0, 0)); q1.push_back(wxPoint(10, 0));
    std::vector<wxPoint> q2; q2.push_back(wxPoint(0, 1)); q2.push_back(wxPoint(10, 1));
    par.push_back(q1); par.push_back(q2);
    std::vector<wnNet> n4 = wnBuildNets(par, std::vector<wxPoint>());
    Check(n4.size() == 2, "parallel wires stay separate");
}


// ---------------------------------------------------------------------------
// Move repair
// ---------------------------------------------------------------------------

static std::string DumpPiece(const wnRelocPiece& p)
{
    char b[128];
    snprintf(b, sizeof(b), "(%d,%d)-(%d,%d) src=%d/%d", p.a.x, p.a.y, p.b.x, p.b.y, p.srcA, p.srcB);
    return std::string(b);
}

static bool PiecesOrtho(const std::vector<wnRelocPiece>& v)
{
    for (size_t i = 0; i < v.size(); ++i)
        if (v[i].a.x != v[i].b.x && v[i].a.y != v[i].b.y) return false;
    return true;
}

static void TestRelocate()
{
    // Whole segment translated: one piece, ends keep their identity.
    std::vector<wnRelocPiece> r1 = wnRelocate(wxPoint(0, 0), wxPoint(4, 0),
                                              wxPoint(0, 1), wxPoint(4, 1));
    Check(r1.size() == 1 && r1[0].a == wxPoint(0, 1) && r1[0].b == wxPoint(4, 1) &&
          r1[0].srcA == 0 && r1[0].srcB == 1, "relocate: translation stays one piece",
          r1.empty() ? "" : DumpPiece(r1[0]));

    // One horizontal end lifted: bend, and the moved end keeps the horizontal
    // direction so the wire still leaves its port sideways.
    std::vector<wnRelocPiece> r2 = wnRelocate(wxPoint(0, 0), wxPoint(4, 0),
                                              wxPoint(0, 1), wxPoint(4, 0));
    Check(r2.size() == 2 && PiecesOrtho(r2), "relocate: bent into orthogonal pieces");
    Check(r2.size() == 2 && r2[0].a == wxPoint(0, 1) && r2[0].b == wxPoint(4, 1) &&
          r2[0].srcA == 0 && r2[0].srcB == -1, "relocate: first piece leaves the moved end horizontally",
          r2.empty() ? "" : DumpPiece(r2[0]));
    Check(r2.size() == 2 && r2[1].a == wxPoint(4, 1) && r2[1].b == wxPoint(4, 0) &&
          r2[1].srcA == -1 && r2[1].srcB == 1, "relocate: second piece reaches the fixed end",
          r2.size() < 2 ? "" : DumpPiece(r2[1]));

    // Vertical wire pulled sideways: the moved end keeps the vertical direction.
    std::vector<wnRelocPiece> r3 = wnRelocate(wxPoint(0, 0), wxPoint(0, 4),
                                              wxPoint(2, 0), wxPoint(0, 4));
    Check(r3.size() == 2 && PiecesOrtho(r3) &&
          r3[0].a == wxPoint(2, 0) && r3[0].b == wxPoint(2, 4) &&
          r3[1].a == wxPoint(2, 4) && r3[1].b == wxPoint(0, 4),
          "relocate: vertical wire keeps its direction at the moved end",
          r3.empty() ? "" : DumpPiece(r3[0]));

    // Collapsed segment disappears.
    std::vector<wnRelocPiece> r4 = wnRelocate(wxPoint(0, 0), wxPoint(4, 0),
                                              wxPoint(2, 2), wxPoint(2, 2));
    Check(r4.empty(), "relocate: collapsed segment is dropped");

    // Both ends moved differently: still orthogonal, no piece lost.
    std::vector<wnRelocPiece> r5 = wnRelocate(wxPoint(0, 0), wxPoint(4, 0),
                                              wxPoint(0, 1), wxPoint(4, 5));
    Check(r5.size() == 2 && PiecesOrtho(r5), "relocate: both ends moved stays orthogonal");
}

static void TestDisplacement()
{
    std::vector<std::pair<wxPoint, wxPoint>> segs;
    segs.push_back(std::make_pair(wxPoint(0, 0), wxPoint(4, 0)));   // 0: horizontal
    segs.push_back(std::make_pair(wxPoint(2, 0), wxPoint(2, 3)));   // 1: T branch off it

    // No drag, one port moved: only that point is displaced.
    std::vector<std::pair<wxPoint, wxPoint>> pm;
    pm.push_back(std::make_pair(wxPoint(0, 0), wxPoint(0, 1)));
    std::vector<size_t> none;
    std::map<wxPoint, wxPoint, wnPtLess> d1 = wnBuildDisplacement(segs, none, pm, wxPoint(0, 0));
    Check(d1.size() == 1 && d1[wxPoint(0, 0)] == wxPoint(0, 1), "displacement: port move only");

    // Dragging segment 0: its own ends and the branch point travel with it.
    std::vector<size_t> drag;
    drag.push_back(0);
    std::vector<std::pair<wxPoint, wxPoint>> noPorts;
    std::map<wxPoint, wxPoint, wnPtLess> d2 = wnBuildDisplacement(segs, drag, noPorts, wxPoint(0, -1));
    Check(d2[wxPoint(0, 0)] == wxPoint(0, -1), "displacement: dragged end travels");
    Check(d2[wxPoint(4, 0)] == wxPoint(4, -1), "displacement: other dragged end travels");
    Check(d2[wxPoint(2, 0)] == wxPoint(2, -1), "displacement: touching branch travels");
    Check(d2.find(wxPoint(2, 3)) == d2.end(), "displacement: the far end stays put");

    // A moved port owns its location and wins over the drag.
    std::map<wxPoint, wxPoint, wnPtLess> d3 = wnBuildDisplacement(segs, drag, pm, wxPoint(0, -1));
    Check(d3[wxPoint(0, 0)] == wxPoint(0, 1), "displacement: port wins over the drag");

    // delta == 0 never moves anything through the wire rule.
    std::map<wxPoint, wxPoint, wnPtLess> d4 = wnBuildDisplacement(segs, drag, noPorts, wxPoint(0, 0));
    Check(d4.empty(), "displacement: zero delta moves nothing");
}

// The scenario the user asked for: move a wire, the wire hanging off it is
// stretched instead of being torn away.
static void TestRepairScenario()
{
    std::vector<std::pair<wxPoint, wxPoint>> segs;
    segs.push_back(std::make_pair(wxPoint(0, 0), wxPoint(4, 0)));
    segs.push_back(std::make_pair(wxPoint(2, 0), wxPoint(2, 3)));

    std::vector<size_t> drag;
    drag.push_back(0);
    std::vector<std::pair<wxPoint, wxPoint>> noPorts;
    std::map<wxPoint, wxPoint, wnPtLess> d =
        wnBuildDisplacement(segs, drag, noPorts, wxPoint(0, -1));

    auto move = [&](const wxPoint& p) -> wxPoint
    {
        std::map<wxPoint, wxPoint, wnPtLess>::const_iterator it = d.find(p);
        return (it == d.end()) ? p : it->second;
    };

    std::vector<wnRelocPiece> a = wnRelocate(segs[0].first, segs[0].second,
                                             move(segs[0].first), move(segs[0].second));
    std::vector<wnRelocPiece> b = wnRelocate(segs[1].first, segs[1].second,
                                             move(segs[1].first), move(segs[1].second));

    Check(a.size() == 1 && a[0].a == wxPoint(0, -1) && a[0].b == wxPoint(4, -1),
          "scenario: dragged wire moved up");
    Check(b.size() == 1 && b[0].a == wxPoint(2, -1) && b[0].b == wxPoint(2, 3),
          "scenario: branch stretched to stay connected",
          b.empty() ? "" : DumpPiece(b[0]));
    Check(!b.empty() && wnPointOnSeg(b[0].a, a[0].a, a[0].b),
          "scenario: branch still touches the dragged wire");

    // And the component flavour: a port lifted by one grid.
    std::vector<std::pair<wxPoint, wxPoint>> pm;
    pm.push_back(std::make_pair(wxPoint(0, 0), wxPoint(0, 1)));
    std::vector<size_t> none;
    std::map<wxPoint, wxPoint, wnPtLess> dp = wnBuildDisplacement(segs, none, pm, wxPoint(0, 0));
    std::map<wxPoint, wxPoint, wnPtLess>::const_iterator it0 = dp.find(wxPoint(0, 0));
    std::vector<wnRelocPiece> c = wnRelocate(segs[0].first, segs[0].second,
                                             it0->second, segs[0].second);
    Check(c.size() == 2 && c[0].a == wxPoint(0, 1) && c[0].b == wxPoint(4, 1) &&
          c[1].a == wxPoint(4, 1) && c[1].b == wxPoint(4, 0),
          "scenario: wire follows a lifted port and keeps leaving it sideways",
          c.empty() ? "" : DumpPiece(c[0]));
}


// ---------------------------------------------------------------------------
// Crossings: two wires that merely pass through each other stay independent.
// ---------------------------------------------------------------------------
static void TestCrossing()
{
    std::vector<std::vector<wxPoint>> segs;

    // A horizontal wire crosses a vertical one at (2,0); neither ends there.
    std::vector<wxPoint> h; h.push_back(wxPoint(0, 0)); h.push_back(wxPoint(4, 0));
    std::vector<wxPoint> v; v.push_back(wxPoint(2, -2)); v.push_back(wxPoint(2, 2));
    segs.push_back(h);
    segs.push_back(v);

    std::vector<wnNet> n = wnBuildNets(segs, std::vector<wxPoint>());
    Check(n.size() == 2, "plain crossing is NOT a connection", "nets=" + std::to_string(n.size()));
    bool dot = false;
    for (const wnNet& x : n) if (!x.junctions.empty()) dot = true;
    Check(!dot, "plain crossing gets no junction dot");

    // An explicit connection point joins them.
    std::vector<wxPoint> jp;
    jp.push_back(wxPoint(2, 0));
    std::vector<wnNet> n2 = wnBuildNets(segs, std::vector<wxPoint>(), jp);
    Check(n2.size() == 1, "explicit junction joins the crossing");
    bool dot2 = false;
    for (const wnNet& x : n2)
        for (const wxPoint& p : x.junctions) if (p == wxPoint(2, 0)) dot2 = true;
    Check(dot2, "explicit junction draws a dot");

    // A T branch still connects: the vertical wire ENDS on the horizontal one.
    std::vector<std::vector<wxPoint>> t;
    t.push_back(h);
    std::vector<wxPoint> t2; t2.push_back(wxPoint(2, 0)); t2.push_back(wxPoint(2, 2));
    t.push_back(t2);
    std::vector<wnNet> n3 = wnBuildNets(t, std::vector<wxPoint>());
    Check(n3.size() == 1, "T branch is a connection");
    bool dot3 = false;
    for (const wnNet& x : n3)
        for (const wxPoint& p : x.junctions) if (p == wxPoint(2, 0)) dot3 = true;
    Check(dot3, "T branch draws a dot");

    // End against end is still a connection.
    std::vector<std::vector<wxPoint>> e;
    std::vector<wxPoint> e1; e1.push_back(wxPoint(0, 0)); e1.push_back(wxPoint(2, 0));
    std::vector<wxPoint> e2; e2.push_back(wxPoint(2, 0)); e2.push_back(wxPoint(4, 0));
    e.push_back(e1); e.push_back(e2);
    Check(wnBuildNets(e, std::vector<wxPoint>()).size() == 1, "end to end is a connection");

    // Collinear overlap is still a connection (it always contains an end).
    std::vector<std::vector<wxPoint>> o;
    std::vector<wxPoint> o1; o1.push_back(wxPoint(0, 0)); o1.push_back(wxPoint(4, 0));
    std::vector<wxPoint> o2; o2.push_back(wxPoint(2, 0)); o2.push_back(wxPoint(6, 0));
    o.push_back(o1); o.push_back(o2);
    Check(wnBuildNets(o, std::vector<wxPoint>()).size() == 1, "collinear overlap is a connection");

    // One wire fully contained in another: still one net.
    std::vector<std::vector<wxPoint>> c;
    std::vector<wxPoint> c1; c1.push_back(wxPoint(0, 0)); c1.push_back(wxPoint(6, 0));
    std::vector<wxPoint> c2; c2.push_back(wxPoint(2, 0)); c2.push_back(wxPoint(4, 0));
    c.push_back(c1); c.push_back(c2);
    Check(wnBuildNets(c, std::vector<wxPoint>()).size() == 1, "contained wire joins the long one");
}

int main()
{
    TestNormalize();
    TestExtendPath();
    TestInsertJog();
    TestNets();
    TestCrossing();
    TestRelocate();
    TestDisplacement();
    TestRepairScenario();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
