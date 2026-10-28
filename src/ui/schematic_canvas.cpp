#include "src/ui/schematic_canvas.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

#include <wx/cursor.h>
#include <wx/dcbuffer.h>
#include <wx/dc.h>
#include <wx/settings.h>
#include <wx/textdlg.h>

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
    DrawTextLabels(dc, snapshot);
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

    if (tool_mode == ToolMode::kText) {
        const SchematicSnapshot snapshot = editor_.GetSnapshot();
        const std::optional<TextLabelId> label_id = HitTextLabel(last_mouse_model_, snapshot);
        if (label_id.has_value()) {
            editor_.SelectTextLabel(*label_id);
            BeginTextLabelDrag(*label_id, last_mouse_model_);
            CaptureMouse();
        } else {
            wxTextEntryDialog dialog(this, L"输入文本内容：", L"添加文本", wxEmptyString);
            if (dialog.ShowModal() == wxID_OK) {
                const std::string text = dialog.GetValue().Trim().ToStdString();
                if (!text.empty() && !editor_.AddTextLabel(last_mouse_model_, text).ok) {
                    wxBell();
                }
            }
        }
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
                // 元件已在多选里时不要重置选区：直接整组拖；
                // 否则才收敛成单选（顺带把点在选外元件上的行为变成"换选"）。
                if (!IsComponentSelected(*component_id)) {
                    editor_.SelectComponent(*component_id);
                }
                BeginComponentDrag(*component_id, last_mouse_model_);
                CaptureMouse();
            }
        }
    } else {
        const SchematicSnapshot snapshot = editor_.GetSnapshot();
        const std::optional<TextLabelId> label_id = HitTextLabel(last_mouse_model_, snapshot);
        if (label_id.has_value()) {
            editor_.SelectTextLabel(*label_id);
            BeginTextLabelDrag(*label_id, last_mouse_model_);
            CaptureMouse();
        } else {
            const std::optional<WireId> wire_id = HitWire(last_mouse_model_);
            if (wire_id.has_value()) {
                editor_.SelectWire(*wire_id);
            } else {
                editor_.SelectComponent(std::nullopt);
            }
        }
    }
    RefreshEditorViews();
}

