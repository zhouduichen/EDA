///////////////////////////////////////////////////////////////////////////
// canvas.cpp -- CircuitCanvas implementation.
///////////////////////////////////////////////////////////////////////////

#include "canvas.h"
#include "wirenet.h"

#include <wx/dcbuffer.h>
#include <wx/dcmemory.h>
#include <wx/image.h>
#include <wx/textdlg.h>
#include <wx/filedlg.h>
#include <cmath>
#include <algorithm>
#include <map>
#include <set>

namespace
{
    // Order for grid points (see wirenet.h).
    typedef wnPtLess PtLess;

    const double kPi = 3.14159265358979323846;
    struct DPt { double x, y; DPt(double X = 0, double Y = 0) : x(X), y(Y) {} };

    inline wxPoint P(double x, double y)
    {
        return wxPoint((int)wxRound(x), (int)wxRound(y));
    }

    // Sample a quadratic Bezier curve from a to b with control point c.
    void QuadPts(const DPt& a, const DPt& c, const DPt& b, std::vector<wxPoint>& out, int steps = 24)
    {
        for (int i = 0; i <= steps; ++i)
        {
            double t = (double)i / steps;
            double mt = 1.0 - t;
            double x = mt * mt * a.x + 2 * mt * t * c.x + t * t * b.x;
            double y = mt * mt * a.y + 2 * mt * t * c.y + t * t * b.y;
            out.push_back(P(x, y));
        }
    }

    double DistanceToSegment(const wxPoint& p, const wxPoint& a, const wxPoint& b)
    {
        double vx = b.x - a.x;
        double vy = b.y - a.y;
        double wx = p.x - a.x;
        double wy = p.y - a.y;
        double len2 = vx * vx + vy * vy;
        double t = len2 > 0.0 ? (wx * vx + wy * vy) / len2 : 0.0;
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
        double dx = p.x - (a.x + t * vx);
        double dy = p.y - (a.y + t * vy);
        return sqrt(dx * dx + dy * dy);
    }

    wxColour ValueColour(LogicValue v)
    {
        switch (v)
        {
            case LV_One:     return wxColour(0, 180, 0);
            case LV_Zero:    return wxColour(0, 105, 255);
            default:         return wxColour(130, 130, 130);
        }
    }

    bool IsInvertedGate(ComponentType t)
    {
        return t == CT_Not || t == CT_Nand || t == CT_Nor || t == CT_Xnor;
    }

    ComponentType TypeForTool(int tool)
    {
        switch (tool)
        {
            case TOOL_AND:    return CT_And;
            case TOOL_OR:     return CT_Or;
            case TOOL_NOT:    return CT_Not;
            case TOOL_NAND:   return CT_Nand;
            case TOOL_NOR:    return CT_Nor;
            case TOOL_XOR:    return CT_Xor;
            case TOOL_XNOR:   return CT_Xnor;
            case TOOL_BUFFER: return CT_Buffer;
            case TOOL_PIN_IN: return CT_PinIn;
            case TOOL_PIN_OUT:return CT_PinOut;
            case TOOL_SUBCIRCUIT: return CT_SubCircuit;
        }
        return CT_And;
    }
}

// ---------------------------------------------------------------------------
// Construction / setup
// ---------------------------------------------------------------------------

CircuitCanvas::CircuitCanvas(wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
              wxTAB_TRAVERSAL | wxFULL_REPAINT_ON_RESIZE)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(*wxWHITE);

    Bind(wxEVT_PAINT,            &CircuitCanvas::OnPaint, this);
    Bind(wxEVT_ERASE_BACKGROUND, &CircuitCanvas::OnErase, this);
    Bind(wxEVT_LEFT_DOWN,        &CircuitCanvas::OnMouse, this);
    Bind(wxEVT_LEFT_UP,          &CircuitCanvas::OnMouse, this);
    Bind(wxEVT_RIGHT_DOWN,       &CircuitCanvas::OnMouse, this);
    Bind(wxEVT_RIGHT_UP,         &CircuitCanvas::OnMouse, this);
    Bind(wxEVT_MOTION,           &CircuitCanvas::OnMouse, this);
    Bind(wxEVT_MIDDLE_DOWN,      &CircuitCanvas::OnMouse, this);
    Bind(wxEVT_MIDDLE_UP,        &CircuitCanvas::OnMouse, this);
    Bind(wxEVT_LEFT_DCLICK,      &CircuitCanvas::OnLeftDClick, this);
    Bind(wxEVT_MOUSEWHEEL,       &CircuitCanvas::OnMouseWheel, this);
    Bind(wxEVT_KEY_DOWN,         &CircuitCanvas::OnKey, this);
    Bind(wxEVT_CHAR,             &CircuitCanvas::OnKey, this);
    // The hover highlight on connection points has to go away with the mouse.
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& e)
    {
        m_hasMouse = false;
        Refresh();
        e.Skip();
    });
}

void CircuitCanvas::SetCircuit(Circuit* c)
{
    if (m_drawingWire && HasCapture()) ReleaseMouse();
    m_circuit = c;
    m_drawingWire = false;
    m_path.clear();
    m_moving = m_movingWire = m_wireBranchPending = m_rubberBanding = m_panning = false;
    m_dragWireIndex = -1;
    m_undo.clear();
    m_redo.clear();
    Refresh();
}

void CircuitCanvas::SetTool(int tool)
{
    if (m_drawingWire) CancelWire();
    m_tool = tool;
    m_moving = m_movingWire = m_wireBranchPending = m_rubberBanding = false;
    m_dragWireIndex = -1;
    Refresh();
}

void CircuitCanvas::SetSimulationEnabled(bool on)
{
    bool changed = (m_sim != on);
    m_sim = on;
    if (on && m_circuit) EvaluateCircuit(*m_circuit);
    Refresh();
    if (changed && m_onSimulation) m_onSimulation(m_sim);
}

void CircuitCanvas::SetZoom(double z)
{
    if (z < 0.25) z = 0.25;
    if (z > 4.0)  z = 4.0;
    m_zoom = z;
    Refresh();
    if (m_onZoom) m_onZoom(z);
}

// ---------------------------------------------------------------------------
// Coordinate transforms
// ---------------------------------------------------------------------------

wxPoint CircuitCanvas::ToPx(const wxPoint& g) const
{
    return wxPoint((int)wxRound(m_offset.x + g.x * GRID * m_zoom),
                   (int)wxRound(m_offset.y + g.y * GRID * m_zoom));
}

wxPoint CircuitCanvas::ToGrid(const wxPoint& p) const
{
    return wxPoint((int)wxRound((p.x - m_offset.x) / (GRID * m_zoom)),
                   (int)wxRound((p.y - m_offset.y) / (GRID * m_zoom)));
}

wxRect CircuitCanvas::CompRectPx(const Component& c) const
{
    wxPoint tl = ToPx(c.gridPos);
    wxPoint br = ToPx(c.gridPos + wxPoint(c.Width(), c.Height()));
    return wxRect(tl, br);
}

// ---------------------------------------------------------------------------
// Hit testing
// ---------------------------------------------------------------------------

bool CircuitCanvas::FindPortAt(const wxPoint& px, int& compId, int& portIdx) const
{
    if (!m_circuit) return false;
    const double tol = std::max(8.0, std::min(18.0, GRID * m_zoom * 0.6));
    double best = tol * tol;
    bool found = false;
    for (auto& c : m_circuit->comps)
    {
        for (size_t p = 0; p < c.ports.size(); ++p)
        {
            wxPoint pp = ToPx(c.ports[p].grid);
            double dx = px.x - pp.x, dy = px.y - pp.y;
            double d2 = dx * dx + dy * dy;
            if (d2 <= best)
            {
                compId = c.id;
                portIdx = (int)p;
                best = d2;
                found = true;
            }
        }
    }
    return found;
}

Component* CircuitCanvas::HitComponent(const wxPoint& px)
{
    if (!m_circuit) return nullptr;
    for (auto it = m_circuit->comps.rbegin(); it != m_circuit->comps.rend(); ++it)
    {
        if (CompRectPx(*it).Contains(px)) return &(*it);
    }
    return nullptr;
}

int CircuitCanvas::HitText(const wxPoint& px) const
{
    if (!m_circuit) return -1;
    for (size_t i = 0; i < m_circuit->texts.size(); ++i)
    {
        const TextLabel& t = m_circuit->texts[i];
        wxPoint p = ToPx(t.grid);
        wxSize s = GetTextExtent(t.text);
        if (wxRect(p, s).Inflate(3).Contains(px)) return (int)i;
    }
    return -1;
}

