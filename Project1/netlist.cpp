///////////////////////////////////////////////////////////////////////////
// netlist.cpp -- net building + KiCad netlist text generation.
///////////////////////////////////////////////////////////////////////////

#include "netlist.h"
#include "model.h"
#include "wirenet.h"

#include <algorithm>
#include <ctime>
#include <cwchar>

namespace
{
    // " and \ would break the s-expression quoting.
    wxString Escape(const wxString& text)
    {
        wxString out;
        for (size_t i = 0; i < text.size(); ++i)
        {
            const wxChar ch = text[i];
            if (ch == wxT('"') || ch == wxT('\\')) out += wxT('\\');
            out += ch;
        }
        return out;
    }

    // Reference designator.  Unique by construction: every component id is
    // used once, and pins (P) and the rest (U) live in different namespaces.
    wxString RefOf(const Component& c)
    {
        return wxString(IsPinType(c.type) ? wxT("P") : wxT("U"))
             + wxString::Format(wxT("%d"), c.id);
    }

    // The logical name of one port, kept in (pinfunction).
    //
    //   gates          A, B, C ... for the inputs, Y for the output
    //   input pin      OUT (it drives the net)
    //   output pin     IN  (it reads the net)
    //   sub-circuit    the label of the pin inside the definition, falling
    //                  back to that pin's own designator
    wxString PinNameOf(const Circuit& c, const Component& comp, int portIdx)
    {
        if (IsSubCircuitType(comp.type))
        {
            const Circuit* def = c.FindCircuitByName(comp.subName);
            if (def && portIdx >= 0 && portIdx < (int)comp.ports.size())
            {
                // The instance ports were laid out by BuildSubLayout in
                // RebuildSubPorts -- running it again hands back the same
                // order, so port i matches pins[i] of the definition.
                const SubLayout L = BuildSubLayout(*def);
                if (portIdx < (int)L.pins.size())
                {
                    const Component* pin = def->FindComponent(L.pins[portIdx].compId);
                    if (pin && !pin->label.IsEmpty()) return pin->label;
                    if (pin) return RefOf(*pin);
                }
            }
            return wxString::Format(wxT("PIN%d"), portIdx + 1);
        }

        if (IsPinType(comp.type))
            return comp.type == CT_PinIn ? wxT("OUT") : wxT("IN");

        // A gate: every non-output port is an input, the last port drives.
        const Port& p = comp.ports[portIdx];
        if (p.isOutput) return wxT("Y");
        return wxString(static_cast<wxChar>(wxT('A') + portIdx));
    }

    // Smallest DIP of our 74xx-style family that holds `ports` signal pads
    // plus the two power pads (GND/VCC).
    int DipSize(int ports)
    {
        for (int i = 0; i < 5; ++i)
            if (14 + 2 * i >= ports + 2) return 14 + 2 * i;
        return 22;
    }

    // The (pin) of a netlist node must name a PAD of the footprint, and real
    // pads are numbered, not lettered: our DIP family reserves the two power
    // pads (GND at size/2, VCC at size -- exactly right for DIP-14's 7/14
    // and DIP-16's 8/16), a 1x01 header has just pad "1".  The ports take
    // the remaining pads in order, so a 2-input gate sits on pads 1/2 with
    // its output on 3, and a 6-input gate skips pad 7: inputs on 1..6,
    // output on 8.
    wxString PadOf(const Component& comp, int portIdx)
    {
        if (IsPinType(comp.type)) return wxT("1");

        const int size = DipSize((int)comp.ports.size());
        const int gnd = size / 2, vcc = size;
        int pad = 0;
        for (int k = 0; k <= portIdx; ++k)
        {
            do { ++pad; } while (pad <= size && (pad == gnd || pad == vcc));
            if (pad > size) return wxString::Format(wxT("%d"), k + 1);   // ran out of pads
        }
        return wxString::Format(wxT("%d"), pad);
    }

    const wxChar* PinTypeOf(const Component& comp, const Port& p)
    {
        if (IsPinType(comp.type)) return wxT("passive");
        return p.isOutput ? wxT("output") : wxT("input");
    }

