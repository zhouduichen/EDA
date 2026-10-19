// End to end smoke test for the model layer.
// Logisim semantics: one Wire = one segment, nets come from geometry.
#include "model.h"
#include "wirenet.h"

#include <cstdio>
#include <crtdbg.h>

static int g_fail = 0;

static void Check(bool ok, const char* what)
{
    printf("%-55s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) ++g_fail;
}

static int AddComp(Circuit& c, ComponentType t, int x, int y, int inputs = 2)
{
    Component comp;
    comp.id = c.nextId++;
    comp.type = t;
    comp.gridPos = wxPoint(x, y);
    comp.inputs = inputs;
    comp.RebuildPorts();
    c.comps.push_back(comp);
    return comp.id;
}

static void AddSeg(Circuit& c, const wxPoint& a, const wxPoint& b,
                   int sc = -1, int sp = -1, int dc = -1, int dp = -1)
{
    Wire w;
    w.srcComp = sc; w.srcPort = sp;
    w.dstComp = dc; w.dstPort = dp;
    w.points.push_back(a);
    w.points.push_back(b);
    w.group = c.nextWireGroup++;
    c.wires.push_back(w);
}


static int AddSub(Circuit& c, const wxString& name, int x, int y)
{
    Component comp;
    comp.id = c.nextId++;
    comp.type = CT_SubCircuit;
    comp.subName = name;
    comp.gridPos = wxPoint(x, y);
    comp.RebuildPorts();      // resolved later, once the project pointer exists
    c.comps.push_back(comp);
    return comp.id;
}

// Wire up a project: point every circuit at the vector and re-resolve every
// sub-circuit instance.  Must be repeated after the vector is reallocated.
static void LinkProject(std::vector<Circuit>& cs)
{
    for (auto& c : cs) { c.project = &cs; c.LinkComponents(); }
    for (auto& c : cs) c.RefreshSubCircuits();
}


// ---------------------------------------------------------------------------
// Sub-circuits: a circuit used as a component.
// ---------------------------------------------------------------------------
static void TestSubCircuit()
{
    // INV: an inverter wrapped into its own circuit.
    Circuit inv;
    inv.name = wxT("INV");
    const int ip = AddComp(inv, CT_PinIn,  0, 0);
    const int ig = AddComp(inv, CT_Not,    4, 0);
    const int io = AddComp(inv, CT_PinOut, 9, 0);
    AddSeg(inv, wxPoint(2, 1), wxPoint(4, 1), ip, 0, ig, 0);
    AddSeg(inv, wxPoint(7, 1), wxPoint(9, 1), ig, 1, io, 0);

    // main: pin -> INV instance -> probe
    Circuit main;
    main.name = wxT("main");
    const int mp = AddComp(main, CT_PinIn,   0, 10);   // port (2,11)
    const int ms = AddSub  (main, wxT("INV"), 10, 10);
    const int mo = AddComp(main, CT_PinOut, 22, 10);   // port (22,11)
    AddSeg(main, wxPoint(2, 11),  wxPoint(12, 11), mp, 0, ms, 0);
    AddSeg(main, wxPoint(19, 11), wxPoint(22, 11), ms, 1, mo, 0);
    main.FindComponent(mp)->value = 1;

    std::vector<Circuit> proj;
    proj.push_back(inv);
    proj.push_back(main);
    LinkProject(proj);

    Circuit& mi = proj[1];
    Component* inst = mi.FindComponent(ms);
    Check(inst && inst->ports.size() == 2, "sub-circuit instance gets one port per pin");
    // The pins of a custom part must land ON THE EDGE of the box, otherwise the
    // user has to hunt for a connection point buried inside the rectangle.
    Check(inst && inst->ports[0].grid.x == inst->gridPos.x && !inst->ports[0].isOutput,
          "instance input port sits on the left edge");
    Check(inst && inst->ports[1].grid.x == inst->gridPos.x + inst->subW && inst->ports[1].isOutput,
          "instance output port sits on the right edge");
    Check(inst && inst->ports[0].grid.y > inst->gridPos.y &&
                  inst->ports[0].grid.y < inst->gridPos.y + inst->subH,
          "instance port lies strictly inside the box height");
    Check(inst && inst->ports[0].grid == wxPoint(10, 11) &&
                  inst->ports[1].grid == wxPoint(21, 11),
          "instance ports are the laid out edge coordinates");
    // The box is at least as large as the definition, so a wire the user lays
    // out in the parent circuit cannot run over the definition's own geometry.
    Check(inst && inst->subW == 11 && inst->subH == 2, "instance body is the layout box");

    EvaluateCircuit(mi, &proj);
    Check(mi.FindComponent(mo)->ports[0].value == LV_Zero, "INV(1) = 0 through the instance");

    mi.FindComponent(mp)->value = 0;
    EvaluateCircuit(mi, &proj);
    Check(mi.FindComponent(mo)->ports[0].value == LV_One, "INV(0) = 1 through the instance");

    // An instance whose definition is gone must not crash, just go unanswered.
    Circuit orphan;
    orphan.name = wxT("orphan");
    AddSub(orphan, wxT("NOPE"), 0, 0);
    std::vector<Circuit> proj2;
    proj2.push_back(orphan);
    LinkProject(proj2);
    Check(proj2[0].comps[0].subBroken, "unknown definition is reported as broken");
    EvaluateCircuit(proj2[0], &proj2);
    Check(true, "evaluating a broken instance does not crash");

    // Round trip of a whole project (definitions must survive).
    const wxString text = SerializeProject(proj);
    std::vector<Circuit> loaded;
    Check(DeserializeProject(text, loaded), "project file loads");
    Check(loaded.size() == 2, "both circuits survive the round trip");
    Check(loaded.size() == 2 && loaded[0].name == wxT("INV"), "circuit name preserved");
    bool hasSub = false;
    for (const Circuit& c : loaded)
        for (const Component& c2 : c.comps)
            if (c2.type == CT_SubCircuit && c2.subName == wxT("INV")) hasSub = true;
    Check(hasSub, "SUBNAME record survives the round trip");

    LinkProject(loaded);
    EvaluateCircuit(loaded[1], &loaded);
    Component* lo = loaded[1].FindComponent(mo);
    Check(lo && lo->ports[0].value == LV_One, "loaded project evaluates the same");

    // Deleting the definition takes the instances with it, wires included.
    std::vector<Circuit> del;
    Circuit def; def.name = wxT("INV");
    AddComp(def, CT_PinIn,  0, 0);
    AddComp(def, CT_PinOut, 9, 0);
    Circuit use; use.name = wxT("main");
    const int up = AddComp(use, CT_PinIn, 0, 11);
    const int us = AddSub  (use, wxT("INV"), 10, 10);
    AddSeg  (use, wxPoint(2, 11), wxPoint(12, 11), up, 0, us, 0);
    del.push_back(def); del.push_back(use);
    LinkProject(del);
    Check(del[1].comps.size() == 2, "instance present before deleting the definition");

    const int gone = RemoveSubCircuitInstances(del, wxT("INV"));
    Check(gone == 1, "one instance removed with its definition");
    Check(del[1].comps.size() == 1, "only the pin is left in the using circuit");
    Check(del[1].wires.empty(), "the wire that fed the instance is gone too");
    Check(RemoveSubCircuitInstances(del, wxT("NOPE")) == 0, "deleting an unknown part removes nothing");

    // A legacy single-circuit file still loads as a one-circuit project.
    std::vector<Circuit> legacyProj;
    wxString legacyText(wxT("EDA_CIRCUIT 1\n"));
    legacyText += wxT("COMP 1 PIN_IN 0 0 2 1 x\n");
    legacyText += wxT("WIRE 1 0 -1 -1 2 0 0 4 0 7\n");
    Check(DeserializeProject(legacyText, legacyProj), "legacy file loads as a project");
    Check(legacyProj.size() == 1, "legacy file becomes a single circuit");
}


// ---------------------------------------------------------------------------
// Crossings must not conduct: two wires that just pass through each other are
// two independent nets.
// ---------------------------------------------------------------------------
static void TestCrossingEval()
{
    Circuit c;
    // Horizontal: A(0,0) -> port (2,1) -> X(10,0) -> port (10,1)
    const int a  = AddComp(c, CT_PinIn,  0, 0);
    const int x  = AddComp(c, CT_PinOut, 10, 0);
    // Vertical:   B(4,-1) -> port (6,0) -> Y(6,7) -> port (6,8)
    const int b  = AddComp(c, CT_PinIn,  4, -1);
    const int y  = AddComp(c, CT_PinOut, 6, 7);
    AddSeg(c, wxPoint(2, 1), wxPoint(10, 1), a, 0, x, 0);
    AddSeg(c, wxPoint(6, 0), wxPoint(6, 8),  b, 0, y, 0);
    c.FindComponent(a)->value = 1;
    c.FindComponent(b)->value = 0;

    EvaluateCircuit(c);
    Check(c.FindComponent(x)->ports[0].value == LV_One,  "crossing: A still reaches X");
    Check(c.FindComponent(y)->ports[0].value == LV_Zero, "crossing: B does not leak into Y");

    // Marking the crossing as a connection makes the two drivers fight.
    c.junctions.push_back(wxPoint(6, 1));
    EvaluateCircuit(c);
    Check(c.FindComponent(x)->ports[0].value == LV_Unknown, "junction joins the crossing (X)");
    Check(c.FindComponent(y)->ports[0].value == LV_Unknown, "junction joins the crossing (Y)");

    // ...and the junction survives a save / load cycle.
    const wxString text = SerializeCircuit(c);
    Circuit loaded;
    DeserializeCircuit(text, loaded);
    bool kept = false;
    for (const wxPoint& p : loaded.junctions) if (p == wxPoint(6, 1)) kept = true;
    Check(kept, "junction survives the round trip");
}

// ---------------------------------------------------------------------------
// Custom parts: the pins of a definition are laid out ON THE EDGE of the
// instance box, and the expanded copy inside still reaches them.
// ---------------------------------------------------------------------------
static void TestSubCircuitPinLayout()
{
    // Pins deliberately NOT aligned with the gate: with the old rule (keep the
    // pin's position relative to the definition) they would be buried inside
    // the rectangle.
    Circuit def;
    def.name = wxT("AND2");
    const int d0 = AddComp(def, CT_PinIn,  0, 0);      // port (2,1)
    const int d1 = AddComp(def, CT_PinIn,  0, 8);      // port (2,9)
    const int dg = AddComp(def, CT_And,   20, 4);      // in (20,5)(20,7) out (22,6)
    const int dz = AddComp(def, CT_PinOut, 30, 6);     // port (30,7)
    AddSeg(def, wxPoint(2, 1),  wxPoint(20, 1), d0, 0);
    AddSeg(def, wxPoint(20, 1), wxPoint(20, 5), -1, -1, dg, 0);
    AddSeg(def, wxPoint(2, 9),  wxPoint(18, 9), d1, 0);
    AddSeg(def, wxPoint(18, 9), wxPoint(18, 7));
    AddSeg(def, wxPoint(18, 7), wxPoint(20, 7), -1, -1, dg, 1);
    AddSeg(def, wxPoint(22, 6), wxPoint(28, 6), dg, 2);
    AddSeg(def, wxPoint(28, 6), wxPoint(28, 7));
    AddSeg(def, wxPoint(28, 7), wxPoint(30, 7), -1, -1, dz, 0);

    Circuit top;
    top.name = wxT("main");
    const int ti = AddSub  (top, wxT("AND2"), 10, 20);

    std::vector<Circuit> proj;
    proj.push_back(def);
    proj.push_back(top);
    LinkProject(proj);

    Component* inst = proj[1].FindComponent(ti);
    Check(inst && inst->ports.size() == 3, "layout: one port per pin");
    if (!inst) return;

    bool inOnLeft = true, outOnRight = true, inside = true;
    for (const Port& p : inst->ports)
    {
        if (p.isOutput) outOnRight = outOnRight && (p.grid.x == inst->gridPos.x + inst->subW);
        else            inOnLeft  = inOnLeft  && (p.grid.x == inst->gridPos.x);
        if (p.grid.y <= inst->gridPos.y || p.grid.y >= inst->gridPos.y + inst->subH)
            inside = false;
    }
    Check(inOnLeft,   "layout: every input sits on the left edge");
    Check(outOnRight, "layout: every output sits on the right edge");
    Check(inside,     "layout: no port on a corner");
    Check(inst->subH >= 3, "layout: the box grows with the number of pins");
    Check(inst->ports[0].grid.y < inst->ports[1].grid.y,
          "layout: inputs keep the order they were drawn in");

    // Neighbouring pins must be two grid steps apart: one step apart would let
    // a wire arriving at one of them graze the other and short both nets.
    Check(inst->ports[1].grid.y - inst->ports[0].grid.y >= 2,
          "layout: neighbouring inputs are two grid steps apart");

    // The instance has to compute through the edge ports: 1 AND 1 -> 1.
    // Every wire below is a plain horizontal run laid out at the very same row
    // as the port it feeds, so nothing can T into the box by accident.
    const wxPoint in0   = inst->ports[0].grid;
    const wxPoint in1   = inst->ports[1].grid;
    const wxPoint outAt = inst->ports[2].grid;
    const int t0 = AddComp(proj[1], CT_PinIn,  0, in0.y - 1);
    const int t1 = AddComp(proj[1], CT_PinIn,  0, in1.y - 1);
    const wxPoint toPt = outAt + wxPoint(6, 0);
    const int to = AddComp(proj[1], CT_PinOut, toPt.x, toPt.y - 1);
    proj[1].FindComponent(t0)->value = 1;
    proj[1].FindComponent(t1)->value = 1;

    AddSeg(proj[1], wxPoint(2, in0.y), in0, t0, 0, ti, 0);
    AddSeg(proj[1], wxPoint(2, in1.y), in1, t1, 0, ti, 1);
    AddSeg(proj[1], outAt,              toPt,  ti, 2, to, 0);
    proj[1].RefreshWireEndpoints();

    EvaluateCircuit(proj[1], &proj);
    Check(proj[1].FindComponent(to) &&
          proj[1].FindComponent(to)->ports[0].value == LV_One,
          "layout: instance computes through the edge ports");

    // The two inputs must really be two inputs, not the same net twice.
    proj[1].FindComponent(t1)->value = 0;
    EvaluateCircuit(proj[1], &proj);
    Component* op = proj[1].FindComponent(to);
    Check(op && op->ports[0].value == LV_Zero,
          "layout: the second edge port is a separate net");

    // A definition with no pins is still a (pinless) box, not a crash.
    Circuit bare;
    bare.name = wxT("BARE");
    AddComp(bare, CT_And, 0, 0);
    Circuit user;
    user.name = wxT("u");
    AddSub(user, wxT("BARE"), 0, 0);
    std::vector<Circuit> p2;
    p2.push_back(bare);
    p2.push_back(user);
    LinkProject(p2);
    Check(p2[1].comps[0].ports.empty() && !p2[1].comps[0].subBroken,
          "layout: a definition without pins is a pinless box");
    EvaluateCircuit(p2[1], &p2);
    Check(true, "layout: a pinless instance does not crash");
}

// ---------------------------------------------------------------------------
// Custom parts UNDER SIMULATION.
//
// The layout tests above only look at coordinates.  These put the box to work:
// every input combination, several instances at once, a part inside a part,
// a part with two outputs, a part with a dead pin, and the pathological case
// of a circuit that (indirectly) instantiates itself.
// ---------------------------------------------------------------------------

static LogicValue Read(Circuit& c, int compId)
{
    Component* p = c.FindComponent(compId);
    if (!p || p->ports.empty()) return LV_Unknown;
    return p->ports[0].value;
}

// AND2: two inputs on the left, one output on the right, pins deliberately
// drawn nowhere near the gate -- the whole point of the edge layout.
static void MakeAnd2(Circuit& def)
{
    def.name = wxT("AND2");
    const int a = AddComp(def, CT_PinIn,  0, 0);     // port (2,1)
    const int b = AddComp(def, CT_PinIn,  0, 8);     // port (2,9)
    const int g = AddComp(def, CT_And,   20, 4);     // in (20,5)(20,7), out (22,6)
    const int z = AddComp(def, CT_PinOut,30, 6);     // port (30,7)
    AddSeg(def, wxPoint(2, 1),  wxPoint(20, 1), a, 0);
    AddSeg(def, wxPoint(20, 1), wxPoint(20, 5), -1, -1, g, 0);
    AddSeg(def, wxPoint(2, 9),  wxPoint(18, 9), b, 0);
    AddSeg(def, wxPoint(18, 9), wxPoint(18, 7));
    AddSeg(def, wxPoint(18, 7), wxPoint(20, 7), -1, -1, g, 1);
    AddSeg(def, wxPoint(22, 6), wxPoint(28, 6), g, 2);
    AddSeg(def, wxPoint(28, 6), wxPoint(28, 7));
    AddSeg(def, wxPoint(28, 7), wxPoint(30, 7), -1, -1, z, 0);
}

// Wire a top circuit up to one instance of a 3-port part (2 in, 1 out).
// Returns false when the instance did not expose three ports.
static bool WireUpAnd2(Circuit& top, int subId, int& p0, int& p1, int& po)
{
    Component* inst = top.FindComponent(subId);
    if (!inst || inst->ports.size() != 3) return false;

    const wxPoint i0 = inst->ports[0].grid;
    const wxPoint i1 = inst->ports[1].grid;
    const wxPoint o0 = inst->ports[2].grid;

    p0 = AddComp(top, CT_PinIn,  0, i0.y - 1);            // port (2, i0.y)
    p1 = AddComp(top, CT_PinIn,  0, i1.y - 1);            // port (2, i1.y)
    po = AddComp(top, CT_PinOut, o0.x + 4, o0.y - 1);     // port (o0.x+4, o0.y)

    AddSeg(top, wxPoint(2, i0.y), i0, p0, 0, subId, 0);
    AddSeg(top, wxPoint(2, i1.y), i1, p1, 0, subId, 1);
    AddSeg(top, o0, wxPoint(o0.x + 4, o0.y), subId, 2, po, 0);
    top.RefreshWireEndpoints();
    return true;
}

// NAND2 = an AND2 instance followed by a NOT.  Split in two on purpose: the
// components are created before the project pointer exists, so the wiring can
// only happen after LinkProject has resolved the nested instance's ports.
static void MakeNand2Comps(Circuit& n, int& a, int& b, int& z, int& s, int& nt)
{
    n.name = wxT("NAND2");
    a  = AddComp(n, CT_PinIn,   0, 0);     // port (2,1)
    b  = AddComp(n, CT_PinIn,   0, 20);    // port (2,21)
    z  = AddComp(n, CT_PinOut, 60, 24);    // port (60,25)
    s  = AddSub  (n, wxT("AND2"), 10, 20);
    nt = AddComp(n, CT_Not,    46, 24);    // in (46,25), out (49,25)
}

static bool WireNand2(Circuit& n, int a, int b, int z, int s, int nt)
{
    Component* inner = n.FindComponent(s);
    if (!inner || inner->ports.size() != 3) return false;
    AddSeg(n, wxPoint(2, 1),  wxPoint(10, 1), a, 0);
    AddSeg(n, wxPoint(10, 1), inner->ports[0].grid, -1, -1, s, 0);
    AddSeg(n, wxPoint(2, 21), wxPoint(8, 21), b, 0);
    AddSeg(n, wxPoint(8, 21), wxPoint(8, 26));
    AddSeg(n, wxPoint(8, 26), inner->ports[1].grid, -1, -1, s, 1);
    AddSeg(n, inner->ports[2].grid, wxPoint(46, 25), s, 2, nt, 0);
    AddSeg(n, wxPoint(49, 25), wxPoint(60, 25), nt, 1, z, 0);
    n.RefreshWireEndpoints();
    return true;
}

static void TestSubCircuitEval()
{
    // ---- 1. one instance: every input combination --------------------------
    {
        Circuit def; MakeAnd2(def);
        Circuit top; top.name = wxT("main");
        const int s = AddSub(top, wxT("AND2"), 10, 20);

        std::vector<Circuit> proj;
        proj.push_back(def);
        proj.push_back(top);
        LinkProject(proj);

        Circuit& T = proj[1];
        int p0 = -1, p1 = -1, po = -1;
        if (!WireUpAnd2(T, s, p0, p1, po))
        {
            Check(false, "sub eval: the instance exposes three ports");
            return;
        }

        const int cases[4][2] = { {1,1}, {1,0}, {0,1}, {0,0} };
        const LogicValue want[4] = { LV_One, LV_Zero, LV_Zero, LV_Zero };
        bool all = true;
        for (int k = 0; k < 4; ++k)
        {
            T.FindComponent(p0)->value = cases[k][0];
            T.FindComponent(p1)->value = cases[k][1];
            EvaluateCircuit(T, &proj);
            if (Read(T, po) != want[k]) all = false;
        }
        Check(all, "sub eval: AND2 part computes all four input combinations");

        // The instance's own port is written back, so the box can be probed
        // and the value is what the user sees on the wire.
        Check(T.FindComponent(s)->ports[2].value != LV_Unknown,
              "sub eval: the instance output port carries a value");
    }

    // ---- 2. two instances of the same part stay independent ----------------
    {
        Circuit def; MakeAnd2(def);
        Circuit top; top.name = wxT("main");
        const int s1 = AddSub(top, wxT("AND2"), 10, 20);
        const int s2 = AddSub(top, wxT("AND2"), 10, 60);

        std::vector<Circuit> proj;
        proj.push_back(def);
        proj.push_back(top);
        LinkProject(proj);

        Circuit& T = proj[1];
        int a[2], b[2], o[2];
        if (!WireUpAnd2(T, s1, a[0], b[0], o[0]) ||
            !WireUpAnd2(T, s2, a[1], b[1], o[1]))
        {
            Check(false, "sub eval: two instances both expose three ports");
            return;
        }

        T.FindComponent(a[0])->value = 1;
        T.FindComponent(b[0])->value = 1;
        T.FindComponent(a[1])->value = 1;
        T.FindComponent(b[1])->value = 0;
        EvaluateCircuit(T, &proj);

        Check(Read(T, o[0]) == LV_One,  "sub eval: instance 1 (1 AND 1) = 1");
        Check(Read(T, o[1]) == LV_Zero, "sub eval: instance 2 (1 AND 0) = 0, not shorted to 1");
    }

    // ---- 3. a part inside a part -------------------------------------------
    {
        Circuit and2; MakeAnd2(and2);

        Circuit nand;
        int na, nb, nz, ns, nn;
        MakeNand2Comps(nand, na, nb, nz, ns, nn);

        Circuit top; top.name = wxT("main");
        const int ts = AddSub(top, wxT("NAND2"), 10, 60);

        std::vector<Circuit> proj;
        proj.push_back(and2);
        proj.push_back(nand);
        proj.push_back(top);
        LinkProject(proj);

        Circuit& N = proj[1];
        if (!WireNand2(N, na, nb, nz, ns, nn))
        {
            Check(false, "sub eval: the nested instance resolves three ports");
            return;
        }

        Circuit& T = proj[2];
        int p0 = -1, p1 = -1, po = -1;
        if (!WireUpAnd2(T, ts, p0, p1, po))
        {
            Check(false, "sub eval: the outer instance exposes three ports");
            return;
        }
        T.FindComponent(p0)->value = 1;
        T.FindComponent(p1)->value = 1;
        EvaluateCircuit(T, &proj);
        Check(Read(T, po) == LV_Zero, "sub eval: nested part NOT(AND(1,1)) = 0");

        T.FindComponent(p1)->value = 0;
        EvaluateCircuit(T, &proj);
        Check(Read(T, po) == LV_One,  "sub eval: nested part NOT(AND(1,0)) = 1");
    }

    // ---- 4. a part with two outputs ----------------------------------------
    {
        Circuit def; def.name = wxT("SPLIT");
        const int a  = AddComp(def, CT_PinIn,   0, 0);     // port (2,1)
        const int bf = AddComp(def, CT_Buffer, 10, 0);     // in (10,1), out (13,1)
        const int nt = AddComp(def, CT_Not,    10, 8);     // in (10,9), out (13,9)
        const int z1 = AddComp(def, CT_PinOut, 20, 0);     // port (20,1)
        const int z2 = AddComp(def, CT_PinOut, 20, 8);     // port (20,9)
        AddSeg(def, wxPoint(2, 1),  wxPoint(6, 1),  a, 0);
        AddSeg(def, wxPoint(6, 1),  wxPoint(10, 1), -1, -1, bf, 0);
        AddSeg(def, wxPoint(6, 1),  wxPoint(6, 9));
        AddSeg(def, wxPoint(6, 9),  wxPoint(10, 9), -1, -1, nt, 0);
        AddSeg(def, wxPoint(13, 1), wxPoint(20, 1), bf, 1, z1, 0);
        AddSeg(def, wxPoint(13, 9), wxPoint(20, 9), nt, 1, z2, 0);

        Circuit top; top.name = wxT("main");
        const int s = AddSub(top, wxT("SPLIT"), 10, 20);

        std::vector<Circuit> proj;
        proj.push_back(def);
        proj.push_back(top);
        LinkProject(proj);

        Circuit& T = proj[1];
        Component* inst = T.FindComponent(s);
        if (!inst || inst->ports.size() != 3)
        {
            Check(false, "sub eval: a two-output part exposes three ports");
            return;
        }
        const wxPoint i0  = inst->ports[0].grid;
        const wxPoint oA  = inst->ports[1].grid;
        const wxPoint oB  = inst->ports[2].grid;
        const int pi  = AddComp(T, CT_PinIn,  0, i0.y - 1);
        const int pa  = AddComp(T, CT_PinOut, oA.x + 4, oA.y - 1);
        const int pb  = AddComp(T, CT_PinOut, oB.x + 4, oB.y - 1);
        AddSeg(T, wxPoint(2, i0.y), i0, pi, 0, s, 0);
        AddSeg(T, oA, wxPoint(oA.x + 4, oA.y), s, 1, pa, 0);
        AddSeg(T, oB, wxPoint(oB.x + 4, oB.y), s, 2, pb, 0);
        T.RefreshWireEndpoints();

        T.FindComponent(pi)->value = 1;
        EvaluateCircuit(T, &proj);
        Check(Read(T, pa) == LV_One,  "sub eval: two outputs, straight = 1");
        Check(Read(T, pb) == LV_Zero, "sub eval: two outputs, inverted = 0");

        T.FindComponent(pi)->value = 0;
        EvaluateCircuit(T, &proj);
        Check(Read(T, pa) == LV_Zero, "sub eval: two outputs follow the input (straight)");
        Check(Read(T, pb) == LV_One,  "sub eval: two outputs follow the input (inverted)");
    }

    // ---- 5. a pin of the definition that is not wired up -------------------
    // The port still exists on the box and the user can still plug a wire into
    // it; it simply leads nowhere.  The ports that ARE wired must keep working.
    {
        Circuit def; def.name = wxT("HALF");
        const int a  = AddComp(def, CT_PinIn,   0, 0);     // port (2,1)
        AddComp(def, CT_PinIn, 0, 8);                      // port (2,9): never wired
        const int bf = AddComp(def, CT_Buffer, 10, 0);     // in (10,1), out (13,1)
        const int z  = AddComp(def, CT_PinOut, 20, 0);     // port (20,1)
        AddSeg(def, wxPoint(2, 1),  wxPoint(10, 1),  a, 0, bf, 0);
        AddSeg(def, wxPoint(13, 1), wxPoint(20, 1), bf, 1, z, 0);

        Circuit top; top.name = wxT("main");
        const int s = AddSub(top, wxT("HALF"), 10, 20);

        std::vector<Circuit> proj;
        proj.push_back(def);
        proj.push_back(top);
        LinkProject(proj);

        Circuit& T = proj[1];
        Component* inst = T.FindComponent(s);
        if (!inst || inst->ports.size() != 3)
        {
            Check(false, "sub eval: the dead-pin part still exposes three ports");
            return;
        }
        const wxPoint i0 = inst->ports[0].grid;
        const wxPoint i1 = inst->ports[1].grid;
        const wxPoint o0 = inst->ports[2].grid;
        const int pi = AddComp(T, CT_PinIn,  0, i0.y - 1);
        const int pd = AddComp(T, CT_PinIn,  0, i1.y - 1);
        const int po = AddComp(T, CT_PinOut, o0.x + 4, o0.y - 1);
        AddSeg(T, wxPoint(2, i0.y), i0, pi, 0, s, 0);
        AddSeg(T, wxPoint(2, i1.y), i1, pd, 0, s, 1);
        AddSeg(T, o0, wxPoint(o0.x + 4, o0.y), s, 2, po, 0);
        T.RefreshWireEndpoints();

        T.FindComponent(pi)->value = 1;
        T.FindComponent(pd)->value = 0;
        EvaluateCircuit(T, &proj);
        Check(Read(T, po) == LV_One, "sub eval: an unwired definition pin does not break the rest");

        T.FindComponent(pi)->value = 0;
        T.FindComponent(pd)->value = 1;
        EvaluateCircuit(T, &proj);
        Check(Read(T, po) == LV_Zero, "sub eval: the dead pin really is dead");
    }

    // ---- 6. a part placed at the origin (the expansion shift can go negative)
    {
        Circuit def; MakeAnd2(def);
        Circuit top; top.name = wxT("main");
        const int s = AddSub(top, wxT("AND2"), 0, 0);

        std::vector<Circuit> proj;
        proj.push_back(def);
        proj.push_back(top);
        LinkProject(proj);

        Circuit& T = proj[1];
        int p0 = -1, p1 = -1, po = -1;
        if (!WireUpAnd2(T, s, p0, p1, po))
        {
            Check(false, "sub eval: an instance at the origin exposes three ports");
            return;
        }
        T.FindComponent(p0)->value = 1;
        T.FindComponent(p1)->value = 1;
        EvaluateCircuit(T, &proj);
        Check(Read(T, po) == LV_One, "sub eval: an instance at the origin still computes");
    }

    // ---- 7. a circuit that (indirectly) instantiates itself ----------------
    // It must not hang, must not blow the stack, and must simply not answer.
    {
        Circuit a; a.name = wxT("A");
        AddSub(a, wxT("B"), 0, 0);
        Circuit b; b.name = wxT("B");
        AddSub(b, wxT("A"), 0, 0);

        std::vector<Circuit> proj;
        proj.push_back(a);
        proj.push_back(b);
        LinkProject(proj);
        EvaluateCircuit(proj[0], &proj);
        Check(true, "sub eval: mutual recursion terminates instead of hanging");
    }

    // ---- 8. three levels of nesting ----------------------------------------
    {
        Circuit and2; MakeAnd2(and2);

        Circuit nand; nand.name = wxT("NAND2");
        int na, nb, nz, ns, nn;
        MakeNand2Comps(nand, na, nb, nz, ns, nn);

        // AND3 = NOT(NAND2(a,b)) == AND(a,b), i.e. a part inside a part inside
        // a part.  The deepest one is two expansions away from the top.
        Circuit deep; deep.name = wxT("AND3");
        const int da = AddComp(deep, CT_PinIn,   0, 0);    // port (2,1)
        const int db = AddComp(deep, CT_PinIn,   0, 20);   // port (2,21)
        const int dz = AddComp(deep, CT_PinOut, 90, 34);   // port (90,35)
        const int ds = AddSub  (deep, wxT("NAND2"), 10, 20);
        const int dn = AddComp(deep, CT_Not,    76, 34);   // in (76,35), out (79,35)

        Circuit top; top.name = wxT("main");
        const int ts = AddSub(top, wxT("AND3"), 10, 60);

        std::vector<Circuit> proj;
        proj.push_back(and2);
        proj.push_back(nand);
        proj.push_back(deep);
        proj.push_back(top);
        LinkProject(proj);

        WireNand2(proj[1], na, nb, nz, ns, nn);

        Circuit& D = proj[2];
        Component* inner = D.FindComponent(ds);
        if (!inner || inner->ports.size() != 3)
        {
            Check(false, "sub eval: the level-2 instance resolves three ports");
            return;
        }
        AddSeg(D, wxPoint(2, 1),  wxPoint(10, 1), da, 0);
        AddSeg(D, wxPoint(10, 1), inner->ports[0].grid, -1, -1, ds, 0);
        AddSeg(D, wxPoint(2, 21), wxPoint(8, 21),  db, 0);
        AddSeg(D, wxPoint(8, 21), wxPoint(8, inner->ports[1].grid.y));
        AddSeg(D, wxPoint(8, inner->ports[1].grid.y), inner->ports[1].grid, -1, -1, ds, 1);
        AddSeg(D, inner->ports[2].grid, wxPoint(76, 35), ds, 2, dn, 0);
        AddSeg(D, wxPoint(79, 35), wxPoint(90, 35), dn, 1, dz, 0);
        D.RefreshWireEndpoints();

        Circuit& T = proj[3];
        int p0 = -1, p1 = -1, po = -1;
        if (!WireUpAnd2(T, ts, p0, p1, po))
        {
            Check(false, "sub eval: the level-3 instance exposes three ports");
            return;
        }
        T.FindComponent(p0)->value = 1;
        T.FindComponent(p1)->value = 1;
        EvaluateCircuit(T, &proj);
        Check(Read(T, po) == LV_One, "sub eval: three levels deep, 1 AND 1 = 1");

        T.FindComponent(p1)->value = 0;
        EvaluateCircuit(T, &proj);
        Check(Read(T, po) == LV_Zero, "sub eval: three levels deep, 1 AND 0 = 0");
    }

    // ---- 9. two instances of the same part INSIDE one definition -----------
    {
        Circuit and2; MakeAnd2(and2);

        // DUAL: out1 = AND(a,b), out2 = AND(c,d).  Four inputs, two outputs.
        Circuit dual; dual.name = wxT("DUAL");
        const int a = AddComp(dual, CT_PinIn,  0, 0);      // (2,1)
        const int b = AddComp(dual, CT_PinIn,  0, 8);      // (2,9)
        const int c = AddComp(dual, CT_PinIn,  0, 40);     // (2,41)
        const int d = AddComp(dual, CT_PinIn,  0, 48);     // (2,49)
        const int s1 = AddSub  (dual, wxT("AND2"), 10, 20);
        const int s2 = AddSub  (dual, wxT("AND2"), 10, 60);
        const int z1 = AddComp(dual, CT_PinOut, 50, 24);   // (50,25)
        const int z2 = AddComp(dual, CT_PinOut, 50, 64);   // (50,65)

        Circuit top; top.name = wxT("main");
        const int ts = AddSub(top, wxT("DUAL"), 10, 100);

        std::vector<Circuit> proj;
        proj.push_back(and2);
        proj.push_back(dual);
        proj.push_back(top);
        LinkProject(proj);

        Circuit& D = proj[1];
        Component* in1 = D.FindComponent(s1);
        Component* in2 = D.FindComponent(s2);
        if (!in1 || !in2 || in1->ports.size() != 3 || in2->ports.size() != 3)
        {
            Check(false, "sub eval: both nested instances resolve three ports");
            return;
        }
        AddSeg(D, wxPoint(2, 1),  wxPoint(10, 1), a, 0);
        AddSeg(D, wxPoint(10, 1), in1->ports[0].grid, -1, -1, s1, 0);
        AddSeg(D, wxPoint(2, 9),  wxPoint(8, 9), b, 0);
        AddSeg(D, wxPoint(8, 9),  wxPoint(8, in1->ports[1].grid.y));
        AddSeg(D, wxPoint(8, in1->ports[1].grid.y), in1->ports[1].grid, -1, -1, s1, 1);
        AddSeg(D, wxPoint(2, 41), wxPoint(10, 41), c, 0);
        AddSeg(D, wxPoint(10, 41), in2->ports[0].grid, -1, -1, s2, 0);
        AddSeg(D, wxPoint(2, 49), wxPoint(8, 49), d, 0);
        AddSeg(D, wxPoint(8, 49), wxPoint(8, in2->ports[1].grid.y));
        AddSeg(D, wxPoint(8, in2->ports[1].grid.y), in2->ports[1].grid, -1, -1, s2, 1);
        AddSeg(D, in1->ports[2].grid, wxPoint(50, 25), s1, 2, z1, 0);
        AddSeg(D, in2->ports[2].grid, wxPoint(50, 65), s2, 2, z2, 0);
        D.RefreshWireEndpoints();

        Circuit& T = proj[2];
        Component* inst = T.FindComponent(ts);
        if (!inst || inst->ports.size() != 6)
        {
            Check(false, "sub eval: the DUAL instance exposes six ports");
            return;
        }
        // Snapshot the pin positions first: AddComp() pushes into T.comps and
        // may reallocate it, which would dangle `inst` (and any Port inside it).
        wxPoint inPorts[4], outPorts[2];
        for (int k = 0; k < 4; ++k) inPorts[k]  = inst->ports[k].grid;
        for (int k = 0; k < 2; ++k) outPorts[k] = inst->ports[4 + k].grid;

        int pi[4], po[2];
        for (int k = 0; k < 4; ++k)
        {
            pi[k] = AddComp(T, CT_PinIn, 0, inPorts[k].y - 1);
            AddSeg(T, wxPoint(2, inPorts[k].y), inPorts[k], pi[k], 0, ts, k);
        }
        for (int k = 0; k < 2; ++k)
        {
            po[k] = AddComp(T, CT_PinOut, outPorts[k].x + 4, outPorts[k].y - 1);
            AddSeg(T, outPorts[k], wxPoint(outPorts[k].x + 4, outPorts[k].y),
                   ts, 4 + k, po[k], 0);
        }
        T.RefreshWireEndpoints();

        T.FindComponent(pi[0])->value = 1;
        T.FindComponent(pi[1])->value = 1;
        T.FindComponent(pi[2])->value = 1;
        T.FindComponent(pi[3])->value = 0;
        EvaluateCircuit(T, &proj);
        Check(Read(T, po[0]) == LV_One,  "sub eval: two nested copies, first = 1");
        Check(Read(T, po[1]) == LV_Zero, "sub eval: two nested copies stay independent");
    }

    // ---- 10. simulating the definition circuit itself ----------------------
    // The user looks at the part's own tab and pokes its pins; the nested
    // instance inside it has to work there too.
    {
        Circuit and2; MakeAnd2(and2);
        Circuit nand; nand.name = wxT("NAND2");
        int na, nb, nz, ns, nn;
        MakeNand2Comps(nand, na, nb, nz, ns, nn);

        std::vector<Circuit> proj;
        proj.push_back(and2);
        proj.push_back(nand);
        LinkProject(proj);
        WireNand2(proj[1], na, nb, nz, ns, nn);

        Circuit& N = proj[1];
        N.FindComponent(na)->value = 1;
        N.FindComponent(nb)->value = 1;
        EvaluateCircuit(N, &proj);
        Check(Read(N, nz) == LV_Zero, "sub eval: the definition's own tab simulates, 1 NAND 1 = 0");

        N.FindComponent(nb)->value = 0;
        EvaluateCircuit(N, &proj);
        Check(Read(N, nz) == LV_One,  "sub eval: the definition's own tab follows its pins");
    }

    // ---- 11. two drivers fighting over one port of a part ------------------
    {
        Circuit def; MakeAnd2(def);
        Circuit top; top.name = wxT("main");
        const int s = AddSub(top, wxT("AND2"), 10, 20);
        std::vector<Circuit> proj;
        proj.push_back(def);
        proj.push_back(top);
        LinkProject(proj);

        Circuit& T = proj[1];
        int p0 = -1, p1 = -1, po = -1;
        if (!WireUpAnd2(T, s, p0, p1, po)) { Check(false, "sub eval: conflict setup"); return; }

        // A second pin driving the very same port of the box: come in from four
        // rows above so the run does not disturb the other input's wire.
        const wxPoint ip = T.FindComponent(s)->ports[0].grid;
        const int extra = AddComp(T, CT_PinIn, 0, ip.y - 5);      // port (2, ip.y-4)
        AddSeg(T, wxPoint(2, ip.y - 4), wxPoint(ip.x, ip.y - 4), extra, 0);
        AddSeg(T, wxPoint(ip.x, ip.y - 4), ip, -1, -1, s, 0);
        T.RefreshWireEndpoints();

        T.FindComponent(p0)->value    = 1;
        T.FindComponent(extra)->value = 0;
        T.FindComponent(p1)->value    = 1;
        EvaluateCircuit(T, &proj);
        Check(Read(T, po) == LV_Unknown, "sub eval: two drivers on one port = unknown");
    }

    // ---- 12. save / load round trip of a working custom part ----------------
    {
        Circuit def; MakeAnd2(def);
        Circuit top; top.name = wxT("main");
        const int s = AddSub(top, wxT("AND2"), 10, 20);
        std::vector<Circuit> proj;
        proj.push_back(def);
        proj.push_back(top);
        LinkProject(proj);

        int p0 = -1, p1 = -1, po = -1;
        WireUpAnd2(proj[1], s, p0, p1, po);
        proj[1].FindComponent(p0)->value = 1;
        proj[1].FindComponent(p1)->value = 1;

        const wxString text = SerializeProject(proj);
        std::vector<Circuit> loaded;
        Check(DeserializeProject(text, loaded), "sub eval: project with a part saves and loads");
        Check(loaded.size() == 2, "sub eval: both circuits survive");
        LinkProject(loaded);
        EvaluateCircuit(loaded[1], &loaded);
        Check(Read(loaded[1], po) == LV_One, "sub eval: the loaded part computes the same");
    }

    // ---- 13. a part used as a building block, not just as a probe ----------
    // The output of the box has to drive ordinary logic in the parent...
    {
        Circuit def; MakeAnd2(def);
        Circuit top; top.name = wxT("main");
        const int s = AddSub(top, wxT("AND2"), 10, 20);

        std::vector<Circuit> proj;
        proj.push_back(def);
        proj.push_back(top);
        LinkProject(proj);

        Circuit& T = proj[1];
        Component* inst = T.FindComponent(s);
        if (!inst || inst->ports.size() != 3)
        {
            Check(false, "sub eval: chained part exposes three ports");
            return;
        }
        const wxPoint i0 = inst->ports[0].grid;
        const wxPoint i1 = inst->ports[1].grid;
        const wxPoint o0 = inst->ports[2].grid;

        const int p0 = AddComp(T, CT_PinIn,  0, i0.y - 1);
        const int p1 = AddComp(T, CT_PinIn,  0, i1.y - 1);
        const int nt = AddComp(T, CT_Not,   o0.x + 2, o0.y - 1);  // in (o0.x+2,o0.y)
        const int po = AddComp(T, CT_PinOut, o0.x + 7, o0.y - 1); // port (o0.x+7,o0.y)

        AddSeg(T, wxPoint(2, i0.y), i0, p0, 0, s, 0);
        AddSeg(T, wxPoint(2, i1.y), i1, p1, 0, s, 1);
        AddSeg(T, o0, wxPoint(o0.x + 2, o0.y), s, 2, nt, 0);
        AddSeg(T, wxPoint(o0.x + 5, o0.y), wxPoint(o0.x + 7, o0.y), nt, 1, po, 0);
        T.RefreshWireEndpoints();

        T.FindComponent(p0)->value = 1;
        T.FindComponent(p1)->value = 1;
        EvaluateCircuit(T, &proj);
        Check(Read(T, po) == LV_Zero, "sub eval: part output drives parent NOT (1 AND 1)");

        T.FindComponent(p1)->value = 0;
        EvaluateCircuit(T, &proj);
        Check(Read(T, po) == LV_One, "sub eval: part output drives parent NOT (1 AND 0)");
    }

    // ... and ordinary logic in the parent has to drive the box's input.
    {
        Circuit def; MakeAnd2(def);
        Circuit top; top.name = wxT("main");
        const int s = AddSub(top, wxT("AND2"), 14, 20);

        std::vector<Circuit> proj;
        proj.push_back(def);
        proj.push_back(top);
        LinkProject(proj);

        Circuit& T = proj[1];
        Component* inst = T.FindComponent(s);
        if (!inst || inst->ports.size() != 3)
        {
            Check(false, "sub eval: driven part exposes three ports");
            return;
        }
        const wxPoint i0 = inst->ports[0].grid;
        const wxPoint i1 = inst->ports[1].grid;
        const wxPoint o0 = inst->ports[2].grid;

        const int pi = AddComp(T, CT_PinIn, 0, i0.y - 1);
        const int nt = AddComp(T, CT_Not,   4, i0.y - 1);       // in (4,i0.y), out (7,i0.y)
        const int p1 = AddComp(T, CT_PinIn, 0, i1.y - 1);
        const int po = AddComp(T, CT_PinOut, o0.x + 4, o0.y - 1);

        AddSeg(T, wxPoint(2, i0.y), wxPoint(4, i0.y), pi, 0, nt, 0);
        AddSeg(T, wxPoint(7, i0.y), i0, nt, 1, s, 0);
        AddSeg(T, wxPoint(2, i1.y), i1, p1, 0, s, 1);
        AddSeg(T, o0, wxPoint(o0.x + 4, o0.y), s, 2, po, 0);
        T.RefreshWireEndpoints();

        T.FindComponent(pi)->value = 0;      // NOT(0) = 1 -> AND input high
        T.FindComponent(p1)->value = 1;
        EvaluateCircuit(T, &proj);
        Check(Read(T, po) == LV_One, "sub eval: parent NOT drives part input (NOT 0 AND 1)");

        T.FindComponent(p1)->value = 0;
        EvaluateCircuit(T, &proj);
        Check(Read(T, po) == LV_Zero, "sub eval: parent NOT drives part input (NOT 0 AND 0)");
    }
}

int main()
{
    // Unbuffered so a hang localizes to the last printed check.  Assertions go
    // to stderr instead of a modal dialog, otherwise a failed check looks like
    // a hang when the process is started from a script.
    setvbuf(stdout, nullptr, _IONBF, 0);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);

    Circuit c;

    // PinIn(0,0) -> AND(6,0) -> PinOut(12,0)
    const int pinId  = AddComp(c, CT_PinIn,  0, 0);
    const int gateId = AddComp(c, CT_And,    6, 0);
    const int outId  = AddComp(c, CT_PinOut, 12, 0);
    c.FindComponent(pinId)->value = 1;

    Check(c.FindComponent(pinId)->ports.size() == 1 &&
          c.FindComponent(pinId)->ports[0].grid == wxPoint(2, 1), "pin port position");
    Check(c.FindComponent(gateId)->ports.size() == 3 &&
          c.FindComponent(gateId)->ports[2].grid == wxPoint(8, 2), "and gate output port");

    // Horizontal trunk, then a branch that leaves it at (4,1) and turns.
    AddSeg(c, wxPoint(2, 1), wxPoint(6, 1), pinId, 0, gateId, 0);
    AddSeg(c, wxPoint(4, 1), wxPoint(4, 3));
    AddSeg(c, wxPoint(4, 3), wxPoint(6, 3), -1, -1, gateId, 1);
    // Z shaped run from the gate output to the probe.
    AddSeg(c, wxPoint(8, 2),  wxPoint(10, 2), gateId, 2);
    AddSeg(c, wxPoint(10, 2), wxPoint(10, 1));
    AddSeg(c, wxPoint(10, 1), wxPoint(12, 1), -1, -1, outId, 0);

    std::vector<std::vector<wxPoint>> polys;
    for (const Wire& w : c.wires) polys.push_back(w.points);
    std::vector<wxPoint> portPts;
    for (auto& comp : c.comps)
        for (auto& p : comp.ports) portPts.push_back(p.grid);
    std::vector<wnNet> nets = wnBuildNets(polys, portPts);

    Check(nets.size() == 2, "six segments form two nets");
    bool hasJunction = false;
    for (const wnNet& n : nets)
        for (const wxPoint& p : n.junctions)
            if (p == wxPoint(4, 1)) hasJunction = true;
    Check(hasJunction, "branch point (4,1) is a junction");

    EvaluateCircuit(c);
    Check(c.FindComponent(pinId)->ports[0].value == LV_One, "input pin drives 1");
    Check(c.FindComponent(gateId)->ports[0].value == LV_One, "AND input 0 = 1");
    Check(c.FindComponent(gateId)->ports[1].value == LV_One, "AND input 1 = 1 (branch)");
    Check(c.FindComponent(gateId)->ports[2].value == LV_One, "AND output = 1");
    Check(c.FindComponent(outId)->ports[0].value == LV_One, "output pin reads 1");

    c.FindComponent(pinId)->value = 0;
    EvaluateCircuit(c);
    Check(c.FindComponent(gateId)->ports[0].value == LV_Zero, "AND input follows the pin");
    Check(c.FindComponent(outId)->ports[0].value == LV_Zero, "output pin reads 0");

    // Round trip through the file format.
    const wxString text = SerializeCircuit(c);
    Circuit loaded;
    Check(DeserializeCircuit(text, loaded), "deserialize");
    Check(loaded.wires.size() == 6, "all six segments survive the round trip");
    bool allTwo = true;
    for (const Wire& w : loaded.wires) if (w.points.size() != 2) allTwo = false;
    Check(allTwo, "every stored wire is a single segment");
    EvaluateCircuit(loaded);
    Component* lp = loaded.FindComponent(outId);
    Check(lp && lp->ports[0].value == LV_Zero, "loaded circuit evaluates the same");

    // Backward compatibility: an old file may keep a whole polyline inside one
    // WIRE record; it has to be split into segments on load.
    Circuit legacy;
    wxString legacyText(wxT("EDA_CIRCUIT 1\n"));
    legacyText += wxT("COMP 1 PIN_IN 0 0 2 1 x\n");
    legacyText += wxT("WIRE 1 0 -1 -1 4 0 0 4 0 8 0 8 4 7\n");
    Check(DeserializeCircuit(legacyText, legacy), "legacy file loads");
    Check(legacy.wires.size() == 3, "legacy polyline split into 3 segments");

    // A floating gate input must stay unknown.
    Circuit c2;
    const int g2 = AddComp(c2, CT_And, 0, 0);
    const int o2 = AddComp(c2, CT_PinOut, 6, 0);
    AddSeg(c2, wxPoint(2, 2), wxPoint(2, 1), g2, 2);
    AddSeg(c2, wxPoint(2, 1), wxPoint(6, 1), -1, -1, o2, 0);
    EvaluateCircuit(c2);
    Check(c2.FindComponent(g2)->ports[2].value == LV_Unknown, "floating input stays unknown");

    TestSubCircuit();
    TestSubCircuitPinLayout();
    TestSubCircuitEval();
    TestCrossingEval();

    printf("\n%s\n", g_fail ? "SOME CHECKS FAILED" : "all checks passed");
    return g_fail ? 1 : 0;
}
