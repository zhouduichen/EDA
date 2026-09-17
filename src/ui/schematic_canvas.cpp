#include "src/ui/schematic_canvas.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <wx/dcbuffer.h>
#include <wx/dc.h>
#include <wx/settings.h>

namespace cse::ui {

namespace {

constexpr double kPinHitRadius = 8.0;

double DistanceSquared(Point2D first, Point2D second) {
    const double dx = first.x - second.x;
    const double dy = first.y - second.y;
    return dx * dx + dy * dy;
}

double DistanceToSegmentSquared(Point2D point, Point2D start, Point2D end) {
    const double dx = end.x - start.x;
    const double dy = end.y - start.y;
    const double length_squared = dx * dx + dy * dy;
    if (length_squared <= std::numeric_limits<double>::epsilon()) {
        return DistanceSquared(point, start);
    }
    const double parameter = std::clamp(
        ((point.x - start.x) * dx + (point.y - start.y) * dy) / length_squared, 0.0, 1.0);
    const Point2D projection{start.x + parameter * dx, start.y + parameter * dy};
    return DistanceSquared(point, projection);
}

int NormalizeQuarterTurn(double rotation_deg) {
    int quarter_turn = static_cast<int>(std::lround(rotation_deg / 90.0)) % 4;
    if (quarter_turn < 0) {
        quarter_turn += 4;
    }
    return quarter_turn;
}

wxColour PinColour(PinDirection direction) {
    switch (direction) {
        case PinDirection::kInput:
            return wxColour(90, 160, 230);
        case PinDirection::kOutput:
            return wxColour(230, 150, 70);
        case PinDirection::kBidirectional:
            return wxColour(150, 150, 150);
    }
    return wxColour(150, 150, 150);
}

}  // namespace

SchematicCanvas::SchematicCanvas(wxWindow* parent, editor::EditorController& editor,
                                 const IComponentLibrary& library)
    : wxPanel(parent, wxID_ANY), editor_(editor), library_(library) {
    SetName("panel_canvas");
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetFocus();
    Bind(wxEVT_PAINT, &SchematicCanvas::OnPaint, this);
    Bind(wxEVT_LEFT_DOWN, &SchematicCanvas::OnMouseLeftDown, this);
    Bind(wxEVT_LEFT_UP, &SchematicCanvas::OnMouseLeftUp, this);
    Bind(wxEVT_MOTION, &SchematicCanvas::OnMouseMove, this);
    Bind(wxEVT_RIGHT_DOWN, &SchematicCanvas::OnMouseRightDown, this);
    Bind(wxEVT_MOUSEWHEEL, &SchematicCanvas::OnMouseWheel, this);
    Bind(wxEVT_KEY_DOWN, &SchematicCanvas::OnKeyDown, this);
}

void SchematicCanvas::ZoomIn() {
    zoom_ = std::min(4.0, zoom_ * 1.2);
    Refresh();
}

void SchematicCanvas::ZoomOut() {
    zoom_ = std::max(0.25, zoom_ / 1.2);
    Refresh();
}

void SchematicCanvas::FitContent() {
    zoom_ = 1.0;
    offset_x_ = 40.0;
    offset_y_ = 40.0;
    Refresh();
}

void SchematicCanvas::OnPaint(wxPaintEvent& event) {
    static_cast<void>(event);
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(wxColour(248, 249, 251)));
    dc.Clear();
    DrawGrid(dc);
    const SchematicSnapshot snapshot = editor_.GetSnapshot();
    DrawWires(dc, snapshot);
    DrawComponents(dc, snapshot);
    DrawTemporaryWire(dc);
}

