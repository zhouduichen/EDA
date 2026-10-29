///////////////////////////////////////////////////////////////////////////
// canvas.h -- CircuitCanvas : the drawing surface for the circuit.
//
// The canvas is created as a child of the framework's m_panel4, so the
// generated layout is left untouched.  All coordinates are grid based; the
// grid itself is not painted (Logisim-like white background without dots).
///////////////////////////////////////////////////////////////////////////

#ifndef EDA_CANVAS_H
#define EDA_CANVAS_H

#include <wx/panel.h>
#include <functional>
#include <map>
#include <vector>
#include "model.h"
#include "wirenet.h"

// Active tool.  The toolbar tools and the palette (tree) select one of these.
enum Tool
{
    TOOL_EDIT = 0,   // edit selection / add wires (the default)
    TOOL_POKE,       // change values within circuit
    TOOL_TEXT,       // edit text in circuit
    TOOL_PIN_IN,
    TOOL_PIN_OUT,
    TOOL_NOT,
    TOOL_AND,
    TOOL_OR,
    TOOL_NAND,
    TOOL_NOR,
    TOOL_XOR,
    TOOL_XNOR,
    TOOL_BUFFER,
    TOOL_SUBCIRCUIT  // place an instance of another circuit of the project
};

class CircuitCanvas : public wxPanel
{
public:
    explicit CircuitCanvas(wxWindow* parent);

    void SetCircuit(Circuit* c);
    Circuit* GetCircuit() { return m_circuit; }

    // Every circuit of the project, so that sub-circuit instances can be
    // resolved and evaluated.  Owned by the frame.
    void SetProject(const std::vector<Circuit>* p) { m_project = p; Refresh(); }

    // Which circuit TOOL_SUBCIRCUIT should instantiate.
    void SetSubCircuitToPlace(const wxString& n) { m_subToPlace = n; }
    const wxString& GetSubCircuitToPlace() const { return m_subToPlace; }

    void SetTool(int tool);
    int  GetTool() const { return m_tool; }

    void SetSimulationEnabled(bool on);
    bool IsSimulationEnabled() const { return m_sim; }

    void SetZoom(double z);
    double GetZoom() const { return m_zoom; }

    // Callbacks used by the owning frame.
    void SetStatusCallback(std::function<void(const wxString&)> cb)    { m_onStatus = cb; }
    void SetSelectionCallback(std::function<void()> cb)                { m_onSelectionChanged = cb; }
    void SetZoomCallback(std::function<void(double)> cb)               { m_onZoom = cb; }
    void SetSimulationCallback(std::function<void(bool)> cb)             { m_onSimulation = cb; }

    // ---- Actions invoked by menus / toolbar / keys ----
    void NewCircuit();
    void DeleteSelection();
    void SelectAll();
    void DuplicateSelection();
    void CutSelection();
    void CopySelection();
    void PasteSelection();
    void AddVertexToSelectedWire();
    void RemoveVertexFromSelectedWire();
    void Undo();
    void Redo();
    void ResetSimulation();
    void StepSimulation();
    bool ExportImage(const wxString& path);

    wxString Serialize() const;
    bool Deserialize(const wxString& s);

    // Property grid support.
    void SetSelectedLabel(const wxString& s);
    void SetSelectedInputs(int n);
    void SetSelectedPinValue(int v);

private:
    Circuit* m_circuit = nullptr;
    const std::vector<Circuit>* m_project = nullptr;
    wxString m_subToPlace;
    int    m_tool  = TOOL_EDIT;
    double m_zoom  = 1.0;
    wxPoint m_offset;           // pan offset in pixels
    bool   m_sim   = false;

    std::function<void(const wxString&)> m_onStatus;
    std::function<void()>                m_onSelectionChanged;
    std::function<void(double)>          m_onZoom;
    std::function<void(bool)>            m_onSimulation;