void SchematicCanvas::OnMouseLeftUp(wxMouseEvent& event) {
    static_cast<void>(event);
    if (dragging_component_) {
        EndComponentDrag();
    }
    if (dragging_label_) {
        EndTextLabelDrag();
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
        // 元件跟着鼠标走：把「按下点 → 当前位置」的位移加到各自原坐标上，再整体吸附网格。
        const Point2D delta{last_mouse_model_.x - drag_press_model_.x,
                            last_mouse_model_.y - drag_press_model_.y};
        std::vector<Point2D> previews;
        previews.reserve(drag_origin_positions_.size());
        for (const Point2D& origin : drag_origin_positions_) {
            previews.push_back(editor_.SnapToGrid({origin.x + delta.x, origin.y + delta.y}));
        }
        drag_preview_positions_ = std::move(previews);
        SetCursor(wxCursor(wxCURSOR_SIZING));
        Refresh();
        return;
    }
    if (dragging_label_ && event.LeftIsDown()) {
        // 文本标注跟着鼠标走：位移加到原坐标上再吸附网格。
        const Point2D delta{last_mouse_model_.x - label_drag_press_model_.x,
                            last_mouse_model_.y - label_drag_press_model_.y};
        label_drag_preview_position_ = editor_.SnapToGrid(
            {label_drag_origin_position_.x + delta.x, label_drag_origin_position_.y + delta.y});
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
    AbortTextLabelDrag();
    wire_target_ = WireTarget{};
    UpdateHoverCursor(last_mouse_model_);
    RefreshEditorViews();
}

// 捕获被系统抢走（弹窗、切窗口等）时收不到 LEFT_UP，拖动状态会卡在半路：
// 直接把这次拖动作废，元件留在原处。
void SchematicCanvas::OnMouseCaptureLost(wxMouseCaptureLostEvent& event) {
    static_cast<void>(event);
    AbortComponentDrag();
    AbortTextLabelDrag();
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
    if (event.ControlDown() && event.GetKeyCode() == 'A') {
        editor_.SelectAll();
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
        case 'T':
            editor_.SetToolMode(ToolMode::kText);
            break;
        case 'R':
            static_cast<void>(editor_.RotateSelected());
            break;
        case WXK_LEFT:
        case WXK_RIGHT:
        case WXK_UP:
        case WXK_DOWN: {
            // 方向键微调选中的元件（支持多选）：一次一格，导线跟着引脚走；
            // 连续微调并进同一条撤销命令，Ctrl+Z 一次退回整段微调。
            const SelectionState selection = editor_.GetSelection();
            if (selection.component_ids.empty()) {
                event.Skip();
                return;
            }
            Point2D delta{0.0, 0.0};
            if (event.GetKeyCode() == WXK_LEFT) {
                delta.x = -kDefaultGridSize;
            } else if (event.GetKeyCode() == WXK_RIGHT) {
                delta.x = kDefaultGridSize;
            } else if (event.GetKeyCode() == WXK_UP) {
                delta.y = -kDefaultGridSize;
            } else {
                delta.y = kDefaultGridSize;
            }
            std::vector<ComponentId> ids;
            std::vector<Point2D> targets;
            ids.reserve(selection.component_ids.size());
            targets.reserve(selection.component_ids.size());
            for (ComponentId component_id : selection.component_ids) {
                const std::optional<ComponentInstance> component =
                    editor_.GetComponent(component_id);
                if (!component.has_value()) {
                    continue;
                }
                ids.push_back(component_id);
                targets.push_back({component->position.x + delta.x,
                                   component->position.y + delta.y});
            }
            if (ids.empty() || !editor_.MoveComponents(ids, targets, true)) {
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

wxPoint SchematicCanvas::RotateScreenPoint(wxPoint point, wxPoint center,
                                           int quarter_turn) const {
    const int local_x = point.x - center.x;
    const int local_y = point.y - center.y;
    switch (quarter_turn) {
        case 1:
            return {center.x - local_y, center.y + local_x};
        case 2:
            return {center.x - local_x, center.y - local_y};
        case 3:
            return {center.x + local_y, center.y - local_x};
        default:
            return point;
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

// 文本标注命中：以文字的像素尺寸换算成模型矩形（略放宽半格容差）。
std::optional<TextLabelId> SchematicCanvas::HitTextLabel(Point2D model_point,
                                                         const SchematicSnapshot& snapshot) const {
    for (auto iterator = snapshot.text_labels.rbegin();
         iterator != snapshot.text_labels.rend(); ++iterator) {
        const wxSize size = GetTextExtent(wxString::FromUTF8(iterator->text));
        const double width = static_cast<double>(size.x) / zoom_ + 4.0;
        const double height = static_cast<double>(size.y) / zoom_ + 4.0;
        if (model_point.x >= iterator->position.x - 2.0 &&
            model_point.x <= iterator->position.x + width &&
            model_point.y >= iterator->position.y - 2.0 &&
            model_point.y <= iterator->position.y + height) {
            return iterator->id;
        }
    }
    return std::nullopt;
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

bool SchematicCanvas::IsTextLabelSelected(TextLabelId label_id) const {
    const SelectionState selection = editor_.GetSelection();
    return std::find(selection.text_label_ids.begin(), selection.text_label_ids.end(),
                     label_id) != selection.text_label_ids.end();
}

// ---- 元件拖动：拖动过程中只改预览坐标，松手才落一条 MoveSelection 命令 ----
// Shift 多选后按下其中任何一个，整组一起拖。

bool SchematicCanvas::HasDragPreview() const {
    if (!dragging_component_ || !drag_preview_positions_.has_value()) {
        return false;
    }
    for (std::size_t index = 0; index < drag_origin_positions_.size(); ++index) {
        if (!SamePoint((*drag_preview_positions_)[index], drag_origin_positions_[index])) {
            return true;
        }
    }
    return false;
}

void SchematicCanvas::BeginComponentDrag(ComponentId component_id, Point2D press_model) {
    const std::optional<ComponentInstance> component = editor_.GetComponent(component_id);
    if (!component.has_value()) {
        return;
    }
    // 按下的元件在多选里就整组拖，否则只拖它自己。
    drag_component_ids_ = IsComponentSelected(component_id) &&
                                  !editor_.GetSelection().component_ids.empty()
                              ? editor_.GetSelection().component_ids
                              : std::vector<ComponentId>{component_id};
    drag_origin_positions_.clear();
    for (ComponentId dragged_id : drag_component_ids_) {
        const std::optional<ComponentInstance> dragged = editor_.GetComponent(dragged_id);
        if (!dragged.has_value()) {
            drag_component_ids_.clear();
            return;
        }
        drag_origin_positions_.push_back(dragged->position);
    }
    dragging_component_ = true;
    drag_press_model_ = press_model;
    drag_preview_positions_ = drag_origin_positions_;
}

void SchematicCanvas::EndComponentDrag() {
    if (HasDragPreview()) {
        static_cast<void>(editor_.MoveComponents(drag_component_ids_,
                                                 *drag_preview_positions_, false));
    }
    dragging_component_ = false;
    drag_component_ids_.clear();
    drag_origin_positions_.clear();
    drag_preview_positions_.reset();
}

void SchematicCanvas::AbortComponentDrag() {
    // 只有真在拖元件时才放掉捕获：中键平移也持有捕获，右键不该打断它。
    const bool was_dragging = dragging_component_;
    dragging_component_ = false;
    drag_component_ids_.clear();
    drag_origin_positions_.clear();
    drag_preview_positions_.reset();
    if (was_dragging && HasCapture()) {
        ReleaseMouse();
    }
}

void SchematicCanvas::BeginTextLabelDrag(TextLabelId label_id, Point2D press_model) {
    const SchematicSnapshot snapshot = editor_.GetSnapshot();
    const auto iterator = std::find_if(
        snapshot.text_labels.begin(), snapshot.text_labels.end(),
        [label_id](const TextLabel& label) { return label.id == label_id; });
    if (iterator == snapshot.text_labels.end()) {
        return;
    }
    dragging_label_ = true;
    dragging_label_id_ = label_id;
    label_drag_origin_position_ = iterator->position;
    label_drag_press_model_ = press_model;
    label_drag_preview_position_ = iterator->position;
}

void SchematicCanvas::EndTextLabelDrag() {
    if (HasTextLabelDragPreview() &&
        !SamePoint(*label_drag_preview_position_, label_drag_origin_position_)) {
        static_cast<void>(
            editor_.MoveTextLabel(dragging_label_id_, *label_drag_preview_position_));
    }
    dragging_label_ = false;
    dragging_label_id_ = kInvalidTextLabelId;
    label_drag_preview_position_.reset();
}

void SchematicCanvas::AbortTextLabelDrag() {
    const bool was_dragging = dragging_label_;
    dragging_label_ = false;
    dragging_label_id_ = kInvalidTextLabelId;
    label_drag_preview_position_.reset();
    if (was_dragging && HasCapture()) {
        ReleaseMouse();
    }
}

bool SchematicCanvas::HasTextLabelDragPreview() const {
    return dragging_label_ && label_drag_preview_position_.has_value();
}

// 拖着元件画的时候，用预览坐标替换模型坐标：挂在它引脚上的导线会一起走。
// 这些导线的 route_points 是按挪动前的位置存的，不清空的话逐段补直角
// 会拐出锯齿——预览里清掉，让 WirePolyline 按新几何重新规划。
void SchematicCanvas::ApplyDragPreview(SchematicSnapshot& snapshot) const {
    if (!HasDragPreview()) {
        return;
    }
    std::unordered_set<ComponentId> dragged(drag_component_ids_.begin(),
                                            drag_component_ids_.end());
    for (std::size_t index = 0; index < drag_component_ids_.size(); ++index) {
        for (ComponentInstance& component : snapshot.components) {
            if (component.id == drag_component_ids_[index]) {
                component.position = (*drag_preview_positions_)[index];
                break;
            }
        }
    }
    for (WireModel& wire : snapshot.wires) {
        const bool from_dragged =
            wire.from.pin.has_value() && dragged.count(wire.from.pin->component_id) != 0;
        const bool to_dragged =
            wire.to.pin.has_value() && dragged.count(wire.to.pin->component_id) != 0;
        if (from_dragged || to_dragged) {
            wire.route_points.clear();
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
        case ToolMode::kText:
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
        const int quarter = NormalizeQuarterTurn(component.rotation_deg);
        const wxPoint top_left = ModelToScreen(component.position);
        const int width = static_cast<int>(std::lround(definition->default_bounds.width * zoom_));
        const int height = static_cast<int>(std::lround(definition->default_bounds.height * zoom_));
        const bool selected = IsComponentSelected(component.id);
        // 图形必须跟着元件一起转：形状顶点先按未旋转坐标算好，再绕元件中心转
        // quarter 圈，与引脚圆点（GetPinPosition）和命中测试用同一套旋转公式。
        const wxPoint center{top_left.x + width / 2, top_left.y + height / 2};
        const auto rotate = [this, quarter, center](wxPoint point) {
            return RotateScreenPoint(point, center, quarter);
        };
        const auto rotated_corners = [&rotate, top_left, width, height]() {
            std::vector<wxPoint> corners;
            corners.reserve(4);
            corners.push_back(rotate(top_left));
            corners.push_back(rotate({top_left.x + width, top_left.y}));
            corners.push_back(rotate({top_left.x + width, top_left.y + height}));
            corners.push_back(rotate({top_left.x, top_left.y + height}));
            return corners;
        };
        const auto draw_rotated_polygon = [&dc, &rotate](const std::vector<wxPoint>& points,
                                                         const wxBrush& brush) {
            std::vector<wxPoint> rotated;
            rotated.reserve(points.size());
            for (wxPoint point : points) {
                rotated.push_back(rotate(point));
            }
            dc.SetBrush(brush);
            dc.DrawPolygon(static_cast<int>(rotated.size()), rotated.data());
        };

        // 旋转后的外接框：位号和徽标都钉在框上，转完不跑偏。
        const std::vector<wxPoint> corners = rotated_corners();
        int min_x = corners.front().x;
        int max_x = corners.front().x;
        int min_y = corners.front().y;
        int max_y = corners.front().y;
        for (const wxPoint& corner : corners) {
            min_x = std::min(min_x, corner.x);
            max_x = std::max(max_x, corner.x);
            min_y = std::min(min_y, corner.y);
            max_y = std::max(max_y, corner.y);
        }

        // 悬停提示：鼠标底下的元件加一圈虚线，点下去抓的就是它。
        const bool hovered = !selected && !dragging_component_ &&
                             hover_component_id_.has_value() &&
                             *hover_component_id_ == component.id;
        if (hovered) {
            dc.SetPen(wxPen(wxColour(120, 170, 220), 1, wxPENSTYLE_DOT));
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            const wxPoint hover_corners[4] = {
                rotate({top_left.x - 3, top_left.y - 3}),
                rotate({top_left.x + width + 3, top_left.y - 3}),
                rotate({top_left.x + width + 3, top_left.y + height + 3}),
                rotate({top_left.x - 3, top_left.y + height + 3}),
            };
            dc.DrawPolygon(4, hover_corners);
        }
        dc.SetPen(wxPen(selected ? wxColour(210, 75, 60) : wxColour(55, 65, 80),
                        selected ? 3 : 2));
        if (definition->category == "custom") {
            draw_rotated_polygon(corners, wxBrush(wxColour(237, 232, 250)));
        } else if (component.type_id == "io.input") {
            draw_rotated_polygon(corners, wxBrush(wxColour(224, 240, 252)));
            const int middle_y = top_left.y + height / 2;
            const std::vector<std::pair<wxPoint, wxPoint>> arrow{
                {{top_left.x + width - 14, middle_y}, {top_left.x + width, middle_y}},
                {{top_left.x + width - 14, middle_y}, {top_left.x + width - 22, middle_y - 6}},
                {{top_left.x + width - 14, middle_y}, {top_left.x + width - 22, middle_y + 6}},
            };
            for (const auto& [from, to] : arrow) {
                const wxPoint rotated_from = rotate(from);
                const wxPoint rotated_to = rotate(to);
                dc.DrawLine(rotated_from, rotated_to);
            }
        } else if (component.type_id == "io.output") {
            draw_rotated_polygon(corners, wxBrush(wxColour(252, 239, 220)));
        } else if (component.type_id == "logic.not") {
            const std::vector<wxPoint> triangle{
                {top_left.x, top_left.y},
                {top_left.x + width, top_left.y + height / 2},
                {top_left.x, top_left.y + height},
            };
            draw_rotated_polygon(triangle, wxBrush(wxColour(235, 238, 244)));
            dc.SetBrush(wxBrush(wxColour(248, 249, 251)));
            dc.DrawCircle(rotate({top_left.x + width + 5, top_left.y + height / 2}), 5);
        } else if (component.type_id == "logic.and") {
            const std::vector<wxPoint> gate{
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
            draw_rotated_polygon(gate, wxBrush(wxColour(235, 238, 244)));
        } else {
            const std::vector<wxPoint> gate{
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
            draw_rotated_polygon(gate, wxBrush(wxColour(235, 238, 244)));
            if (component.type_id == "logic.xor") {
                dc.SetPen(wxPen(selected ? wxColour(210, 75, 60) : wxColour(55, 65, 80),
                                selected ? 3 : 2));
                const std::vector<wxPoint> extra_curve{
                    {top_left.x + static_cast<int>(width * 0.04),
                     top_left.y + static_cast<int>(height * 0.18)},
                    {top_left.x + static_cast<int>(width * 0.22), top_left.y + height / 2},
                    {top_left.x + static_cast<int>(width * 0.04),
                     top_left.y + static_cast<int>(height * 0.82)},
                };
                std::vector<wxPoint> rotated_curve;
                rotated_curve.reserve(extra_curve.size());
                for (wxPoint point : extra_curve) {
                    rotated_curve.push_back(rotate(point));
                }
                dc.DrawLines(static_cast<int>(rotated_curve.size()), rotated_curve.data());
            }
        }

        // 名字画在旋转后外接框的中心，位号挂在框下方；文字本身不跟着转，保持可读。
        const wxSize label_size = dc.GetTextExtent(definition->display_name);
        dc.DrawText(definition->display_name,
                    (min_x + max_x) / 2 - label_size.x / 2,
                    (min_y + max_y) / 2 - label_size.y / 2);

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
                const wxPoint badge_center((min_x + max_x) / 2, min_y - 10);
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
        dc.DrawText(component.reference, min_x, max_y + 3);
        dc.SetTextForeground(*wxBLACK);
    }
}

// 文本标注：选中描红，拖动时画在预览位置。
void SchematicCanvas::DrawTextLabels(wxDC& dc, const SchematicSnapshot& snapshot) {
    for (const TextLabel& label : snapshot.text_labels) {
        Point2D position = label.position;
        if (dragging_label_ && dragging_label_id_ == label.id &&
            label_drag_preview_position_.has_value()) {
            position = *label_drag_preview_position_;
        }
        dc.SetTextForeground(IsTextLabelSelected(label.id) ? wxColour(210, 75, 60)
                                                           : wxColour(40, 45, 55));
        dc.DrawText(wxString::FromUTF8(label.text), ModelToScreen(position));
        dc.SetTextForeground(*wxBLACK);
    }
}

// 拖动中的元件原位留一个虚线框，方便看清「从哪拖到哪」。
void SchematicCanvas::DrawDragOriginMarker(wxDC& dc, const SchematicSnapshot& snapshot) {
    if (!HasDragPreview()) {
        return;
    }
    for (std::size_t index = 0; index < drag_component_ids_.size(); ++index) {
        const auto iterator = std::find_if(
            snapshot.components.begin(), snapshot.components.end(),
            [&drag_id = drag_component_ids_[index]](const ComponentInstance& component) {
                return component.id == drag_id;
            });
        if (iterator == snapshot.components.end()) {
            continue;
        }
        const std::optional<ComponentDefinition> definition =
            library_.FindByTypeId(iterator->type_id);
        if (!definition.has_value()) {
            continue;
        }
        const int quarter = NormalizeQuarterTurn(iterator->rotation_deg);
        const wxPoint origin = ModelToScreen(drag_origin_positions_[index]);
        const int width = static_cast<int>(std::lround(definition->default_bounds.width * zoom_));
        const int height = static_cast<int>(std::lround(definition->default_bounds.height * zoom_));
        const wxPoint center{origin.x + width / 2, origin.y + height / 2};
        const wxPoint corners[4] = {
            RotateScreenPoint({origin.x, origin.y}, center, quarter),
            RotateScreenPoint({origin.x + width, origin.y}, center, quarter),
            RotateScreenPoint({origin.x + width, origin.y + height}, center, quarter),
            RotateScreenPoint({origin.x, origin.y + height}, center, quarter),
        };
        dc.SetPen(wxPen(wxColour(150, 158, 170), 1, wxPENSTYLE_DOT));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawPolygon(4, corners);
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
