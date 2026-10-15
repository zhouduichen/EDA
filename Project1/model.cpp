///////////////////////////////////////////////////////////////////////////
// model.cpp -- data model implementation (simulation + serialization)
///////////////////////////////////////////////////////////////////////////

#include "model.h"
#include "wirenet.h"
#include <wx/tokenzr.h>
#include <algorithm>

wxString TypeName(ComponentType t)
{
    switch (t)
    {
        case CT_And:     return wxT("与门");
        case CT_Or:      return wxT("或门");
        case CT_Not:     return wxT("非门");
        case CT_Nand:    return wxT("与非门");
        case CT_Nor:     return wxT("或非门");
        case CT_Xor:     return wxT("异或门");
        case CT_Xnor:    return wxT("同或门");
        case CT_Buffer:  return wxT("缓冲器");
        case CT_PinIn:   return wxT("输入引脚");
        case CT_PinOut:  return wxT("输出引脚");
        case CT_SubCircuit: return wxT("子电路");
    }
    return wxT("未知元件");
}

const wxChar* TypeToken(ComponentType t)
{
    switch (t)
    {
        case CT_And:     return wxT("AND");
        case CT_Or:      return wxT("OR");
        case CT_Not:     return wxT("NOT");
        case CT_Nand:    return wxT("NAND");
        case CT_Nor:     return wxT("NOR");
        case CT_Xor:     return wxT("XOR");
        case CT_Xnor:    return wxT("XNOR");
        case CT_Buffer:  return wxT("BUFFER");
        case CT_PinIn:   return wxT("PIN_IN");
        case CT_PinOut:  return wxT("PIN_OUT");
        case CT_SubCircuit: return wxT("SUB");
    }
    return wxT("AND");
}

bool TypeFromToken(const wxString& tok, ComponentType& t)
{
    if      (tok == wxT("AND"))     t = CT_And;
    else if (tok == wxT("OR"))      t = CT_Or;
    else if (tok == wxT("NOT"))     t = CT_Not;
    else if (tok == wxT("NAND"))    t = CT_Nand;
    else if (tok == wxT("NOR"))     t = CT_Nor;
    else if (tok == wxT("XOR"))     t = CT_Xor;
    else if (tok == wxT("XNOR"))    t = CT_Xnor;
    else if (tok == wxT("BUFFER"))  t = CT_Buffer;
    else if (tok == wxT("PIN_IN"))  t = CT_PinIn;
    else if (tok == wxT("PIN_OUT")) t = CT_PinOut;
    else if (tok == wxT("SUB"))     t = CT_SubCircuit;
    else return false;
    return true;
}

// ---------------------------------------------------------------------------
// 3-valued logic
// ---------------------------------------------------------------------------

LogicValue ComputeGateValue(const Component& c)
{
    std::vector<LogicValue> ins;
    for (auto& p : c.ports)
        if (!p.isOutput) ins.push_back(p.value);

    bool unknown = false;
    for (auto v : ins) if (v == LV_Unknown) unknown = true;

    switch (c.type)
    {
        case CT_And:
            for (auto v : ins) if (v == LV_Zero) return LV_Zero;
            return unknown ? LV_Unknown : LV_One;

        case CT_Or:
            for (auto v : ins) if (v == LV_One) return LV_One;
            return unknown ? LV_Unknown : LV_Zero;

        case CT_Nand:
            for (auto v : ins) if (v == LV_Zero) return LV_One;
            return unknown ? LV_Unknown : LV_Zero;

        case CT_Nor:
            for (auto v : ins) if (v == LV_One) return LV_Zero;
            return unknown ? LV_Unknown : LV_One;

        case CT_Xor:
        {
            if (unknown) return LV_Unknown;
            bool r = false;
            for (auto v : ins) r ^= (v == LV_One);
            return r ? LV_One : LV_Zero;
        }
        case CT_Xnor:
        {
            if (unknown) return LV_Unknown;
            bool r = false;
            for (auto v : ins) r ^= (v == LV_One);
            return r ? LV_Zero : LV_One;
        }
        case CT_Not:
        {
            LogicValue v = ins.empty() ? LV_Unknown : ins[0];
            if (v == LV_Unknown) return LV_Unknown;
            return (v == LV_One) ? LV_Zero : LV_One;
        }
        case CT_Buffer:
            return ins.empty() ? LV_Unknown : ins[0];

        default:
            return LV_Unknown;
    }
}