void SchematicCanvas::OnMouseLeftDown(wxMouseEvent& event) {
    SetFocus();
    last_mouse_model_ = ScreenToModel(event.GetPosition());
    const ToolMode tool_mode = editor_.GetToolMode();

    if (tool_mode == ToolMode::kPlaceComponent) {
        if (!editor_.PlaceComponent(last_mouse_model_)) {
            wxBell();
        }
        RefreshEditorViews();
        return;
    }

    if (tool_mode == ToolMode::kWire) {
        const std::optional<PinHit> pin_hit = HitPin(last_mouse_model_);
        if (pin_hit.has_value()) {
            const std::optional<editor::PendingWire> pending_wire = editor_.GetPendingWire();
            const bool success = pending_wire.has_value()
                                     ? editor_.CompleteWire(pin_hit->pin, pin_hit->position)
                                     : editor_.BeginWire(pin_hit->pin, pin_hit->position);
            if (!success) {
                wxBell();
            }
            RefreshEditorViews();
        }
        return;
    }

    if (tool_mode == ToolMode::kPan || event.MiddleIsDown()) {
        pan_start_screen_ = event.GetPosition();
        pan_start_offset_ = {offset_x_, offset_y_};
        CaptureMouse();
        return;
    }

    const std::optional<ComponentId> component_id = HitComponent(last_mouse_model_);
    if (component_id.has_value()) {
        editor_.SelectComponent(*component_id);
        const std::optional<ComponentInstance> component = editor_.GetComponent(*component_id);
        if (component.has_value()) {
            dragging_component_ = true;
            dragging_component_id_ = *component_id;
            drag_preview_position_ = component->position;
            CaptureMouse();
        }
    } else {
        const std::optional<WireId> wire_id = HitWire(last_mouse_model_);
        if (wire_id.has_value()) {
            editor_.SelectWire(*wire_id);
        } else {
            editor_.SelectComponent(std::nullopt);
        }
    }
    RefreshEditorViews();
}

void SchematicCanvas::OnMouseLeftUp(wxMouseEvent& event) {
    static_cast<void>(event);
    if (dragging_component_) {
        if (drag_preview_position_.has_value()) {
            static_cast<void>(editor_.MoveComponent(dragging_component_id_,
                                                     *drag_preview_position_));
        }
        dragging_component_ = false;
        dragging_component_id_ = kInvalidComponentId;
        drag_preview_position_.reset();
    }
    if (HasCapture()) {
        ReleaseMouse();
    }
    RefreshEditorViews();
}

void SchematicCanvas::OnMouseMove(wxMouseEvent& event) {
    last_mouse_model_ = ScreenToModel(event.GetPosition());
    if (dragging_component_ && event.LeftIsDown()) {
        drag_preview_position_ = editor_.SnapToGrid(last_mouse_model_);
        Refresh();
        return;
    }
    if (editor_.GetToolMode() == ToolMode::kPan && event.LeftIsDown()) {
        const wxPoint current_screen = event.GetPosition();
        offset_x_ = pan_start_offset_.x + current_screen.x - pan_start_screen_.x;
        offset_y_ = pan_start_offset_.y + current_screen.y - pan_start_screen_.y;
        Refresh();
        return;
    }
    if (editor_.GetPendingWire().has_value()) {
        Refresh();
    }
}

void SchematicCanvas::OnMouseRightDown(wxMouseEvent& event) {
    static_cast<void>(event);
    editor_.CancelWire();
    dragging_component_ = false;
    drag_preview_position_.reset();
    if (HasCapture()) {
        ReleaseMouse();
    }
    RefreshEditorViews();
}

void SchematicCanvas::OnMouseWheel(wxMouseEvent& event) {
    const wxPoint cursor = event.GetPosition();
    const Point2D model_before_zoom = ScreenToModel(cursor);
    if (event.GetWheelRotation() > 0) {
        ZoomIn();
    } else {
        ZoomOut();
    }
    offset_x_ = cursor.x - model_before_zoom.x * zoom_;
    offset_y_ = cursor.y - model_before_zoom.y * zoom_;
    Refresh();
}

void SchematicCanvas::OnKeyDown(wxKeyEvent& event) {
    if (event.ControlDown() && event.GetKeyCode() == 'Z') {
        static_cast<void>(editor_.Undo());
        RefreshEditorViews();
        return;
    }
    if (event.ControlDown() && event.GetKeyCode() == 'Y') {
        static_cast<void>(editor_.Redo());
        RefreshEditorViews();
        return;
    }

    switch (event.GetKeyCode()) {
        case 'S':
            editor_.SetToolMode(ToolMode::kSelect);
            break;
        case 'W':
            editor_.SetToolMode(ToolMode::kWire);
            break;
        case 'P':
            editor_.SetToolMode(ToolMode::kPan);
            break;
        case 'R':
            static_cast<void>(editor_.RotateSelected());
            break;
        case WXK_DELETE:
        case WXK_BACK:
            static_cast<void>(editor_.DeleteSelection());
            break;
        case WXK_ESCAPE:
            editor_.CancelWire();
            editor_.SetToolMode(ToolMode::kSelect);
            break;
        default:
            event.Skip();
            return;
    }
    RefreshEditorViews();
}

