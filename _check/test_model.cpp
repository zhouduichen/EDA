// End to end smoke test for the model layer.
// Logisim semantics: one Wire = one segment, nets come from geometry.
#include "model.h"
#include "wirenet.h"

#include <cstdio>

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

int main()
{
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
    TestCrossingEval();

    printf("\n%s\n", g_fail ? "SOME CHECKS FAILED" : "all checks passed");
    return g_fail ? 1 : 0;
}