namespace
{
    // How deeply sub-circuits may be nested.  It also stops the expansion
    // from looping forever when a circuit ends up instantiating itself.
    const int kMaxSubDepth = 8;

    const Circuit* FindCircuitIn(const std::vector<Circuit>& cs, const wxString& name)
    {
        if (name.IsEmpty()) return nullptr;
        for (const Circuit& c : cs)
            if (c.name == name) return &c;
        return nullptr;
    }

    // One component taking part in the evaluation.  Components that live in
    // the circuit being evaluated are mirrored back into it (back != nullptr);
    // components that came from an expanded sub-circuit are private copies.
    struct EvalNode
    {
        Component  comp;
        Component* back      = nullptr;   // nullptr = expanded copy
        bool       innerPinIn  = false;   // a PIN_IN inside an expanded circuit
        bool       innerPinOut = false;   // a PIN_OUT inside an expanded circuit
    };

    struct EvalSlot
    {
        int  node    = -1;
        int  portIdx = -1;
        bool drive    = false;   // puts its value onto the net
        bool alsoSink = false;   // ... and picks the net value up as well
    };

    // Copy the contents of `def` into the evaluation set, shifted by `delta`.
    // Because the shift is exactly the one RebuildSubPorts used, the pins of
    // the definition land on top of the ports of the instance and the net
    // extraction joins them without any extra bookkeeping.
    void ExpandCircuit(const Circuit& def, const wxPoint& delta, const wxPoint& origin,
                       const std::vector<Circuit>* project, int depth,
                       std::vector<std::vector<wxPoint>>& polys,
                       std::vector<int>& polyToWire,
                       std::vector<EvalNode>& nodes,
                       std::vector<std::pair<wxPoint, wxPoint>>& aliasPts)
    {
        for (const Wire& w : def.wires)
        {
            if (w.points.size() < 2) continue;
            std::vector<wxPoint> p;
            p.reserve(w.points.size());
            for (const wxPoint& q : w.points) p.push_back(q + delta);
            polys.push_back(p);
            polyToWire.push_back(-1);      // private copy, not a wire of the parent
        }

        const SubLayout layout = BuildSubLayout(def);

        for (const Component& sc : def.comps)
        {
            if (sc.type == CT_SubCircuit)
            {
                if (depth >= kMaxSubDepth || !project) continue;
                const Circuit* sub = FindCircuitIn(*project, sc.subName);
                if (!sub || sub == &def) continue;
                const wxPoint org = sub->ContentBounds().GetLeftTop();
                ExpandCircuit(*sub, sc.gridPos + delta - org, sc.gridPos + delta,
                              project, depth + 1, polys, polyToWire, nodes, aliasPts);
                continue;
            }

            EvalNode n;
            n.comp        = sc;
            n.comp.gridPos = sc.gridPos + delta;
            for (auto& p : n.comp.ports) p.grid = p.grid + delta;
            n.innerPinIn  = (sc.type == CT_PinIn);
            n.innerPinOut = (sc.type == CT_PinOut);

            // The pin keeps its own coordinates, so the wire the user drew
            // inside the definition is still attached to it.  What changed is
            // that the *instance* now shows its port on the edge of the box
            // (see BuildSubLayout), which is a different point.  Declare the
            // two to be one node instead of bridging them with a hidden wire:
            // a bridge would have to cross the definition's own wiring and
            // could short two nets together.
            if (IsPinType(sc.type) && !n.comp.ports.empty())
            {
                for (const SubPinMap& m : layout.pins)
                {
                    if (m.compId != sc.id) continue;
                    const wxPoint edge = origin + m.edge;      // instance port
                    const wxPoint body = n.comp.ports[0].grid; // definition pin
                    if (edge != body)
                        aliasPts.push_back(std::make_pair(edge, body));
                    break;
                }
            }

            nodes.push_back(n);
        }
    }
}

