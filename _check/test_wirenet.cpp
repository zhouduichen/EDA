// Unit tests for the wire geometry / net extraction module.
// Build: g++ -std=c++17 -I stub -I ../Project1 test_wirenet.cpp ../Project1/wirenet.cpp
#include "wirenet.h"

#include <cstdio>
#include <initializer_list>
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

// Minimal routing: a first-time draw must come out as the shortest clean route,
// and a deliberate zig-zag must survive untouched.
static void TestRoute()
{
    auto P = [](int x, int y) { return wxPoint(x, y); };
    std::vector<wxPoint> out;

    // Level ends: one straight segment, whatever the hand did on the way there.
    wnRoute(P(2, 4), P(20, 4), WN_H, WN_H, out);
    Check(out.size() == 2 && out[1] == P(20, 4), "route: level ends go straight", Dump(out));

    // A component pin is met from the side: out, across, in -- never a staircase.
    wnRoute(P(10, 4), P(24, 9), WN_H, WN_H, out);
    Check(out.size() == 4 && out[1] == P(17, 4) && out[2] == P(17, 9) && out[3] == P(24, 9),
          "route: two pins get the Z", Dump(out));
    Check(Ortho(out), "route: the Z stays orthogonal", Dump(out));

    // No room for a Z -> the single bend still meets the target level head on.
    wnRoute(P(10, 4), P(11, 9), WN_H, WN_H, out);
    Check(out.size() == 3 && out[1] == P(10, 9) && Ortho(out),
          "route: cramped ends fall back to one bend", Dump(out));

    // Free end: the source direction decides which bend.
    wnRoute(P(10, 4), P(24, 9), WN_H, WN_ANY, out);
    Check(out.size() == 3 && out[1] == P(24, 4) && Ortho(out),
          "route: a horizontal source leaves horizontally", Dump(out));
    wnRoute(P(10, 4), P(24, 9), WN_V, WN_ANY, out);
    Check(out.size() == 3 && out[1] == P(10, 9) && Ortho(out),
          "route: a vertical source leaves vertically", Dump(out));

    // Turning back to the left is still handled: one bend, no fold.
    wnRoute(P(20, 4), P(5, 9), WN_H, WN_H, out);
    Check(out.size() == 3 && Ortho(out) && out.back() == P(5, 9),
          "route: a backwards run still gets one bend", Dump(out));

    // Both ends on the same axis.
    wnRoute(P(4, 4), P(4, 12), WN_V, WN_V, out);
    Check(out.size() == 2 && out[1] == P(4, 12), "route: same column goes straight", Dump(out));

    // Collapsed to a single point.
    wnRoute(P(4, 4), P(4, 4), WN_H, WN_H, out);
    Check(out.size() == 1, "route: a zero length run collapses", Dump(out));

    Check(wnAxisSide(P(0, 0), P(5, 1)) == WN_H, "axis side: horizontal wins on a tie");
    Check(wnAxisSide(P(0, 0), P(1, 5)) == WN_V, "axis side: vertical when it dominates");

    // Joining legs: the shared joint is stored once, so a click-to-place run is
    // a clean polyline and not a pile of repeated points.
    std::vector<wxPoint> run;
    run.push_back(P(0, 0));
    run.push_back(P(6, 0));
    std::vector<wxPoint> leg;
    leg.push_back(P(6, 0));
    leg.push_back(P(6, 4));
    wnJoinRun(run, leg);
    Check(run.size() == 3 && run[2] == P(6, 4), "join: the shared joint is not duplicated", Dump(run));

    // A connection mark only counts while a wire runs through it.  This is the
    // rule that stops a deleted wire from leaving an undying dot behind.
    std::vector<std::vector<wxPoint>> wires;
    std::vector<wxPoint> trunk;
    trunk.push_back(P(2, 4));
    trunk.push_back(P(20, 4));
    wires.push_back(trunk);
    Check(wnOnWires(wires, P(10, 4)),  "orphan check: a point on the wire counts");
    Check(wnOnWires(wires, P(2, 4)),   "orphan check: an end point counts");
    Check(!wnOnWires(wires, P(10, 5)), "orphan check: a point beside it does not");
    wires.clear();
    Check(!wnOnWires(wires, P(10, 4)), "orphan check: no wires means no mark");
}