int CircuitCanvas::HitWire(const wxPoint& px, int& segment, wxPoint& snapGrid) const
{
    segment = -1;
    if (!m_circuit) return -1;
    const double tol = std::max(6.0, std::min(10.0, GRID * m_zoom * 0.35));
    double best = tol;
    int bestWire = -1;
    wxPoint g = ToGrid(px);
    for (int wi = (int)m_circuit->wires.size() - 1; wi >= 0; --wi)
    {
        const Wire& w = m_circuit->wires[wi];
        for (size_t i = 0; i + 1 < w.points.size(); ++i)
        {
            wxPoint a = ToPx(w.points[i]);
            wxPoint b = ToPx(w.points[i + 1]);
            wxPoint ga = w.points[i];
            wxPoint gb = w.points[i + 1];
            if (a.x == b.x || a.y == b.y)
            {
                double d = DistanceToSegment(px, a, b);
                if (d <= best)
                {
                    best = d;
                    bestWire = wi;
                    segment = (int)i;
                    if (a.y == b.y)
                    {
                        int lo = std::min(ga.x, gb.x), hi = std::max(ga.x, gb.x);
                        snapGrid = wxPoint(std::max(lo, std::min(hi, g.x)), ga.y);
                    }
                    else
                    {
                        int lo = std::min(ga.y, gb.y), hi = std::max(ga.y, gb.y);
                        snapGrid = wxPoint(ga.x, std::max(lo, std::min(hi, g.y)));
                    }
                }
            }
            else
            {
                wxPoint corner(b.x, a.y);
                double d1 = DistanceToSegment(px, a, corner);
                double d2 = DistanceToSegment(px, corner, b);
                if (std::min(d1, d2) <= best)
                {
                    best = std::min(d1, d2);
                    bestWire = wi;
                    segment = (int)i;
                    if (d1 <= d2)
                    {
                        int lo = std::min(ga.x, gb.x), hi = std::max(ga.x, gb.x);
                        snapGrid = wxPoint(std::max(lo, std::min(hi, g.x)), ga.y);
                    }
                    else
                    {
                        int lo = std::min(ga.y, gb.y), hi = std::max(ga.y, gb.y);
                        snapGrid = wxPoint(gb.x, std::max(lo, std::min(hi, g.y)));
                    }
                }
            }
        }
    }
    return bestWire;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void CircuitCanvas::Status(const wxString& s)
{
    if (m_onStatus) m_onStatus(s);
}

void CircuitCanvas::NotifySelectionChanged()
{
    if (m_onSelectionChanged) m_onSelectionChanged();
}

void CircuitCanvas::DoEvaluate()
{
    if (m_sim && m_circuit) EvaluateCircuit(*m_circuit, m_project);
}

void CircuitCanvas::SnapshotForUndo()
{
    if (!m_circuit) return;
    wxString s = SerializeCircuit(*m_circuit);
    m_undo.push_back(s);
    if (m_undo.size() > 100) m_undo.erase(m_undo.begin());
    m_redo.clear();
}

void CircuitCanvas::ClearSelection()
{
    if (!m_circuit) return;
    for (auto& c : m_circuit->comps) c.selected = false;
    for (auto& w : m_circuit->wires)
    {
        w.selected = false;
        w.selectedSegment = -1;
    }
}

Component* CircuitCanvas::PlaceComponent(ComponentType t, const wxPoint& grid)
{
    if (!m_circuit) return nullptr;
    Component c;
    c.id = m_circuit->nextId++;
    c.type = t;
    c.gridPos = grid;
    c.inputs = IsVariableInputGate(t) ? 2 : 1;
    if (t == CT_PinIn) c.value = 0;
    if (t == CT_SubCircuit) c.subName = m_subToPlace;
    c.owner = m_circuit;          // a sub-circuit instance needs it to resolve
    c.RebuildPorts();
    m_circuit->comps.push_back(c);
    return &m_circuit->comps.back();
}

// Logisim decides connections by location: a port is wired when some segment
// ends (or passes) exactly on it.
bool CircuitCanvas::PortIsConnected(int compId, int portIdx) const
{
    if (!m_circuit) return false;
    const Port* p = m_circuit->FindPort(compId, portIdx);
    if (!p) return false;

    for (const Wire& w : m_circuit->wires)
    {
        if ((w.srcComp == compId && w.srcPort == portIdx) ||
            (w.dstComp == compId && w.dstPort == portIdx))
            return true;
        // Same rule as the net extraction: a wire has to END on the port, a
        // wire that merely runs across it is not connected.
        if (w.points.size() != 2) continue;
        if (p->grid == w.points[0] || p->grid == w.points[1]) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Wire drawing
// ---------------------------------------------------------------------------

void CircuitCanvas::StartWire(int compId, int portIdx, const wxPoint& px)
{
    Port* p = m_circuit ? m_circuit->FindPort(compId, portIdx) : nullptr;
    if (!p) return;

    m_drawingWire      = true;
    m_wireSticky       = false;
    m_wirePrefix.clear();
    m_pointerTrail.clear();
    m_wireSrcComp      = compId;
    m_wireSrcPort      = portIdx;
    m_wireSrcIsPort    = true;
    m_wireSrcIsOutput  = p->isOutput;
    m_path.clear();
    m_path.push_back(p->grid);
    m_wireDownPx       = px;
    m_wireDragged      = false;
    m_wireTarget       = p->grid;
    m_wireTargetOnPort = false;
    m_wireTargetOnWire = false;
    m_wireTargetComp   = -1;
    m_wireTargetPort   = -1;
    m_wireTargetWire   = -1;

    if (!HasCapture()) CaptureMouse();
    Status(wxT("连线：按住左键拖动到目标端口 / 导线后松开，右键或 Esc 取消；也可以直接松开左键逐点点击布线"));
    Refresh();
}

void CircuitCanvas::StartWireAtPoint(const wxPoint& grid, const wxPoint& px)
{
    if (!m_circuit) return;

    m_drawingWire      = true;
    m_wireSticky       = false;
    m_wirePrefix.clear();
    m_pointerTrail.clear();
    m_wireSrcComp      = -1;
    m_wireSrcPort      = -1;
    m_wireSrcIsPort    = false;
    m_wireSrcIsOutput  = false;
    m_path.clear();
    m_path.push_back(grid);
    m_wireDownPx       = px;
    m_wireDragged      = false;
    m_wireTarget       = grid;
    m_wireTargetOnPort = false;
    m_wireTargetOnWire = false;
    m_wireTargetComp   = -1;
    m_wireTargetPort   = -1;
    m_wireTargetWire   = -1;

    if (!HasCapture()) CaptureMouse();
    Status(wxT("分叉连线：拖到目标端口、导线或空白处后松开"));
    Refresh();
}

void CircuitCanvas::UpdateWireTarget(const wxPoint& px)
{
    if (!m_circuit) return;

    m_wireTargetOnPort = false;
    m_wireTargetOnWire = false;
    m_wireTargetComp   = -1;
    m_wireTargetPort   = -1;
    m_wireTargetWire   = -1;
    m_wireTarget       = ToGrid(px);

    // Say out loud what a release would do right now.  Without this the user
    // has to guess whether the wire is about to land on the pin or one grid
    // step beside it.
    auto hint = [&]()
    {
        if (m_wireTargetOnPort)
        {
            const Component* c = m_circuit->FindComponent(m_wireTargetComp);
            const Port* p = m_circuit->FindPort(m_wireTargetComp, m_wireTargetPort);
            if (c && p)
            {
                wxString what = TypeName(c->type);
                if (c->type == CT_SubCircuit && !c->subName.IsEmpty()) what = c->subName;
                Status(wxString::Format(wxT("松开即接入 %s 的%s引脚 #%d"),
                       what, p->isOutput ? wxT("输出") : wxT("输入"),
                       m_wireTargetPort + 1));
                return;
            }
        }
        if (m_wireTargetOnWire)
            Status(wxT("松开即在这条导线上创建一个分支点"));
        else
            Status(wxT("松开即落线；拖动到引脚 / 导线上会自动吸附，右键或 Esc 取消"));
    };

    // A port wins over everything else.
    int compId = -1, portIdx = -1;
    if (FindPortAt(px, compId, portIdx))
    {
        if (!(m_wireSrcIsPort && compId == m_wireSrcComp && portIdx == m_wireSrcPort))
        {
            const Port* p = m_circuit->FindPort(compId, portIdx);
            if (p)
            {
                m_wireTarget       = p->grid;
                m_wireTargetOnPort = true;
                m_wireTargetComp   = compId;
                m_wireTargetPort   = portIdx;
                hint();
                return;
            }
        }
    }

    // Over a component body: snap to a nearby port, but never drag the wire
    // across the whole component to reach one.
    Component* body = HitComponent(px);
    if (body)
    {
        const double tol = std::max(10.0, std::min(26.0, GRID * m_zoom * 1.2));
        double best = tol * tol;
        for (size_t i = 0; i < body->ports.size(); ++i)
        {
            if (m_wireSrcIsPort && m_wireSrcComp == body->id &&
                (int)i == m_wireSrcPort) continue;
            if (m_wireSrcIsPort && body->ports[i].isOutput == m_wireSrcIsOutput) continue;
            wxPoint pp = ToPx(body->ports[i].grid);
            double dx = px.x - pp.x, dy = px.y - pp.y;
            double d2 = dx * dx + dy * dy;
            if (d2 < best)
            {
                best = d2;
                m_wireTarget       = body->ports[i].grid;
                m_wireTargetOnPort = true;
                m_wireTargetComp   = body->id;
                m_wireTargetPort   = (int)i;
            }
        }
        if (m_wireTargetOnPort) { hint(); return; }
    }

    // Otherwise snap onto an existing wire so a junction is created.
    int seg = -1;
    wxPoint snap;
    int wi = HitWire(px, seg, snap);
    if (wi >= 0)
    {
        m_wireTarget       = snap;
        m_wireTargetOnWire = true;
        m_wireTargetWire   = wi;
    }

    hint();
}

// Logisim WiringTool: the drawn path simply follows the pointer.
//
// In a sticky run the path is rebuilt from the frozen prefix plus every grid
// point the pointer has visited since the last click.  Replaying the whole
// trail -- rather than just jumping to the newest point -- is what keeps the
// corner the user just clicked pinned where they put it while the rubber band
// goes on from there.
void CircuitCanvas::UpdateWirePath(const wxPoint& g)
{
    if (!m_wireSticky) { wnExtendPath(m_path, g); return; }

    if (m_pointerTrail.empty() || m_pointerTrail.back() != g)
        m_pointerTrail.push_back(g);

    // A very long trail costs nothing to replay, but freezing it keeps the
    // per-move work bounded on a slow machine.
    if (m_pointerTrail.size() > 512)
    {
        m_wirePrefix = m_path;
        m_pointerTrail.clear();
        m_pointerTrail.push_back(g);
    }

    wnReplayTrail(m_wirePrefix, m_pointerTrail, m_path);
}

// The geometry that FinishWire() is about to write into the circuit.
//
// The preview and the commit both go through here on purpose.  A wire records
// which port it is plugged into, but the netlist is built from the drawn
// geometry alone -- so a preview that stopped one grid step short of the port
// would show a connection that does not exist.  One function, one answer.
std::vector<wxPoint> CircuitCanvas::BuildCommitPath() const
{
    std::vector<wxPoint> pts = m_path;
    wnNormalize(pts);
    if (!m_circuit || pts.size() < 2) return pts;

    if (m_wireTargetOnPort)
    {
        const Port* tp = m_circuit->FindPort(m_wireTargetComp, m_wireTargetPort);
        if (tp && pts.back() != tp->grid)
        {
            const wxPoint g   = tp->grid;
            const wxPoint lst = pts.back();
            const wxPoint prv = pts[pts.size() - 2];
            if (wnAligned(prv, g))
            {
                pts.back() = g;                       // still one straight leg
            }
            else
            {
                const wxPoint c = (prv.y == lst.y) ? wxPoint(lst.x, g.y)
                                                  : wxPoint(g.x, lst.y);
                if (c != lst && c != g) pts.push_back(c);
                pts.push_back(g);
            }
        }
    }
    wnNormalize(pts);
    return pts;
}

void CircuitCanvas::FinishWire()
{
    if (!m_circuit || !m_drawingWire || m_path.size() < 2) { CancelWire(); return; }
    if (m_path.front() == m_path.back()) { CancelWire(); return; }

    // Validate the port we are about to land on.
    int dstComp = -1, dstPort = -1;
    if (m_wireTargetOnPort)
    {
        const Port* p = m_circuit->FindPort(m_wireTargetComp, m_wireTargetPort);
        if (!p) { CancelWire(); return; }
        if (m_wireSrcIsPort && m_wireSrcComp == m_wireTargetComp &&
            m_wireSrcPort == m_wireTargetPort) { CancelWire(); return; }
        if (m_wireSrcIsPort && p->isOutput == m_wireSrcIsOutput)
        {
            Status(wxT("不能连接两个同类型的端口"));
            CancelWire();
            return;
        }
        if (!p->isOutput && PortIsConnected(m_wireTargetComp, m_wireTargetPort))
        {
            Status(wxT("该输入端已由其他导线驱动"));
            CancelWire();
            return;
        }
        dstComp = m_wireTargetComp;
        dstPort = m_wireTargetPort;
    }

    // Belt and braces.  FinishWire() validates the port above, and the shared
    // path builder already bent the last leg onto it -- whatever is committed
    // here is byte for byte what the user saw while dragging.
    std::vector<wxPoint> pts = BuildCommitPath();
    if (pts.size() < 2 || pts.front() == pts.back()) { CancelWire(); return; }

    SnapshotForUndo();

    Wire head;
    head.points = pts;
    if (m_wireSrcIsPort) { head.srcComp = m_wireSrcComp; head.srcPort = m_wireSrcPort; }
    if (dstComp >= 0)    { head.dstComp = dstComp;       head.dstPort = dstPort; }

    // Keep the convention "source end drives, destination end reads".
    auto isOutput = [&](int c, int p) {
        const Port* q = (c >= 0) ? m_circuit->FindPort(c, p) : nullptr;
        return q && q->isOutput;
    };
    if (isOutput(head.dstComp, head.dstPort) && !isOutput(head.srcComp, head.srcPort))
    {
        std::swap(head.srcComp, head.dstComp);
        std::swap(head.srcPort, head.dstPort);
        std::reverse(pts.begin(), pts.end());
        head.points = pts;
    }

    // Put the port references on the very ends of the whole run.
    const int firstIdx = AppendRun(pts, m_circuit->nextWireGroup++);
    if (firstIdx >= 0)
    {
        m_circuit->wires[firstIdx].srcComp = head.srcComp;
        m_circuit->wires[firstIdx].srcPort = head.srcPort;
        m_circuit->wires.back().dstComp    = head.dstComp;
        m_circuit->wires.back().dstPort    = head.dstPort;
    }

    CancelWire();
    DoEvaluate();
    NotifySelectionChanged();
    Refresh();
    Status(wxT("已添加导线"));
}

// Store a polyline as one Wire per segment, exactly like Logisim does.
// Returns the index of the first segment added, or -1.
int CircuitCanvas::AppendRun(const std::vector<wxPoint>& path, int group)
{
    if (!m_circuit || path.size() < 2) return -1;
    int firstIdx = -1;

    for (size_t i = 0; i + 1 < path.size(); ++i)
    {
        if (path[i] == path[i + 1]) continue;

        bool dup = false;
        for (const Wire& w : m_circuit->wires)
        {
            if (w.points.size() != 2) continue;
            if ((w.points[0] == path[i]     && w.points[1] == path[i + 1]) ||
                (w.points[0] == path[i + 1] && w.points[1] == path[i]))
            {
                dup = true;
                break;
            }
        }
        if (dup) continue;

        Wire w;
        w.group = group;
        w.points.push_back(path[i]);
        w.points.push_back(path[i + 1]);
        if (firstIdx < 0) firstIdx = (int)m_circuit->wires.size();
        m_circuit->wires.push_back(w);
    }
    return firstIdx;
}

void CircuitCanvas::CancelWire()
{
    m_drawingWire      = false;
    m_wireDragged      = false;
    m_wireSticky       = false;
    m_wirePrefix.clear();
    m_pointerTrail.clear();
    m_wireSrcComp      = -1;
    m_wireSrcPort      = -1;
    m_wireSrcIsPort    = false;
    m_wireSrcIsOutput  = false;
    m_wireTargetOnPort = false;
    m_wireTargetOnWire = false;
    m_wireTargetComp   = -1;
    m_wireTargetPort   = -1;
    m_wireTargetWire   = -1;
    m_path.clear();
    if (HasCapture()) ReleaseMouse();
    Refresh();
}

// Logisim WireRepair: take a snapshot of every wire and every port before a
// component moves, so the repair can be replayed from a clean state.
void CircuitCanvas::SnapshotWiresAndPorts()
{
    m_wireSnapshot.clear();
    m_portSnapshot.clear();
    if (!m_circuit) return;

    m_wireSnapshot = m_circuit->wires;
    for (auto& c : m_circuit->comps)
    {
        std::vector<wxPoint> pts;
        pts.reserve(c.ports.size());
        for (auto& p : c.ports) pts.push_back(p.grid);
        m_portSnapshot[c.id] = pts;
    }
}

// Re-lay every wire from the snapshot after something moved.
//
// Every end point is first resolved to its new location:
//   * a dragged (selected) segment keeps its shape and simply translates,
//     and its ends come off whatever port they were plugged into;
//   * an end that belongs to a port follows that port;
//   * any other end is looked up in the displacement table, so an end that
//     used to touch a dragged segment travels with it.
//
// When the two new ends are no longer on one axis the segment is split in
// two by a jog.  The jog is placed so that the piece next to the *moved* end
// keeps the direction the wire had, which is what Logisim does and what keeps
// a wire leaving a port along the side it used to leave.
void CircuitCanvas::RepairWiresForMove(const wxPoint& delta)
{
    if (!m_circuit) return;

    // ---- describe the pre-drag world -------------------------------------
    std::vector<std::pair<wxPoint, wxPoint>> segs;
    std::vector<size_t> dragIdx;
    segs.reserve(m_wireSnapshot.size());
    for (size_t i = 0; i < m_wireSnapshot.size(); ++i)
    {
        const Wire& w = m_wireSnapshot[i];
        if (w.points.size() != 2) continue;
        segs.push_back(std::make_pair(w.points[0], w.points[1]));
        if (w.selected) dragIdx.push_back(segs.size() - 1);
    }

    std::vector<std::pair<wxPoint, wxPoint>> portMove;
    for (const Component& c : m_circuit->comps)
    {
        std::map<int, std::vector<wxPoint>>::const_iterator it = m_portSnapshot.find(c.id);
        if (it == m_portSnapshot.end()) continue;
        const std::vector<wxPoint>& old = it->second;
        for (size_t k = 0; k < old.size() && k < c.ports.size(); ++k)
            portMove.push_back(std::make_pair(old[k], c.ports[k].grid));
    }

    const std::map<wxPoint, wxPoint, PtLess> disp =
        wnBuildDisplacement(segs, dragIdx, portMove, delta);

    // ---- where does one end of a snapshot wire go? ------------------------
    //   * a dragged segment keeps its shape and comes off its port
    //   * an end that owns a port follows that port
    //   * anything else consults the displacement table
    auto resolveEnd = [&](const Wire& w, const wxPoint& oldPt,
                          int comp, int port, int& outComp, int& outPort) -> wxPoint
    {
        if (w.selected) { outComp = outPort = -1; return oldPt + delta; }
        if (comp >= 0)
        {
            const Port* p = m_circuit->FindPort(comp, port);
            if (p) { outComp = comp; outPort = port; return p->grid; }
        }
        std::map<wxPoint, wxPoint, PtLess>::const_iterator it = disp.find(oldPt);
        if (it != disp.end()) { outComp = outPort = -1; return it->second; }
        outComp = comp;
        outPort = port;
        return oldPt;
    };

    auto pushUnique = [](std::vector<Wire>& v, const Wire& w) -> void
    {
        if (w.points.size() != 2) return;
        if (w.points[0] == w.points[1]) return;              // collapsed
        for (const Wire& o : v)
        {
            if (o.points.size() != 2) continue;
            if ((o.points[0] == w.points[0] && o.points[1] == w.points[1]) ||
                (o.points[0] == w.points[1] && o.points[1] == w.points[0]))
                return;                                      // already there
        }
        v.push_back(w);
    };

    // ---- re-lay every segment --------------------------------------------
    std::vector<Wire> out;
    out.reserve(m_wireSnapshot.size() * 2 + 4);

    for (const Wire& w : m_wireSnapshot)
    {
        if (w.points.size() != 2) continue;

        int aComp = -1, aPort = -1, bComp = -1, bPort = -1;
        const wxPoint na = resolveEnd(w, w.points[0], w.srcComp, w.srcPort, aComp, aPort);
        const wxPoint nb = resolveEnd(w, w.points[1], w.dstComp, w.dstPort, bComp, bPort);

        const std::vector<wnRelocPiece> pieces = wnRelocate(w.points[0], w.points[1], na, nb);

        auto bind = [&](int src, int& comp, int& port) -> void
        {
            if      (src == 0) { comp = aComp; port = aPort; }
            else if (src == 1) { comp = bComp; port = bPort; }
            else               { comp = -1;    port = -1;    }  // inserted jog
        };

        for (const wnRelocPiece& pc : pieces)
        {
            Wire nw = w;
            nw.points.clear();
            nw.points.push_back(pc.a);
            nw.points.push_back(pc.b);
            bind(pc.srcA, nw.srcComp, nw.srcPort);
            bind(pc.srcB, nw.dstComp, nw.dstPort);
            pushUnique(out, nw);
        }
    }

    m_circuit->wires.swap(out);

    // Re-glue every end that ended up on a port (the split jog has none).
    for (Wire& w : m_circuit->wires)
    {
        if (w.srcComp < 0) BindEndToPort(w, true);
        if (w.dstComp < 0) BindEndToPort(w, false);
    }
}

void CircuitCanvas::DropDegenerateWires()
{
    if (!m_circuit) return;
    m_circuit->wires.erase(
        std::remove_if(m_circuit->wires.begin(), m_circuit->wires.end(),
                       [](const Wire& w) { return w.points.size() < 2; }),
        m_circuit->wires.end());
}

// Remove every wire segment that touches one of the given port locations.
void CircuitCanvas::DeleteWiresOnPorts(const std::vector<wxPoint>& portPts)
{
    if (!m_circuit || portPts.empty()) return;
    m_circuit->wires.erase(
        std::remove_if(m_circuit->wires.begin(), m_circuit->wires.end(),
            [&](const Wire& w) {
                if (w.points.size() != 2) return true;
                for (const wxPoint& p : portPts)
                    if (w.points[0] == p || w.points[1] == p) return true;
                return false;
            }),
        m_circuit->wires.end());
}

// Purely geometric re-attachment: no "input already driven" test, because a
// move repair only relocates existing ends and must never refuse to glue one
// back to the port it is still sitting on.
bool CircuitCanvas::BindEndToPort(Wire& w, bool atStart)
{
    if (!m_circuit || w.points.size() != 2) return false;

    const wxPoint g = atStart ? w.points.front() : w.points.back();
    for (const Component& c : m_circuit->comps)
    {
        for (size_t k = 0; k < c.ports.size(); ++k)
        {
            if (c.ports[k].grid != g) continue;
            if (atStart) { w.srcComp = c.id; w.srcPort = (int)k; }
            else         { w.dstComp = c.id; w.dstPort = (int)k; }
            return true;
        }
    }
    return false;
}

bool CircuitCanvas::FindFreeWireEnd(const wxPoint& px, int& wireIndex, wxPoint& endGrid) const
{
    wireIndex = -1;
    if (!m_circuit) return false;

    const double tol = std::max(6.0, std::min(12.0, GRID * m_zoom * 0.5));
    double best = tol * tol;

    for (int i = 0; i < (int)m_circuit->wires.size(); ++i)
    {
        const Wire& w = m_circuit->wires[i];
        if (w.points.size() < 2) continue;

        for (int which = 0; which < 2; ++which)
        {
            const bool atStart = (which == 0);
            if (atStart ? (w.srcComp >= 0) : (w.dstComp >= 0)) continue;  // glued to a port
            const wxPoint g = atStart ? w.points.front() : w.points.back();
            wxPoint pp = ToPx(g);
            double dx = px.x - pp.x, dy = px.y - pp.y;
            double d2 = dx * dx + dy * dy;
            if (d2 < best)
            {
                best = d2;
                wireIndex = i;
                endGrid   = g;
            }
        }
    }
    return wireIndex >= 0;
}

void CircuitCanvas::SelectInRect(const wxRect& r)
{
    if (!m_circuit) return;
    wxRect rr = r;
    if (rr.width < 0)  { rr.x += rr.width;  rr.width = -rr.width; }
    if (rr.height < 0) { rr.y += rr.height; rr.height = -rr.height; }

    for (auto& c : m_circuit->comps)
        if (rr.Intersects(CompRectPx(c))) c.selected = true;

    for (auto& w : m_circuit->wires)
    {
        for (size_t i = 0; i + 1 < w.points.size(); ++i)
        {
            wxPoint a = ToPx(w.points[i]);
            wxPoint b = ToPx(w.points[i + 1]);
            wxRect seg(std::min(a.x, b.x), std::min(a.y, b.y),
                       std::abs(b.x - a.x), std::abs(b.y - a.y));
            if (rr.Intersects(seg))
            {
                w.selected = true;
                w.selectedSegment = -1;
                break;
            }
        }
    }
}

wxRect CircuitCanvas::ComputeGridBounds() const
{
    if (!m_circuit || (m_circuit->comps.empty() && m_circuit->wires.empty() && m_circuit->texts.empty()))
        return wxRect(0, 0, 0, 0);

    int minx = 1 << 28, miny = 1 << 28, maxx = -(1 << 28), maxy = -(1 << 28);
    auto grow = [&](int x, int y) {
        minx = std::min(minx, x); miny = std::min(miny, y);
        maxx = std::max(maxx, x); maxy = std::max(maxy, y);
    };
    for (auto& c : m_circuit->comps)
    {
        grow(c.gridPos.x, c.gridPos.y);
        grow(c.gridPos.x + c.Width(), c.gridPos.y + c.Height());
    }
    for (auto& w : m_circuit->wires)
        for (auto& p : w.points) grow(p.x, p.y);
    for (auto& t : m_circuit->texts) grow(t.grid.x, t.grid.y);

    return wxRect(minx, miny, maxx - minx + 1, maxy - miny + 1);
}

wxRect CircuitCanvas::ComputeContentBoundsPx() const
{
    wxRect g = ComputeGridBounds();
    if (g.IsEmpty()) return wxRect();
    int margin = GRID;
    wxPoint tl = ToPx(wxPoint(g.x - margin, g.y - margin));
    wxPoint br = ToPx(wxPoint(g.x + g.width + margin, g.y + g.height + margin));
    return wxRect(tl, br);
}

// ---------------------------------------------------------------------------
// Mouse handling
// ---------------------------------------------------------------------------

void CircuitCanvas::OnMouse(wxMouseEvent& e)
{
    if (!m_circuit) return;

    wxPoint px = e.GetPosition();
    wxPoint grid = ToGrid(px);
    m_curGrid = grid;
    m_curPx   = px;
    m_hasMouse = true;

    // Panning with the middle mouse button.
    if (e.MiddleDown())
    {
        m_panning = true;
        m_panStartPx = px;
        m_panStartOffset = m_offset;
        CaptureMouse();
        return;
    }
    if (m_panning && e.Dragging() && e.MiddleIsDown())
    {
        m_offset = m_panStartOffset + (px - m_panStartPx);
        Refresh();
        return;
    }
    if (e.MiddleUp())
    {
        m_panning = false;
        if (HasCapture()) ReleaseMouse();
        Refresh();
        return;
    }

    if (e.LeftDown())
    {
        SetFocusIgnoringChildren();
        if (m_drawingWire)
        {
            // A second press while the button is still down is not part of the
            // Logisim wiring flow -- ignore it.  During a click-to-place run it
            // is the opposite: every click pins a corner or finishes the run.
            if (!m_wireSticky) return;

            UpdateWireTarget(px);
            UpdateWirePath(m_wireTarget);

            if (m_wireTargetOnPort) { FinishWire(); return; }

            // Pin the corner: what has been drawn so far stops following the
            // mouse and the rubber band continues from here.
            m_wirePrefix = m_path;
            m_pointerTrail.clear();
            Refresh();
            return;
        }
        switch (m_tool)
        {
            case TOOL_EDIT: OnEditLeftDown(e); break;
            case TOOL_POKE: OnPokeClick(e); break;
            case TOOL_TEXT: OnTextClick(e); break;
            default: OnPlaceClick(grid); break;
        }
        return;
    }

    // While a wire is being drawn, every mouse move extends the path.
    if (m_drawingWire && (e.Dragging() || e.Moving()))
    {
        if (e.LeftIsDown() && !m_wireDragged)
        {
            int dx = px.x - m_wireDownPx.x;
            int dy = px.y - m_wireDownPx.y;
            if (dx * dx + dy * dy > 9) m_wireDragged = true;
        }
        UpdateWireTarget(px);
        UpdateWirePath(m_wireTarget);
        Refresh();
        return;
    }

    if (e.Dragging() && e.LeftIsDown())
    {
        if (m_wireBranchPending)
        {
            int dx = px.x - m_wireBranchStartPx.x;
            int dy = px.y - m_wireBranchStartPx.y;
            if (dx * dx + dy * dy > 16)
            {
                m_wireBranchPending = false;
                m_dragWireIndex = -1;
                StartWireAtPoint(m_wireBranchPoint, m_wireBranchStartPx);
                // Catch up with the pointer straight away so the preview does
                // not lag one mouse event behind.
                UpdateWireTarget(px);
                UpdateWirePath(m_wireTarget);
                Refresh();
            }
            return;
        }
        if (m_movingWire) { OnWireMoveDrag(e); return; }
        if (m_draggingEnd) { OnWireEndDrag(e); return; }
        if (m_moving) { OnMoveDrag(e); return; }
        if (m_rubberBanding)
        {
            m_selectRect = wxRect(m_dragStartPx, px);
            Refresh();
            return;
        }
    }
    if (e.LeftUp())
    {
        if (m_wireBranchPending)
        {
            // Released without dragging: this was a plain click -> select.
            m_wireBranchPending = false;
            const int idx = m_dragWireIndex;
            m_dragWireIndex = -1;
            if (HasCapture()) ReleaseMouse();

            if (idx >= 0 && idx < (int)m_circuit->wires.size())
            {
                Wire& w = m_circuit->wires[idx];
                if (e.ShiftDown())
                    w.selected = !w.selected;
                else if (!w.selected)
                {
                    ClearSelection();
                    w.selected = true;
                }
                w.selectedSegment = -1;
                if (w.selected)
                    Status(wxT("已选中导线段：再次按下并拖动可移动，Delete 删除"));
            }
            NotifySelectionChanged();
            Refresh();
            return;
        }
        if (m_drawingWire)
        {
            // A click-to-place run never ends on the button release: the click
            // either pinned a corner (handled on the way down) or started one.
            if (m_wireSticky) return;

            // Logisim: releasing without having dragged creates nothing.  The
            // path has to be extended one last time: the last motion event can
            // be a whole grid step away from where the button came up, and the
            // net extraction is purely geometric.
            if (m_wireDragged)
            {
                UpdateWireTarget(px);
                UpdateWirePath(m_wireTarget);
                FinishWire();
            }
            else if (m_wireSrcIsPort)
            {
                // A plain click on a port keeps the run alive instead of
                // throwing it away, so long routes can be placed click by
                // click with no button held.
                m_wireSticky = true;
                m_wirePrefix = m_path;
                m_pointerTrail.clear();
                // The button is up now, so the mouse must go back to the rest
                // of the application even though the run is still alive.
                if (HasCapture()) ReleaseMouse();
                Status(wxT("点击放置拐点；点到引脚上完成，双击完成，右键或 Esc 取消"));
                Refresh();
            }
            else               { CancelWire(); }
            return;
        }
        if (m_draggingEnd)
        {
            m_draggingEnd = false;
            if (HasCapture()) ReleaseMouse();

            if (!m_endMoveSnapshotted)
            {
                // Released without moving: a plain click still selects the
                // segment, exactly like clicking any other wire.
                ClearSelection();
                if (m_endWireIndex >= 0 && m_endWireIndex < (int)m_circuit->wires.size())
                {
                    m_circuit->wires[m_endWireIndex].selected = true;
                    Status(wxT("已选中导线段：再次按下并拖动可移动，Delete 删除"));
                }
            }
            else
            {
                DropDegenerateWires();
            }

            m_endWireIndex       = -1;
            m_endMoveSnapshotted = false;
            DoEvaluate();
            NotifySelectionChanged();
            Refresh();
            return;
        }
        if (m_movingWire)
        {
            m_movingWire = false;
            m_wireMoveSnapshotted = false;
            m_dragWireIndex = -1;
            if (HasCapture()) ReleaseMouse();
            DoEvaluate();
            NotifySelectionChanged();
            Refresh();
            return;
        }
        if (m_moving)
        {
            m_moving = false;
            m_moveSnapshotted = false;
            if (HasCapture()) ReleaseMouse();
            NotifySelectionChanged();
            Refresh();
            return;
        }
        if (m_rubberBanding)
        {
            m_rubberBanding = false;
            if (HasCapture()) ReleaseMouse();
            SelectInRect(m_selectRect);
            m_selectRect = wxRect();
            NotifySelectionChanged();
            Refresh();
            return;
        }
    }

    if (e.RightDown())
    {
        if (m_drawingWire)
        {
            CancelWire();
        }
        else if (m_tool != TOOL_EDIT)
        {
            SetTool(TOOL_EDIT);
            Status(wxT("编辑工具"));
        }
        else
        {
            ClearSelection();
            NotifySelectionChanged();
            Refresh();
        }
    }
}

void CircuitCanvas::OnEditLeftDown(const wxMouseEvent& e)
{
    wxPoint px = e.GetPosition();
    wxPoint grid = ToGrid(px);

    // 1. A port starts a new wire.
    int compId = -1, portIdx = -1;
    if (FindPortAt(px, compId, portIdx))
    {
        StartWire(compId, portIdx, px);
        return;
    }

    // 2. A component is selected / moved.
    Component* c = HitComponent(px);
    if (c)
    {
        if (e.ShiftDown())
            c->selected = !c->selected;
        else if (!c->selected)
        {
            ClearSelection();
            c->selected = true;
        }
        m_moving = true;
        m_moveSnapshotted = false;
        m_dragStartPx = px;
        m_dragStartGrid = grid;
        m_moveOrigins.clear();
        for (auto& comp : m_circuit->comps)
            if (comp.selected) m_moveOrigins.push_back(comp.gridPos);
        SnapshotWiresAndPorts();
        CaptureMouse();
        NotifySelectionChanged();
        Refresh();
        return;
    }

    // 3. A wire.
    int wireSegment = -1;
    wxPoint wireSnap;
    int wireIndex = HitWire(px, wireSegment, wireSnap);
    if (wireIndex >= 0)
    {
        // Grabbing a loose end of an existing wire stretches that very wire.
        int freeIdx = -1;
        wxPoint freeEnd;
        const bool onFreeEnd =
            FindFreeWireEnd(px, freeIdx, freeEnd) && freeIdx == wireIndex;
        if (onFreeEnd) wireSnap = freeEnd;

        const bool wasSelected = m_circuit->wires[wireIndex].selected;
        const bool forceBranch = e.ControlDown() || e.AltDown();

        if (onFreeEnd && !wasSelected && !forceBranch)
        {
            m_draggingEnd          = true;
            m_endWireIndex         = wireIndex;
            m_endAtStart           = (m_circuit->wires[wireIndex].points.front() == freeEnd);
            m_endMoveSnapshotted   = false;
            SnapshotWiresAndPorts();
            CaptureMouse();
            Status(wxT("拖动端点：拉长 / 缩短 / 转向这条导线；Ctrl+拖动可改为分叉新线"));
            Refresh();
            return;
        }

        // Not selected yet (or Ctrl/Alt): the press is "pending".  Drag ->
        // branch a new wire out of this point, release -> select the segment.
        if (!wasSelected || forceBranch)
        {
            m_wireBranchPending  = true;
            m_wireBranchStartPx  = px;
            m_wireBranchPoint    = wireSnap;
            m_dragWireIndex      = wireIndex;
            CaptureMouse();
            Status(wxT("拖动可从该位置分叉一条新导线；直接松开则选中该段"));
            Refresh();
            return;
        }

        // Already selected: dragging now moves every selected segment.
        Status(wxT("正在移动选中的导线段：相连线段会自动跟随"));
        m_dragWireIndex        = wireIndex;
        m_movingWire           = true;
        m_wireMoveSnapshotted  = false;
        m_wireDragStartGrid    = grid;
        SnapshotWiresAndPorts();
        CaptureMouse();
        Refresh();
        return;
    }

    // 4. Empty space: rubber band selection.
    if (!e.ShiftDown()) ClearSelection();
    NotifySelectionChanged();
    m_rubberBanding = true;
    m_dragStartPx = px;
    m_selectRect = wxRect(px, px);
    CaptureMouse();
    Refresh();
}

void CircuitCanvas::OnMoveDrag(const wxMouseEvent& e)
{
    wxPoint grid = ToGrid(e.GetPosition());
    wxPoint delta = grid - m_dragStartGrid;
    if (delta == wxPoint(0, 0)) return;

    if (!m_moveSnapshotted)
    {
        SnapshotForUndo();
        m_moveSnapshotted = true;
    }

    // Rebuild from the snapshot so repeated drags never pile up jogs.
    size_t i = 0;
    for (auto& comp : m_circuit->comps)
    {
        if (comp.selected)
        {
            if (i < m_moveOrigins.size())
            {
                comp.gridPos = m_moveOrigins[i] + delta;
                comp.RebuildPorts();
            }
            ++i;
        }
    }
    // Rebuilt from the snapshot every time, so repeated drags never pile up
    // jogs: the components follow the pointer and every wire is re-laid from
    // its pre-drag geometry.
    RepairWiresForMove(delta);
    Refresh();
}

// Dragging a selected wire translates it; every wire that used to touch it is
// stretched (or bent) by the same repair pass so nothing is left dangling.
void CircuitCanvas::OnWireMoveDrag(const wxMouseEvent& e)
{
    if (!m_circuit || m_dragWireIndex < 0) return;

    wxPoint grid  = ToGrid(e.GetPosition());
    wxPoint delta = grid - m_wireDragStartGrid;
    if (delta == wxPoint(0, 0)) return;

    if (!m_wireMoveSnapshotted)
    {
        SnapshotForUndo();
        m_wireMoveSnapshotted = true;
    }

    RepairWiresForMove(delta);
    DoEvaluate();
    Refresh();
}

// Drag the loose end of one wire.  The far end stays where it is, the segment
// is re-laid so it still runs horizontally / vertically (turning a corner
// splits it in two), and any end point that used to sit inside the segment
// travels along with it.  Everything is rebuilt from the press-time snapshot,
// so pulling back and forth never piles up jogs.
void CircuitCanvas::OnWireEndDrag(const wxMouseEvent& e)
{
    if (!m_circuit || !m_draggingEnd) return;
    if (m_endWireIndex < 0 || m_endWireIndex >= (int)m_wireSnapshot.size()) return;

    const wxPoint px = e.GetPosition();
    wxPoint target = ToGrid(px);

    // The end snaps to a port, then to an existing wire, then to the grid --
    // the same priority a brand new run uses.
    int compId = -1, portIdx = -1;
    if (FindPortAt(px, compId, portIdx))
    {
        const Port* p = m_circuit->FindPort(compId, portIdx);
        if (p) target = p->grid;
    }
    else
    {
        int seg = -1;
        wxPoint snap;
        if (HitWire(px, seg, snap) >= 0) target = snap;
    }

    // Rebuild from the snapshot so the drag is idempotent.
    m_circuit->wires = m_wireSnapshot;

    Wire& w = m_circuit->wires[m_endWireIndex];
    if (w.points.size() != 2) return;

    const wxPoint oldA = w.points[0];
    const wxPoint oldB = w.points[1];
    wxPoint newA = oldA, newB = oldB;
    (m_endAtStart ? newA : newB) = target;
    if (newA == newB) return;                  // collapsed: keep the last shape

    if (!m_endMoveSnapshotted)
    {
        SnapshotForUndo();
        m_endMoveSnapshotted = true;
    }

    const int aComp = w.srcComp, aPort = w.srcPort;
    const int bComp = w.dstComp, bPort = w.dstPort;

    const std::vector<wnRelocPiece> pieces = wnRelocate(oldA, oldB, newA, newB);
    if (pieces.empty()) return;

    std::vector<Wire> out;
    for (size_t k = 0; k < pieces.size(); ++k)
    {
        Wire nw;
        nw.group    = w.group;
        // Deliberately keep the selection state: the wire stays "not selected",
        // so grabbing its end a second time stretches it again instead of
        // suddenly turning into a whole-run move.
        nw.selected = w.selected;
        nw.points.push_back(pieces[k].a);
        nw.points.push_back(pieces[k].b);

        auto bind = [&](int src, int& comp, int& port)
        {
            if      (src == 0) { comp = aComp; port = aPort; }
            else if (src == 1) { comp = bComp; port = bPort; }
            else               { comp = -1;    port = -1;    }   // inserted jog
        };
        bind(pieces[k].srcA, nw.srcComp, nw.srcPort);
        bind(pieces[k].srcB, nw.dstComp, nw.dstPort);
        out.push_back(nw);
    }

    m_circuit->wires.erase(m_circuit->wires.begin() + m_endWireIndex);
    for (size_t k = 0; k < out.size(); ++k) m_circuit->wires.push_back(out[k]);

    // Re-glue the ends that are sitting on a port (the inserted jog has none).
    for (Wire& q : m_circuit->wires)
    {
        if (q.srcComp < 0) BindEndToPort(q, true);
        if (q.dstComp < 0) BindEndToPort(q, false);
    }

    DoEvaluate();
    Refresh();
}

void CircuitCanvas::OnPlaceClick(const wxPoint& grid)
{
    ComponentType t = TypeForTool(m_tool);
    SnapshotForUndo();
    PlaceComponent(t, grid);
    DoEvaluate();
    SetTool(TOOL_EDIT);
    Status(wxT("已放置 ") + TypeName(t) + wxT("，已返回编辑模式"));
}

void CircuitCanvas::OnPokeClick(const wxMouseEvent& e)
{
    Component* c = HitComponent(e.GetPosition());
    if (c && c->type == CT_PinIn)
    {
        SnapshotForUndo();
        c->value = c->value ? 0 : 1;
        SetSimulationEnabled(true);
        Status(c->value ? wxT("已设置为高电平（绿色）")
                        : wxT("已设置为低电平（蓝色）"));
    }
}

void CircuitCanvas::OnTextClick(const wxMouseEvent& e)
{
    wxPoint px = e.GetPosition();
    Component* c = HitComponent(px);
    if (c)
    {
        wxString s = wxGetTextFromUser(wxT("标签："), wxT("编辑标签"), c->label, this);
        if (!s.IsEmpty())
        {
            SnapshotForUndo();
            c->label = s;
            NotifySelectionChanged();
            Refresh();
        }
        return;
    }

    wxString s = wxGetTextFromUser(wxT("文本："), wxT("添加文本"), wxT(""), this);
    if (!s.IsEmpty())
    {
        SnapshotForUndo();
        TextLabel tl;
        tl.grid = ToGrid(px);
        tl.text = s;
        m_circuit->texts.push_back(tl);
        Refresh();
    }
}

// Double clicking a crossing marks (or clears) it as a connection point.  This
// is the only way to join two wires that merely cross: everything else follows
// the Logisim rule that a connection needs an end point.
void CircuitCanvas::OnLeftDClick(wxMouseEvent& e)
{
    // Double clicking during a click-to-place run is the "I am done" gesture.
    if (m_drawingWire && m_wireSticky)
    {
        UpdateWireTarget(e.GetPosition());
        UpdateWirePath(m_wireTarget);
        FinishWire();
        return;
    }
    if (m_drawingWire) return;
    if (m_tool != TOOL_EDIT) return;
    ToggleJunction(e.GetPosition());
}

void CircuitCanvas::ToggleJunction(const wxPoint& px)
{
    if (!m_circuit) return;

    const wxPoint g = ToGrid(px);
    bool onWire = false;
    for (const Wire& w : m_circuit->wires)
    {
        if (w.points.size() != 2) continue;
        if (wnPointOnSeg(g, w.points[0], w.points[1])) { onWire = true; break; }
    }
    if (!onWire)
    {
        Status(wxT("连接点必须放在导线上"));
        return;
    }

    SnapshotForUndo();
    for (size_t i = 0; i < m_circuit->junctions.size(); ++i)
    {
        if (m_circuit->junctions[i] != g) continue;
        m_circuit->junctions.erase(m_circuit->junctions.begin() + i);
        Status(wxT("已取消该连接点"));
        DoEvaluate();
        Refresh();
        return;
    }

    m_circuit->junctions.push_back(g);
    Status(wxT("已添加连接点：此处导线互相连通"));
    DoEvaluate();
    Refresh();
}

void CircuitCanvas::OnMouseWheel(wxMouseEvent& e)
{
    if (!m_circuit) return;
    double factor = e.GetWheelRotation() > 0 ? 1.1 : 1.0 / 1.1;
    double nz = m_zoom * factor;
    if (nz < 0.25) nz = 0.25;
    if (nz > 4.0)  nz = 4.0;
    if (nz == m_zoom) return;

    // Keep the grid point under the cursor fixed while zooming.
    wxPoint px = e.GetPosition();
    wxPoint g = ToGrid(px);
    m_zoom = nz;
    m_offset.x = px.x - (int)wxRound(g.x * GRID * m_zoom);
    m_offset.y = px.y - (int)wxRound(g.y * GRID * m_zoom);
    Refresh();
    if (m_onZoom) m_onZoom(m_zoom);
}

void CircuitCanvas::OnKey(wxKeyEvent& e)
{
    bool ctrl = e.ControlDown();
    int k = e.GetKeyCode();

    if (ctrl && k == 'Z') { Undo(); return; }
    if (ctrl && k == 'Y') { Redo(); return; }
    if (ctrl && k == 'C') { CopySelection(); return; }
    if (ctrl && k == 'X') { CutSelection(); return; }
    if (ctrl && k == 'V') { PasteSelection(); return; }
    if (ctrl && k == 'A') { SelectAll(); return; }

    if (k == WXK_DELETE || k == WXK_NUMPAD_DELETE) { DeleteSelection(); return; }

    if (k == WXK_ESCAPE)
    {
        if (m_drawingWire) CancelWire();
        else { SetTool(TOOL_EDIT); ClearSelection(); NotifySelectionChanged(); Refresh(); }
        return;
    }

    e.Skip();
}

// ---------------------------------------------------------------------------
// Actions (menus / toolbar / keys)
// ---------------------------------------------------------------------------

void CircuitCanvas::NewCircuit()
{
    SnapshotForUndo();
    m_circuit->Clear();
    m_drawingWire = false;
    DoEvaluate();
    Refresh();
    NotifySelectionChanged();
}

void CircuitCanvas::DeleteSelection()
{
    if (!m_circuit) return;
    bool any = false;
    for (auto& c : m_circuit->comps) if (c.selected) any = true;
    for (auto& w : m_circuit->wires) if (w.selected) any = true;
    if (!any) return;

    SnapshotForUndo();

    std::set<int> del;
    std::vector<wxPoint> deadPorts;
    for (auto& c : m_circuit->comps)
    {
        if (!c.selected) continue;
        del.insert(c.id);
        for (auto& p : c.ports) deadPorts.push_back(p.grid);
    }

    // A segment counts as attached when its end sits on a removed port, which
    // is the same rule Logisim uses (connection is decided by location).
    auto& wires = m_circuit->wires;
    wires.erase(std::remove_if(wires.begin(), wires.end(),
        [&](const Wire& w) {
            if (w.selected || (w.srcComp >= 0 && del.count(w.srcComp)) ||
                (w.dstComp >= 0 && del.count(w.dstComp))) return true;
            if (w.points.size() != 2) return true;
            for (const wxPoint& p : deadPorts)
                if (w.points[0] == p || w.points[1] == p) return true;
            return false;
        }),
        wires.end());

    auto& comps = m_circuit->comps;
    comps.erase(std::remove_if(comps.begin(), comps.end(),
        [](const Component& c) { return c.selected; }), comps.end());

    DoEvaluate();
    Refresh();
    NotifySelectionChanged();
}

void CircuitCanvas::SelectAll()
{
    if (!m_circuit) return;
    for (auto& c : m_circuit->comps) c.selected = true;
    for (auto& w : m_circuit->wires)
    {
        w.selected = true;
        w.selectedSegment = -1;
    }
    Refresh();
    NotifySelectionChanged();
}

void CircuitCanvas::DuplicateSelection()
{
    if (!m_circuit) return;
    std::vector<int> ids;
    for (auto& c : m_circuit->comps) if (c.selected) ids.push_back(c.id);
    if (ids.empty()) return;

    SnapshotForUndo();

    std::map<int, int> idmap;
    std::vector<Component> newComps;
    for (auto& c : m_circuit->comps)
    {
        if (!c.selected) continue;
        Component n = c;
        n.id = m_circuit->nextId++;
        n.gridPos += wxPoint(2, 2);
        n.RebuildPorts();
        n.selected = true;
        idmap[c.id] = n.id;
        newComps.push_back(n);
    }
    for (auto& c : m_circuit->comps) c.selected = false;

    std::vector<Wire> newWires;
    std::map<int, int> wireGroups;
    for (auto& w : m_circuit->wires)
    {
        if (idmap.count(w.srcComp) || idmap.count(w.dstComp))
        {
            Wire nw = w;
            nw.srcComp = idmap.count(w.srcComp) ? idmap[w.srcComp] : -1;
            nw.srcPort = nw.srcComp >= 0 ? w.srcPort : -1;
            nw.dstComp = idmap.count(w.dstComp) ? idmap[w.dstComp] : -1;
            nw.dstPort = nw.dstComp >= 0 ? w.dstPort : -1;
            if (!wireGroups.count(nw.group))
                wireGroups[nw.group] = m_circuit->nextWireGroup++;
            nw.group = wireGroups[nw.group];
            for (auto& p : nw.points) p += wxPoint(2, 2);
            newWires.push_back(nw);
        }
    }

    for (auto& nc : newComps) m_circuit->comps.push_back(nc);
    for (auto& nw : newWires) m_circuit->wires.push_back(nw);

    m_circuit->RefreshWireEndpoints();
    DoEvaluate();
    Refresh();
    NotifySelectionChanged();
}

void CircuitCanvas::CopySelection()
{
    if (!m_circuit) return;
    Circuit tmp;
    tmp.name = wxT("clipboard");
    std::map<int, int> idmap;
    int next = 1;
    for (auto& c : m_circuit->comps)
    {
        if (!c.selected) continue;
        Component n = c;
        n.id = next++;
        idmap[c.id] = n.id;
        tmp.comps.push_back(n);
    }
    for (auto& w : m_circuit->wires)
    {
        if (idmap.count(w.srcComp) || idmap.count(w.dstComp))
        {
            Wire nw = w;
            nw.srcComp = idmap.count(w.srcComp) ? idmap[w.srcComp] : -1;
            nw.srcPort = nw.srcComp >= 0 ? w.srcPort : -1;
            nw.dstComp = idmap.count(w.dstComp) ? idmap[w.dstComp] : -1;
            nw.dstPort = nw.dstComp >= 0 ? w.dstPort : -1;
            tmp.wires.push_back(nw);
        }
    }
    tmp.nextId = next;
    if (tmp.comps.empty())
    {
        Status(wxT("没有可复制的选中对象"));
        return;
    }
    m_clipboard = SerializeCircuit(tmp);
    m_clipOffset = 0;
    Status(wxT("已复制到剪贴板"));
}

void CircuitCanvas::CutSelection()
{
    CopySelection();
    DeleteSelection();
}

void CircuitCanvas::PasteSelection()
{
    if (m_clipboard.IsEmpty() || !m_circuit) return;
    Circuit tmp;
    if (!DeserializeCircuit(m_clipboard, tmp) || tmp.comps.empty()) return;

    SnapshotForUndo();
    m_clipOffset += 2;

    std::map<int, int> idmap;
    std::map<int, int> wireGroups;
    for (auto& c : tmp.comps)
    {
        int old = c.id;
        c.id = m_circuit->nextId++;
        idmap[old] = c.id;
        c.gridPos += wxPoint(m_clipOffset, m_clipOffset);
        c.RebuildPorts();
        c.selected = false;
    }
    for (auto& w : tmp.wires)
    {
        if (idmap.count(w.srcComp)) w.srcComp = idmap[w.srcComp];
        else if (w.srcComp >= 0) w.srcComp = -1;
        if (idmap.count(w.dstComp)) w.dstComp = idmap[w.dstComp];
        else if (w.dstComp >= 0) w.dstComp = -1;
        if (!wireGroups.count(w.group))
            wireGroups[w.group] = m_circuit->nextWireGroup++;
        w.group = wireGroups[w.group];
        for (auto& p : w.points) p += wxPoint(m_clipOffset, m_clipOffset);
    }

    for (auto& c : m_circuit->comps) c.selected = false;
    for (auto& c : tmp.comps)
    {
        m_circuit->comps.push_back(c);
        m_circuit->comps.back().selected = true;
    }
    for (auto& w : tmp.wires) m_circuit->wires.push_back(w);

    m_circuit->RefreshWireEndpoints();
    DoEvaluate();
    Refresh();
    NotifySelectionChanged();
}

void CircuitCanvas::AddVertexToSelectedWire()
{
    if (!m_circuit) return;

    int idx = -1;
    for (int i = 0; i < (int)m_circuit->wires.size(); ++i)
        if (m_circuit->wires[i].selected) { idx = i; break; }
    if (idx < 0) { Status(wxT("请先选择一段导线")); return; }

    Wire w = m_circuit->wires[idx];
    std::vector<wxPoint> pts = w.points;
    if (pts.size() != 2 || !wnInsertJog(pts, 0))
    {
        Status(wxT("该线段太短，无法插入拐点"));
        return;
    }

    SnapshotForUndo();

    // One segment becomes several, still one Wire each.
    std::vector<Wire> add;
    for (size_t i = 0; i + 1 < pts.size(); ++i)
    {
        Wire s;
        s.group = w.group;
        s.points.push_back(pts[i]);
        s.points.push_back(pts[i + 1]);
        if (i == 0) { s.srcComp = w.srcComp; s.srcPort = w.srcPort; }
        if (i + 2 == pts.size()) { s.dstComp = w.dstComp; s.dstPort = w.dstPort; }
        add.push_back(s);
    }

    std::vector<Wire>::iterator it = m_circuit->wires.begin() + idx;
    it = m_circuit->wires.erase(it);
    m_circuit->wires.insert(it, add.begin(), add.end());

    DoEvaluate();
    Refresh();
}

// Merge the selected segment with a neighbour when both lie on one line, so
// the shared vertex disappears.
void CircuitCanvas::RemoveVertexFromSelectedWire()
{
    if (!m_circuit) return;

    int idx = -1;
    for (int i = 0; i < (int)m_circuit->wires.size(); ++i)
        if (m_circuit->wires[i].selected) { idx = i; break; }
    if (idx < 0) { Status(wxT("请先选择一段导线")); return; }

    const Wire w = m_circuit->wires[idx];
    if (w.points.size() != 2) return;

    for (int end = 0; end < 2; ++end)
    {
        const wxPoint pivot = w.points[end];
        const wxPoint outer = w.points[1 - end];

        for (int j = 0; j < (int)m_circuit->wires.size(); ++j)
        {
            if (j == idx) continue;
            const Wire o = m_circuit->wires[j];
            if (o.points.size() != 2) continue;
            if (o.points[0] != pivot && o.points[1] != pivot) continue;

            const wxPoint other = (o.points[0] == pivot) ? o.points[1] : o.points[0];
            if (outer == other) continue;                 // zero length result
            if (!wnAligned(outer, other)) continue;       // not a straight line

            SnapshotForUndo();
            Wire merged;
            merged.group = w.group;
            merged.points.push_back(outer);
            merged.points.push_back(other);

            // Port references follow the geometry, as everywhere else.
            auto portAt = [&](const wxPoint& g, int& comp, int& port)
            {
                comp = -1;
                port = -1;
                for (auto& c : m_circuit->comps)
                    for (size_t k = 0; k < c.ports.size(); ++k)
                        if (c.ports[k].grid == g) { comp = c.id; port = (int)k; return; }
            };
            portAt(merged.points[0], merged.srcComp, merged.srcPort);
            portAt(merged.points[1], merged.dstComp, merged.dstPort);

            int a = idx, b = j;
            if (a > b) std::swap(a, b);
            m_circuit->wires.erase(m_circuit->wires.begin() + b);
            m_circuit->wires.erase(m_circuit->wires.begin() + a);
            m_circuit->wires.push_back(merged);

            DoEvaluate();
            Refresh();
            return;
        }
    }
    Status(wxT("该线段两端没有可合并的拐点"));
}

void CircuitCanvas::Undo()
{
    if (m_undo.empty()) { Status(wxT("没有可撤销的操作")); return; }
    m_redo.push_back(SerializeCircuit(*m_circuit));
    DeserializeCircuit(m_undo.back(), *m_circuit);
    m_undo.pop_back();
    m_drawingWire = false;
    Refresh();
    NotifySelectionChanged();
}

void CircuitCanvas::Redo()
{
    if (m_redo.empty()) { Status(wxT("没有可重做的操作")); return; }
    m_undo.push_back(SerializeCircuit(*m_circuit));
    DeserializeCircuit(m_redo.back(), *m_circuit);
    m_redo.pop_back();
    m_drawingWire = false;
    Refresh();
    NotifySelectionChanged();
}

void CircuitCanvas::ResetSimulation()  { DoEvaluate(); Refresh(); }
void CircuitCanvas::StepSimulation()   { DoEvaluate(); Refresh(); }

wxString CircuitCanvas::Serialize() const
{
    return m_circuit ? SerializeCircuit(*m_circuit) : wxString();
}

bool CircuitCanvas::Deserialize(const wxString& s)
{
    if (!m_circuit) return false;
    if (!DeserializeCircuit(s, *m_circuit)) return false;
    m_drawingWire = false;
    DoEvaluate();
    Refresh();
    NotifySelectionChanged();
    return true;
}

void CircuitCanvas::SetSelectedLabel(const wxString& s)
{
    if (!m_circuit) return;
    for (auto& c : m_circuit->comps)
        if (c.selected) { c.label = s; Refresh(); return; }
}

void CircuitCanvas::SetSelectedInputs(int n)
{
    if (!m_circuit) return;
    for (auto& c : m_circuit->comps)
    {
        if (c.selected && IsVariableInputGate(c.type))
        {
            if (n < 1) n = 1;
            if (n > 8) n = 8;
            int oldInputs = c.inputs;

            SnapshotWiresAndPorts();
            c.inputs = n;
            c.RebuildPorts();

            // Inputs keep their indices. The output moves when the input count
            // changes, so remap it and drop wires attached to removed inputs.
            std::vector<Wire> kept;
            kept.reserve(m_circuit->wires.size());
            for (Wire w : m_circuit->wires)
            {
                bool remove = false;
                if (w.srcComp == c.id)
                {
                    if (w.srcPort == oldInputs) w.srcPort = c.inputs;
                    else if (w.srcPort >= c.inputs) remove = true;
                }
                if (w.dstComp == c.id && w.dstPort >= c.inputs)
                    remove = true;
                if (!remove) kept.push_back(w);
            }
            m_circuit->wires.swap(kept);

            RepairWiresForMove(wxPoint(0, 0));   // ports moved, no wire dragged
            DoEvaluate();
            Refresh();
            return;
        }
    }
}

void CircuitCanvas::SetSelectedPinValue(int v)
{
    if (!m_circuit) return;
    for (auto& c : m_circuit->comps)
        if (c.selected && c.type == CT_PinIn) { c.value = v ? 1 : 0; DoEvaluate(); Refresh(); return; }
}

bool CircuitCanvas::ExportImage(const wxString& path)
{
    if (!m_circuit) return false;
    wxRect gb = ComputeGridBounds();
    if (gb.IsEmpty()) { Status(wxT("没有可导出的内容")); return false; }

    int margin = GRID;
    wxPoint saveOffset = m_offset;
    double  saveZoom   = m_zoom;

    // Save and clear the selection so the image is clean.
    std::vector<int> selIds;
    for (auto& c : m_circuit->comps) if (c.selected) { selIds.push_back(c.id); c.selected = false; }

    m_zoom = 1.0;
    m_offset = wxPoint(-(gb.x - margin) * GRID, -(gb.y - margin) * GRID);
    int w = (gb.width + 2 * margin) * GRID;
    int h = (gb.height + 2 * margin) * GRID;

    wxBitmap bmp(w, h);
    {
        wxMemoryDC dc(bmp);
        dc.SetBackground(wxBrush(*wxWHITE));
        dc.Clear();
        DrawScene(dc);
    }

    m_offset = saveOffset;
    m_zoom = saveZoom;
    for (int id : selIds) { Component* c = m_circuit->FindComponent(id); if (c) c->selected = true; }
    Refresh();

    wxImage img = bmp.ConvertToImage();
    if (!img.SaveFile(path, wxBITMAP_TYPE_PNG))
    {
        Status(wxT("图像写入失败"));
        return false;
    }
    Status(wxT("图像已导出至 ") + path);
    return true;
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

void CircuitCanvas::OnPaint(wxPaintEvent&)
{
    wxBufferedPaintDC dc(this);
    DrawScene(dc);
}

void CircuitCanvas::DrawScene(wxDC& dc)
{
    dc.SetBackground(wxBrush(*wxWHITE));
    dc.Clear();
    if (!m_circuit) return;

    for (auto& w : m_circuit->wires) DrawWire(dc, w);
    DrawJunctions(dc);
    for (auto& c : m_circuit->comps) DrawComponent(dc, c);
    for (auto& t : m_circuit->texts) DrawTextLabel(dc, t);
    DrawPortDots(dc);

    // Live wire preview: exactly the geometry a release would commit.
    if (m_drawingWire)
    {
        const std::vector<wxPoint> preview = BuildCommitPath();
        dc.SetPen(wxPen(wxColour(0, 120, 255), 2));
        for (size_t i = 0; i + 1 < preview.size(); ++i)
        {
            wxPoint a = ToPx(preview[i]);
            wxPoint b = ToPx(preview[i + 1]);
            if (a.x == b.x || a.y == b.y)
            {
                dc.DrawLine(a, b);
            }
            else
            {
                wxPoint corner(b.x, a.y);
                dc.DrawLine(a, corner);
                dc.DrawLine(corner, b);
            }
        }

        // Circle showing where the path would land.
        const wxPoint t = ToPx(m_wireTarget);
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.SetPen(wxPen(m_wireTargetOnPort ? wxColour(0, 160, 0)
                                           : wxColour(0, 120, 255), 2));
        dc.DrawCircle(t, m_wireTargetOnPort ? 6 : 4);
    }

    // Rubber-band selection rectangle.
    if (m_rubberBanding)
    {
        dc.SetPen(wxPen(wxColour(0, 0, 255), 1, wxPENSTYLE_DOT));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRectangle(m_selectRect);
    }
}

void CircuitCanvas::DrawWire(wxDC& dc, const Wire& w)
{
    wxColour col(30, 30, 30);
    if (m_sim)
        col = ValueColour(w.value);
    for (size_t i = 0; i + 1 < w.points.size(); ++i)
    {
        bool segmentSelected = w.selected &&
            (w.selectedSegment < 0 || w.selectedSegment == (int)i);
        dc.SetPen(wxPen(col, segmentSelected ? 4 : 3));
        wxPoint a = ToPx(w.points[i]);
        wxPoint b = ToPx(w.points[i + 1]);
        if (a.x == b.x || a.y == b.y)
        {
            dc.DrawLine(a, b);
        }
        else
        {
            wxPoint corner(b.x, a.y);
            dc.DrawLine(a, corner);
            dc.DrawLine(corner, b);
        }
    }
}

void CircuitCanvas::DrawJunctions(wxDC& dc)
{
    if (!m_circuit) return;

    // Cheap signature so the (quadratic) net extraction only runs when the
    // wire geometry actually changed.
    size_t sig = 1469598103934665603ull;
    for (const Wire& w : m_circuit->wires)
    {
        sig ^= (size_t)w.points.size() + 0x9e3779b9 + (sig << 6) + (sig >> 2);
        for (const wxPoint& p : w.points)
        {
            sig ^= (size_t)p.x * 73856093u ^ (size_t)p.y * 19349663u;
            sig = (sig << 7) | (sig >> (sizeof(size_t) * 8 - 7));
        }
    }
    for (const wxPoint& p : m_circuit->junctions)
    {
        sig ^= (size_t)p.x * 73856093u ^ (size_t)p.y * 19349663u;
        sig = (sig << 7) | (sig >> (sizeof(size_t) * 8 - 7));
    }

    if (sig != m_junctionSig)
    {
        m_junctionSig = sig;
        m_junctions.clear();

        std::vector<std::vector<wxPoint>> polys;
        polys.reserve(m_circuit->wires.size());
        for (const Wire& w : m_circuit->wires)
            if (w.points.size() >= 2) polys.push_back(w.points);

        // A dot is drawn wherever three or more wire directions meet, plus
        // every point the user explicitly marked as a connection.
        const std::vector<wnNet> nets =
            wnBuildNets(polys, std::vector<wxPoint>(), m_circuit->junctions);
        for (const wnNet& net : nets)
            m_junctions.insert(m_junctions.end(), net.junctions.begin(), net.junctions.end());
        for (const wxPoint& p : m_circuit->junctions)
        {
            bool known = false;
            for (const wxPoint& q : m_junctions) if (q == p) { known = true; break; }
            if (!known) m_junctions.push_back(p);
        }
    }
    if (m_junctions.empty()) return;

    int r = (int)wxRound(3.5 * m_zoom);
    if (r < 2) r = 2;
    dc.SetPen(wxPen(wxColour(20, 20, 20), 1));
    dc.SetBrush(wxBrush(wxColour(20, 20, 20)));

    for (const wxPoint& p : m_junctions)
        dc.DrawCircle(ToPx(p), r);
}

void CircuitCanvas::DrawComponent(wxDC& dc, const Component& c)
{
    double x0 = m_offset.x + c.gridPos.x * GRID * m_zoom;
    double y0 = m_offset.y + c.gridPos.y * GRID * m_zoom;
    double w  = c.Width()  * GRID * m_zoom;
    double h  = c.Height() * GRID * m_zoom;
    double x1 = x0 + w, y1 = y0 + h, ym = (y0 + y1) / 2.0;

    wxColour fill    = c.selected ? wxColour(219, 234, 255) : *wxWHITE;
    wxColour outline = c.selected ? wxColour(0, 90, 220) : wxColour(20, 20, 20);
    const bool inverted = IsInvertedGate(c.type);
    const double bubbleR = GRID * m_zoom / 2.0;
    const double bodyX1 = inverted ? x1 - 2.0 * bubbleR : x1;

    std::vector<wxPoint> poly;

    switch (c.type)
    {
        case CT_And:
        case CT_Nand:
        {
            // D-shape: straight left edge, semicircular right side.
            double rx = bodyX1 - x0, ry = h / 2.0;
            for (int i = 0; i <= 32; ++i)
            {
                double a = -kPi / 2.0 + kPi * i / 32.0;
                poly.push_back(P(x0 + rx * cos(a), ym + ry * sin(a)));
            }
            break;
        }
        case CT_Or:
        case CT_Nor:
        case CT_Xor:
        case CT_Xnor:
        {
            double bw = bodyX1 - x0;
            DPt A(x0, y0), B(x0, y1), C(bodyX1, ym);
            DPt cu(x0 + 0.55 * bw, y0);
            DPt cl(x0 + 0.55 * bw, y1);
            DPt cb(x0 - 0.28 * bw, ym);
            QuadPts(A, cu, C, poly, 24);   // top edge
            QuadPts(C, cl, B, poly, 24);   // bottom edge
            QuadPts(B, cb, A, poly, 24);   // back edge
            break;
        }
        case CT_Not:
        case CT_Buffer:
            poly.push_back(P(x0, y0));
            poly.push_back(P(x0, y1));
            poly.push_back(P(bodyX1, ym));
            break;
        case CT_PinIn:
            poly.push_back(P(x0, y0));
            poly.push_back(P(x0, y1));
            poly.push_back(P(x1, ym));
            break;
        case CT_PinOut:
            poly.push_back(P(x1, y0));
            poly.push_back(P(x1, y1));
            poly.push_back(P(x0, ym));
            break;
        case CT_SubCircuit:
            break;      // drawn as a labelled black box below
    }

    if (!poly.empty())
    {
        dc.SetPen(wxPen(outline, 2));
        dc.SetBrush(wxBrush(fill));
        dc.DrawPolygon((int)poly.size(), &poly[0], 0, 0);
    }

    // ---- sub-circuit instance: a box with the pins of its definition -----
    if (c.type == CT_SubCircuit)
    {
        wxColour boxFill = c.selected ? wxColour(219, 234, 255) : wxColour(246, 246, 250);
        wxColour boxLine = c.subBroken ? wxColour(200, 40, 40) : outline;

        dc.SetPen(wxPen(boxLine, 2));
        dc.SetBrush(wxBrush(boxFill));
        dc.DrawRectangle((int)wxRound(x0), (int)wxRound(y0),
                         (int)wxRound(w),  (int)wxRound(h));

        // Pin stubs so the instance reads like a real component.
        const int stub = (int)wxRound(GRID * m_zoom * 0.4);
        dc.SetPen(wxPen(boxLine, 2));
        for (const Port& p : c.ports)
        {
            wxPoint pp = ToPx(p.grid);
            if (p.isOutput) dc.DrawLine(pp.x, pp.y, pp.x + stub, pp.y);
            else            dc.DrawLine(pp.x - stub, pp.y, pp.x, pp.y);
        }

        wxString cap = c.subName.IsEmpty() ? wxString(wxT("?")) : c.subName;
        if (c.subBroken) cap += wxT(" (未找到)");
        dc.SetTextForeground(c.subBroken ? wxColour(200, 40, 40) : wxColour(0, 0, 0));
        dc.SetFont(GetFont());
        wxSize ts = dc.GetTextExtent(cap);
        dc.DrawText(cap,
                    (int)wxRound((x0 + x1) / 2.0 - ts.GetWidth() / 2.0),
                    (int)wxRound(ym - ts.GetHeight() / 2.0));
    }

    // XOR / XNOR extra curve near the back edge.
    if (c.type == CT_Xor || c.type == CT_Xnor)
    {
        std::vector<wxPoint> back;
        double bw = bodyX1 - x0;
        QuadPts(DPt(x0 - 0.10 * bw, y0 + 0.08 * h),
                DPt(x0 - 0.40 * bw, ym),
                DPt(x0 - 0.10 * bw, y1 - 0.08 * h),
                back, 24);
        dc.SetPen(wxPen(outline, 2));
        for (size_t i = 0; i + 1 < back.size(); ++i)
            dc.DrawLine(back[i], back[i + 1]);
    }

    // Inversion bubble for NOT / NAND / NOR / XNOR.
    if (inverted)
    {
        wxPoint outPx(-1, -1);
        for (auto& p : c.ports) if (p.isOutput) { outPx = ToPx(p.grid); break; }
        if (outPx.x >= 0)
        {
            int br = (int)wxRound(bubbleR);
            dc.SetPen(wxPen(outline, 2));
            dc.SetBrush(wxBrush(fill));
            dc.DrawCircle(outPx.x - br, outPx.y, br);
        }
    }

    // Label below the component.
    if (!c.label.IsEmpty())
    {
        dc.SetTextForeground(wxColour(0, 0, 0));
        dc.SetFont(GetFont());
        wxSize ts = dc.GetTextExtent(c.label);
        int tx = (int)wxRound((x0 + x1) / 2.0 - ts.GetWidth() / 2.0);
        int ty = (int)wxRound(y1 + 4);
        dc.DrawText(c.label, tx, ty);
    }

    // Pin values while simulating.
    if (m_sim && (c.type == CT_PinIn || c.type == CT_PinOut))
    {
        LogicValue v;
        if (c.type == CT_PinIn) v = c.value ? LV_One : LV_Zero;
        else v = c.ports.empty() ? LV_Unknown : c.ports[0].value;
        wxString txt = (v == LV_One) ? wxT("1") : (v == LV_Zero) ? wxT("0") : wxT("X");
        dc.SetTextForeground(ValueColour(v));
        wxSize ts = dc.GetTextExtent(txt);
        dc.DrawText(txt, (int)wxRound(x0 + w / 2.0 - ts.x / 2.0), (int)wxRound(ym - ts.y - 4));
    }
}

void CircuitCanvas::DrawTextLabel(wxDC& dc, const TextLabel& t)
{
    dc.SetTextForeground(wxColour(0, 0, 0));
    dc.SetFont(GetFont());
    wxPoint p = ToPx(t.grid);
    dc.DrawText(t.text, p.x, p.y);
}

void CircuitCanvas::DrawPortDots(wxDC& dc)
{
    if (!m_circuit) return;
    int r = (int)wxRound(2.5 * m_zoom);
    if (r < 2) r = 2;

    // A pin that nothing is plugged into is the number one reason a circuit
    // silently does nothing, so the two states look clearly different:
    // wired = filled dark dot, floating = hollow warm ring.
    for (auto& c : m_circuit->comps)
    {
        for (size_t k = 0; k < c.ports.size(); ++k)
        {
            const wxPoint pp = ToPx(c.ports[k].grid);
            if (PortIsConnected(c.id, (int)k))
            {
                dc.SetPen(wxPen(wxColour(60, 60, 60), 1));
                dc.SetBrush(wxBrush(wxColour(60, 60, 60)));
                dc.DrawCircle(pp, r);
            }
            else
            {
                dc.SetPen(wxPen(wxColour(198, 74, 40), 2));
                dc.SetBrush(wxBrush(*wxWHITE));
                dc.DrawCircle(pp, r + 1);
            }
        }
    }

    // Hover / drag feedback: ring the connection point the mouse is on, so it
    // is obvious where a wire would attach before the button goes down.
    if (!m_hasMouse || m_tool != TOOL_EDIT) return;

    int compId = -1, portIdx = -1;
    if (FindPortAt(m_curPx, compId, portIdx))
    {
        const Port* p = m_circuit->FindPort(compId, portIdx);
        if (p)
        {
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.SetPen(wxPen(wxColour(0, 160, 0), 2));
            dc.DrawCircle(ToPx(p->grid), r + 4);
        }
    }
}