// Evaluates the circuit by extracting electrical nets from the drawn wire
// geometry.  Two wires belong to the same net as soon as they touch or cross
// (see wirenet.cpp), so multi segment runs, branches and T junctions all
// behave like the single copper net the user sees on screen.
//
// Sub-circuit instances are *expanded into* the net list instead of being
// evaluated as opaque blocks: their contents are copied at the instance
// position, so pins and ports coincide and everything connects geometrically.
void EvaluateCircuit(Circuit& c, const std::vector<Circuit>* project, int depth)
{
    if (project) c.project = project;

    // ---- build the flat net list ------------------------------------------
    std::vector<std::vector<wxPoint>> polys;
    std::vector<int>                  polyToWire;
    std::vector<EvalNode>             nodes;
    // (instance edge port, definition pin port) pairs that must be treated as
    // one and the same electrical point.
    std::vector<std::pair<wxPoint, wxPoint>> aliasPts;

    polys.reserve(c.wires.size());
    for (size_t i = 0; i < c.wires.size(); ++i)
    {
        if (c.wires[i].points.size() < 2) continue;
        polys.push_back(c.wires[i].points);
        polyToWire.push_back((int)i);
    }

    for (Component& comp : c.comps)
    {
        EvalNode n;
        n.comp = comp;
        n.back = &comp;
        nodes.push_back(n);

        if (comp.type == CT_SubCircuit && project && depth < kMaxSubDepth)
        {
            const Circuit* def = FindCircuitIn(*project, comp.subName);
            if (def && def != &c)
            {
                const wxPoint org = def->ContentBounds().GetLeftTop();
                ExpandCircuit(*def, comp.gridPos - org, comp.gridPos, project, depth + 1,
                              polys, polyToWire, nodes, aliasPts);
            }
        }
    }

    // Initialise: only the input pins of the *real* circuit carry a value.
    // Pins inside an expanded copy are fed through the net instead.
    for (auto& n : nodes)
    {
        for (auto& p : n.comp.ports)
        {
            if (n.back && n.comp.type == CT_PinIn && p.isOutput && !n.innerPinIn)
                p.value = n.comp.value ? LV_One : LV_Zero;
            else
                p.value = LV_Unknown;
        }
    }

    // ---- ports -------------------------------------------------------------
    std::vector<wxPoint>  portPos;
    std::vector<EvalSlot> slots;

    for (size_t ni = 0; ni < nodes.size(); ++ni)
    {
        EvalNode& n = nodes[ni];
        for (size_t k = 0; k < n.comp.ports.size(); ++k)
        {
            const Port& p = n.comp.ports[k];

            bool drive    = p.isOutput;
            bool alsoSink = false;

            if (n.comp.type == CT_SubCircuit)
            {
                // The instance only anchors the geometry; the pins of the
                // expanded copy do the real work.
                drive = false;
            }
            else if (n.innerPinIn && p.isOutput)
            {
                drive = false;                 // fed from the outside world
            }
            else if (n.innerPinOut && !p.isOutput)
            {
                drive    = true;               // carries the result outwards
                alsoSink = true;
            }

            portPos.push_back(p.grid);
            EvalSlot s;
            s.node     = (int)ni;
            s.portIdx  = (int)k;
            s.drive    = drive;
            s.alsoSink = alsoSink;
            slots.push_back(s);
        }
    }

    // Turn the positional alias pairs collected during expansion into slot
    // indices.  A pair is dropped when either end is off a wire (nothing to
    // join) -- that is the "instance pin is not connected" case, and leaving
    // the two nets apart is exactly right there.
    std::vector<std::pair<int, int>> portAlias;
    if (!aliasPts.empty())
    {
        for (size_t k = 0; k < aliasPts.size(); ++k)
        {
            int a = -1, b = -1;
            for (size_t i = 0; i < portPos.size(); ++i)
            {
                if (portPos[i] == aliasPts[k].first  && a < 0) a = (int)i;
                if (portPos[i] == aliasPts[k].second && b < 0) b = (int)i;
            }
            if (a >= 0 && b >= 0 && a != b)
                portAlias.push_back(std::make_pair(a, b));
        }
    }

    const std::vector<wnNet> nets = wnBuildNets(polys, portPos, c.junctions, portAlias);

    bool changed = true;
    int guard = 0;
    while (changed && guard++ < 1000)
    {
        changed = false;

        std::vector<LogicValue> netValue(nets.size(), LV_Unknown);
        std::vector<bool>       conflict(nets.size(), false);

        // Gather the value driven onto each net by its driving ports.
        for (size_t i = 0; i < nets.size(); ++i)
        {
            for (size_t k = 0; k < nets[i].ports.size(); ++k)
            {
                const EvalSlot& s = slots[nets[i].ports[k]];
                if (!s.drive) continue;
                const Port& p = nodes[s.node].comp.ports[s.portIdx];
                if (p.value == LV_Unknown) continue;
                if (netValue[i] == LV_Unknown) netValue[i] = p.value;
                else if (netValue[i] != p.value) conflict[i] = true;
            }
        }

        // Fan the value out to every wire and every sinking port of the net.
        for (size_t i = 0; i < nets.size(); ++i)
        {
            const LogicValue v = conflict[i] ? LV_Unknown : netValue[i];

            for (size_t k = 0; k < nets[i].segs.size(); ++k)
            {
                int pi = nets[i].segs[k].poly;
                if (pi < 0 || pi >= (int)polyToWire.size()) continue;
                const int wi = polyToWire[pi];
                if (wi >= 0 && wi < (int)c.wires.size())
                    c.wires[wi].value = v;
            }

            for (size_t k = 0; k < nets[i].ports.size(); ++k)
            {
                const EvalSlot& s = slots[nets[i].ports[k]];
                if (s.drive && !s.alsoSink) continue;
                Port& p = nodes[s.node].comp.ports[s.portIdx];
                if (p.value != v)
                {
                    p.value = v;
                    changed = true;
                }
            }
        }

        // Evaluate every gate, expanded copies included.
        for (auto& n : nodes)
        {
            if (!IsGateType(n.comp.type)) continue;
            Port* out = nullptr;
            for (auto& p : n.comp.ports)
                if (p.isOutput) { out = &p; break; }
            if (!out) continue;

            LogicValue res = ComputeGateValue(n.comp);
            if (out->value != res)
            {
                out->value = res;
                changed = true;
            }
        }
    }

    // ---- write the values back into the real circuit -----------------------
    for (auto& n : nodes)
    {
        if (!n.back) continue;
        const size_t cnt = std::min(n.comp.ports.size(), n.back->ports.size());
        for (size_t k = 0; k < cnt; ++k)
            n.back->ports[k].value = n.comp.ports[k].value;
    }
}

