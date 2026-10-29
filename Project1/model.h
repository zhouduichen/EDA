///////////////////////////////////////////////////////////////////////////
// model.h -- data model for the digital logic circuit editor
//
// This file is part of the "kernel" built on top of the wxFormBuilder
// generated framework (eda.h / eda.cpp). It is independent of the GUI.
///////////////////////////////////////////////////////////////////////////

#ifndef EDA_MODEL_H
#define EDA_MODEL_H

#include <wx/string.h>
#include <wx/gdicmn.h>
#include <algorithm>
#include <vector>

// Grid resolution in pixels (at zoom 1.0).  Components snap to this grid and
// every port lies on a grid intersection.
const int GRID = 20;

enum ComponentType
{
    CT_And = 0,
    CT_Or,
    CT_Not,
    CT_Nand,
    CT_Nor,
    CT_Xor,
    CT_Xnor,
    CT_Buffer,
    CT_PinIn,       // input pin: a switch that drives a value into the circuit
    CT_PinOut,      // output pin: a probe that displays a value
    CT_SubCircuit   // an instance of another circuit of the same project
};

enum LogicValue
{
    LV_Zero = 0,
    LV_One  = 1,
    LV_Unknown = 2
};

// A single connection point of a component, always on a grid intersection.
struct Port
{
    wxPoint    grid;        // absolute grid coordinates
    bool       isOutput;    // true = drives a wire (source), false = reads (sink)
    LogicValue value;       // current logic value (used during simulation)

    Port() : grid(0, 0), isOutput(false), value(LV_Unknown) {}
    Port(const wxPoint& g, bool out) : grid(g), isOutput(out), value(LV_Unknown) {}
};

struct Component
{
    int           id = 0;
    ComponentType type = CT_And;
    wxPoint       gridPos;  // top-left grid coordinate of the bounding box
    int           inputs = 2;   // number of inputs (logic gates only)
    int           value  = 0;   // PIN_IN value (0 / 1)
    wxString      label;
    bool          selected = false;   // runtime only (not serialized)
    std::vector<Port> ports;

    // CT_SubCircuit only -----------------------------------------------------
    wxString subName;            // which circuit of the project to instantiate
    int      subW = 0;           // body size in grid units, cached from it
    int      subH = 0;
    bool     subBroken = false;  // the referenced circuit no longer exists

    // Back pointer used while rebuilding ports: a sub-circuit instance has to
    // look at its definition, which lives in the sibling circuits of the
    // owning Circuit.  Runtime only, never serialized.
    struct Circuit* owner = nullptr;

    int Width() const;
    int Height() const;
    void RebuildPorts();
    void RebuildSubPorts();   // CT_SubCircuit: ports taken from the definition
};

struct TextLabel
{
    wxPoint  grid;
    wxString text;
    bool     selected = false;   // runtime only (not serialized)
};

// Logisim model: one Wire is ONE horizontal or vertical segment (two end
// points).  A polyline is simply several Wire objects that touch each other,
// and the electrical connection is decided by the net (bundle) extraction in
// wirenet.cpp -- not by these fields.
//
// points is always exactly two points: [a, b].
// srcComp/srcPort describe points[0], dstComp/dstPort describe points[1];
// they are -1 when that end is not sitting on a component port.
struct Wire
{
    int srcComp = -1, srcPort = -1;   // port at points[0], -1 = free
    int dstComp = -1, dstPort = -1;   // port at points[1], -1 = free
    std::vector<wxPoint> points;      // exactly 2 points
    int  group = 0;                   // segments created by one drag share a group
    LogicValue value = LV_Unknown;    // propagated net value, runtime only
    bool selected = false;            // runtime only
    int  selectedSegment = -1;        // unused, kept for compatibility
};

struct Circuit
{
    wxString name = wxT("main");
    std::vector<Component> comps;
    std::vector<Wire>       wires;
    std::vector<TextLabel>  texts;
    std::vector<wxPoint>    junctions; // kept for file compatibility (drawing uses nets)
    int nextId = 1;
    int nextWireGroup = 1;