Point2D SchematicCanvas::ScreenToModel(wxPoint screen_point) const {
    return {(screen_point.x - offset_x_) / zoom_, (screen_point.y - offset_y_) / zoom_};
}

wxPoint SchematicCanvas::ModelToScreen(Point2D model_point) const {
    return {static_cast<int>(std::lround(model_point.x * zoom_ + offset_x_)),
            static_cast<int>(std::lround(model_point.y * zoom_ + offset_y_))};
}

Point2D SchematicCanvas::GetPinPosition(const ComponentInstance& component,
                                        const ComponentDefinition& definition,
                                        const PinDefinition& pin) const {
    const Point2D rotated = RotateRelative(pin.relative_position, definition.default_bounds,
                                           component.rotation_deg);
    return {component.position.x + rotated.x, component.position.y + rotated.y};
}

Point2D SchematicCanvas::RotateRelative(Point2D relative, Rect2D bounds,
                                        double rotation_deg) const {
    const Point2D center{bounds.width / 2.0, bounds.height / 2.0};
    const double local_x = relative.x - center.x;
    const double local_y = relative.y - center.y;
    switch (NormalizeQuarterTurn(rotation_deg)) {
        case 1:
            return {center.x - local_y, center.y + local_x};
        case 2:
            return {center.x - local_x, center.y - local_y};
        case 3:
            return {center.x + local_y, center.y - local_x};
        default:
            return relative;
    }
}

std::optional<ComponentId> SchematicCanvas::HitComponent(Point2D model_point) const {
    const SchematicSnapshot snapshot = editor_.GetSnapshot();
    for (auto iterator = snapshot.components.rbegin(); iterator != snapshot.components.rend();
         ++iterator) {
        const std::optional<ComponentDefinition> definition =
            library_.FindByTypeId(iterator->type_id);
        if (!definition.has_value()) {
            continue;
        }
        const Point2D relative{model_point.x - iterator->position.x,
                               model_point.y - iterator->position.y};
        const Point2D local = RotateRelative(relative, definition->default_bounds,
                                              -iterator->rotation_deg);
        if (local.x >= 0.0 && local.x <= definition->default_bounds.width && local.y >= 0.0 &&
            local.y <= definition->default_bounds.height) {
            return iterator->id;
        }
    }
    return std::nullopt;
}

std::optional<SchematicCanvas::PinHit> SchematicCanvas::HitPin(Point2D model_point) const {
    const SchematicSnapshot snapshot = editor_.GetSnapshot();
    const double hit_radius_squared = (kPinHitRadius / zoom_) * (kPinHitRadius / zoom_);
    for (auto component_iterator = snapshot.components.rbegin();
         component_iterator != snapshot.components.rend(); ++component_iterator) {
        const std::optional<ComponentDefinition> definition =
            library_.FindByTypeId(component_iterator->type_id);
        if (!definition.has_value()) {
            continue;
        }
        for (const PinDefinition& pin : definition->pins) {
            const Point2D position = GetPinPosition(*component_iterator, *definition, pin);
            if (DistanceSquared(model_point, position) <= hit_radius_squared) {
                return PinHit{PinRef{component_iterator->id, pin.local_id}, position};
            }
        }
    }
    return std::nullopt;
}

std::optional<WireId> SchematicCanvas::HitWire(Point2D model_point) const {
    const SchematicSnapshot snapshot = editor_.GetSnapshot();
    const double hit_radius_squared = (6.0 / zoom_) * (6.0 / zoom_);
    for (auto wire_iterator = snapshot.wires.rbegin(); wire_iterator != snapshot.wires.rend();
         ++wire_iterator) {
        std::vector<Point2D> points;
        points.push_back(ResolveEndpoint(wire_iterator->from));
        points.insert(points.end(), wire_iterator->route_points.begin(),
                      wire_iterator->route_points.end());
        points.push_back(ResolveEndpoint(wire_iterator->to));
        for (std::size_t index = 1; index < points.size(); ++index) {
            if (DistanceToSegmentSquared(model_point, points[index - 1], points[index]) <=
                hit_radius_squared) {
                return wire_iterator->id;
            }
        }
    }
    return std::nullopt;
}

