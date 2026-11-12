///////////////////////////////////////////////////////////////////////////
// netlist.cpp -- net building + gEDA pcb netlist / layout generation.
///////////////////////////////////////////////////////////////////////////

#include "netlist.h"
#include "model.h"
#include "wirenet.h"

#include <algorithm>

namespace
{
    // -----------------------------------------------------------------------
    // Physical packages
    //
    // The canvas holds logic symbols; a PCB holds chips.  Gates that live
    // inside the same 74xx package share one Element() and one RefDes, and
    // only their pin numbers differ -- what the reference material calls the
    // slot.  Grouping like this is what keeps five gates from turning into
    // five chips on the board.
    // -----------------------------------------------------------------------
    enum PkgKind
    {
        PK_Quad,      // 4 x 2-input gate: 7400 / 7409 / 7432 / 7486 / 74266
        PK_QuadNor,   // 7402: same idea, but the output sits first on the package
        PK_Hex,       // 6 x inverter / buffer: 7404 / 7407
        PK_Generic,   // a DIP of its own: wide gates, sub-circuit instances
        PK_Pin        // input / output pin: a one-pad connector
    };

    struct Package
    {
        wxString refdes;    // U1, U2 ... (P<id> for pins)
        wxString part;      // 7409, 7432, AND, ...
        wxString footprint; // DIP14, 1PIN
        int      size = 0;  // pads, power pins included
        int      slots = 1;
        int      used = 0;
        PkgKind  kind = PK_Generic;
    };

    // Where every port of every component physically lands.
    struct Board
    {
        std::vector<Package>           packages;
        int                            maxId = 0;
        std::vector<wxString>          refdesOf;   // [compId]
        std::vector<std::vector<int> > padOf;      // [compId][portIdx]
    };

    struct QuadPins
    {
        int inA, inB, out;
    };

    // 7400 / 7409 / 7432 / 7486 / 74266 use the same pinout.
    const QuadPins kStdQuad[4] =
    {
        {  1,  2,  3 },
        {  4,  5,  6 },
        {  9, 10,  8 },
        { 12, 13, 11 }
    };

    // 7402 puts the output first on each gate.
    const QuadPins kNorQuad[4] =
    {
        {  2,  3,  1 },
        {  5,  6,  4 },
        {  8,  9, 10 },
        { 11, 12, 13 }
    };

    // 7404 / 7407: gate n is in/out on these pads.
    const int kHexIn[6]  = { 1, 3, 5, 9, 11, 13 };
    const int kHexOut[6] = { 2, 4, 6, 8, 10, 12 };

    bool QuadPartOf(ComponentType t, const wxChar** part, bool& nor)
    {
        nor = false;
        switch (t)
        {
            case CT_And:  *part = wxT("7409");  return true;
            case CT_Or:   *part = wxT("7432");  return true;
            case CT_Xor:  *part = wxT("7486");  return true;
            case CT_Nand: *part = wxT("7400");  return true;
            case CT_Xnor: *part = wxT("74266"); return true;
            case CT_Nor:  *part = wxT("7402");  nor = true; return true;
            default: return false;
        }
    }

    bool HexPartOf(ComponentType t, const wxChar** part)
    {
        if (t == CT_Not)    { *part = wxT("7404"); return true; }   // hex inverter
        if (t == CT_Buffer) { *part = wxT("7407"); return true; }   // hex buffer
        return false;
    }

    // Smallest even-pin DIP that holds `ports` signal pads plus GND and Vcc.
    int DipSize(int ports)
    {
        int size = ports + 2;
        if (size < 14) size = 14;
        if (size % 2) ++size;
        return size;
    }

    // The pointer is valid until the next push_back into b.packages: use it
    // for the component being placed right now, never keep it across calls.
    Package* NewPackage(Board& b, PkgKind kind, const wxString& part,
                        const wxString& footprint, int size, int slots)
    {
        Package p;
        p.kind = kind;
        p.part = part;
        p.footprint = footprint;
        p.size = size;
        p.slots = slots;
        b.packages.push_back(p);
        return &b.packages.back();
    }

    // First package of this kind/part with a free gate slot, if any.
    Package* OpenPackage(Board& b, PkgKind kind, const wxString& part)
    {
        for (Package& p : b.packages)
            if (p.kind == kind && p.part == part && p.used < p.slots)
                return &p;
        return nullptr;
    }