static bool InsideAnyBlock(const std::vector<wnRect>& blocks, const wxPoint& p)
{
    for (size_t i = 0; i < blocks.size(); ++i)
    {
        const wnRect& r = blocks[i];
        if (r.Empty()) continue;
        if (p.x >= r.x0 && p.x <= r.x1 && p.y >= r.y0 && p.y <= r.y1) return true;
    }
    return false;
}

// Nothing between the two ends of the route may sit on a component body.  The
// ends themselves are allowed to: that is how a wire reaches a pin, which sits
// on the edge of the very body it is being kept out of.
static bool RouteStaysClear(const std::vector<wnRect>& blocks,
                            const std::vector<wxPoint>& path)
{
    if (path.empty()) return false;
    for (size_t i = 0; i + 1 < path.size(); ++i)
    {
        const int dx = (path[i + 1].x > path[i].x) ? 1 : (path[i + 1].x < path[i].x ? -1 : 0);
        const int dy = (path[i + 1].y > path[i].y) ? 1 : (path[i + 1].y < path[i].y ? -1 : 0);
        if (dx == 0 && dy == 0) return false;             // a zero length piece
        wxPoint p = path[i];
        while (p != path[i + 1])
        {
            p = wxPoint(p.x + dx, p.y + dy);
            if (p == path.front() || p == path.back()) continue;
            if (InsideAnyBlock(blocks, p)) return false;
        }
    }
    return true;
}

