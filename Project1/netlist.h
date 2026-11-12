///////////////////////////////////////////////////////////////////////////
// netlist.h -- export in the form the gEDA workflow expects.
//
// 任务5 的参考流程是 gschem -> gsch2pcb -> pcb:
//
//   * gsch2pcb hands pcb two files: a netlist (.net) and a preliminary
//     layout (.pcb) that already contains one Element() per physical
//     package, all piled on the same point;
//   * the netlist is plain text, one line per net:
//
//         netname<TAB>refdes-pin refdes-pin ...
//
//     (see 任务5参考/文件/full_adder.net);
//   * pcb matches those refdes-pin pairs against the Element()s and draws
//     the rats nest.
//
// So the gate symbols on the canvas have to be grouped into real ICs:
// several gates of one 74xx chip share a RefDes and are told apart by the
// pin numbers only (the reference calls this the slot).  The electrical
// connectivity itself still comes from wnBuildNets -- the same extraction
// the simulation runs on -- so what the export calls connected is exactly
// what the simulator computes.
///////////////////////////////////////////////////////////////////////////

#ifndef EDA_NETLIST_H
#define EDA_NETLIST_H

#include <wx/string.h>
#include <vector>

struct Circuit;

// One pin of a net: the component id and the index into its Component::ports.
struct NlPin
{
    int compId  = -1;
    int portIdx = -1;
};

struct NlNet
{
    std::vector<NlPin> pins;
};

// Group every port of the circuit into nets.  Pure-wire groups (nothing
// plugged in) are dropped; a net with a single port (a wire end that leads
// nowhere) is kept.  A port that touches no wire at all is unconnected and
// does not appear in any net.
void BuildNetlist(const Circuit& c, std::vector<NlNet>& out);

// The gEDA pcb text netlist, ready for pcb's "File -> Load netlist".
// Signal nets are called unnamed_net1..N; every IC package contributes its
// GND / Vcc pins to the two power nets, exactly like gsch2pcb writes them.
wxString ExportGedaNetlist(const Circuit& c);

// The preliminary layout file for pcb's "File -> Load layout data to
// buffer": one Element() per package, pins numbered like the netlist.
wxString ExportGedaPcb(const Circuit& c);

#endif // EDA_NETLIST_H