    void AssignQuadPads(Board& b, const Component& comp, int slot, bool nor)
    {
        const QuadPins& q = nor ? kNorQuad[slot] : kStdQuad[slot];
        std::vector<int>& pads = b.padOf[comp.id];
        pads.clear();
        pads.push_back(q.inA);
        pads.push_back(q.inB);
        pads.push_back(q.out);
    }

    // Sequential pads, skipping the two power pins; port order is the same
    // one the netlist and the simulator use.
    void AssignGenericPads(Board& b, const Component& comp, int size)
    {
        std::vector<int>& pads = b.padOf[comp.id];
        pads.clear();
        const int gnd = size / 2, vcc = size;
        int pad = 0;
        for (size_t k = 0; k < comp.ports.size(); ++k)
        {
            do { ++pad; } while (pad == gnd || pad == vcc);
            pads.push_back(pad);
        }
    }

    Board BuildBoard(const Circuit& c)
    {
        Board b;
        // Ids are the key into refdesOf/padOf; a damaged file could carry
        // zero/negative ones, and they have no place on a physical board.
        for (const Component& comp : c.comps)
            if (comp.id > 0) b.maxId = std::max(b.maxId, comp.id);
        b.refdesOf.assign(b.maxId + 1, wxString());
        b.padOf.resize(b.maxId + 1);

        // Stable refdes numbering: components in id order.
        std::vector<const Component*> comps;
        comps.reserve(c.comps.size());
        for (const Component& comp : c.comps)
            if (comp.id > 0) comps.push_back(&comp);
        std::sort(comps.begin(), comps.end(),
                  [](const Component* a, const Component* b) { return a->id < b->id; });

        int nextU = 1;
        for (const Component* comp : comps)
        {
            b.padOf[comp->id].clear();

            if (IsPinType(comp->type))
            {
                // One-pad header; the RefDes is derived from the id so it
                // can never collide with the U-numbered chips.
                Package p;
                p.kind = PK_Pin;
                p.part = wxT("CONN");
                p.footprint = wxT("1PIN");
                p.size = 1;
                p.refdes = wxString::Format(wxT("P%d"), comp->id);
                b.packages.push_back(p);
                b.refdesOf[comp->id] = p.refdes;
                b.padOf[comp->id].push_back(1);
                continue;
            }

            const wxChar* part = nullptr;
            bool nor = false;

            if (comp->ports.size() == 3 && QuadPartOf(comp->type, &part, nor))
            {
                const PkgKind kind = nor ? PK_QuadNor : PK_Quad;
                Package* p = OpenPackage(b, kind, part);
                if (!p)
                {
                    p = NewPackage(b, kind, part, wxT("DIP14"), 14, 4);
                    p->refdes = wxString::Format(wxT("U%d"), nextU++);
                }
                const int slot = p->used++;
                b.refdesOf[comp->id] = p->refdes;
                AssignQuadPads(b, *comp, slot, nor);
                continue;
            }

            if (comp->ports.size() == 2 && HexPartOf(comp->type, &part))
            {
                Package* p = OpenPackage(b, PK_Hex, part);
                if (!p)
                {
                    p = NewPackage(b, PK_Hex, part, wxT("DIP14"), 14, 6);
                    p->refdes = wxString::Format(wxT("U%d"), nextU++);
                }
                const int slot = p->used++;
                b.refdesOf[comp->id] = p->refdes;
                std::vector<int>& pads = b.padOf[comp->id];
                pads.clear();
                pads.push_back(kHexIn[slot]);
                pads.push_back(kHexOut[slot]);
                continue;
            }

            // A gate with more than two inputs, or a sub-circuit instance:
            // a DIP of its own, big enough for every port plus power.
            const int size = DipSize((int)comp->ports.size());
            wxString value;
            if (IsSubCircuitType(comp->type))
                value = comp->subName.IsEmpty() ? wxString(wxT("SUB")) : comp->subName;
            else
                value = wxString(TypeToken(comp->type));

            Package* p = NewPackage(b, PK_Generic, value,
                                    wxString::Format(wxT("DIP%d"), size), size, 1);
            p->refdes = wxString::Format(wxT("U%d"), nextU++);
            p->used = 1;
            b.refdesOf[comp->id] = p->refdes;
            AssignGenericPads(b, *comp, size);
        }

        return b;
    }

