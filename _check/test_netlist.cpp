// Netlist build + export test, run with the stub wx headers under g++.
#include "model.h"
#include "netlist.h"

#include <cstdio>

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

int main()
{
    // ---- test 1: gate circuit, T-join, dangling pin ------------------------
    {
        Circuit c;
        AddComp(c, CT_PinIn,  1, 0, 0);
        AddComp(c, CT_PinIn,  2, 0, 6);
        AddComp(c, CT_And,    3, 10, 0);
        AddComp(c, CT_Xor,    4, 10, 6);
        AddComp(c, CT_PinOut, 5, 16, 0);
        AddComp(c, CT_PinOut, 6, 16, 6);

        AddWire(c, 1, 0,  3, 0,   2, 1, 10, 1);   // P1 -> AND.A
        AddWire(c, 2, 0,  4, 0,   2, 7, 10, 7);   // P2 -> XOR.A
        AddWire(c, 3, 2, -1, -1, 12, 2, 16, 2);   // AND.Y out
        AddWire(c, -1, -1, 5, 0, 16, 2, 16, 1);   // ... -> P5 (T-join)
        AddWire(c, 4, 2, -1, -1, 14, 8, 16, 8);   // XOR.Y out (width 4)
        AddWire(c, -1, -1, 6, 0, 16, 8, 16, 7);   // ... -> P6

        std::vector<NlNet> nets;
        BuildNetlist(c, nets);
        EXPECT(nets.size() == 4);
        EXPECT(nets[0].name == wxT("Net-(P1-OUT)"));
        EXPECT(nets[0].pins.size() == 2 && nets[0].pins[0].compId == 1
               && nets[0].pins[1].compId == 3);
        EXPECT(nets[1].name == wxT("Net-(P2-OUT)"));
        EXPECT(nets[2].name == wxT("Net-(U3-Y)"));
        EXPECT(nets[3].name == wxT("Net-(U4-Y)"));

        wxString text = ExportKiCadNetlist(c, wxT("test.net"));
        EXPECT(Count(text, wxT("(comp (ref \"")) == 6);
        EXPECT(Count(text, wxT("(net (code \"")) == 4);
        EXPECT(Count(text, wxT("(value \"与门\")")) == 1);
        // every component must carry a real KiCad footprint, or the PCB
        // editor refuses to place it
        EXPECT(Count(text, wxT("(footprint \"Package_DIP:DIP-14_W7.62mm\")")) == 2);
        EXPECT(Count(text,
            wxT("(footprint \"Connector_PinHeader_2.54mm:PinHeader_1x01_P2.54mm_Vertical\")")) == 4);
        EXPECT(Count(text, wxT("(ref \"U3\") (pin \"3\") (pinfunction \"Y\") (pintype \"output\")")) == 1);
        EXPECT(Count(text, wxT("(ref \"P1\") (pin \"1\") (pinfunction \"OUT\") (pintype \"passive\")")) == 1);
        EXPECT(Count(text, wxT("(export (version \"E\")")) == 1);
    }

    // ---- test 2: sub-circuit instance as a black box ------------------------
    {
        std::vector<Circuit> project;

        Circuit def;
        def.name = wxT("half");
        AddComp(def, CT_PinIn,  1, 0, 0, 2, wxT("X"));
        AddComp(def, CT_PinOut, 2, 6, 0, 2, wxT("S"));
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

        // the box is 8 x 2: input X on the left edge (10,1), output S (18,1)
        EXPECT(main.comps[0].ports.size() == 2);
        EXPECT(main.comps[0].ports[0].grid == wxPoint(10, 1));
        EXPECT(main.comps[0].ports[1].grid == wxPoint(18, 1));

        AddWire(main, 2, 0, 1, 0, 2, 1, 10, 1);

        std::vector<NlNet> nets;
        BuildNetlist(main, nets);
        // one wired net (X); the unwired S port is unconnected, so it makes
        // no net at all -- KiCad reads absent pins as unconnected
        EXPECT(nets.size() == 1);
        // instance pin carries the definition pin's label
        EXPECT(nets[0].name == wxT("Net-(U1-X)"));
        EXPECT(nets[0].pins.size() == 2 && nets[0].pins[0].compId == 1);

        wxString text = ExportKiCadNetlist(main, wxT("sub.net"));
        EXPECT(Count(text, wxT("(part \"SUB\")")) == 1);
        EXPECT(Count(text, wxT("(ref \"U1\") (pin \"1\") (pinfunction \"X\")")) == 1);
        EXPECT(Count(text, wxT("(pintype \"passive\")")) == 1);
    }

    // ---- test 3: pad numbering skips the DIP power pads --------------------
    {
        Circuit c;
        AddComp(c, CT_Nand, 1, 10, 0, 6);   // 6 inputs + output = 7 ports
        for (int i = 0; i < 6; ++i)
        {
            AddComp(c, CT_PinIn, 10 + i, 0, 2 * i);
            AddWire(c, 10 + i, 0, 1, i, 2, 2 * i + 1, 10, 2 * i + 1);
        }
        AddComp(c, CT_PinOut, 20, 20, 0);
        AddWire(c, 1, 6, -1, -1, 17, 6, 19, 6);   // gate output (Nand width 7)
        AddWire(c, -1, -1, -1, -1, 19, 6, 19, 1);
        AddWire(c, -1, -1, 20, 0, 19, 1, 20, 1);

        wxString text = ExportKiCadNetlist(c, wxT("dip.net"));
        EXPECT(Count(text, wxT("(footprint \"Package_DIP:DIP-14_W7.62mm\")")) == 1);
        // inputs on pads 1..6, output skips pad 7 (GND) and lands on 8
        EXPECT(Count(text, wxT("(pin \"6\") (pinfunction \"F\")")) == 1);
        EXPECT(Count(text, wxT("(pin \"8\") (pinfunction \"Y\")")) == 1);
        EXPECT(Count(text, wxT("(pin \"7\")")) == 0);
    }

    if (g_fail == 0) printf("netlist test: ALL OK\n");
    else             printf("netlist test: %d FAILURES\n", g_fail);
    return g_fail ? 1 : 0;
}
