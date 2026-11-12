// Netlist build + gEDA export test, run with the stub wx headers under g++.
#include "model.h"
#include "netlist.h"

#include <cstdio>
#include <string>
#include <vector>

static int g_fail = 0;
#define EXPECT(cond) do { if (!(cond)) { ++g_fail; printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static Component& AddComp(Circuit& c, ComponentType t, int id, int x, int y,
                          int inputs = 2, const wxString& label = wxT(""))
{
    Component comp;
    comp.id = id; comp.type = t; comp.gridPos = wxPoint(x, y);
    comp.inputs = inputs; comp.label = label;
    c.comps.push_back(comp);
    Component& ref = c.comps.back();
    ref.owner = &c;
    ref.RebuildPorts();
    return ref;
}

static void AddWire(Circuit& c, int sc, int sp, int dc, int dp,
                    int x1, int y1, int x2, int y2)
{
    Wire w;
    w.srcComp = sc; w.srcPort = sp; w.dstComp = dc; w.dstPort = dp;
    w.points.push_back(wxPoint(x1, y1));
    w.points.push_back(wxPoint(x2, y2));
    c.wires.push_back(w);
}

static int Count(const wxString& text, const wxString& what)
{
    int n = 0;
    // simple scan on the utf-8 form
    std::string s = text.utf8_string();
    std::string w = what.utf8_string();
    for (size_t i = 0; i + w.size() <= s.size(); )
    {
        if (s.compare(i, w.size(), w) == 0) { ++n; i += w.size(); }
        else ++i;
    }
    return n;
}

// Every "REF-PIN" pair the .pcb actually defines.  The two files are
// generated from the same board model, but the invariant is cheap to check
// and a mismatch would make pcb silently drop every pin.
static std::vector<std::string> PcbPins(const wxString& pcb)
{
    std::vector<std::string> out;
    const std::string s = pcb.utf8_string();
    std::string ref;
    size_t pos = 0;
    while (pos < s.size())
    {
        size_t eol = s.find('\n', pos);
        if (eol == std::string::npos) eol = s.size();
        const std::string line = s.substr(pos, eol - pos);
        pos = eol + 1;

        if (line.compare(0, 8, "Element(") == 0)
        {
            // Element(flags "footprint" "refdes" "value" ...)
            const size_t q1 = line.find('"');
            const size_t q2 = (q1 == std::string::npos) ? q1 : line.find('"', q1 + 1);
            const size_t q3 = (q2 == std::string::npos) ? q2 : line.find('"', q2 + 1);
            const size_t q4 = (q3 == std::string::npos) ? q3 : line.find('"', q3 + 1);
            if (q3 != std::string::npos && q4 != std::string::npos)
                ref = line.substr(q3 + 1, q4 - q3 - 1);
        }
        else
        {
            const size_t p = line.find("Pin(");
            if (p == std::string::npos || ref.empty()) continue;
            const size_t q1 = line.find('"', p);
            const size_t q2 = (q1 == std::string::npos) ? q1 : line.find('"', q1 + 1);
            if (q1 == std::string::npos || q2 == std::string::npos) continue;
            out.push_back(ref + "-" + line.substr(q1 + 1, q2 - q1 - 1));
        }
    }
    return out;
}

static std::vector<std::string> NetPins(const wxString& net)
{
    std::vector<std::string> out;
    const std::string s = net.utf8_string();
    size_t pos = 0;
    while (pos < s.size())
    {
        size_t eol = s.find('\n', pos);
        if (eol == std::string::npos) eol = s.size();
        const std::string line = s.substr(pos, eol - pos);
        pos = eol + 1;

        const size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        const std::string rest = line.substr(tab + 1);
        size_t i = 0;
        while (i < rest.size())
        {
            while (i < rest.size() && rest[i] == ' ') ++i;
            size_t j = i;
            while (j < rest.size() && rest[j] != ' ') ++j;
            if (j > i) out.push_back(rest.substr(i, j - i));
            i = j;
        }
    }
    return out;
}

static bool Has(const std::vector<std::string>& v, const std::string& s)
{
    for (const std::string& x : v) if (x == s) return true;
    return false;
}

