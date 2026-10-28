#include "src/editor/editor_controller.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "src/editor/editor_commands.hpp"

#include "src/model/wire_route.hpp"

namespace cse::editor {

namespace {

constexpr double kPointEpsilon = 0.000001;
// 与建网/布线一致的连接容差：两点距离小于它视为同一连接点。
constexpr double kJoinEpsilon = 0.5;

bool SamePoint(Point2D first, Point2D second) {
    return std::abs(first.x - second.x) < kPointEpsilon &&
           std::abs(first.y - second.y) < kPointEpsilon;
}

// 当前正在画的那一段的起点：有拐点就是最后一个拐点，否则是最初按下的引脚。
Point2D WireAnchor(const PendingWire& pending_wire) {
    return pending_wire.route_points.empty() ? pending_wire.start_position
                                             : pending_wire.route_points.back();
}

// 导线端点的"连接身份"：挂引脚看 (元件, 引脚)，自由端点看坐标。
// 用来拒绝重复导线——同一条连接画两次会得到两条完全重叠的线。
struct EndpointKey {
    bool has_pin{false};
    ComponentId component_id{kInvalidComponentId};
    PinId pin_id{0};
    Point2D position;
};

EndpointKey KeyFor(const WireEndpoint& endpoint, const SchematicSnapshot& snapshot,
                   const IComponentLibrary& library) {
    EndpointKey key;
    key.position = model::ResolveWireEndpoint(snapshot, library, endpoint);
    if (endpoint.pin.has_value()) {
        key.has_pin = true;
        key.component_id = endpoint.pin->component_id;
        key.pin_id = endpoint.pin->pin_id;
    }
    return key;
}

bool SameEndpointKey(const EndpointKey& first, const EndpointKey& second) {
    if (first.has_pin && second.has_pin) {
        return first.component_id == second.component_id && first.pin_id == second.pin_id;
    }
    if (first.has_pin != second.has_pin) {
        return false;
    }
    return std::abs(first.position.x - second.position.x) < kJoinEpsilon &&
           std::abs(first.position.y - second.position.y) < kJoinEpsilon;
}

// 这条新导线是否与既有导线连接完全相同（端点不分先后）。
bool DuplicateWireExists(const WireModel& wire, const SchematicSnapshot& snapshot,
                         const IComponentLibrary& library) {
    const EndpointKey from_key = KeyFor(wire.from, snapshot, library);
    const EndpointKey to_key = KeyFor(wire.to, snapshot, library);
    for (const WireModel& existing : snapshot.wires) {
        const EndpointKey existing_from = KeyFor(existing.from, snapshot, library);
        const EndpointKey existing_to = KeyFor(existing.to, snapshot, library);
        if ((SameEndpointKey(from_key, existing_from) &&
             SameEndpointKey(to_key, existing_to)) ||
            (SameEndpointKey(from_key, existing_to) &&
             SameEndpointKey(to_key, existing_from))) {
            return true;
        }
    }
    return false;
}

}  // namespace

EditorController::EditorController(model::SchematicModel& model, IComponentLibrary& library,
                                   IComponentFactory& factory)
    : model_(model), library_(library), factory_(factory) {}

void EditorController::SetToolMode(ToolMode mode) {
    tool_mode_ = mode;
    if (mode != ToolMode::kWire) {
        pending_wire_.reset();
    }
}

ToolMode EditorController::GetToolMode() const {
    return tool_mode_;
}

void EditorController::SetPlacementType(std::string type_id) {
    placement_type_ = std::move(type_id);
    tool_mode_ = ToolMode::kPlaceComponent;
    // 模式切走时和 SetToolMode 一样清掉画到一半的线，不然草稿静默存活，
    // 回到连线模式时旧橡皮筋凭空复活。
    pending_wire_.reset();
}

const std::string& EditorController::GetPlacementType() const {
    return placement_type_;
}

bool EditorController::PlaceComponent(Point2D position) {
    if (placement_type_.empty()) {
        return false;
    }

    const Result<ComponentInstance> result = factory_.Create(placement_type_, SnapToGrid(position));
    if (!result.ok) {
        return false;
    }

    const ComponentId component_id = result.value.id;
    if (!ExecuteCommand(std::make_unique<AddComponentCommand>(
            NextCommandId(), model_, result.value))) {
        return false;
    }

    SelectComponent(component_id);
    return true;
}

void EditorController::SelectComponent(std::optional<ComponentId> component_id) {
    SelectionState next_selection;
    if (component_id.has_value() && model_.ContainsComponent(*component_id)) {
        next_selection.component_ids.push_back(*component_id);
    }
    if (next_selection.component_ids == selection_.component_ids &&
        next_selection.wire_ids == selection_.wire_ids) {
        return;
    }
    selection_ = std::move(next_selection);
    NotifySelectionChanged();
}

void EditorController::ToggleComponentSelection(ComponentId component_id) {
    if (!model_.ContainsComponent(component_id)) {
        return;
    }
    SelectionState next = selection_;
    next.wire_ids.clear();
    const auto iterator = std::find(next.component_ids.begin(), next.component_ids.end(), component_id);
    if (iterator == next.component_ids.end()) {
        next.component_ids.push_back(component_id);
        std::sort(next.component_ids.begin(), next.component_ids.end());
    } else {
        next.component_ids.erase(iterator);
    }
    if (next.component_ids != selection_.component_ids || next.wire_ids != selection_.wire_ids) {
        selection_ = std::move(next);
        NotifySelectionChanged();
    }
}

Result<ComponentId> EditorController::CreateComposite(std::string type_id,
                                                       std::string display_name) {
    if (selection_.component_ids.empty()) {
        return Result<ComponentId>::Failure(ErrorCode::kInvalidArgument,
                                             "请先选择要封装的电路元件");
    }
    if (type_id.empty() || display_name.empty()) {
        return Result<ComponentId>::Failure(ErrorCode::kInvalidArgument,
                                             "元件名称和类型标识不能为空");
    }

    const SchematicSnapshot source = model_.GetSnapshot();
    std::unordered_set<ComponentId> selected(selection_.component_ids.begin(),
                                              selection_.component_ids.end());
    auto module = std::make_shared<CompositeModule>();
    module->definition.type_id = std::move(type_id);
    module->definition.display_name = std::move(display_name);
    module->definition.category = "custom";

    std::vector<ComponentInstance> inputs;
    std::vector<ComponentInstance> outputs;
    Point2D top_left{std::numeric_limits<double>::max(), std::numeric_limits<double>::max()};
    double max_right = 0.0;
    double max_bottom = 0.0;
    for (const ComponentInstance& component : source.components) {
        if (selected.count(component.id) == 0) {
            continue;
        }
        if (component.type_id == "io.input") {
            inputs.push_back(component);
        } else if (component.type_id == "io.output") {
            outputs.push_back(component);
        } else if (library_.FindComposite(component.type_id) != nullptr) {
            return Result<ComponentId>::Failure(ErrorCode::kInvalidComponent,
                                                 "首版暂不支持封装内嵌套自定义元件");
        }
        const auto definition = library_.FindByTypeId(component.type_id);
        if (!definition.has_value()) {
            return Result<ComponentId>::Failure(ErrorCode::kNotFound,
                                                 "选区包含元件库中不存在的元件类型");
        }
        top_left.x = std::min(top_left.x, component.position.x);
        top_left.y = std::min(top_left.y, component.position.y);
        max_right = std::max(max_right, component.position.x + definition->default_bounds.width);
        max_bottom = std::max(max_bottom, component.position.y + definition->default_bounds.height);
        module->implementation.components.push_back(component);
    }
    if (inputs.empty() || outputs.empty()) {
        return Result<ComponentId>::Failure(ErrorCode::kInvalidComponent,
                                             "选区至少需要一个 Input 和一个 Output 元件作为端口");
    }

    auto by_position = [](const ComponentInstance& first, const ComponentInstance& second) {
        return first.position.y != second.position.y ? first.position.y < second.position.y
                                                     : first.position.x < second.position.x;
    };
    std::sort(inputs.begin(), inputs.end(), by_position);
    std::sort(outputs.begin(), outputs.end(), by_position);

    const double pin_step = 20.0;
    const std::size_t rows = std::max(inputs.size(), outputs.size());
    module->definition.default_bounds = {0.0, 0.0, 100.0,
                                         std::max(40.0, 20.0 * rows + 10.0)};
    PinId external_pin = 1;
    auto add_ports = [&](const std::vector<ComponentInstance>& ports, PinDirection direction,
                         std::vector<CompositePortMapping>& mappings, double x) -> bool {
        for (std::size_t index = 0; index < ports.size(); ++index) {
            const ComponentInstance& instance = ports[index];
            const auto definition = library_.FindByTypeId(instance.type_id);
            if (!definition.has_value()) {
                return false;
            }
            const auto internal_pin = std::find_if(
                definition->pins.begin(), definition->pins.end(), [direction](const PinDefinition& pin) {
                    return pin.direction == (direction == PinDirection::kInput
                                                 ? PinDirection::kOutput
                                                 : PinDirection::kInput);
                });
            if (internal_pin == definition->pins.end()) {
                return false;
            }
            const auto label = instance.properties.find("label");
            const std::string pin_name = label != instance.properties.end() && !label->second.empty()
                                             ? label->second
                                             : instance.reference;
            const Point2D relative{x, 15.0 + pin_step * index};
            module->definition.pins.push_back(
            PinDefinition{external_pin, pin_name, direction, relative});
            mappings.push_back({external_pin, instance.id, internal_pin->local_id});
            ++external_pin;
        }
        return true;
    };
    if (!add_ports(inputs, PinDirection::kInput, module->inputs, 0.0) ||
        !add_ports(outputs, PinDirection::kOutput, module->outputs,
                   module->definition.default_bounds.width)) {
        return Result<ComponentId>::Failure(ErrorCode::kInvalidComponent,
                                             "Input/Output 元件缺少有效方向引脚");
    }

    for (const WireModel& wire : source.wires) {
        const bool from_selected = wire.from.pin.has_value() &&
                                   selected.count(wire.from.pin->component_id) != 0;
        const bool to_selected = wire.to.pin.has_value() &&
                                 selected.count(wire.to.pin->component_id) != 0;
        if (!from_selected && !to_selected) {
            continue;
        }
        if ((wire.from.pin.has_value() && !from_selected) ||
            (wire.to.pin.has_value() && !to_selected)) {
            return Result<ComponentId>::Failure(ErrorCode::kInvalidConnection,
                                                 "有导线连接到选区外，请先断开跨界连线");
        }
        if (!wire.from.pin.has_value() || !wire.to.pin.has_value()) {
            return Result<ComponentId>::Failure(ErrorCode::kInvalidConnection,
                                                 "封装电路的每根导线都必须连接两个选区内引脚");
        }
        module->implementation.wires.push_back(wire);
    }

    module->definition.category = "custom";
    const Result<void> registered = library_.RegisterComposite(module);
    if (!registered.ok) {
        return Result<ComponentId>::Failure(registered.code, registered.message);
    }

    const Point2D center{(top_left.x + max_right) / 2.0, (top_left.y + max_bottom) / 2.0};
    const auto created = factory_.Create(module->definition.type_id, SnapToGrid(center));
    if (!created.ok) {
        return Result<ComponentId>::Failure(created.code, created.message);
    }
    SchematicSnapshot replacement = GetSnapshot();
    replacement.components.erase(
        std::remove_if(replacement.components.begin(), replacement.components.end(),
                       [&selected](const ComponentInstance& component) {
                           return selected.count(component.id) != 0;
                       }),
        replacement.components.end());
    replacement.wires.erase(
        std::remove_if(replacement.wires.begin(), replacement.wires.end(),
                       [&selected](const WireModel& wire) {
                           return (wire.from.pin.has_value() &&
                                   selected.count(wire.from.pin->component_id) != 0) ||
                                  (wire.to.pin.has_value() &&
                                   selected.count(wire.to.pin->component_id) != 0);
                       }),
        replacement.wires.end());
    replacement.components.push_back(created.value);
    replacement.composite_modules.push_back(module);
    LoadSnapshot(replacement);
    SelectComponent(created.value.id);
    return Result<ComponentId>::Success(created.value.id);
}

void EditorController::SelectWire(std::optional<WireId> wire_id) {
    SelectionState next_selection;
    if (wire_id.has_value() && model_.ContainsWire(*wire_id)) {
        next_selection.wire_ids.push_back(*wire_id);
    }
    if (next_selection.component_ids == selection_.component_ids &&
        next_selection.wire_ids == selection_.wire_ids &&
        next_selection.text_label_ids == selection_.text_label_ids) {
        return;
    }
    selection_ = std::move(next_selection);
    NotifySelectionChanged();
}

void EditorController::SelectTextLabel(std::optional<TextLabelId> label_id) {
    SelectionState next_selection;
    if (label_id.has_value() && model_.ContainsTextLabel(*label_id)) {
        next_selection.text_label_ids.push_back(*label_id);
    }
    if (next_selection.component_ids == selection_.component_ids &&
        next_selection.wire_ids == selection_.wire_ids &&
        next_selection.text_label_ids == selection_.text_label_ids) {
        return;
    }
    selection_ = std::move(next_selection);
    NotifySelectionChanged();
}

void EditorController::SelectAll() {
    const SchematicSnapshot snapshot = model_.GetSnapshot();
    SelectionState next_selection;
    next_selection.component_ids.reserve(snapshot.components.size());
    for (const ComponentInstance& component : snapshot.components) {
        next_selection.component_ids.push_back(component.id);
    }
    next_selection.wire_ids.reserve(snapshot.wires.size());
    for (const WireModel& wire : snapshot.wires) {
        next_selection.wire_ids.push_back(wire.id);
    }
    next_selection.text_label_ids.reserve(snapshot.text_labels.size());
    for (const TextLabel& label : snapshot.text_labels) {
        next_selection.text_label_ids.push_back(label.id);
    }
    if (next_selection.component_ids == selection_.component_ids &&
        next_selection.wire_ids == selection_.wire_ids &&
        next_selection.text_label_ids == selection_.text_label_ids) {
        return;
    }
    selection_ = std::move(next_selection);
    NotifySelectionChanged();
}

bool EditorController::MoveComponent(ComponentId component_id, Point2D position) {
    return MoveComponents({component_id}, {position}, false);
}

bool EditorController::MoveComponents(const std::vector<ComponentId>& component_ids,
                                      const std::vector<Point2D>& positions,
                                      bool coalesce_with_previous) {
    if (component_ids.empty() || component_ids.size() != positions.size()) {
        return false;
    }

    std::vector<MoveSelectionCommand::ComponentMove> moves;
    std::vector<ComponentId> moved_ids;
    std::vector<Point2D> moved_positions;
    for (std::size_t index = 0; index < component_ids.size(); ++index) {
        const std::optional<ComponentInstance> component =
            model_.GetComponent(component_ids[index]);
        if (!component.has_value()) {
            continue;
        }
        const Point2D new_position = SnapToGrid(positions[index]);
        if (component->position.x == new_position.x && component->position.y == new_position.y) {
            continue;
        }
        moves.push_back({component_ids[index], component->position, new_position});
        moved_ids.push_back(component_ids[index]);
        moved_positions.push_back(new_position);
    }
    if (moves.empty()) {
        return false;
    }

    std::vector<MoveSelectionCommand::WireRouteChange> wire_changes;
    const std::vector<WireRouteChange> planned = PlanWireReroutes(moved_ids, moved_positions);
    wire_changes.reserve(planned.size());
    for (const WireRouteChange& change : planned) {
        wire_changes.push_back({change.wire_id, change.old_route_points,
                                change.new_route_points});
    }

    // 方向键连续微调：并进上一条"微调专用"的 MoveSelection，撤销一次回起点，
    // 不然每按一下方向键就多一条撤销记录，撤销像"没反应"。
    // 拖动提交的命令 coalescable=false，不会被微调并进去。
    if (coalesce_with_previous && !undo_stack_.empty()) {
        auto* previous = dynamic_cast<MoveSelectionCommand*>(undo_stack_.back().get());
        if (previous != nullptr && previous->Coalescable() &&
            previous->CoversComponents(moved_ids)) {
            previous->UpdateTargets(moved_positions, wire_changes);
            NotifyModelChanged();
            return true;
        }
    }

    return ExecuteCommand(std::make_unique<MoveSelectionCommand>(
        NextCommandId(), model_, std::move(moves), std::move(wire_changes),
        coalesce_with_previous));
}

std::vector<EditorController::WireRouteChange> EditorController::PlanWireReroutes(
    const std::vector<ComponentId>& moved_ids, const std::vector<Point2D>& new_positions) const {
    SchematicSnapshot probe = model_.GetSnapshot();
    for (std::size_t index = 0; index < moved_ids.size(); ++index) {
        for (ComponentInstance& component : probe.components) {
            if (component.id == moved_ids[index]) {
                component.position = new_positions[index];
                break;
            }
        }
    }
    const std::unordered_set<ComponentId> moved(moved_ids.begin(), moved_ids.end());

    std::vector<WireRouteChange> changes;
    for (const WireModel& wire : probe.wires) {
        const bool from_moved =
            wire.from.pin.has_value() && moved.count(wire.from.pin->component_id) != 0;
        const bool to_moved =
            wire.to.pin.has_value() && moved.count(wire.to.pin->component_id) != 0;
        if (!from_moved && !to_moved) {
            continue;
        }
        WireRouteChange change;
        change.wire_id = wire.id;
        change.old_route_points = wire.route_points;
        change.new_route_points = model::PlanWireRoute(
            probe, library_, model::ResolveWireEndpoint(probe, library_, wire.from),
            model::ResolveWireEndpoint(probe, library_, wire.to), wire.from.pin, wire.to.pin);
        changes.push_back(std::move(change));
    }
    return changes;
}

bool EditorController::RotateSelected() {
    if (selection_.component_ids.empty()) {
        return false;
    }

    std::vector<RotateSelectionCommand::ComponentRotate> rotations;
    for (ComponentId component_id : selection_.component_ids) {
        const std::optional<ComponentInstance> component = model_.GetComponent(component_id);
        if (!component.has_value()) {
            continue;
        }
        double new_rotation = std::fmod(component->rotation_deg + 90.0, 360.0);
        if (new_rotation < 0.0) {
            new_rotation += 360.0;
        }
        rotations.push_back({component_id, component->rotation_deg, new_rotation});
    }
    if (rotations.empty()) {
        return false;
    }
    return ExecuteCommand(std::make_unique<RotateSelectionCommand>(
        NextCommandId(), model_, std::move(rotations)));
}

bool EditorController::DeleteSelection() {
    if (selection_.component_ids.empty() && selection_.wire_ids.empty() &&
        selection_.text_label_ids.empty()) {
        return false;
    }

    std::vector<ComponentInstance> components;
    std::vector<WireModel> wires;
    std::vector<TextLabel> text_labels;
    for (ComponentId component_id : selection_.component_ids) {
        const std::optional<ComponentInstance> component = model_.GetComponent(component_id);
        if (component.has_value()) {
            components.push_back(*component);
        }
    }
    // 只删显式选中的导线：删元件不再连带删线，半张图的线不会跟着消失；
    // 挂空的导线端点退化为自由坐标，仍可单独选中删除或重新接线。
    std::unordered_set<WireId> wire_ids;
    for (WireId wire_id : selection_.wire_ids) {
        const std::optional<WireModel> wire = model_.GetWire(wire_id);
        if (wire.has_value() && wire_ids.insert(wire_id).second) {
            wires.push_back(*wire);
        }
    }
    for (TextLabelId label_id : selection_.text_label_ids) {
        const std::optional<TextLabel> label = model_.GetTextLabel(label_id);
        if (label.has_value()) {
            text_labels.push_back(*label);
        }
    }

    // 选区里可能全是已经不存在的 id（比如撤销后留下的死引用）：
    // 过滤后没东西可删就别把空命令推进撤销栈污染历史。
    if (components.empty() && wires.empty() && text_labels.empty()) {
        return false;
    }

    if (!ExecuteCommand(std::make_unique<DeleteSelectionCommand>(
            NextCommandId(), model_, std::move(components), std::move(wires),
            std::move(text_labels)))) {
        return false;
    }

    selection_ = {};
    pending_wire_.reset();
    NotifySelectionChanged();
    return true;
}

Result<TextLabelId> EditorController::AddTextLabel(Point2D position, std::string text) {
    // 文本要放进单行记录里保存，换行会破坏工程文件格式，这里先拍平。
    for (char& character : text) {
        if (character == '\n' || character == '\r' || character == '\t') {
            character = ' ';
        }
    }
    while (!text.empty() && text.front() == ' ') {
        text.erase(text.begin());
    }
    while (!text.empty() && text.back() == ' ') {
        text.pop_back();
    }
    if (text.empty()) {
        return Result<TextLabelId>::Failure(ErrorCode::kInvalidArgument, "文本内容不能为空");
    }

    TextLabel label;
    label.position = SnapToGrid(position);
    label.text = text;
    auto command = std::make_unique<AddTextLabelCommand>(NextCommandId(), model_, label);
    AddTextLabelCommand* raw = command.get();
    if (!ExecuteCommand(std::move(command))) {
        return Result<TextLabelId>::Failure(ErrorCode::kInvalidArgument,
                                            "无法添加文本标注");
    }
    SelectTextLabel(raw->Label().id);
    return Result<TextLabelId>::Success(raw->Label().id);
}

bool EditorController::MoveTextLabel(TextLabelId label_id, Point2D position) {
    const std::optional<TextLabel> label = model_.GetTextLabel(label_id);
    if (!label.has_value()) {
        return false;
    }
    const Point2D new_position = SnapToGrid(position);
    if (label->position.x == new_position.x && label->position.y == new_position.y) {
        return false;
    }
    return ExecuteCommand(std::make_unique<MoveTextLabelCommand>(
        NextCommandId(), model_, label_id, label->position, new_position));
}

bool EditorController::BeginWire(PinRef pin, Point2D position) {
    if (tool_mode_ != ToolMode::kWire || !IsValidPin(pin)) {
        return false;
    }
    PendingWire pending_wire;
    pending_wire.start_pin = pin;
    pending_wire.start_position = position;
    pending_wire_ = std::move(pending_wire);
    return true;
}

bool EditorController::AddWireBend(Point2D position) {
    if (!pending_wire_.has_value() || SamePoint(WireAnchor(*pending_wire_), position)) {
        return false;
    }
    pending_wire_->route_points.push_back(position);
    return true;
}

bool EditorController::RemoveLastWireBend() {
    if (!pending_wire_.has_value() || pending_wire_->route_points.empty()) {
        return false;
    }
    pending_wire_->route_points.pop_back();
    return true;
}

bool EditorController::CompleteWire(PinRef pin, Point2D position) {
    if (!pending_wire_.has_value() || !IsValidPin(pin) ||
        (pending_wire_->start_pin.component_id == pin.component_id &&
         pending_wire_->start_pin.pin_id == pin.pin_id)) {
        return false;
    }
    return FinishPendingWire(pin, position);
}

bool EditorController::CompleteWireAtPoint(Point2D position) {
    return FinishPendingWire(std::nullopt, position);
}

bool EditorController::FinishWireAtLastBend() {
    if (!pending_wire_.has_value() || pending_wire_->route_points.empty()) {
        return false;
    }
    const Point2D last_bend = pending_wire_->route_points.back();
    pending_wire_->route_points.pop_back();
    return FinishPendingWire(std::nullopt, last_bend);
}

// 收尾：把开始引脚、已定拐点、终点组装成一条导线，中间不产生零长度段。
bool EditorController::FinishPendingWire(std::optional<PinRef> end_pin, Point2D position) {
    if (!pending_wire_.has_value()) {
        return false;
    }

    std::vector<Point2D> route_points;
    Point2D previous = pending_wire_->start_position;
    for (Point2D bend : pending_wire_->route_points) {
        if (SamePoint(previous, bend)) {
            continue;
        }
        route_points.push_back(bend);
        previous = bend;
    }
    // 终点正好落在最后一个拐点上时，这个拐点直接升级成终点。
    while (!route_points.empty() && SamePoint(route_points.back(), position)) {
        route_points.pop_back();
    }
    if (route_points.empty() && SamePoint(pending_wire_->start_position, position)) {
        return false;  // 退化成零长度导线
    }

    WireModel wire;
    wire.from.pin = pending_wire_->start_pin;
    wire.from.position = pending_wire_->start_position;
    wire.to.pin = end_pin;
    wire.to.position = position;
    wire.route_points = std::move(route_points);

    // 完全相同的连接已经存在时拒绝：两条重叠的导线画布上看不出来，
    // 但选中/删除会一次次"咬"到同一条线。
    if (DuplicateWireExists(wire, model_.GetSnapshot(), library_)) {
        return false;
    }

    const bool executed = ExecuteCommand(
        std::make_unique<AddWireCommand>(NextCommandId(), model_, std::move(wire)));
    if (executed) {
        pending_wire_.reset();
        selection_ = {};
        NotifySelectionChanged();
    }
    return executed;
}

void EditorController::CancelWire() {
    pending_wire_.reset();
}

std::optional<PendingWire> EditorController::GetPendingWire() const {
    return pending_wire_;
}

Point2D EditorController::SnapToGrid(Point2D position) const {
    return {std::round(position.x / kDefaultGridSize) * kDefaultGridSize,
            std::round(position.y / kDefaultGridSize) * kDefaultGridSize};
}

bool EditorController::Undo() {
    if (undo_stack_.empty()) {
        return false;
    }
    // 撤销改了模型，正在画的线可能挂在刚被撤掉的元件上——
    // 不取消的话半成品虚线会一直悬在画布上，只能靠 Esc 清掉。
    pending_wire_.reset();
    std::unique_ptr<ICommand> command = std::move(undo_stack_.back());
    undo_stack_.pop_back();
    command->Undo();
    redo_stack_.push_back(std::move(command));
    NotifyModelChanged();
    return true;
}

bool EditorController::Redo() {
    if (redo_stack_.empty()) {
        return false;
    }
    // 理由同 Undo：模型要变，画到一半的线一并取消。
    pending_wire_.reset();
    std::unique_ptr<ICommand> command = std::move(redo_stack_.back());
    redo_stack_.pop_back();
    if (!command->Execute()) {
        redo_stack_.push_back(std::move(command));
        return false;
    }
    undo_stack_.push_back(std::move(command));
    NotifyModelChanged();
    return true;
}

bool EditorController::CanUndo() const {
    return !undo_stack_.empty();
}

bool EditorController::CanRedo() const {
    return !redo_stack_.empty();
}

SchematicSnapshot EditorController::GetSnapshot() const {
    SchematicSnapshot snapshot = model_.GetSnapshot();
    snapshot.composite_modules.clear();
    for (const ComponentDefinition& definition : library_.GetAll()) {
        const auto module = library_.FindComposite(definition.type_id);
        if (module != nullptr) {
            snapshot.composite_modules.push_back(
                std::const_pointer_cast<CompositeModule>(module));
        }
    }
    return snapshot;
}

// 整体替换模型内容，用于打开工程文件。替换是破坏性的：
// 撤销/重做栈和选区一起清空，历史对不上新图。
void EditorController::LoadSnapshot(const SchematicSnapshot& snapshot) {
    for (const auto& module : snapshot.composite_modules) {
        if (module != nullptr && library_.FindComposite(module->definition.type_id) == nullptr) {
            static_cast<void>(library_.RegisterComposite(std::make_shared<CompositeModule>(*module)));
        }
    }
    pending_wire_.reset();
    selection_ = {};
    undo_stack_.clear();
    redo_stack_.clear();
    model_.Clear();
    for (const ComponentInstance& component : snapshot.components) {
        static_cast<void>(model_.AddComponent(component));
    }
    for (const WireModel& wire : snapshot.wires) {
        WireModel copy = wire;
        static_cast<void>(model_.AddWire(copy));
    }
    factory_.SyncWithSnapshot(model_.GetSnapshot());
    NotifyModelChanged();
}

std::optional<ComponentInstance> EditorController::GetComponent(ComponentId id) const {
    return model_.GetComponent(id);
}

SelectionState EditorController::GetSelection() const {
    return selection_;
}

void EditorController::SetModelChangedHandler(ModelChangedHandler handler) {
    model_changed_handler_ = std::move(handler);
}

void EditorController::SetSelectionChangedHandler(SelectionChangedHandler handler) {
    selection_changed_handler_ = std::move(handler);
}

bool EditorController::IsValidPin(PinRef pin) const {
    if (!model_.ContainsComponent(pin.component_id) || pin.pin_id == 0) {
        return false;
    }

    const std::optional<ComponentInstance> component = model_.GetComponent(pin.component_id);
    if (!component.has_value()) {
        return false;
    }
    const std::optional<ComponentDefinition> definition =
        library_.FindByTypeId(component->type_id);
    if (!definition.has_value()) {
        return false;
    }
    return std::any_of(definition->pins.begin(), definition->pins.end(),
                       [pin](const PinDefinition& definition_pin) {
                           return definition_pin.local_id == pin.pin_id;
                       });
}

bool EditorController::ExecuteCommand(std::unique_ptr<ICommand> command) {
    if (command == nullptr || !command->Execute()) {
        return false;
    }
    undo_stack_.push_back(std::move(command));
    redo_stack_.clear();
    NotifyModelChanged();
    return true;
}

void EditorController::NotifyModelChanged() {
    if (model_changed_handler_) {
        model_changed_handler_(ModelChangedEvent{model_.GetRevision()});
    }
}

void EditorController::NotifySelectionChanged() {
    if (selection_changed_handler_) {
        selection_changed_handler_(SelectionChangedEvent{selection_.component_ids,
                                                         selection_.wire_ids});
    }
}

CommandId EditorController::NextCommandId() {
    return next_command_id_++;
}

}  // namespace cse::editor