    // ---- Wire drawing state (Logisim WiringTool behaviour) -----------------
    // Press on a port (or Ctrl+drag from a wire) and drag: the path follows
    // the mouse, adding a corner every time the movement changes direction and
    // removing one when the mouse comes back.  Releasing finishes the run.
    bool   m_drawingWire = false;
    int    m_wireSrcComp = -1, m_wireSrcPort = -1;
    bool   m_wireSrcIsPort = false;      // path starts on a component port
    bool   m_wireSrcIsOutput = false;
    // The run in progress.  It is a *routed* polyline, not the raw pointer
    // trail: every point the user actually committed, plus the shortest clean
    // route from the last of them to where the pointer is now.  That is what
    // makes a first-time draw come out straight instead of staircased.
    std::vector<wxPoint> m_routed;
    wxPoint m_wireStart;                 // fixed start of the run
    std::vector<wxPoint> m_wirePrefix;   // corners the user pinned by clicking
    wxPoint m_wireDownPx;
    bool   m_wireDragged = false;

    // Click-to-place ("sticky") run.  A plain click on a port does not cancel
    // the wire: the run stays alive and follows the mouse with no button held,
    // every further click pins one more corner, and a click on a port -- or a
    // double click -- finishes it.  Holding the button down and dragging keeps
    // working exactly as before; this is the alternative for long routes where
    // holding the button the whole way is just painful, and it is also how a
    // deliberate zig-zag is built.
    bool   m_wireSticky = false;

    // Where the path would land right now (refreshed on every mouse move).
    wxPoint m_wireTarget;
    bool    m_wireTargetOnPort = false;
    bool    m_wireTargetIsOutput = false;   // so the route meets it from its side
    int     m_wireTargetComp = -1, m_wireTargetPort = -1;
    bool    m_wireTargetOnWire = false;
    int     m_wireTargetWire = -1;

    // Snapshot used by the Logisim style "wire repair" while moving components.
    std::vector<Wire>                 m_wireSnapshot;
    std::map<int, std::vector<wxPoint>> m_portSnapshot;

    bool m_moving = false;
    bool m_moveSnapshotted = false;
    bool m_movingText = false;
    bool m_textMoveSnapshotted = false;
    bool m_textMoved = false;
    int  m_textDragIndex = -1;
    std::vector<wxPoint> m_textMoveOrigins;
    bool m_movingWire = false;
    bool m_wireMoveSnapshotted = false;
    bool m_wireBranchPending = false;
    int  m_dragWireIndex = -1;

    // Dragging the loose end of an existing wire.  Pulling it out lengthens the
    // wire, pushing it back shortens it, and moving it sideways turns it --
    // which is what people expect to be able to do to a wire they already drew,
    // instead of deleting it and starting over.
    bool m_draggingEnd = false;
    bool m_endMoveSnapshotted = false;
    int  m_endWireIndex = -1;
    bool m_endAtStart = false;
    wxPoint m_wireDragStartGrid;
    wxPoint m_wireBranchStartPx;
    wxPoint m_wireBranchPoint;
    bool m_rubberBanding = false;
    bool m_panning = false;
    wxPoint m_dragStartPx;
    wxPoint m_dragStartGrid;
    wxPoint m_panStartPx;
    wxPoint m_panStartOffset;
    wxRect  m_selectRect;
    std::vector<wxPoint> m_moveOrigins;

    wxPoint m_curGrid;
    wxPoint m_curPx;
    bool    m_hasMouse = false;

    // Cached junction dots; rebuilt only when the wire geometry changes.
    std::vector<wxPoint> m_junctions;
    size_t   m_junctionSig = 0;

    wxString m_clipboard;
    int      m_clipOffset = 0;

    std::vector<wxString> m_undo;
    std::vector<wxString> m_redo;

    // Every component body as a rectangle a route must not cross.  Used by the
    // live router and by the move repair, so a wire walks around a component
    // instead of disappearing underneath it.
    std::vector<wnRect> ComponentBlocks() const;

    // Coordinate transforms.
    wxPoint ToPx(const wxPoint& g) const;
    wxPoint ToGrid(const wxPoint& p) const;
    wxRect  CompRectPx(const Component& c) const;