Point2D SchematicCanvas::ResolveEndpoint(const WireEndpoint& endpoint) const {
    if (!endpoint.pin.has_value()) {
        return endpoint.position;
    }
    const std::optional<ComponentInstance> component =
        editor_.GetComponent(endpoint.pin->component_id);
    if (!component.has_value()) {
        return endpoint.position;
    }
    const std::optional<ComponentDefinition> definition =
        library_.FindByTypeId(component->type_id);
    if (!definition.has_value()) {
        return endpoint.position;
    }
    const auto pin_iterator = std::find_if(
        definition->pins.begin(), definition->pins.end(), [endpoint](const PinDefinition& pin) {
            return pin.local_id == endpoint.pin->pin_id;
        });
    if (pin_iterator == definition->pins.end()) {
        return endpoint.position;
    }
    return GetPinPosition(*component, *definition, *pin_iterator);
}

bool SchematicCanvas::IsComponentSelected(ComponentId component_id) const {
    const SelectionState selection = editor_.GetSelection();
    return std::find(selection.component_ids.begin(), selection.component_ids.end(), component_id) !=
           selection.component_ids.end();
}

bool SchematicCanvas::IsWireSelected(WireId wire_id) const {
    const SelectionState selection = editor_.GetSelection();
    return std::find(selection.wire_ids.begin(), selection.wire_ids.end(), wire_id) !=
           selection.wire_ids.end();
}

void SchematicCanvas::DrawGrid(wxDC& dc) {
    dc.SetPen(wxPen(wxColour(190, 196, 205), 1));
    const wxSize size = GetClientSize();
    const double model_left = -offset_x_ / zoom_;
    const double model_top = -offset_y_ / zoom_;
    const double model_right = (size.x - offset_x_) / zoom_;
    const double model_bottom = (size.y - offset_y_) / zoom_;
    const double first_x = std::floor(model_left / kDefaultGridSize) * kDefaultGridSize;
    const double first_y = std::floor(model_top / kDefaultGridSize) * kDefaultGridSize;
    for (double x = first_x; x <= model_right; x += kDefaultGridSize) {
        for (double y = first_y; y <= model_bottom; y += kDefaultGridSize) {
            dc.DrawPoint(ModelToScreen({x, y}));
        }
    }
}

void SchematicCanvas::DrawWires(wxDC& dc, const SchematicSnapshot& snapshot) {
    for (const WireModel& wire : snapshot.wires) {
        const wxColour colour = IsWireSelected(wire.id) ? wxColour(210, 75, 60)
                                                    : wxColour(40, 115, 150);
        dc.SetPen(wxPen(colour, IsWireSelected(wire.id) ? 3 : 2));
        std::vector<Point2D> points;
        points.push_back(ResolveEndpoint(wire.from));
        points.insert(points.end(), wire.route_points.begin(), wire.route_points.end());
        points.push_back(ResolveEndpoint(wire.to));
        for (std::size_t index = 1; index < points.size(); ++index) {
            dc.DrawLine(ModelToScreen(points[index - 1]), ModelToScreen(points[index]));
        }
    }
}