    // A PCB is made of footprints, so every component must name one -- KiCad
    // refuses to place a part without it ("no footprint defined for symbol").
    // Gates land in a classic 74xx-style DIP (room for inputs + output +
    // power), pins in a one-way 2.54mm header, sub-circuits in whatever DIP
    // still has room.  All of these live in KiCad's standard footprint
    // libraries, so the import finds them out of the box.
    wxString FootprintOf(const Component& c)
    {
        if (IsPinType(c.type))
            return wxT("Connector_PinHeader_2.54mm:PinHeader_1x01_P2.54mm_Vertical");
        return wxString::Format(wxT("Package_DIP:DIP-%d_W7.62mm"),
                                DipSize((int)c.ports.size()));
    }

    wxString CurrentDate()
    {
        std::time_t now = std::time(nullptr);
        std::tm lt{};
#ifdef _WIN32
        localtime_s(&lt, &now);
#else
        localtime_r(&now, &lt);
#endif
        wchar_t buf[32] = {};
        std::wcsftime(buf, 32, L"%Y-%m-%dT%H:%M:%S", &lt);
        return wxString(buf);
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

        const Component* first = c.FindComponent(net.pins.front().compId);
        if (first)
            net.name = wxT("Net-(") + RefOf(*first) + wxT("-")
                     + PinNameOf(c, *first, net.pins.front().portIdx) + wxT(")");

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

wxString ExportKiCadNetlist(const Circuit& c, const wxString& source)
{
    std::vector<NlNet> nets;
    BuildNetlist(c, nets);

    // compId -> comp, so the net section can name every node's ref / pin.
    // Built by hand because ids are not dense.
    std::vector<const Component*> byId;
    int maxId = 0;
    for (const Component& comp : c.comps) maxId = std::max(maxId, comp.id);
    byId.assign(maxId + 1, nullptr);
    for (const Component& comp : c.comps) byId[comp.id] = &comp;

    wxString t;
    t += wxT("(export (version \"E\")\n");
    t += wxT("\t(design\n");
    t += wxT("\t\t(source \"") + Escape(source) + wxT("\")\n");
    t += wxT("\t\t(date \"")   + CurrentDate()        + wxT("\")\n");
    t += wxT("\t\t(tool \"LogicSim (wxWidgets)\")\n");
    t += wxT("\t)\n");

    t += wxT("\t(components\n");
    for (const Component& comp : c.comps)
    {
        const wxString value = comp.label.IsEmpty() ? TypeName(comp.type) : comp.label;
        t += wxT("\t\t(comp (ref \"") + Escape(RefOf(comp)) + wxT("\")\n");
        t += wxT("\t\t\t(value \"")   + Escape(value)       + wxT("\")\n");
        t += wxT("\t\t\t(footprint \"") + Escape(FootprintOf(comp)) + wxT("\")\n");
        t += wxT("\t\t\t(libsource (lib \"LogicSim\") (part \"");
        t += TypeToken(comp.type);
        t += wxT("\") (description \"\"))\n");
        t += wxT("\t\t\t(sheetpath (names \"/\") (tstamps \"/\"))\n");
        t += wxT("\t\t)\n");
    }
    t += wxT("\t)\n");

    t += wxT("\t(nets\n");
    for (size_t ni = 0; ni < nets.size(); ++ni)
    {
        const NlNet& net = nets[ni];
        t += wxT("\t\t(net (code \"") + wxString::Format(wxT("%d"), (int)ni + 1)
           + wxT("\") (name \"") + Escape(net.name) + wxT("\")\n");

        for (size_t k = 0; k < net.pins.size(); ++k)
        {
            const NlPin& pin = net.pins[k];
            const Component* comp = (pin.compId >= 0 && pin.compId <= maxId)
                                  ? byId[pin.compId] : nullptr;
            if (!comp || pin.portIdx < 0 || pin.portIdx >= (int)comp->ports.size())
            {
                // Should not happen (the nets were just built from these
                // components), but a netlist must not name parts it has not.
                t += wxT("\t\t\t(node (ref \"?\") (pin \"")
                   + wxString::Format(wxT("%d"), pin.portIdx) + wxT("\"))\n");
                continue;
            }
            t += wxT("\t\t\t(node (ref \"") + Escape(RefOf(*comp))
               + wxT("\") (pin \"")         + Escape(PadOf(*comp, pin.portIdx))
               + wxT("\") (pinfunction \"") + Escape(PinNameOf(c, *comp, pin.portIdx))
               + wxT("\") (pintype \"")     + PinTypeOf(*comp, comp->ports[pin.portIdx])
               + wxT("\"))\n");
        }
        t += wxT("\t\t)\n");
    }
    t += wxT("\t)\n");
    t += wxT(")\n");
    return t;
}