int main()
{
    // ---- test 1: one AND gate, one package, gEDA line format --------------
    {
        Circuit c;
        AddComp(c, CT_PinIn,  1, 0, 0);
        AddComp(c, CT_PinIn,  2, 0, 2);
        AddComp(c, CT_And,    3, 10, 0);
        AddComp(c, CT_PinOut, 4, 16, 0);

        AddWire(c, 1, 0,  3, 0,   2, 1, 10, 1);   // P1 -> AND.A
        AddWire(c, 2, 0,  3, 1,   2, 3, 10, 3);   // P2 -> AND.B
        AddWire(c, 3, 2, -1, -1, 12, 2, 16, 2);   // AND.Y out
        AddWire(c, -1, -1, 4, 0, 16, 2, 16, 1);   // ... -> P4

        std::vector<NlNet> nets;
        BuildNetlist(c, nets);
        EXPECT(nets.size() == 3);
        EXPECT(nets[0].pins.size() == 2 && nets[0].pins[0].compId == 1
               && nets[0].pins[1].compId == 3);
        EXPECT(nets[2].pins.size() == 2 && nets[2].pins[0].compId == 3
               && nets[2].pins[1].compId == 4);

        const wxString text = ExportGedaNetlist(c);
        // name<TAB>refdes-pin, one line per net, exactly like full_adder.net
        EXPECT(Count(text, wxT("unnamed_net1\tP1-1 U1-1 \n")) == 1);
        EXPECT(Count(text, wxT("unnamed_net2\tP2-1 U1-2 \n")) == 1);
        EXPECT(Count(text, wxT("unnamed_net3\tU1-3 P4-1 \n")) == 1);
        // the package's power pins form the two power nets
        EXPECT(Count(text, wxT("GND\tU1-7 \n")) == 1);
        EXPECT(Count(text, wxT("Vcc\tU1-14 \n")) == 1);

        const wxString pcb = ExportGedaPcb(c);
        EXPECT(Count(pcb, wxT("FileVersion[20070407]")) == 1);
        EXPECT(Count(pcb, wxT("Element(0x00 \"DIP14\" \"U1\" \"7409\"")) == 1);
        EXPECT(Count(pcb, wxT("Element(0x00 \"1PIN\" \"P1\" \"CONN\"")) == 1);
        EXPECT(Count(pcb, wxT("Element(0x00 \"1PIN\" \"P4\" \"CONN\"")) == 1);
        EXPECT(Count(pcb, wxT("Pin(")) == 17);          // 14 pads + 3 headers
        EXPECT(Count(pcb, wxT("Layer(10 \"silk\")")) == 1);
    }

    // ---- test 2: two AND gates share one 7409 package ---------------------
    {
        Circuit c;
        AddComp(c, CT_PinIn,  1, 0, 0);
        AddComp(c, CT_And,    2, 10, 0);
        AddComp(c, CT_PinIn,  3, 0, 6);
        AddComp(c, CT_And,    4, 10, 6);

        AddWire(c, 1, 0, 2, 0, 2, 1, 10, 1);
        AddWire(c, 3, 0, 4, 0, 2, 7, 10, 7);   // second gate: slot 2, input A = pad 4

        const wxString text = ExportGedaNetlist(c);
        EXPECT(Count(text, wxT("U1-1 ")) == 1);
        EXPECT(Count(text, wxT("U1-4 ")) == 1);   // gate 2 input A
        EXPECT(Count(text, wxT("GND\tU1-7 \n")) == 1);

        const wxString pcb = ExportGedaPcb(c);
        EXPECT(Count(pcb, wxT("\"7409\"")) == 1);  // one chip, not two
        EXPECT(Count(pcb, wxT("Element(0x00 \"DIP14\" \"U1\" \"7409\"")) == 1);
    }

    // ---- test 3: seven inverters fit in two 7404 packages -----------------
    {
        Circuit c;
        for (int i = 0; i < 7; ++i) AddComp(c, CT_Not, i + 1, 10, 4 * i);
        // The seventh inverter is gate 1 of the second chip, input on pad 1.
        AddComp(c, CT_PinIn, 8, 0, 24);
        AddWire(c, 8, 0, 7, 0, 2, 25, 10, 25);

        const wxString text = ExportGedaNetlist(c);
        EXPECT(Count(text, wxT("U2-1 ")) == 1);
        EXPECT(Count(text, wxT("GND\tU1-7 U2-7 \n")) == 1);
        EXPECT(Count(text, wxT("Vcc\tU1-14 U2-14 \n")) == 1);

        const wxString pcb = ExportGedaPcb(c);
        EXPECT(Count(pcb, wxT("\"7404\"")) == 2);
        EXPECT(Count(pcb, wxT("Element(0x00 \"DIP14\" \"U2\" \"7404\"")) == 1);
    }

    // ---- test 4: a 6-input gate gets a DIP of its own, pads skip power ----
    {
        Circuit c;
        AddComp(c, CT_Nand, 1, 10, 0, 6);   // 6 inputs + output = 7 ports
        for (int i = 0; i < 6; ++i)
        {
            AddComp(c, CT_PinIn, 10 + i, 0, 2 * i);
            AddWire(c, 10 + i, 0, 1, i, 2, 2 * i + 1, 10, 2 * i + 1);
        }
        AddComp(c, CT_PinOut, 20, 20, 0);
        AddWire(c, 1, 6, -1, -1, 17, 6, 20, 6);   // gate output (Nand width 7)
        AddWire(c, -1, -1, 20, 0, 20, 6, 20, 1);

        const wxString text = ExportGedaNetlist(c);
        EXPECT(Count(text, wxT("U1-6 ")) == 1);    // input F
        EXPECT(Count(text, wxT("U1-8 ")) == 1);    // output skips pad 7 (GND)
        EXPECT(Count(text, wxT("U1-7 ")) == 1);    // only in the GND net

        const wxString pcb = ExportGedaPcb(c);
        EXPECT(Count(pcb, wxT("Element(0x00 \"DIP14\" \"U1\" \"NAND\"")) == 1);
    }

    // ---- test 5: sub-circuit instance as a black box ----------------------
    {
        std::vector<Circuit> project;

        Circuit def;
        def.name = wxT("half");
        AddComp(def, CT_PinIn,  1, 0, 0);
        AddComp(def, CT_PinOut, 2, 6, 0);
        project.push_back(def);

        Circuit main;
        main.name = wxT("main");
        project.push_back(main);

        // instance needs owner + project before its ports are rebuilt
        Component inst;
        inst.id = 1; inst.type = CT_SubCircuit; inst.gridPos = wxPoint(10, 0);
        inst.subName = wxT("half");
        inst.owner = &main;
        main.comps.push_back(inst);
        AddComp(main, CT_PinIn, 2, 0, 0);
        main.project = &project;
        main.comps[0].RebuildPorts();

        EXPECT(main.comps[0].ports.size() == 2);
        AddWire(main, 2, 0, 1, 0, 2, 1, 10, 1);

        const wxString text = ExportGedaNetlist(main);
        // the box pin becomes pad 1 of the instance's own DIP
        EXPECT(Count(text, wxT("unnamed_net1\tU1-1 P2-1 \n")) == 1);

        const wxString pcb = ExportGedaPcb(main);
        EXPECT(Count(pcb, wxT("Element(0x00 \"DIP14\" \"U1\" \"half\"")) == 1);
    }

    // ---- test 6: pin-only circuit has no power nets -----------------------
    {
        Circuit c;
        AddComp(c, CT_PinIn,  1, 0, 0);
        AddComp(c, CT_PinOut, 2, 6, 0);
        AddWire(c, 1, 0, 2, 0, 2, 1, 6, 1);

        const wxString text = ExportGedaNetlist(c);
        EXPECT(Count(text, wxT("unnamed_net1\tP1-1 P2-1 \n")) == 1);
        EXPECT(Count(text, wxT("GND\t")) == 0);
        EXPECT(Count(text, wxT("Vcc\t")) == 0);

        const wxString pcb = ExportGedaPcb(c);
        EXPECT(Count(pcb, wxT("Element(0x00 \"1PIN\"")) == 2);
    }

    // ---- test 7: the reference full adder becomes U1/U2/U3 ----------------
    {
        // 2 x XOR (7486), 2 x AND (7409), 1 x OR (7432), the same five gates
        // as 任务5参考/文件/full_adder.sch.  Two gates of one chip share the
        // RefDes; slot 2 sits on pads 4/5/6.
        Circuit c;
        AddComp(c, CT_Xor, 1, 10, 0);
        AddComp(c, CT_Xor, 2, 10, 6);
        AddComp(c, CT_And, 3, 20, 0);
        AddComp(c, CT_And, 4, 20, 6);
        AddComp(c, CT_Or,  5, 30, 0);

        const wxString pcb = ExportGedaPcb(c);
        EXPECT(Count(pcb, wxT("Element(0x00 \"DIP14\" \"U1\" \"7486\"")) == 1);
        EXPECT(Count(pcb, wxT("Element(0x00 \"DIP14\" \"U2\" \"7409\"")) == 1);
        EXPECT(Count(pcb, wxT("Element(0x00 \"DIP14\" \"U3\" \"7432\"")) == 1);
        EXPECT(Count(pcb, wxT("Element(")) == 3);

        const wxString text = ExportGedaNetlist(c);
        EXPECT(Count(text, wxT("GND\tU1-7 U2-7 U3-7 \n")) == 1);
        EXPECT(Count(text, wxT("Vcc\tU1-14 U2-14 U3-14 \n")) == 1);
    }

    // ---- test 8: slots 3/4, the 7402 pinout, and the net/.pcb invariant ---
    {
        Circuit c;
        for (int i = 0; i < 4; ++i)
        {
            AddComp(c, CT_Xor, i + 1, 10, 8 * i);
            AddComp(c, CT_PinOut, 11 + i, 20, 8 * i);
            AddWire(c, i + 1, 2, -1, -1, 14, 8 * i + 2, 20, 8 * i + 2);
            AddWire(c, -1, -1, 11 + i, 0, 20, 8 * i + 2, 20, 8 * i + 1);
        }
        // 7402 puts its output first: pads 2/3 in, pad 1 out for slot 1.
        AddComp(c, CT_Nor, 20, 10, 40);
        AddComp(c, CT_PinIn,  21, 0, 40);
        AddComp(c, CT_PinIn,  22, 0, 42);
        AddComp(c, CT_PinOut, 23, 20, 40);
        AddWire(c, 21, 0, 20, 0, 2, 41, 10, 41);
        AddWire(c, 22, 0, 20, 1, 2, 43, 10, 43);
        AddWire(c, 20, 2, -1, -1, 15, 42, 20, 42);
        AddWire(c, -1, -1, 23, 0, 20, 42, 20, 41);

        const wxString text = ExportGedaNetlist(c);
        EXPECT(Count(text, wxT("U1-3 ")) == 1);    // XOR slot 1 out
        EXPECT(Count(text, wxT("U1-6 ")) == 1);    // slot 2
        EXPECT(Count(text, wxT("U1-8 ")) == 1);    // slot 3
        EXPECT(Count(text, wxT("U1-11 ")) == 1);   // slot 4
        EXPECT(Count(text, wxT("U2-1 ")) == 1);    // NOR out
        EXPECT(Count(text, wxT("U2-2 ")) == 1);    // NOR in A
        EXPECT(Count(text, wxT("U2-3 ")) == 1);    // NOR in B

        const wxString pcb = ExportGedaPcb(c);
        const std::vector<std::string> defined = PcbPins(pcb);
        const std::vector<std::string> used = NetPins(text);
        EXPECT(!used.empty());
        int missing = 0;
        for (const std::string& pin : used)
            if (!Has(defined, pin)) ++missing;
        EXPECT(missing == 0);
        if (missing)
            for (const std::string& pin : used)
                if (!Has(defined, pin)) printf("pin not on board: %s\n", pin.c_str());
    }

    // ---- test 9: empty and sparse-id circuits -----------------------------
    {
        Circuit empty;
        EXPECT(ExportGedaNetlist(empty).IsEmpty());
        EXPECT(ExportGedaPcb(empty).IsEmpty());

        // ids are not dense in a real project; numbering must not shift.
        Circuit sparse;
        AddComp(sparse, CT_Not, 100, 10, 0);
        EXPECT(Count(ExportGedaPcb(sparse), wxT("\"U1\" \"7404\"")) == 1);
    }

    if (g_fail == 0) printf("netlist test: ALL OK\n");
    else             printf("netlist test: %d FAILURES\n", g_fail);
    return g_fail ? 1 : 0;
}