// ---------------------------------------------------------------------------
// Serialization (simple line based text format, UTF-8)
// ---------------------------------------------------------------------------

wxString SerializeCircuitBody(const Circuit& c)
{
    wxString s;
    s += wxT("NAME ") + c.name + wxT("\n");
    s += wxString::Format(wxT("NEXTID %d\n"), c.nextId);
    s += wxString::Format(wxT("NEXTWG %d\n"), c.nextWireGroup);

    for (auto& comp : c.comps)
    {
        s += wxString::Format(wxT("COMP %d %s %d %d %d %d %s\n"),
                              comp.id, TypeToken(comp.type),
                              comp.gridPos.x, comp.gridPos.y,
                              comp.inputs, comp.value, comp.label.c_str());
        // Kept on its own line so that older versions of the program simply
        // ignore it instead of shifting every field of the COMP record.
        if (comp.type == CT_SubCircuit && !comp.subName.IsEmpty())
            s += wxString::Format(wxT("SUBNAME %d %s\n"), comp.id, comp.subName.c_str());
    }
    for (auto& w : c.wires)
    {
        s += wxString::Format(wxT("WIRE %d %d %d %d %d"),
                              w.srcComp, w.srcPort, w.dstComp, w.dstPort,
                              (int)w.points.size());
        for (auto& p : w.points)
            s += wxString::Format(wxT(" %d %d"), p.x, p.y);
        s += wxString::Format(wxT(" %d\n"), w.group);
    }
    for (auto& t : c.texts)
    {
        s += wxString::Format(wxT("TEXT %d %d %s\n"), t.grid.x, t.grid.y, t.text.c_str());
    }
    for (auto& jp : c.junctions)
    {
        s += wxString::Format(wxT("JUNCTION %d %d\n"), jp.x, jp.y);
    }
    return s;
}

wxString SerializeCircuit(const Circuit& c)
{
    return wxT("EDA_CIRCUIT 1\n") + SerializeCircuitBody(c);
}