static void TestRouteAvoid()
{
    auto P = [](int x, int y) { return wxPoint(x, y); };
    auto R = [](int x0, int y0, int x1, int y1)
    {
        wnRect r; r.x0 = x0; r.y0 = y0; r.x1 = x1; r.y1 = y1; return r;
    };
    std::vector<wxPoint> out, plain;

    // 1. Nothing in the way costs nothing: byte for byte the plain route, so a
    //    wire drawn on open canvas is never "improved" behind the user's back.
    std::vector<wnRect> none;
    wnRoute(P(10, 4), P(24, 9), WN_H, WN_H, plain);
    wnRouteAvoid(P(10, 4), P(24, 9), WN_H, WN_H, none, out);
    Check(out == plain, "avoid: an empty canvas routes exactly like wnRoute", Dump(out));

    std::vector<wnRect> farOff;
    farOff.push_back(R(0, 40, 6, 46));
    wnRouteAvoid(P(10, 4), P(24, 9), WN_H, WN_H, farOff, out);
    Check(out == plain, "avoid: a component out of the way is ignored", Dump(out));

    // 2. An output pin feeding an input pin with a component standing in the
    //    corridor: the straight line would run under the body.
    std::vector<wnRect> blocks;
    blocks.push_back(R(8, 0, 12, 4));
    wnRoute(P(4, 2), P(20, 2), WN_RIGHT, WN_LEFT, plain);
    wnRouteAvoid(P(4, 2), P(20, 2), WN_RIGHT, WN_LEFT, blocks, out);
    Check(!RouteStaysClear(blocks, plain),
          "avoid: the plain route really did cross the body", Dump(plain));
    Check(Ortho(out) && out.front() == P(4, 2) && out.back() == P(20, 2),
          "avoid: the detour still joins its two ends", Dump(out));
    Check(RouteStaysClear(blocks, out), "avoid: the routed run keeps off the body", Dump(out));
    // The polyline only stores corners, so "leaves to the right" reads as: the
    // first leg is horizontal and runs east of the pin.
    Check(out[1].y == 2 && out[1].x > 4,
          "avoid: it leaves the output pin to the right", Dump(out));
    Check(out[out.size() - 2] == P(19, 2),
          "avoid: it reaches the input pin from the left", Dump(out));

    // 3. A wire has to leave a pin on the side the pin faces, and the body
    //    owning the pin is what makes that unavoidable.
    std::vector<wnRect> own;
    own.push_back(R(0, 0, 4, 4));                 // inputs on x=0, outputs on x=4
    wnRouteAvoid(P(0, 2), P(12, 9), WN_LEFT, WN_ANY, own, out);
    Check(out[1] == P(-1, 2), "avoid: an input pin is left westwards", Dump(out));
    Check(RouteStaysClear(own, out),
          "avoid: and never sneaks along the body edge", Dump(out));

    // 4. The bug that started all this: a component dragged on top of a wire.
    //    Stretching the wire used to drive it straight through the body; now
    //    the run steps out of the pin and walks around.
    std::vector<wnRect> moved;
    moved.push_back(R(0, 5, 3, 7));               // gate after the drag
    wnRoute(P(0, 6), P(12, 3), WN_H, WN_ANY, plain);
    wnRouteAvoid(P(0, 6), P(12, 3), WN_LEFT, WN_ANY, moved, out);
    Check(!RouteStaysClear(moved, plain),
          "avoid: the old stretch ran under the body", Dump(plain));
    Check(out.size() == 4 && out[1] == P(-1, 6) && out[2] == P(-1, 3)
              && out[3] == P(12, 3),
          "avoid: out of the pin, up the side, across to the far end", Dump(out));
    Check(RouteStaysClear(moved, out),
          "avoid: the repaired wire no longer crosses the body", Dump(out));

    // 5. Degenerate input still behaves.
    wnRouteAvoid(P(3, 3), P(3, 3), WN_LEFT, WN_RIGHT, blocks, out);
    Check(out.size() == 1 && out[0] == P(3, 3),
          "avoid: a zero length run collapses", Dump(out));

    // 6. Walled in on every side: hopeless, but the user still gets a wire --
    //    one that conducts wrongly beats one that was never created.
    std::vector<wnRect> wall;
    wall.push_back(R(-40, -40, 40, 40));
    wnRouteAvoid(P(0, 0), P(3, 0), WN_RIGHT, WN_LEFT, wall, out);
    Check(out.size() >= 2 && out.front() == P(0, 0) && out.back() == P(3, 0),
          "avoid: even a hopeless route still joins the two ends", Dump(out));
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

// ---------------------------------------------------------------------------
// Regression: a real drag silently shorted two nets.
//
// The user saved 电路1.circ (before) and 电路2.circ (after dragging AND gate
// comp 15 from (-28,-7) to (-24,-8)).  The router moved group 15's vertical
// leg from x=-32 onto x=-30, the very column used by group 13's leg.  Their
// overlap made "an end landing inside another wire" at (-30,-11) and (-30,-7),
// merging two nets and painting two phantom junction dots.
//
// These are the exact WIRE lines of the saved files, so the test is not a
// made-up approximation of the failure.
// ---------------------------------------------------------------------------

static std::vector<wxPoint> Seg(int x1, int y1, int x2, int y2)
{
    std::vector<wxPoint> p;
    p.push_back(wxPoint(x1, y1));
    p.push_back(wxPoint(x2, y2));
    return p;
}

static std::vector<wxPoint> Run(std::initializer_list<wxPoint> pts)
{
    return std::vector<wxPoint>(pts);
}

static void TestMoveTopologyGuard()
{
    // Port index -> grid position, before and after.  Index is the identity
    // used by the guard; only comp 15's three ports moved.
    const std::vector<wxPoint> pb = Run({
        wxPoint(-35,-16), wxPoint(-35,-11),
        wxPoint(-25,-18), wxPoint(-25,-16), wxPoint(-23,-17),
        wxPoint(-28,-6),  wxPoint(-28,-4),  wxPoint(-26,-5)});
    const std::vector<wxPoint> pa = Run({
        wxPoint(-35,-16), wxPoint(-35,-11),
        wxPoint(-25,-18), wxPoint(-25,-16), wxPoint(-23,-17),
        wxPoint(-24,-7),  wxPoint(-24,-5),  wxPoint(-22,-6)});

    std::vector<std::vector<wxPoint>> before;
    before.push_back(Seg(-35,-16, -30,-16));   // g13
    before.push_back(Seg(-30,-16, -30,-6));
    before.push_back(Seg(-30,-6,  -28,-6));
    before.push_back(Seg(-30,-16, -28,-16));   // g14
    before.push_back(Seg(-28,-16, -28,-18));
    before.push_back(Seg(-28,-18, -25,-18));
    before.push_back(Seg(-35,-11, -32,-11));   // g15, vertical at x=-32
    before.push_back(Seg(-32,-11, -32,-4));
    before.push_back(Seg(-32,-4,  -28,-4));

    std::vector<std::vector<wxPoint>> routed;
    routed.push_back(Seg(-35,-16, -30,-16));
    routed.push_back(Seg(-30,-16, -30,-7));
    routed.push_back(Seg(-30,-7,  -24,-7));
    routed.push_back(Seg(-30,-16, -28,-16));
    routed.push_back(Seg(-28,-16, -28,-18));
    routed.push_back(Seg(-28,-18, -25,-18));
    routed.push_back(Seg(-35,-11, -30,-11));   // g15, vertical at x=-30 <-- short
    routed.push_back(Seg(-30,-11, -30,-5));
    routed.push_back(Seg(-30,-5,  -24,-5));

    const std::vector<std::vector<int>> expected = {{0, 2, 5}, {1, 6}};

    Check(wnPortGroups(before, pb) == expected,
          "before: two independent nets", "");
    Check(wnJunctionCount(before) == 1,
          "before: exactly one junction dot", "");
    Check(wnJunctionCount(routed) == 3,
          "bug reproduced: drag paints two extra dots", "");
    Check(wnPortGroups(routed, pa) != expected,
          "guard must REJECT the routed candidate", "");
    Check(wnPortGroups(routed, pa).size() == 1 &&
          wnPortGroups(routed, pa)[0] == std::vector<int>({0, 1, 2, 5, 6}),
          "bug reproduced: the two nets are shorted", "");

    // The fallback keeps each run in its original column and just slides the
    // bend next to the moved end.
    const std::vector<wxPoint> g13 = Run({wxPoint(-35,-16), wxPoint(-30,-16),
                                          wxPoint(-30,-6),  wxPoint(-28,-6)});
    const std::vector<wxPoint> g15 = Run({wxPoint(-35,-11), wxPoint(-32,-11),
                                          wxPoint(-32,-4),  wxPoint(-28,-4)});
    const std::vector<wxPoint> g14 = Run({wxPoint(-30,-16), wxPoint(-28,-16),
                                          wxPoint(-28,-18), wxPoint(-25,-18)});

    const std::vector<wxPoint> s13 = wnStretchRun(g13, g13.front(), wxPoint(-24,-7));
    const std::vector<wxPoint> s15 = wnStretchRun(g15, g15.front(), wxPoint(-24,-5));
    const std::vector<wxPoint> s14 = wnStretchRun(g14, g14.front(), g14.back());

    Check(s13 == Run({wxPoint(-35,-16), wxPoint(-30,-16),
                      wxPoint(-30,-7),  wxPoint(-24,-7)}),
          "stretch g13 keeps x=-30 and slides the last bend", Dump(s13));
    Check(s15 == Run({wxPoint(-35,-11), wxPoint(-32,-11),
                      wxPoint(-32,-5),  wxPoint(-24,-5)}),
          "stretch g15 keeps x=-32 and slides the last bend", Dump(s15));
    Check(s14 == g14, "untouched run is left unchanged", Dump(s14));

    std::vector<std::vector<wxPoint>> stretched;
    for (const std::vector<wxPoint>* r : {&s13, &s14, &s15})
        for (size_t i = 0; i + 1 < r->size(); ++i)
            stretched.push_back(Seg((*r)[i].x, (*r)[i].y,
                                   (*r)[i+1].x, (*r)[i+1].y));

    Check(wnPortGroups(stretched, pa) == expected,
          "fallback preserves the port grouping", "");
    Check(wnJunctionCount(stretched) == 1,
          "fallback paints no phantom dots", "");
    Check(wnJunctionCount(stretched) == wnJunctionCount(before),
          "dot count is unchanged by the drag", "");

    // ---- wnStretchRun edge cases -------------------------------------------
    const std::vector<wxPoint> bent = Run({wxPoint(0,0), wxPoint(5,0),
                                           wxPoint(5,5), wxPoint(10,5)});
    Check(wnStretchRun(bent, bent.front(), wxPoint(12,8)) ==
          Run({wxPoint(0,0), wxPoint(5,0), wxPoint(5,8), wxPoint(12,8)}),
          "tail end moved", "");
    Check(wnStretchRun(bent, wxPoint(0,2), bent.back()) ==
          Run({wxPoint(0,2), wxPoint(5,2), wxPoint(5,5), wxPoint(10,5)}),
          "head end moved", "");
    Check(wnStretchRun(bent, wxPoint(0,2), wxPoint(12,8)) ==
          Run({wxPoint(0,2), wxPoint(5,2), wxPoint(5,8), wxPoint(12,8)}),
          "both ends moved", "");
    Check(Ortho(wnStretchRun(bent, wxPoint(0,2), wxPoint(12,8))),
          "stretched run stays Manhattan", "");
    Check(wnStretchRun(Run({wxPoint(0,0), wxPoint(5,0), wxPoint(10,0)}),
                       wxPoint(0,0), wxPoint(12,0)) ==
          Run({wxPoint(0,0), wxPoint(12,0)}),
          "collinear middle vertex is normalised away", "");

    // ---- junction counting / explicit marks --------------------------------
    std::vector<std::vector<wxPoint>> cross;
    cross.push_back(Seg(0,0, 4,0));
    cross.push_back(Seg(2,-2, 2,2));
    Check(wnJunctionCount(cross) == 0,
          "plain crossing paints no dot", "");
    std::vector<wxPoint> mark; mark.push_back(wxPoint(2,0));
    Check(wnJunctionCount(cross, mark) == 1,
          "explicit mark paints one dot", "");
    Check(wnBuildNets(cross, std::vector<wxPoint>(), mark).size() == 1,
          "explicit mark joins the crossing", "");
}

// ---------------------------------------------------------------------------
// Avoidance fallback -- the four .circ files the user reported.
//
// Every WIRE line below is taken verbatim from C:\Users\34854\Desktop\电路1.circ
// / 电路2.circ / 自定义1.circ / 自定义2.circ.  These are real saved circuits,
// not made-up approximations of the failure.
// ---------------------------------------------------------------------------

static int SlantCount(const std::vector<std::vector<wxPoint>>& segs)
{
    int n = 0;
    for (size_t i = 0; i < segs.size(); ++i)
        if (segs[i].size() == 2 &&
            segs[i][0].x != segs[i][1].x && segs[i][0].y != segs[i][1].y) ++n;
    return n;
}

static bool HasPoint(const std::vector<wxPoint>& path, int x, int y)
{
    for (size_t i = 0; i < path.size(); ++i)
        if (path[i] == wxPoint(x, y)) return true;
    return false;
}

static std::vector<std::vector<wxPoint>> Flatten(
    const std::vector<std::vector<wxPoint>>& ambient,
    const std::vector<std::vector<wxPoint>>& paths)
{
    std::vector<std::vector<wxPoint>> all = ambient;
    for (size_t i = 0; i < paths.size(); ++i)
        for (size_t k = 0; k + 1 < paths[i].size(); ++k)
            if (!(paths[i][k] == paths[i][k + 1]))
                all.push_back(Seg(paths[i][k].x, paths[i][k].y,
                                  paths[i][k+1].x, paths[i][k+1].y));
    return all;
}

static void TestStretchOrthogonality()
{
    // A two-point run whose ends move diagonally used to become a SLANTED wire
    // (自定义2.circ group 3: (25,4) -> (31,9)).  It must never happen again.
    const std::vector<wxPoint> two = Run({wxPoint(25,9), wxPoint(31,9)});
    const std::vector<wxPoint> s1 = wnStretchRun(two, wxPoint(25,4), wxPoint(31,9));
    Check(Ortho(s1), "two-point run stays Manhattan when one end moves", Dump(s1));
    Check(s1.size() >= 3, "two-point run gains a real corner", Dump(s1));
    Check(s1.front() == wxPoint(25,4) && s1.back() == wxPoint(31,9),
          "two-point run still lands on both ends", Dump(s1));

    const std::vector<wxPoint> s2 =
        wnStretchRun(Run({wxPoint(0,0), wxPoint(10,0)}), wxPoint(1,2), wxPoint(12,5));
    Check(Ortho(s2), "two-point run stays Manhattan when both ends move", Dump(s2));

    // (9,3) is a genuine spur: group 17's wire starts there.  wnNormalize
    // used to merge it away and disconnect the branch.
    const std::vector<wxPoint> spur = Run({wxPoint(4,4), wxPoint(9,4),
                                           wxPoint(9,3), wxPoint(9,11),
                                           wxPoint(13,11)});
    const std::vector<wxPoint> s3 = wnStretchRun(spur, wxPoint(4,6), wxPoint(13,11));
    Check(Ortho(s3), "spur run stays Manhattan", Dump(s3));
    Check(HasPoint(s3, 9, 3), "collinear reversal spur is preserved", Dump(s3));

    // Every avoidance candidate must be orthogonal and non-degenerate.
    const std::vector<std::vector<wxPoint>> cand =
        wnStretchCandidates(spur, wxPoint(4,6), wxPoint(13,11), 3);
    Check(cand.size() > 8, "avoidance search has a real candidate set", "");
    bool allOrtho = true, allEnds = true, hasStub = false;
    for (size_t i = 0; i < cand.size(); ++i)
    {
        if (!Ortho(cand[i]) || cand[i].size() < 2) allOrtho = false;
        if (cand[i].front() != wxPoint(4,6) || cand[i].back() != wxPoint(13,11))
            allEnds = false;
        if (HasPoint(cand[i], 4, 4) && HasPoint(cand[i], 9, 4)) hasStub = true;
    }
    Check(allOrtho, "every avoidance candidate is orthogonal", "");
    Check(allEnds,  "every avoidance candidate keeps both port ends", "");
    Check(hasStub,  "candidate set contains the corridor-avoiding stub", "");

    // slideOffset must actually move the sliding bend's corridor.
    const std::vector<wxPoint> off = wnStretchRun(
        Run({wxPoint(0,0), wxPoint(5,0), wxPoint(5,5), wxPoint(10,5)}),
        wxPoint(0,0), wxPoint(12,8), 2);
    Check(Ortho(off), "offset stretch is orthogonal", Dump(off));
    Check(off != wnStretchRun(
              Run({wxPoint(0,0), wxPoint(5,0), wxPoint(5,5), wxPoint(10,5)}),
              wxPoint(0,0), wxPoint(12,8)),
          "slideOffset changes the geometry", Dump(off));
}

static void TestAvoidanceCaseA()
{
    // 电路1.circ -> 电路2.circ: COMP 16 PIN_IN moves (2,3) -> (2,5).
    std::vector<std::vector<wxPoint>> amb;
    amb.push_back(Seg(-35,-16, -30,-16)); amb.push_back(Seg(-30,-16, -30,-7));
    amb.push_back(Seg(-30,-7,  -24,-7));  amb.push_back(Seg(-30,-16, -28,-16));
    amb.push_back(Seg(-28,-16, -28,-18)); amb.push_back(Seg(-28,-18, -25,-18));
    amb.push_back(Seg(-35,-11, -30,-11)); amb.push_back(Seg(-30,-11, -30,-5));
    amb.push_back(Seg(-30,-5,  -24,-5));  amb.push_back(Seg(9,3, 11,3));
    amb.push_back(Seg(11,3, 11,6));       amb.push_back(Seg(11,6, 13,6));
    amb.push_back(Seg(7,6,  10,6));       amb.push_back(Seg(10,6, 10,13));
    amb.push_back(Seg(10,13,13,13));

    std::vector<std::vector<wxPoint>> before = amb;
    before.push_back(Seg(4,4, 9,4));  before.push_back(Seg(9,4, 9,3));
    before.push_back(Seg(9,3, 9,11)); before.push_back(Seg(9,11,13,11));

    // The geometry the OLD code produced (电路2.circ WIRE lines 23..25).
    std::vector<std::vector<wxPoint>> buggy = amb;
    buggy.push_back(Seg(4,6, 9,6));  buggy.push_back(Seg(9,6, 9,11));
    buggy.push_back(Seg(9,11,13,11));

    const int px[16] = {-35,-35,-25,-25,-23,-24,-24,-22,4,7,13,13,15,13,13,15};
    const int py[16] = {-16,-11,-18,-16,-17,-7,-5,-6,4,6,4,6,5,11,13,12};
    std::vector<wxPoint> bp, ap;
    for (int i = 0; i < 16; ++i)
    { bp.push_back(wxPoint(px[i], py[i])); ap.push_back(wxPoint(px[i], py[i])); }
    ap[8] = wxPoint(4,6);                       // only comp 16's port moved

    const std::vector<std::vector<int>> want = wnPortGroups(before, bp);
    Check(want == std::vector<std::vector<int>>({{0,1,2,5,6},{8,11,13},{9,14}}),
          "A before: three independent nets", "");
    Check(wnJunctionCount(before) == 4, "A before: four junction dots", "");
    Check(wnPortGroups(buggy, ap) != want,
          "A bug reproduced: the port grouping changed", "");
    Check(wnJunctionCount(buggy) == 4,
          "A bug has the SAME dot count -- only topology catches it", "");

    std::vector<wnMoveRun> runs(1);
    runs[0].pts = Run({wxPoint(4,4), wxPoint(9,4), wxPoint(9,3),
                       wxPoint(9,11), wxPoint(13,11)});
    runs[0].na  = wxPoint(4,6);
    runs[0].nb  = wxPoint(13,11);
    // Deliberately no router candidate: the legacy stretch alone is the buggy
    // shape, so the avoidance search has to earn the fix by itself.

    const std::vector<std::vector<wxPoint>> chosen =
        wnChooseMovePaths(runs, amb, before, bp, ap,
                          std::vector<wxPoint>(), 3);
    const std::vector<std::vector<wxPoint>> after = Flatten(amb, chosen);

    Check(wnPortGroups(after, ap) == want,
          "A avoidance preserves the electrical topology", "");
    Check(wnJunctionCount(after) == 4,
          "A avoidance invents no extra junction dot", "");
    Check(SlantCount(after) == 0, "A avoidance emits no slanted wire", "");
    Check(chosen.size() == 1 && HasPoint(chosen[0], 9, 3),
          "A avoidance keeps the (9,3) branch attached", Dump(chosen[0]));
    Check(HasPoint(chosen[0], 9, 4),
          "A avoidance keeps the original (9,4) bend", Dump(chosen[0]));

    // Mutation guard: without the avoidance search the buggy shape wins.
    const std::vector<std::vector<wxPoint>> noAvoid =
        wnChooseMovePaths(runs, amb, before, bp, ap,
                          std::vector<wxPoint>(), -1);
    Check(wnPortGroups(Flatten(amb, noAvoid), ap) != want,
          "A negative control: disabling avoidance reproduces the bug", "");
}

static void TestAvoidanceCaseB()
{
    // 自定义1.circ -> 自定义2.circ: SUB 半加器 moves (7,5) -> (7,0).
    const std::vector<wxPoint> g1 = Run({wxPoint(-3,8), wxPoint(2,8),
                                         wxPoint(2,9),  wxPoint(7,9)});
    const std::vector<wxPoint> g2 = Run({wxPoint(-2,12), wxPoint(2,12),
                                         wxPoint(2,11),  wxPoint(7,11)});
    const std::vector<wxPoint> g3 = Run({wxPoint(25,9), wxPoint(31,9)});
    const std::vector<wxPoint> g4 = Run({wxPoint(25,11), wxPoint(28,11),
                                         wxPoint(28,12),  wxPoint(31,12)});

    std::vector<std::vector<wxPoint>> before;
    const std::vector<wxPoint>* gs[4] = {&g1, &g2, &g3, &g4};
    for (int i = 0; i < 4; ++i)
        for (size_t k = 0; k + 1 < gs[i]->size(); ++k)
            before.push_back(Seg((*gs[i])[k].x, (*gs[i])[k].y,
                                 (*gs[i])[k+1].x, (*gs[i])[k+1].y));

    // 自定义2.circ WIRE lines 12..21 (what the old code produced).
    std::vector<std::vector<wxPoint>> buggy;
    buggy.push_back(Seg(-3,8, 2,8));  buggy.push_back(Seg(2,8, 2,4));
    buggy.push_back(Seg(2,4, 7,4));   buggy.push_back(Seg(-2,12, 2,12));
    buggy.push_back(Seg(2,12, 2,6));  buggy.push_back(Seg(2,6, 7,6));
    buggy.push_back(Seg(25,4, 31,9)); buggy.push_back(Seg(25,6, 28,6));
    buggy.push_back(Seg(28,6, 28,12)); buggy.push_back(Seg(28,12, 31,12));

    const std::vector<wxPoint> bp = Run({
        wxPoint(7,9),  wxPoint(7,11), wxPoint(25,9),  wxPoint(25,11),
        wxPoint(-3,8), wxPoint(-2,12), wxPoint(31,9), wxPoint(31,12)});
    const std::vector<wxPoint> ap = Run({
        wxPoint(7,4),  wxPoint(7,6),  wxPoint(25,4),  wxPoint(25,6),
        wxPoint(-3,8), wxPoint(-2,12), wxPoint(31,9), wxPoint(31,12)});

    const std::vector<std::vector<int>> want = wnPortGroups(before, bp);
    Check(want == std::vector<std::vector<int>>({{0,4},{1,5},{2,6},{3,7}}),
          "B before: four independent nets", "");
    Check(wnJunctionCount(before) == 0, "B before: no junction dots", "");
    Check(wnPortGroups(buggy, ap) != want,
          "B bug reproduced: two input nets are shorted", "");
    Check(wnJunctionCount(buggy) == 2,
          "B bug reproduced: two phantom dots appear", "");
    Check(SlantCount(buggy) == 1,
          "B bug reproduced: a slanted wire is emitted", "");

    std::vector<wnMoveRun> runs(4);
    runs[0].pts = g1; runs[0].na = wxPoint(-3,8);  runs[0].nb = wxPoint(7,4);
    runs[1].pts = g2; runs[1].na = wxPoint(-2,12); runs[1].nb = wxPoint(7,6);
    runs[2].pts = g3; runs[2].na = wxPoint(25,4);  runs[2].nb = wxPoint(31,9);
    runs[3].pts = g4; runs[3].na = wxPoint(25,6);  runs[3].nb = wxPoint(31,12);

    const std::vector<std::vector<wxPoint>> chosen =
        wnChooseMovePaths(runs, std::vector<std::vector<wxPoint>>(),
                          before, bp, ap, std::vector<wxPoint>(), 3);
    const std::vector<std::vector<wxPoint>> after =
        Flatten(std::vector<std::vector<wxPoint>>(), chosen);

    Check(wnPortGroups(after, ap) == want,
          "B avoidance preserves all four nets", "");
    Check(wnJunctionCount(after) == 0,
          "B avoidance paints no phantom dots", "");
    Check(SlantCount(after) == 0,
          "B avoidance emits no slanted wire", "");
    bool allOrtho = true;
    for (size_t i = 0; i < chosen.size(); ++i) if (!Ortho(chosen[i])) allOrtho = false;
    Check(allOrtho, "B every chosen path is Manhattan", "");

    // Mutation guard: no avoidance -> short + slant come back.
    const std::vector<std::vector<wxPoint>> noAvoid =
        wnChooseMovePaths(runs, std::vector<std::vector<wxPoint>>(),
                          before, bp, ap, std::vector<wxPoint>(), -1);
    const std::vector<std::vector<wxPoint>> bad =
        Flatten(std::vector<std::vector<wxPoint>>(), noAvoid);
    Check(wnPortGroups(bad, ap) != want || SlantCount(bad) != 0,
          "B negative control: disabling avoidance reproduces the bug", "");
}

int main()
{
    TestNormalize();
    TestExtendPath();
    TestRoute();
    TestRouteAvoid();
    TestInsertJog();
    TestNets();
    TestCrossing();
    TestRelocate();
    TestDisplacement();
    TestRepairScenario();
    TestMoveTopologyGuard();
    TestStretchOrthogonality();
    TestAvoidanceCaseA();
    TestAvoidanceCaseB();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