    // Every circuit of the project, so a CT_SubCircuit component can find the
    // definition it instantiates.  Runtime only, set up by the owner frame.
    const std::vector<Circuit>* project = nullptr;

    Component*      FindComponent(int id);
    const Component* FindComponent(int id) const;
    Port*      FindPort(int compId, int portIdx);
    const Port* FindPort(int compId, int portIdx) const;

    // Look a sibling circuit up by name (returns nullptr when unknown).
    const Circuit* FindCircuitByName(const wxString& n) const;

    // Bounding box of everything drawn, in grid units.  Sub-circuit instances
    // use their cached size so this never recurses.
    wxRect ContentBounds() const;

    // Point every component back at this circuit (needed by RebuildPorts).
    void LinkComponents();

    // Re-link, then rebuild the ports of every sub-circuit instance.  Call
    // this after the project pointer has been (re)set, because the ports of an
    // instance depend on the circuit it instantiates.
    void RefreshSubCircuits();

    // Recompute the endpoint of every wire from its connected port positions.
    void RefreshWireEndpoints();
    void Clear();
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Gate = a logic element that computes an output from its inputs.
inline bool IsGateType(ComponentType t)
{
    return t >= CT_And && t <= CT_Buffer;
}

inline bool IsVariableInputGate(ComponentType t)
{
    return t == CT_And || t == CT_Or || t == CT_Nand || t == CT_Nor ||
           t == CT_Xor || t == CT_Xnor;
}

inline bool IsPinType(ComponentType t)
{
    return t == CT_PinIn || t == CT_PinOut;
}

inline bool IsSubCircuitType(ComponentType t)
{
    return t == CT_SubCircuit;
}

// ---------------------------------------------------------------------------
// Sub-circuit layout
//
// An instance is drawn as a box.  Its ports are the pins of the definition,
// and they have to be ON THE EDGE of that box, otherwise the user has to hunt
// for a connection point buried inside the rectangle.
//
// Inputs go to the left edge, outputs to the right edge, spread evenly from top
// to bottom in the order they were drawn.  Both the instance ports and the
// expanded copy of the definition are produced from this one function, which is
// what keeps them at exactly the same coordinates -- the netlist is built from
// geometry alone, so if the two ever disagreed the instance would go dead.
// ---------------------------------------------------------------------------

struct SubPinMap
{
    int     compId = -1;   // id of the pin component inside the definition
    wxPoint edge;          // port position, relative to the instance origin
};

struct SubLayout
{
    int                    w = 3, h = 2;   // box size in grid units
    std::vector<SubPinMap> pins;           // inputs first, then outputs
};

inline SubLayout BuildSubLayout(const Circuit& def)
{
    SubLayout L;

    std::vector<const Component*> ins, outs;
    for (const Component& c : def.comps)
    {
        if (!IsPinType(c.type) || c.ports.empty()) continue;
        (c.type == CT_PinOut ? outs : ins).push_back(&c);
    }
    if (ins.empty() && outs.empty()) return L;   // no pins yet: the plain box

    // Keep the vertical order the user drew them in.
    auto byY = [](const Component* a, const Component* b)
    {
        if (a->ports[0].grid.y != b->ports[0].grid.y)
            return a->ports[0].grid.y < b->ports[0].grid.y;
        return a->ports[0].grid.x < b->ports[0].grid.x;
    };
    std::sort(ins.begin(),  ins.end(),  byY);
    std::sort(outs.begin(), outs.end(), byY);

    // Two grid steps apart, never one: a wire that reaches a pin must not be
    // able to graze the pin above it and short the two nets together.
    //
    // The box is also never smaller than the definition itself, so the drawing
    // stays inside it and a wire the user lays across the box cannot run over
    // the (invisible) geometry of the definition.
    const int    nL = (int)ins.size(), nR = (int)outs.size();
    const wxRect bb = def.ContentBounds();
    L.w = std::max(6, bb.width);                        // fits the circuit name
    L.h = std::max(2, std::max(2 * std::max(nL, nR), bb.height));

    // A side with fewer pins is centred inside the box.
    auto spread = [&](const std::vector<const Component*>& side, bool onRight)
    {
        const int n = (int)side.size();
        const int off = (L.h - 2 * n) / 2;
        for (int i = 0; i < n; ++i)
        {
            SubPinMap m;
            m.compId = side[i]->id;
            m.edge   = wxPoint(onRight ? L.w : 0, off + 2 * i + 1);
            L.pins.push_back(m);
        }
    };
    spread(ins,  false);
    spread(outs, true);
    return L;
}

// Human readable name, e.g. "AND Gate".
wxString TypeName(ComponentType t);

// Short token used in the file format, e.g. "AND".
const wxChar* TypeToken(ComponentType t);

bool TypeFromToken(const wxString& tok, ComponentType& t);

LogicValue ComputeGateValue(const Component& c);

// project = every circuit of the file, so that CT_SubCircuit instances can be
// expanded; pass nullptr when there is only one circuit.
void EvaluateCircuit(Circuit& c, const std::vector<Circuit>* project = nullptr, int depth = 0);

wxString SerializeCircuit(const Circuit& c);
bool DeserializeCircuit(const wxString& text, Circuit& c);

// Whole-project files (all circuits, so sub-circuit definitions survive).
// A file written by an older version (a single circuit) still loads.
// Delete every instance of the circuit called `name` from the whole project,
// together with the wire segments that were plugged into its ports.
// Returns how many instances were removed.
int RemoveSubCircuitInstances(std::vector<Circuit>& cs, const wxString& name);

wxString SerializeProject(const std::vector<Circuit>& cs);
bool DeserializeProject(const wxString& text, std::vector<Circuit>& cs);

inline LogicValue ValueFromInt(int v) { return v ? LV_One : LV_Zero; }

// ---------------------------------------------------------------------------
// Inline geometry
// ---------------------------------------------------------------------------

inline int Component::Width() const
{
    switch (type)
    {
        case CT_And:  return inputs;          // D-shape body: width == height/2
        case CT_Nand: return inputs + 1;      // body plus inversion bubble
        case CT_Or:   case CT_Xor:  return 4; // curved OR body
        case CT_Nor:  case CT_Xnor: return 5; // body plus inversion bubble
        case CT_Not:  case CT_Buffer:return 3;
        case CT_PinIn: case CT_PinOut: return 2;
        case CT_SubCircuit: return subW > 0 ? subW : 3;
    }
    return 2;
}

inline int Component::Height() const
{
    switch (type)
    {
        case CT_Not:   case CT_Buffer:
        case CT_PinIn: case CT_PinOut: return 2;
        case CT_SubCircuit: return subH > 0 ? subH : 2;
        default: return 2 * inputs;   // logic gates
    }
}

inline void Component::RebuildPorts()
{
    ports.clear();
    switch (type)
    {
        case CT_PinIn:
            ports.push_back(Port(gridPos + wxPoint(2, 1), true));
            break;
        case CT_PinOut:
            ports.push_back(Port(gridPos + wxPoint(0, 1), false));
            break;
        case CT_Not:
        case CT_Buffer:
            ports.push_back(Port(gridPos + wxPoint(0, 1), false));
            ports.push_back(Port(gridPos + wxPoint(3, 1), true));
            break;
        default:  // And / Nand / Or / Nor / Xor / Xnor
        {
            for (int i = 0; i < inputs; ++i)
                ports.push_back(Port(gridPos + wxPoint(0, 1 + 2 * i), false));
            ports.push_back(Port(gridPos + wxPoint(Width(), inputs), true));
            break;
        }
        case CT_SubCircuit:
        {
            RebuildSubPorts();
            break;
        }
    }
}

inline Component* Circuit::FindComponent(int id)
{
    for (auto& c : comps) if (c.id == id) return &c;
    return nullptr;
}

inline const Component* Circuit::FindComponent(int id) const
{
    for (auto& c : comps) if (c.id == id) return &c;
    return nullptr;
}

inline Port* Circuit::FindPort(int compId, int portIdx)
{
    Component* c = FindComponent(compId);
    if (!c || portIdx < 0 || portIdx >= (int)c->ports.size()) return nullptr;
    return &c->ports[portIdx];
}

inline const Port* Circuit::FindPort(int compId, int portIdx) const
{
    const Component* c = FindComponent(compId);
    if (!c || portIdx < 0 || portIdx >= (int)c->ports.size()) return nullptr;
    return &c->ports[portIdx];
}

inline const Circuit* Circuit::FindCircuitByName(const wxString& n) const
{
    if (!project || n.IsEmpty()) return nullptr;
    for (const Circuit& c : *project)
        if (c.name == n) return &c;
    return nullptr;
}

inline wxRect Circuit::ContentBounds() const
{
    bool has = false;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    auto add = [&](int x, int y)
    {
        if (!has) { x0 = x1 = x; y0 = y1 = y; has = true; return; }
        x0 = std::min(x0, x); y0 = std::min(y0, y);
        x1 = std::max(x1, x); y1 = std::max(y1, y);
    };

    // Sub-circuit instances report their cached size on purpose: looking into
    // their definition here would recurse.
    for (const Component& c : comps)
    {
        add(c.gridPos.x, c.gridPos.y);
        add(c.gridPos.x + c.Width(), c.gridPos.y + c.Height());
    }
    for (const Wire& w : wires)
        for (const wxPoint& p : w.points) add(p.x, p.y);

    if (!has) return wxRect(0, 0, 0, 0);
    return wxRect(x0, y0, x1 - x0, y1 - y0);
}

inline void Circuit::LinkComponents()
{
    for (auto& c : comps) c.owner = this;
}

inline void Circuit::RefreshSubCircuits()
{
    LinkComponents();
    for (auto& c : comps)
        if (c.type == CT_SubCircuit) c.RebuildPorts();
    RefreshWireEndpoints();
}

inline void Component::RebuildSubPorts()
{
    ports.clear();
    subW = 0;
    subH = 0;
    subBroken = false;

    const Circuit* def = (owner && owner->project) ? owner->FindCircuitByName(subName) : nullptr;

    // Unknown name, or a circuit instantiating itself: fall back to a small
    // broken box so the user sees that something is wrong instead of nothing.
    if (!def || def == owner)
    {
        subBroken = true;
        subW = 3;
        subH = 2;
        return;
    }

    // The definition is drawn wherever the user made it; only the pins survive
    // on the outside, and they are laid out on the edges of the box.
    // Pins sit on the edge of the box so they are easy to hit with the mouse.
    // ExpandCircuit() runs the very same layout, so the expanded copy of the
    // definition puts its pins on exactly these coordinates and the two join up
    // geometrically, with no id bookkeeping in between.
    const SubLayout L = BuildSubLayout(*def);
    for (const SubPinMap& m : L.pins)
    {
        const Component* c = def->FindComponent(m.compId);
        if (!c) continue;
        ports.push_back(Port(gridPos + m.edge, c->type == CT_PinOut));
    }

    subW = L.w;
    subH = L.h;
}

inline void Circuit::RefreshWireEndpoints()
{
    for (auto& w : wires)
    {
        const Port* s = w.srcComp >= 0 ? FindPort(w.srcComp, w.srcPort) : nullptr;
        const Port* d = w.dstComp >= 0 ? FindPort(w.dstComp, w.dstPort) : nullptr;
        if (!w.points.empty() && s) w.points.front() = s->grid;
        if (!w.points.empty() && d) w.points.back()  = d->grid;
    }
}

inline void Circuit::Clear()
{
    comps.clear();
    wires.clear();
    texts.clear();
    junctions.clear();
    nextId = 1;
    nextWireGroup = 1;
}

#endif // EDA_MODEL_H