    wxString PinRef(const Board& b, const NlPin& pin)
    {
        if (pin.compId < 0 || pin.compId > b.maxId) return wxT("?");
        const std::vector<int>& pads = b.padOf[pin.compId];
        if (pin.portIdx < 0 || pin.portIdx >= (int)pads.size()) return wxT("?");
        return b.refdesOf[pin.compId]
             + wxString::Format(wxT("-%d"), pads[pin.portIdx]);
    }

    void AppendPowerPins(wxString& line, const Board& b, bool vcc)
    {
        for (const Package& p : b.packages)
        {
            if (p.size < 2) continue;          // one-pad connector: no power
            const int pad = vcc ? p.size : p.size / 2;
            line += p.refdes + wxString::Format(wxT("-%d "), pad);
        }
    }

    // Quoted fields of the .pcb file would be broken by a quote or a line
    // break; a sub-circuit name is user text, so it has to be filtered.
    wxString FieldText(const wxString& text)
    {
        wxString out;
        for (size_t i = 0; i < text.size(); ++i)
        {
            const wxChar ch = text[i];
            out += (ch == wxT('"') || ch == wxT('\n') || ch == wxT('\r')) ? wxT('_') : ch;
        }
        return out;
    }

    // One Element() of the .pcb file.  The geometry is embedded in the file
    // (pcb does not consult its footprint library when loading layout data),
    // so the rectangle only has to look like a DIP and carry the pins at the
    // same numbers the netlist uses.
    void AppendElement(wxString& t, const Package& p)
    {
        t += wxT("Element(0x00 \"") + FieldText(p.footprint) + wxT("\" \"")
           + FieldText(p.refdes) + wxT("\" \"") + FieldText(p.part)
           + wxT("\" 220 100 3 100 0x00)\n(\n");

        if (p.kind == PK_Pin)
        {
            t += wxT("\tPin(0 0 60 40 \"1\" 0x101)\n");
            t += wxT("\tElementLine(-50 -50 50 -50 10)\n");
            t += wxT("\tElementLine(50 -50 50 50 10)\n");
            t += wxT("\tElementLine(50 50 -50 50 10)\n");
            t += wxT("\tElementLine(-50 50 -50 -50 10)\n");
        }
        else
        {
            const int rows = p.size / 2;      // pins per side
            for (int k = 1; k <= p.size; ++k)
            {
                int x, y;
                if (k <= rows) { x = 50;  y = 50 + 100 * (k - 1); }
                else           { x = 350; y = 50 + 100 * (rows - (k - rows)); }

                // Pin 1 gets the square pad flag, as in the reference file.
                const wxChar* flags = (k == 1) ? wxT("0x101") : wxT("0x01");
                t += wxT("\tPin(")
                   + wxString::Format(wxT("%d %d 60 28 \"%d\" "), x, y, k)
                   + flags + wxT(")\n");
            }

            const int h = 100 * rows;
            t += wxString::Format(wxT("\tElementLine(0 0 0 %d 10)\n"), h);
            t += wxString::Format(wxT("\tElementLine(0 %d 400 %d 10)\n"), h, h);
            t += wxString::Format(wxT("\tElementLine(400 %d 400 0 10)\n"), h);
            t += wxT("\tElementLine(0 0 150 0 10)\n");
            t += wxT("\tElementLine(250 0 400 0 10)\n");
            t += wxT("\tElementArc(200 0 50 50 0 180 10)\n");
            t += wxT("\tMark(50 50)\n");
        }

        t += wxT(")\n\n");
    }
}