    // Hit testing.
    bool FindPortAt(const wxPoint& px, int& compId, int& portIdx) const;
    Component* HitComponent(const wxPoint& px);
    int HitText(const wxPoint& px) const;
    int HitWire(const wxPoint& px, int& segment, wxPoint& snapGrid) const;
    bool FindFreeWireEnd(const wxPoint& px, int& wireIndex, wxPoint& endGrid) const;

    // Helpers.
    void SnapshotForUndo();
    void NotifySelectionChanged();
    void Status(const wxString& s);
    void DoEvaluate();
    void ClearSelection();
    void SelectInRect(const wxRect& r);
    Component* PlaceComponent(ComponentType t, const wxPoint& grid);
    bool PortIsConnected(int compId, int portIdx) const;
    bool BindEndToPort(Wire& w, bool atStart);   // geometry only, no checks

    // Wire drawing / editing.
    void StartWire(int compId, int portIdx, const wxPoint& px);
    void StartWireAtPoint(const wxPoint& grid, const wxPoint& px);
    void UpdateWireTarget(const wxPoint& px);
    void UpdateWirePath(const wxPoint& g);
    // Rebuild m_routed from the pinned prefix plus the shortest clean leg to
    // `target`.
    void ComputeRouted(const wxPoint& target);
    // The exact geometry FinishWire() would commit right now.  The live preview
    // paints this, so the rubber band never promises something else than what
    // is actually written into the circuit on release.
    std::vector<wxPoint> BuildCommitPath() const;
    void FinishWire();
    void CancelWire();
    int  AppendRun(const std::vector<wxPoint>& path, int group);
    void SnapshotWiresAndPorts();
    // Logisim WireRepair: re-lay every wire from the snapshot using the
    // "old point -> new point" displacement table of the current drag.  Used
    // by component moves *and* wire moves, so whatever touches the dragged
    // object is stretched instead of torn apart.
    void RepairWiresForMove(const wxPoint& delta);
    void DropDegenerateWires();
    void DeleteWiresOnPorts(const std::vector<wxPoint>& portPts);
    // Throw away connection marks whose wires are gone, so a deleted wire can
    // never leave a dot behind that nothing can remove.
    void PruneJunctions();

    wxRect ComputeGridBounds() const;
    wxRect ComputeContentBoundsPx() const;

    // Interaction sub-handlers.
    void OnEditLeftDown(const wxMouseEvent& e);
    void OnPlaceClick(const wxPoint& grid);
    void OnPokeClick(const wxMouseEvent& e);
    void OnTextClick(const wxMouseEvent& e);
    void OnMoveDrag(const wxMouseEvent& e);
    void OnTextMoveDrag(const wxMouseEvent& e);
    void OnWireMoveDrag(const wxMouseEvent& e);
    void OnWireEndDrag(const wxMouseEvent& e);
    void EditTextLabel(int index);
    void BeginTextDrag(int index, const wxMouseEvent& e);

    // Painting.
    void OnPaint(wxPaintEvent& e);
    void DrawScene(wxDC& dc);
    void DrawWire(wxDC& dc, const Wire& w);
    // Blue overprint and end grips, so "this wire is selected -- dragging it
    // now moves it" is impossible to miss.
    void DrawWireSelection(wxDC& dc);
    void DrawJunctions(wxDC& dc);
    void DrawComponent(wxDC& dc, const Component& c);
    void DrawTextLabel(wxDC& dc, const TextLabel& t);
    void DrawPortDots(wxDC& dc);

    // Events.
    void OnMouse(wxMouseEvent& e);
    void OnLeftDClick(wxMouseEvent& e);
    void ToggleJunction(const wxPoint& px);
    void OnMouseWheel(wxMouseEvent& e);
    void OnKey(wxKeyEvent& e);
    void OnErase(wxEraseEvent& e) { /* no-op: handled by buffered paint */ }
};

#endif // EDA_CANVAS_H
