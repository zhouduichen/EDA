#include "src/ui/schematic_canvas.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <wx/cursor.h>
#include <wx/dcbuffer.h>
#include <wx/dc.h>
#include <wx/settings.h>

#include "src/model/wire_route.hpp"

namespace cse::ui {

namespace {

constexpr double kPinHitRadius = 8.0;
constexpr double kWireHitRadius = 6.0;
constexpr double kPointEpsilon = 0.000001;

bool SamePoint(Point2D first, Point2D second) {
    return std::abs(first.x - second.x) < kPointEpsilon &&
           std::abs(first.y - second.y) < kPointEpsilon;
}

double DistanceSquared(Point2D first, Point2D second) {
    const double dx = first.x - second.x;
    const double dy = first.y - second.y;
    return dx * dx + dy * dy;
}

// 布线时的"当前锚点"：有拐点就是最后一个拐点，否则是最初按下的引脚。
Point2D WireAnchorPoint(const editor::PendingWire& pending_wire) {
    return pending_wire.route_points.empty() ? pending_wire.start_position
                                             : pending_wire.route_points.back();
}

// 这一跳的起点是不是挂在真正的引脚上：只有还没打任何拐点的第一跳才是，
// 之后的锚点是画布上定的拐点，避让时不享受"自己人"待遇。
std::optional<PinRef> WireAnchorPin(const editor::PendingWire& pending_wire) {
    if (pending_wire.route_points.empty()) {
        return pending_wire.start_pin;
    }
    return std::nullopt;
}

// 折线长度中点：给悬空导线的「?」标记找个确实落在线上的位置。
Point2D PolylineMidPoint(const std::vector<Point2D>& points) {
    if (points.empty()) {
        return {0.0, 0.0};
    }
    if (points.size() == 1) {
        return points.front();
    }
    double total_length = 0.0;
    for (std::size_t index = 1; index < points.size(); ++index) {
        total_length += std::sqrt(DistanceSquared(points[index - 1], points[index]));
    }
    if (total_length <= std::numeric_limits<double>::epsilon()) {
        return points.front();
    }
    const double half_length = total_length / 2.0;
    double travelled = 0.0;
    for (std::size_t index = 1; index < points.size(); ++index) {
        const double length = std::sqrt(DistanceSquared(points[index - 1], points[index]));
        if (length > 0.0 && travelled + length >= half_length) {
            const double ratio = (half_length - travelled) / length;
            return {points[index - 1].x + (points[index].x - points[index - 1].x) * ratio,
                    points[index - 1].y + (points[index].y - points[index - 1].y) * ratio};
        }
        travelled += length;
    }
    return points.back();
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

// 仿真可视化配色（同仿真对话框图例）：红=1，绿=0，橙=X（未知），灰=Z。
wxColour LogicColour(LogicValue value) {
    switch (value) {
        case LogicValue::kHigh:
            return wxColour(214, 48, 49);
        case LogicValue::kLow:
            return wxColour(9, 158, 90);
        case LogicValue::kUnknown:
            return wxColour(235, 160, 40);
        case LogicValue::kHighImpedance:
            return wxColour(140, 140, 140);
    }
    return wxColour(140, 140, 140);
}

wchar_t LogicChar(LogicValue value) {
    switch (value) {
        case LogicValue::kHigh:
            return L'1';
        case LogicValue::kLow:
            return L'0';
        case LogicValue::kUnknown:
            return L'X';
        case LogicValue::kHighImpedance:
            return L'Z';
    }
    return L'?';
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
    Bind(wxEVT_LEFT_DCLICK, &SchematicCanvas::OnMouseLeftDoubleClick, this);
    Bind(wxEVT_MOTION, &SchematicCanvas::OnMouseMove, this);
    Bind(wxEVT_RIGHT_DOWN, &SchematicCanvas::OnMouseRightDown, this);
    Bind(wxEVT_MOUSEWHEEL, &SchematicCanvas::OnMouseWheel, this);
    Bind(wxEVT_MOUSE_CAPTURE_LOST, &SchematicCanvas::OnMouseCaptureLost, this);
    Bind(wxEVT_KEY_DOWN, &SchematicCanvas::OnKeyDown, this);
}

void SchematicCanvas::SetSimulationResult(std::optional<cse::sim::SimulationResult> result) {
    simulation_ = std::move(result);
    RefreshEditorViews();
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
    SchematicSnapshot snapshot = editor_.GetSnapshot();
    ApplyDragPreview(snapshot);
    DrawWires(dc, snapshot);
    DrawComponents(dc, snapshot);
    DrawDragOriginMarker(dc, snapshot);
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
        const WireTarget target = ComputeWireTarget(last_mouse_model_, editor_.GetSnapshot());
        if (editor_.GetPendingWire().has_value()) {
            CommitWireSegment(target);
        } else if (target.kind == WireTarget::Kind::kPin) {
            if (!editor_.BeginWire(target.pin, target.position)) {
                wxBell();
            }
        } else {
            wxBell();  // 连线从引脚上起笔
        }
        wire_target_ = target;
        RefreshEditorViews();
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
        const std::optional<ComponentInstance> component = editor_.GetComponent(*component_id);
        if (component.has_value()) {
            if (event.ShiftDown()) {
                editor_.ToggleComponentSelection(*component_id);
            } else {
                editor_.SelectComponent(*component_id);
                BeginComponentDrag(*component_id, last_mouse_model_);
                CaptureMouse();
            }
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
        EndComponentDrag();
    }
    if (HasCapture()) {
        ReleaseMouse();
    }
    UpdateHoverCursor(last_mouse_model_);
    RefreshEditorViews();
}

// 双击收尾：连线时在空白处双击 = 就以那个点结束；双击引脚则直接连上去。
void SchematicCanvas::OnMouseLeftDoubleClick(wxMouseEvent& event) {
    last_mouse_model_ = ScreenToModel(event.GetPosition());
    if (editor_.GetToolMode() != ToolMode::kWire || !editor_.GetPendingWire().has_value()) {
        event.Skip();
        return;
    }
    const WireTarget target = ComputeWireTarget(last_mouse_model_, editor_.GetSnapshot());
    if (target.kind == WireTarget::Kind::kPin) {
        CommitWireSegment(target);
    } else if (!editor_.CompleteWireAtPoint(target.position)) {
        wxBell();
    }
    wire_target_ = WireTarget{};
    RefreshEditorViews();
}

void SchematicCanvas::OnMouseMove(wxMouseEvent& event) {
    last_mouse_model_ = ScreenToModel(event.GetPosition());
    if (dragging_component_ && event.LeftIsDown()) {
        // 元件跟着鼠标走：把「按下点 → 当前位置」的位移加到原坐标上，再整体吸附网格。
        const Point2D delta{last_mouse_model_.x - drag_press_model_.x,
                            last_mouse_model_.y - drag_press_model_.y};
        drag_preview_position_ = editor_.SnapToGrid(
            {drag_origin_position_.x + delta.x, drag_origin_position_.y + delta.y});
        SetCursor(wxCursor(wxCURSOR_SIZING));
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

    const SchematicSnapshot snapshot = editor_.GetSnapshot();
    bool needs_refresh = false;
    if (editor_.GetToolMode() == ToolMode::kWire) {
        const WireTarget target = ComputeWireTarget(last_mouse_model_, snapshot);
        if (target.kind != wire_target_.kind ||
            !SamePoint(target.position, wire_target_.position)) {
            needs_refresh = true;
        }
        wire_target_ = target;
        if (editor_.GetPendingWire().has_value()) {
            needs_refresh = true;  // 橡皮筋跟着鼠标走
        }
    } else if (editor_.GetToolMode() == ToolMode::kSelect) {
        const std::optional<ComponentId> hover = HitComponent(last_mouse_model_, snapshot);
        if (hover != hover_component_id_) {
            hover_component_id_ = hover;
            needs_refresh = true;
        }
    }
    UpdateHoverCursor(last_mouse_model_);
    if (needs_refresh) {
        Refresh();
    }
}

void SchematicCanvas::OnMouseRightDown(wxMouseEvent& event) {
    static_cast<void>(event);
    editor_.CancelWire();
    // 右键打断拖动：这次移动不提交，元件留在原处。
    AbortComponentDrag();
    wire_target_ = WireTarget{};
    UpdateHoverCursor(last_mouse_model_);
    RefreshEditorViews();
}

// 捕获被系统抢走（弹窗、切窗口等）时收不到 LEFT_UP，拖动状态会卡在半路：
// 直接把这次拖动作废，元件留在原处。
void SchematicCanvas::OnMouseCaptureLost(wxMouseCaptureLostEvent& event) {
    static_cast<void>(event);
    AbortComponentDrag();
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

    // 布线进行中：退格退掉最后一个拐点，回车以最后一个拐点收尾。
    if (editor_.GetPendingWire().has_value()) {
        if (event.GetKeyCode() == WXK_BACK) {
            if (!editor_.RemoveLastWireBend()) {
                editor_.CancelWire();
            }
            RefreshEditorViews();
            return;
        }
        if (event.GetKeyCode() == WXK_RETURN) {
            if (!editor_.FinishWireAtLastBend()) {
                wxBell();
            }
            wire_target_ = WireTarget{};
            RefreshEditorViews();
            return;
        }
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
        case WXK_LEFT:
        case WXK_RIGHT:
        case WXK_UP:
        case WXK_DOWN: {
            // 方向键微调选中元件：一次一格，导线跟着引脚走。
            const SelectionState selection = editor_.GetSelection();
            if (selection.component_ids.size() != 1) {
                event.Skip();
                return;
            }
            const std::optional<ComponentInstance> component =
                editor_.GetComponent(selection.component_ids.front());
            if (!component.has_value()) {
                event.Skip();
                return;
            }
            Point2D target = component->position;
            if (event.GetKeyCode() == WXK_LEFT) {
                target.x -= kDefaultGridSize;
            } else if (event.GetKeyCode() == WXK_RIGHT) {
                target.x += kDefaultGridSize;
            } else if (event.GetKeyCode() == WXK_UP) {
                target.y -= kDefaultGridSize;
            } else {
                target.y += kDefaultGridSize;
            }
            if (!editor_.MoveComponent(component->id, target)) {
                event.Skip();
                return;
            }
            break;
        }
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
    return HitComponent(model_point, editor_.GetSnapshot());
}

// 从后往前找：绘制顺序是按 id 升序，命中测试反过来 —— 画在上面的先被选中。
std::optional<ComponentId> SchematicCanvas::HitComponent(
    Point2D model_point, const SchematicSnapshot& snapshot) const {
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
    return HitPin(model_point, editor_.GetSnapshot());
}

std::optional<SchematicCanvas::PinHit> SchematicCanvas::HitPin(
    Point2D model_point, const SchematicSnapshot& snapshot) const {
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

std::vector<Point2D> SchematicCanvas::WirePoints(const WireModel& wire,
                                                 const SchematicSnapshot& snapshot) const {
    // 端点解析 + 正交拐点（含自动避让、挪动后补拐点）：和建网用的是同一个函数，
    // 画布上看到的折线就是建网时拿来判断连接的折线。
    return model::WirePolyline(snapshot, library_, wire);
}

std::optional<WireId> SchematicCanvas::HitWire(Point2D model_point) const {
    const SchematicSnapshot snapshot = editor_.GetSnapshot();
    const double hit_radius_squared = (kWireHitRadius / zoom_) * (kWireHitRadius / zoom_);
    for (auto wire_iterator = snapshot.wires.rbegin(); wire_iterator != snapshot.wires.rend();
         ++wire_iterator) {
        const std::vector<Point2D> points = WirePoints(*wire_iterator, snapshot);
        for (std::size_t index = 1; index < points.size(); ++index) {
            if (model::DistanceToSegmentSquared(model_point, points[index - 1], points[index]) <=
                hit_radius_squared) {
                return wire_iterator->id;
            }
        }
    }
    return std::nullopt;
}

// 既有导线上离光标最近的投影点：把新导线接到导线中段（T 形相接）时用。
std::optional<Point2D> SchematicCanvas::HitWirePoint(Point2D model_point,
                                                     const SchematicSnapshot& snapshot) const {
    const double hit_radius_squared = (kWireHitRadius / zoom_) * (kWireHitRadius / zoom_);
    std::optional<Point2D> closest;
    double closest_distance_squared = hit_radius_squared;
    for (const WireModel& wire : snapshot.wires) {
        const std::vector<Point2D> points = WirePoints(wire, snapshot);
        for (std::size_t index = 1; index < points.size(); ++index) {
            const double distance_squared =
                model::DistanceToSegmentSquared(model_point, points[index - 1], points[index]);
            if (distance_squared <= closest_distance_squared) {
                closest_distance_squared = distance_squared;
                closest = model::ClosestPointOnSegment(model_point, points[index - 1],
                                                       points[index]);
            }
        }
    }
    return closest;
}

// 连线落点：引脚 > 既有导线 > 空白（空白按网格吸附）。
SchematicCanvas::WireTarget SchematicCanvas::ComputeWireTarget(
    Point2D model_point, const SchematicSnapshot& snapshot) const {
    WireTarget target;
    const std::optional<PinHit> pin_hit = HitPin(model_point, snapshot);
    if (pin_hit.has_value()) {
        target.kind = WireTarget::Kind::kPin;
        target.pin = pin_hit->pin;
        target.position = pin_hit->position;  // 精确落在引脚上，而不是鼠标位置
        return target;
    }
    const std::optional<Point2D> wire_point = HitWirePoint(model_point, snapshot);
    if (wire_point.has_value()) {
        target.kind = WireTarget::Kind::kWire;
        target.position = *wire_point;
        return target;
    }
    target.kind = WireTarget::Kind::kEmpty;
    target.position = editor_.SnapToGrid(model_point);
    return target;
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

// ---- 元件拖动：拖动过程中只改预览坐标，松手才落一条 MoveComponent 命令 ----

bool SchematicCanvas::HasDragPreview() const {
    return dragging_component_ && drag_preview_position_.has_value() &&
           !SamePoint(*drag_preview_position_, drag_origin_position_);
}

void SchematicCanvas::BeginComponentDrag(ComponentId component_id, Point2D press_model) {
    const std::optional<ComponentInstance> component = editor_.GetComponent(component_id);
    if (!component.has_value()) {
        return;
    }
    dragging_component_ = true;
    dragging_component_id_ = component_id;
    drag_origin_position_ = component->position;
    drag_press_model_ = press_model;
    drag_preview_position_ = component->position;
}

void SchematicCanvas::EndComponentDrag() {
    if (HasDragPreview()) {
        static_cast<void>(editor_.MoveComponent(dragging_component_id_, *drag_preview_position_));
    }
    dragging_component_ = false;
    dragging_component_id_ = kInvalidComponentId;
    drag_preview_position_.reset();
}

void SchematicCanvas::AbortComponentDrag() {
    // 只有真在拖元件时才放掉捕获：中键平移也持有捕获，右键不该打断它。
    const bool was_dragging = dragging_component_;
    dragging_component_ = false;
    dragging_component_id_ = kInvalidComponentId;
    drag_preview_position_.reset();
    if (was_dragging && HasCapture()) {
        ReleaseMouse();
    }
}

// 拖着元件画的时候，用预览坐标替换模型坐标：挂在它引脚上的导线会一起走。
void SchematicCanvas::ApplyDragPreview(SchematicSnapshot& snapshot) const {
    if (!HasDragPreview()) {
        return;
    }
    for (ComponentInstance& component : snapshot.components) {
        if (component.id == dragging_component_id_) {
            component.position = *drag_preview_position_;
            return;
        }
    }
}

// 布线途中落一次点：空白处落点当拐点接着画；落到引脚/导线上就收尾。
void SchematicCanvas::CommitWireSegment(const WireTarget& target) {
    const std::optional<editor::PendingWire> pending_wire = editor_.GetPendingWire();
    if (!pending_wire.has_value() || target.kind == WireTarget::Kind::kNone) {
        return;
    }
    const std::vector<Point2D> path = PlanSegmentTo(*pending_wire, target);

    if (target.kind == WireTarget::Kind::kEmpty) {
        for (Point2D point : path) {
            static_cast<void>(editor_.AddWireBend(point));
        }
        return;
    }

    // 落到引脚/导线上：先把途中的直角拐点补齐，再收尾。
    // 收尾失败（比如点回起始引脚）就把刚补的拐点退回去，
    // 别让一次失败的点击悄悄改掉正在画的折线。
    const std::size_t bend_count_before = pending_wire->route_points.size();
    for (std::size_t index = 0; index + 1 < path.size(); ++index) {
        static_cast<void>(editor_.AddWireBend(path[index]));
    }
    const bool completed = target.kind == WireTarget::Kind::kPin
                               ? editor_.CompleteWire(target.pin, target.position)
                               : editor_.CompleteWireAtPoint(target.position);
    if (!completed) {
        std::optional<editor::PendingWire> pending = editor_.GetPendingWire();
        while (pending.has_value() && pending->route_points.size() > bend_count_before) {
            static_cast<void>(editor_.RemoveLastWireBend());
            pending = editor_.GetPendingWire();
        }
        wxBell();
    }
}

void SchematicCanvas::UpdateHoverCursor(Point2D model_point) {
    if (dragging_component_) {
        SetCursor(wxCursor(wxCURSOR_SIZING));
        return;
    }
    switch (editor_.GetToolMode()) {
        case ToolMode::kWire:
        case ToolMode::kPlaceComponent:
            SetCursor(wxCursor(wxCURSOR_CROSS));
            return;
        case ToolMode::kPan:
            SetCursor(wxCursor(wxCURSOR_HAND));
            return;
        case ToolMode::kSelect:
            break;
    }
    SetCursor(HitComponent(model_point).has_value() ? wxCursor(wxCURSOR_SIZING)
                                                    : wxCursor(wxCURSOR_ARROW));
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

wxColour SchematicCanvas::WireColour(LogicValue value) const {
    return LogicColour(value);
}

std::optional<LogicValue> SchematicCanvas::PinValue(ComponentId component_id,
                                                    PinId pin_id) const {
    if (!simulation_.has_value()) {
        return std::nullopt;
    }
    const auto iterator = simulation_->pin_values.find(PinKey(component_id, pin_id));
    return iterator != simulation_->pin_values.end()
               ? std::optional<LogicValue>(iterator->second)
               : std::nullopt;
}

void SchematicCanvas::DrawWires(wxDC& dc, const SchematicSnapshot& snapshot) {
    for (const WireModel& wire : snapshot.wires) {
        wxColour colour;
        int width = 2;
        std::optional<LogicValue> value;
        if (simulation_.has_value()) {
            const auto value_iterator = simulation_->wire_values.find(wire.id);
            if (value_iterator != simulation_->wire_values.end()) {
                value = value_iterator->second;
            }
        }
        if (IsWireSelected(wire.id)) {
            colour = wxColour(210, 75, 60);
            width = 3;
        } else if (value.has_value()) {
            colour = WireColour(*value);
            width = 3;
        } else {
            colour = wxColour(40, 115, 150);
        }
        dc.SetPen(wxPen(colour, width));
        const std::vector<Point2D> points = WirePoints(wire, snapshot);
        for (std::size_t index = 1; index < points.size(); ++index) {
            dc.DrawLine(ModelToScreen(points[index - 1]), ModelToScreen(points[index]));
        }
        // 拐点上点一个小圆点，折线在哪里转弯一眼能看出来。
        if (!wire.route_points.empty()) {
            dc.SetPen(wxPen(colour, 1));
            dc.SetBrush(wxBrush(colour));
            const int radius = std::max(2, static_cast<int>(std::lround(2.0 * zoom_)));
            for (const Point2D& bend : wire.route_points) {
                dc.DrawCircle(ModelToScreen(bend), radius);
            }
        }
        // 悬空导线（没接入任何网络）在仿真态画个小问号提示。
        if (simulation_.has_value() && !value.has_value()) {
            dc.DrawText(L"?", ModelToScreen(PolylineMidPoint(points)));
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
        // 悬停提示：鼠标底下的元件加一圈虚线，点下去抓的就是它。
        const bool hovered = !selected && !dragging_component_ &&
                             hover_component_id_.has_value() &&
                             *hover_component_id_ == component.id;
        if (hovered) {
            dc.SetPen(wxPen(wxColour(120, 170, 220), 1, wxPENSTYLE_DOT));
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.DrawRectangle(top_left.x - 3, top_left.y - 3, width + 6, height + 6);
        }
        dc.SetPen(wxPen(selected ? wxColour(210, 75, 60) : wxColour(55, 65, 80),
                        selected ? 3 : 2));
        if (definition->category == "custom") {
            dc.SetBrush(wxBrush(wxColour(237, 232, 250)));
            dc.DrawRectangle(top_left.x, top_left.y, width, height);
        } else if (component.type_id == "io.input") {
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
        // 仿真可视化：io 元件头顶画电平徽标（红 1 / 绿 0 / 橙 X / 灰 Z）。
        if (simulation_.has_value() &&
            (component.type_id == "io.input" || component.type_id == "io.output")) {
            const PinDirection wanted = component.type_id == "io.input"
                                            ? PinDirection::kOutput
                                            : PinDirection::kInput;
            std::optional<LogicValue> value;
            for (const PinDefinition& pin : definition->pins) {
                if (pin.direction == wanted) {
                    value = PinValue(component.id, pin.local_id);
                    break;
                }
            }
            if (value.has_value()) {
                const wxPoint badge_center = ModelToScreen(
                    {component.position.x + definition->default_bounds.width / 2.0,
                     component.position.y - 10.0});
                const int radius =
                    std::max(6, static_cast<int>(std::lround(7.0 * zoom_)));
                dc.SetPen(wxPen(*wxBLACK, 1));
                dc.SetBrush(wxBrush(WireColour(*value)));
                dc.DrawCircle(badge_center, radius);
                const wxString character(LogicChar(*value));
                const wxSize char_size = dc.GetTextExtent(character);
                dc.SetTextForeground(*wxWHITE);
                dc.DrawText(character, badge_center.x - char_size.x / 2,
                            badge_center.y - char_size.y / 2);
                dc.SetTextForeground(*wxBLACK);
            }
        }

        dc.SetTextForeground(wxColour(80, 85, 95));
        dc.DrawText(component.reference, top_left.x, top_left.y + height + 3);
        dc.SetTextForeground(*wxBLACK);
    }
}

// 拖动中的元件原位留一个虚线框，方便看清「从哪拖到哪」。
void SchematicCanvas::DrawDragOriginMarker(wxDC& dc, const SchematicSnapshot& snapshot) {
    if (!HasDragPreview()) {
        return;
    }
    for (const ComponentInstance& component : snapshot.components) {
        if (component.id != dragging_component_id_) {
            continue;
        }
        const std::optional<ComponentDefinition> definition =
            library_.FindByTypeId(component.type_id);
        if (!definition.has_value()) {
            return;
        }
        const wxPoint origin = ModelToScreen(drag_origin_position_);
        const int width = static_cast<int>(std::lround(definition->default_bounds.width * zoom_));
        const int height = static_cast<int>(std::lround(definition->default_bounds.height * zoom_));
        dc.SetPen(wxPen(wxColour(150, 158, 170), 1, wxPENSTYLE_DOT));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRectangle(origin.x, origin.y, width, height);
        return;
    }
}

// 待画折线到 target 这一跳的正交路径（含自动避让）。
// 提交和预览走同一个函数，虚线预览就是松手后的形状。
std::vector<Point2D> SchematicCanvas::PlanSegmentTo(const editor::PendingWire& pending_wire,
                                                    const WireTarget& target) const {
    return model::PlanWireSegment(editor_.GetSnapshot(), library_, WireAnchorPoint(pending_wire),
                                  target.position, WireAnchorPin(pending_wire),
                                  target.kind == WireTarget::Kind::kPin
                                      ? std::optional<PinRef>(target.pin)
                                      : std::optional<PinRef>());
}

// 布线预览：已定的折线段 + 到鼠标落点的橡皮筋。
void SchematicCanvas::DrawTemporaryWire(wxDC& dc) {
    const std::optional<editor::PendingWire> pending_wire = editor_.GetPendingWire();
    if (!pending_wire.has_value()) {
        return;
    }
    std::vector<Point2D> points;
    points.push_back(pending_wire->start_position);
    points.insert(points.end(), pending_wire->route_points.begin(),
                  pending_wire->route_points.end());
    if (wire_target_.kind != WireTarget::Kind::kNone) {
        const std::vector<Point2D> path = PlanSegmentTo(*pending_wire, wire_target_);
        points.insert(points.end(), path.begin(), path.end());
    }

    dc.SetPen(wxPen(wxColour(110, 110, 110), 2, wxPENSTYLE_DOT));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    for (std::size_t index = 1; index < points.size(); ++index) {
        dc.DrawLine(ModelToScreen(points[index - 1]), ModelToScreen(points[index]));
    }

    // 已定的拐点画小方块，起点画个圈；落点吸到引脚/导线上时再给个提示。
    const int marker = std::max(2, static_cast<int>(std::lround(2.0 * zoom_)));
    dc.SetPen(wxPen(wxColour(110, 110, 110), 1));
    dc.SetBrush(wxBrush(wxColour(110, 110, 110)));
    for (std::size_t index = 1; index <= pending_wire->route_points.size(); ++index) {
        const wxPoint screen = ModelToScreen(points[index]);
        dc.DrawRectangle(screen.x - marker, screen.y - marker, marker * 2, marker * 2);
    }
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.SetPen(wxPen(wxColour(110, 110, 110), 1));
    dc.DrawCircle(ModelToScreen(pending_wire->start_position), marker * 2);
    if (wire_target_.kind == WireTarget::Kind::kPin) {
        dc.SetPen(wxPen(wxColour(210, 75, 60), 2));
        dc.DrawCircle(ModelToScreen(wire_target_.position), marker * 3);
    } else if (wire_target_.kind == WireTarget::Kind::kWire) {
        dc.SetPen(wxPen(wxColour(210, 75, 60), 1));
        dc.SetBrush(wxBrush(wxColour(210, 75, 60)));
        dc.DrawCircle(ModelToScreen(wire_target_.position), marker);
    }
}

void SchematicCanvas::RefreshEditorViews() {
    Refresh();
    Update();
}

}  // namespace cse::ui