void BuildNetlist(const Circuit& c, std::vector<NlNet>& out)
{
    out.clear();

    std::vector<std::vector<wxPoint>> polys;
    std::vector<wxPoint>              portPos;
    std::vector<NlPin>                portPin;   // which (comp, port) each portPos entry is

    polys.reserve(c.wires.size());
    for (size_t i = 0; i < c.wires.size(); ++i)
        if (c.wires[i].points.size() >= 2) polys.push_back(c.wires[i].points);

    for (const Component& comp : c.comps)
        for (size_t k = 0; k < comp.ports.size(); ++k)
        {
            portPos.push_back(comp.ports[k].grid);
            NlPin pin;
            pin.compId  = comp.id;
            pin.portIdx = (int)k;
            portPin.push_back(pin);
        }

    // Same connection rules as the simulation: ends touching, T junctions,
    // explicit junction dots; a plain crossing stays unconnected.
    const std::vector<wnNet> nets = wnBuildNets(polys, portPos, c.junctions);

    for (size_t ni = 0; ni < nets.size(); ++ni)
    {
        NlNet net;
        for (size_t k = 0; k < nets[ni].ports.size(); ++k)
        {
            const int idx = nets[ni].ports[k];
            if (idx < 0 || idx >= (int)portPin.size()) continue;
            net.pins.push_back(portPin[idx]);
        }
        if (net.pins.empty()) continue;   // wires nobody plugged in

        // Deterministic output: pins by component, then port.
        std::sort(net.pins.begin(), net.pins.end(),
                  [](const NlPin& a, const NlPin& b)
                  {
                      if (a.compId != b.compId) return a.compId < b.compId;
                      return a.portIdx < b.portIdx;
                  });

        out.push_back(net);
    }

    // And the nets themselves in a stable order.
    std::sort(out.begin(), out.end(),
              [](const NlNet& a, const NlNet& b)
              {
                  if (a.pins.front().compId != b.pins.front().compId)
                      return a.pins.front().compId < b.pins.front().compId;
                  return a.pins.front().portIdx < b.pins.front().portIdx;
              });
}

wxString ExportGedaNetlist(const Circuit& c)
{
    std::vector<NlNet> nets;
    BuildNetlist(c, nets);
    const Board board = BuildBoard(c);

    wxString t;
    for (size_t i = 0; i < nets.size(); ++i)
    {
        t += wxString::Format(wxT("unnamed_net%d\t"), (int)i + 1);
        for (const NlPin& pin : nets[i].pins)
            t += PinRef(board, pin) + wxT(" ");
        t += wxT("\n");
    }

    // Every package's power pins form the GND / Vcc nets, as gsch2pcb does.
    wxString gnd, vcc;
    AppendPowerPins(gnd, board, false);
    AppendPowerPins(vcc, board, true);
    if (!gnd.IsEmpty()) t += wxT("GND\t") + gnd + wxT("\n");
    if (!vcc.IsEmpty()) t += wxT("Vcc\t") + vcc + wxT("\n");
    return t;
}

wxString ExportGedaPcb(const Circuit& c)
{
    const Board board = BuildBoard(c);
    // Nothing on the canvas: no board to lay out (the caller checks this
    // before opening any file).
    if (board.packages.empty()) return wxString();

    wxString t;
    t += wxT("# release: pcb 1.99x\n");
    t += wxT("# preliminary layout generated by LogicSim -- load it with\n");
    t += wxT("# pcb's File -> Load layout data to buffer, then load the .net\n");
    t += wxT("# netlist and use Select -> Disperse all elements.\n");
    t += wxT("FileVersion[20070407]\n");
    t += wxT("PCB[\"\" 600000 500000]\n");
    t += wxT("Grid[10000.000000 0 0 0]\n");
    t += wxT("Cursor[0 0 0.000000]\n");
    t += wxT("PolyArea[200000000.000000]\n");
    t += wxT("Thermal[0.500000]\n");
    t += wxT("DRC[1000 1000 1000 1000 1500 1000]\n");
    t += wxT("Flags(\"nameonpcb,uniquename,clearnew,snappin\")\n");
    t += wxT("Groups(\"1,c:2:3:4:5:6,s:7:8\")\n");
    t += wxT("Styles[\"Signal,1000,3600,2000,1000:Power,2500,6000,3500,1000:");
    t += wxT("Fat,4000,6000,3500,1000:Skinny,600,2402,1181,600\"]\n\n");

    for (const Package& p : board.packages)
        AppendElement(t, p);

    t += wxT("Layer(1 \"top\")\n(\n)\n");
    t += wxT("Layer(2 \"ground\")\n(\n)\n");
    t += wxT("Layer(3 \"signal2\")\n(\n)\n");
    t += wxT("Layer(4 \"signal3\")\n(\n)\n");
    t += wxT("Layer(5 \"power\")\n(\n)\n");
    t += wxT("Layer(6 \"bottom\")\n(\n)\n");
    t += wxT("Layer(7 \"outline\")\n(\n)\n");
    t += wxT("Layer(8 \"spare\")\n(\n)\n");
    t += wxT("Layer(9 \"silk\")\n(\n)\n");
    t += wxT("Layer(10 \"silk\")\n(\n)\n");
    return t;
}