static std::vector<wxString> SplitLine(const wxString& line)
{
    std::vector<wxString> out;
    wxString cur;
    for (size_t i = 0; i < line.length(); ++i)
    {
        wxUniChar ch = line[i];
        if (ch == wxT(' ') || ch == wxT('\t'))
        {
            if (!cur.IsEmpty()) { out.push_back(cur); cur.Clear(); }
        }
        else
        {
            cur += ch;
        }
    }
    if (!cur.IsEmpty()) out.push_back(cur);
    return out;
}

bool DeserializeCircuit(const wxString& text, Circuit& c)
{
    c.comps.clear();
    c.wires.clear();
    c.texts.clear();
    c.junctions.clear();
    c.nextId = 1;
    c.nextWireGroup = 1;

    wxStringTokenizer lines(text, wxT("\n"));
    bool ok = false;

    while (lines.HasMoreTokens())
    {
        wxString line = lines.GetNextToken();
        line.Trim();
        if (line.IsEmpty() || line.StartsWith(wxT("#"))) continue;

        std::vector<wxString> tok = SplitLine(line);
        if (tok.empty()) continue;

        if (tok[0] == wxT("EDA_CIRCUIT"))
        {
            ok = true;
            continue;
        }
        if (tok[0] == wxT("NAME"))
        {
            if (tok.size() > 1) c.name = tok[1];
            continue;
        }
        if (tok[0] == wxT("NEXTID"))
        {
            if (tok.size() > 1) c.nextId = wxAtoi(tok[1]);
            continue;
        }
        if (tok[0] == wxT("NEXTWG"))
        {
            if (tok.size() > 1) c.nextWireGroup = wxAtoi(tok[1]);
            continue;
        }
        if (tok[0] == wxT("COMP") && tok.size() >= 7)
        {
            Component comp;
            comp.id = wxAtoi(tok[1]);
            if (!TypeFromToken(tok[2], comp.type)) continue;
            comp.gridPos = wxPoint(wxAtoi(tok[3]), wxAtoi(tok[4]));
            comp.inputs  = wxAtoi(tok[5]);
            comp.value   = wxAtoi(tok[6]);
            for (size_t k = 7; k < tok.size(); ++k)
            {
                if (k > 7) comp.label += wxT(" ");
                comp.label += tok[k];
            }
            if (comp.inputs < 1) comp.inputs = 1;
            comp.RebuildPorts();
            c.comps.push_back(comp);
            continue;
        }
        if (tok[0] == wxT("SUBNAME") && tok.size() >= 3)
        {
            const int id = wxAtoi(tok[1]);
            for (auto& comp : c.comps)
            {
                if (comp.id != id) continue;
                comp.subName = tok[2];
                comp.RebuildPorts();
                break;
            }
            continue;
        }
        if (tok[0] == wxT("WIRE") && tok.size() >= 9)
        {
            Wire w;
            w.srcComp = wxAtoi(tok[1]);
            w.srcPort = wxAtoi(tok[2]);
            w.dstComp = wxAtoi(tok[3]);
            w.dstPort = wxAtoi(tok[4]);
            int n = wxAtoi(tok[5]);
            for (int i = 0; i < n && 6 + 2 * i + 1 < (int)tok.size(); ++i)
                w.points.push_back(wxPoint(wxAtoi(tok[6 + 2 * i]), wxAtoi(tok[7 + 2 * i])));
            int groupIndex = 6 + 2 * n;
            if (groupIndex < (int)tok.size()) w.group = wxAtoi(tok[groupIndex]);

            // Older files stored a whole polyline in one WIRE record; the
            // Logisim model keeps one segment per Wire, so split it up.
            if (w.points.size() < 2)
            {
                continue;
            }
            else if (w.points.size() == 2)
            {
                c.wires.push_back(w);
            }
            else
            {
                for (size_t k = 0; k + 1 < w.points.size(); ++k)
                {
                    Wire s;
                    s.group = w.group;
                    s.points.push_back(w.points[k]);
                    s.points.push_back(w.points[k + 1]);
                    if (k == 0) { s.srcComp = w.srcComp; s.srcPort = w.srcPort; }
                    if (k + 2 == w.points.size()) { s.dstComp = w.dstComp; s.dstPort = w.dstPort; }
                    c.wires.push_back(s);
                }
            }
            continue;
        }
        if (tok[0] == wxT("TEXT") && tok.size() >= 3)
        {
            TextLabel t;
            t.grid = wxPoint(wxAtoi(tok[1]), wxAtoi(tok[2]));
            for (size_t k = 3; k < tok.size(); ++k)
            {
                if (k > 3) t.text += wxT(" ");
                t.text += tok[k];
            }
            c.texts.push_back(t);
            continue;
        }
        if (tok[0] == wxT("JUNCTION") && tok.size() >= 3)
        {
            c.junctions.push_back(wxPoint(wxAtoi(tok[1]), wxAtoi(tok[2])));
            continue;
        }
    }

    c.LinkComponents();
    for (auto& comp : c.comps)
        if (comp.type == CT_SubCircuit) comp.RebuildPorts();
    c.RefreshWireEndpoints();
    for (const Wire& w : c.wires)
        if (w.group >= c.nextWireGroup) c.nextWireGroup = w.group + 1;
    return ok;
}

