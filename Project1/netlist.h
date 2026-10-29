///////////////////////////////////////////////////////////////////////////
// netlist.h -- KiCad netlist export.
//
// The electrical connectivity comes from the very same net extraction the
// simulation runs on (wnBuildNets in wirenet.cpp), so what the export calls
// connected is exactly what the simulation computes.
//
// Sub-circuit instances are exported as ordinary black-box components: the
// ports on the box edge become the pins of the instance, which is what a
// PCB netlist expects of a hierarchical block.
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
    wxString           name;   // "Net-(U1-A)", after the net's first pin
    std::vector<NlPin> pins;
};

// Group every port of the circuit into nets.  Pure-wire groups (nothing
// plugged in) are dropped; a net with a single port (a wire end that leads
// nowhere) is kept.  A port that touches no wire at all is unconnected and
// does not appear in any net -- exactly how KiCad reports absent pins.
void BuildNetlist(const Circuit& c, std::vector<NlNet>& out);

// The full KiCad netlist text (s-expression, version "E"), ready to be
// written to a .net file and imported by KiCad's PCB editor
// ("File -> Import -> Netlist").  `source` is recorded in the (design)
// section; pass the file path it will be saved under.
wxString ExportKiCadNetlist(const Circuit& c, const wxString& source);

#endif // EDA_NETLIST_H