void SchematicCanvas::DrawComponents(wxDC& dc, const SchematicSnapshot& snapshot) {
    for (const ComponentInstance& component : snapshot.components) {
        const std::optional<ComponentDefinition> definition =
            library_.FindByTypeId(component.type_id);
        if (!definition.has_value()) {
            continue;
        }
        const wxPoint top_left = ModelToScreen(component.position);
        const int width = static_cast<int>(std::lround(definition->default_bounds.width * zoom_));
        const int height = static_cast<int>(std::lround(definition->default_bounds.height * zoom_));
        const bool selected = IsComponentSelected(component.id);
        dc.SetPen(wxPen(selected ? wxColour(210, 75, 60) : wxColour(55, 65, 80),
                        selected ? 3 : 2));
        if (component.type_id == "io.input") {
            dc.SetBrush(wxBrush(wxColour(224, 240, 252)));
            dc.DrawRectangle(top_left.x, top_left.y, width, height);
            dc.DrawLine(top_left.x + width - 14, top_left.y + height / 2,
                        top_left.x + width, top_left.y + height / 2);
            dc.DrawLine(top_left.x + width - 14, top_left.y + height / 2,
                        top_left.x + width - 22, top_left.y + height / 2 - 6);
            dc.DrawLine(top_left.x + width - 14, top_left.y + height / 2,
                        top_left.x + width - 22, top_left.y + height / 2 + 6);
        } else if (component.type_id == "io.output") {
            dc.SetBrush(wxBrush(wxColour(252, 239, 220)));
            dc.DrawRectangle(top_left.x, top_left.y, width, height);
        } else if (component.type_id == "logic.not") {
            dc.SetBrush(wxBrush(wxColour(235, 238, 244)));
            const wxPoint triangle[3] = {
                {top_left.x, top_left.y},
                {top_left.x + width, top_left.y + height / 2},
                {top_left.x, top_left.y + height},
            };
            dc.DrawPolygon(3, triangle);
            dc.SetBrush(wxBrush(wxColour(248, 249, 251)));
            dc.DrawCircle(top_left.x + width + 5, top_left.y + height / 2, 5);
        } else if (component.type_id == "logic.and") {
            dc.SetBrush(wxBrush(wxColour(235, 238, 244)));
            const wxPoint gate[7] = {
                {top_left.x, top_left.y},
                {top_left.x + static_cast<int>(width * 0.45), top_left.y},
                {top_left.x + static_cast<int>(width * 0.78),
                 top_left.y + static_cast<int>(height * 0.08)},
                {top_left.x + width, top_left.y + height / 2},
                {top_left.x + static_cast<int>(width * 0.78),
                 top_left.y + static_cast<int>(height * 0.92)},
                {top_left.x + static_cast<int>(width * 0.45), top_left.y + height},
                {top_left.x, top_left.y + height},
            };
            dc.DrawPolygon(7, gate);
        } else {
            dc.SetBrush(wxBrush(wxColour(235, 238, 244)));
            const wxPoint gate[8] = {
                {top_left.x, top_left.y + static_cast<int>(height * 0.22)},
                {top_left.x + static_cast<int>(width * 0.42), top_left.y},
                {top_left.x + static_cast<int>(width * 0.76),
                 top_left.y + static_cast<int>(height * 0.12)},
                {top_left.x + width, top_left.y + height / 2},
                {top_left.x + static_cast<int>(width * 0.76),
                 top_left.y + static_cast<int>(height * 0.88)},
                {top_left.x + static_cast<int>(width * 0.42), top_left.y + height},
                {top_left.x, top_left.y + static_cast<int>(height * 0.78)},
                {top_left.x + static_cast<int>(width * 0.2), top_left.y + height / 2},
            };
            dc.DrawPolygon(8, gate);
            if (component.type_id == "logic.xor") {
                dc.SetPen(wxPen(selected ? wxColour(210, 75, 60) : wxColour(55, 65, 80),
                                selected ? 3 : 2));
                const wxPoint extra_curve[3] = {
                    {top_left.x + static_cast<int>(width * 0.04),
                     top_left.y + static_cast<int>(height * 0.18)},
                    {top_left.x + static_cast<int>(width * 0.22),
                     top_left.y + height / 2},
                    {top_left.x + static_cast<int>(width * 0.04),
                     top_left.y + static_cast<int>(height * 0.82)},
                };
                dc.DrawLines(3, extra_curve);
            }
        }

        const wxSize label_size = dc.GetTextExtent(definition->display_name);
        dc.DrawText(definition->display_name, top_left.x + (width - label_size.x) / 2,
                    top_left.y + (height - label_size.y) / 2);

        for (const PinDefinition& pin : definition->pins) {
            const Point2D pin_position = GetPinPosition(component, *definition, pin);
            const wxPoint screen_pin = ModelToScreen(pin_position);
            dc.SetPen(wxPen(PinColour(pin.direction), 1));
            dc.SetBrush(wxBrush(PinColour(pin.direction)));
            dc.DrawCircle(screen_pin, std::max(3, static_cast<int>(std::lround(3.0 * zoom_))));
        }
        dc.SetTextForeground(wxColour(80, 85, 95));
        dc.DrawText(component.reference, top_left.x, top_left.y + height + 3);
        dc.SetTextForeground(*wxBLACK);
    }
}

void SchematicCanvas::DrawTemporaryWire(wxDC& dc) {
    const std::optional<editor::PendingWire> pending_wire = editor_.GetPendingWire();
    if (!pending_wire.has_value()) {
        return;
    }
    dc.SetPen(wxPen(wxColour(110, 110, 110), 2, wxPENSTYLE_DOT));
    dc.DrawLine(ModelToScreen(pending_wire->start_position), ModelToScreen(last_mouse_model_));
}

void SchematicCanvas::RefreshEditorViews() {
    Refresh();
    Update();
}

}  // namespace cse::ui
