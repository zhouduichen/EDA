#pragma once

#include <optional>

#include <wx/panel.h>

#include "contracts/component_library.hpp"
#include "contracts/geometry.hpp"
#include "contracts/ids.hpp"
#include "contracts/wire_model.hpp"
#include "src/editor/editor_controller.hpp"

class wxKeyEvent;
class wxMouseEvent;
class wxPaintEvent;
class wxDC;

namespace cse::ui {

class SchematicCanvas final : public wxPanel {
public:
    SchematicCanvas(wxWindow* parent, editor::EditorController& editor,
                    const IComponentLibrary& library);

    void ZoomIn();
    void ZoomOut();
    void FitContent();

private:
    struct PinHit {
        PinRef pin;
        Point2D position;
    };

    void OnPaint(wxPaintEvent& event);
    void OnMouseLeftDown(wxMouseEvent& event);
    void OnMouseLeftUp(wxMouseEvent& event);
    void OnMouseMove(wxMouseEvent& event);
    void OnMouseRightDown(wxMouseEvent& event);
    void OnMouseWheel(wxMouseEvent& event);
    void OnKeyDown(wxKeyEvent& event);

    Point2D ScreenToModel(wxPoint screen_point) const;
    wxPoint ModelToScreen(Point2D model_point) const;
    Point2D GetPinPosition(const ComponentInstance& component,
                           const ComponentDefinition& definition,
                           const PinDefinition& pin) const;
    Point2D RotateRelative(Point2D relative, Rect2D bounds, double rotation_deg) const;
    std::optional<ComponentId> HitComponent(Point2D model_point) const;
    std::optional<PinHit> HitPin(Point2D model_point) const;
    std::optional<WireId> HitWire(Point2D model_point) const;
    Point2D ResolveEndpoint(const WireEndpoint& endpoint) const;
    bool IsComponentSelected(ComponentId component_id) const;
    bool IsWireSelected(WireId wire_id) const;
    void DrawGrid(wxDC& dc);
    void DrawWires(wxDC& dc, const SchematicSnapshot& snapshot);
    void DrawComponents(wxDC& dc, const SchematicSnapshot& snapshot);
    void DrawTemporaryWire(wxDC& dc);
    void RefreshEditorViews();

    editor::EditorController& editor_;
    const IComponentLibrary& library_;
    double zoom_{1.0};
    double offset_x_{40.0};
    double offset_y_{40.0};
    Point2D last_mouse_model_;
    bool dragging_component_{false};
    ComponentId dragging_component_id_{kInvalidComponentId};
    std::optional<Point2D> drag_preview_position_;
    Point2D pan_start_offset_;
    wxPoint pan_start_screen_;
};

}  // namespace cse::ui