// ---------------------------------------------------------------------------
// Project files: every circuit, so that sub-circuit definitions are not lost.
// ---------------------------------------------------------------------------

int RemoveSubCircuitInstances(std::vector<Circuit>& cs, const wxString& name)
{
    int removed = 0;

    for (size_t i = 0; i < cs.size(); ++i)
    {
        std::vector<wxPoint> dead;
        for (const Component& c : cs[i].comps)
        {
            if (c.type != CT_SubCircuit || c.subName != name) continue;
            ++removed;
            for (const Port& p : c.ports) dead.push_back(p.grid);
        }
        if (dead.empty()) continue;

        cs[i].comps.erase(
            std::remove_if(cs[i].comps.begin(), cs[i].comps.end(),
                [&](const Component& c) {
                    return c.type == CT_SubCircuit && c.subName == name;
                }),
            cs[i].comps.end());

        cs[i].wires.erase(
            std::remove_if(cs[i].wires.begin(), cs[i].wires.end(),
                [&](const Wire& w) {
                    if (w.points.size() != 2) return true;
                    for (const wxPoint& p : dead)
                        if (w.points[0] == p || w.points[1] == p) return true;
                    return false;
                }),
            cs[i].wires.end());
    }
    return removed;
}

wxString SerializeProject(const std::vector<Circuit>& cs)
{
    wxString s = wxT("EDA_PROJECT 1\n");
    for (const Circuit& c : cs)
    {
        s += wxT("CIRCUIT ") + c.name + wxT("\n");
        s += SerializeCircuitBody(c);
        s += wxT("ENDCIRCUIT\n");
    }
    return s;
}

bool DeserializeProject(const wxString& text, std::vector<Circuit>& cs)
{
    std::vector<Circuit> out;
    wxStringTokenizer lines(text, wxT("\n"));

    wxString  blockName;
    wxString  block;
    bool      inBlock = false;
    bool      sawProject = false;

    auto flush = [&]()
    {
        if (!inBlock) return;
        Circuit c;
        DeserializeCircuit(block, c);
        if (!blockName.IsEmpty()) c.name = blockName;
        out.push_back(c);
        block.Clear();
        blockName.Clear();
        inBlock = false;
    };

    while (lines.HasMoreTokens())
    {
        wxString line = lines.GetNextToken();
        line.Trim(false);
        line.Trim(true);
        if (line.IsEmpty()) continue;

        std::vector<wxString> tok = SplitLine(line);
        if (tok.empty()) continue;

        if (tok[0] == wxT("EDA_PROJECT")) { sawProject = true; continue; }
        if (tok[0] == wxT("CIRCUIT"))
        {
            flush();
            inBlock   = true;
            blockName = (tok.size() > 1) ? tok[1] : wxString(wxT("main"));
            continue;
        }
        if (tok[0] == wxT("ENDCIRCUIT")) { flush(); continue; }

        if (inBlock) block += line + wxT("\n");
    }
    flush();

    // A single-circuit file written by an older version: load it as-is.
    if (!sawProject || out.empty())
    {
        Circuit c;
        if (!DeserializeCircuit(text, c)) return false;
        out.clear();
        out.push_back(c);
    }

    cs.swap(out);
    return true;
}
