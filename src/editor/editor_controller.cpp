#include "src/editor/editor_controller.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "src/editor/editor_commands.hpp"

namespace cse::editor {

EditorController::EditorController(model::SchematicModel& model, const IComponentLibrary& library,
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

void EditorController::SelectWire(std::optional<WireId> wire_id) {
    SelectionState next_selection;
    if (wire_id.has_value() && model_.ContainsWire(*wire_id)) {
        next_selection.wire_ids.push_back(*wire_id);
    }
    if (next_selection.component_ids == selection_.component_ids &&
        next_selection.wire_ids == selection_.wire_ids) {
        return;
    }
    selection_ = std::move(next_selection);
    NotifySelectionChanged();
}

bool EditorController::MoveComponent(ComponentId component_id, Point2D position) {
    const std::optional<ComponentInstance> component = model_.GetComponent(component_id);
    if (!component.has_value()) {
        return false;
    }

    const Point2D new_position = SnapToGrid(position);
    if (component->position.x == new_position.x && component->position.y == new_position.y) {
        return false;
    }

    return ExecuteCommand(std::make_unique<MoveComponentCommand>(
        NextCommandId(), model_, component_id, component->position, new_position));
}

bool EditorController::RotateSelected() {
    if (selection_.component_ids.size() != 1) {
        return false;
    }

    const ComponentId component_id = selection_.component_ids.front();
    const std::optional<ComponentInstance> component = model_.GetComponent(component_id);
    if (!component.has_value()) {
        return false;
    }

    double new_rotation = std::fmod(component->rotation_deg + 90.0, 360.0);
    if (new_rotation < 0.0) {
        new_rotation += 360.0;
    }
    return ExecuteCommand(std::make_unique<RotateComponentCommand>(
        NextCommandId(), model_, component_id, component->rotation_deg, new_rotation));
}

bool EditorController::DeleteSelection() {
    if (selection_.component_ids.empty() && selection_.wire_ids.empty()) {
        return false;
    }

    std::vector<ComponentInstance> components;
    std::vector<WireModel> wires;
    std::unordered_set<WireId> wire_ids;
    for (ComponentId component_id : selection_.component_ids) {
        const std::optional<ComponentInstance> component = model_.GetComponent(component_id);
        if (!component.has_value()) {
            continue;
        }
        components.push_back(*component);
        for (const WireModel& wire : model_.GetWiresForComponent(component_id)) {
            if (wire_ids.insert(wire.id).second) {
                wires.push_back(wire);
            }
        }
    }
    for (WireId wire_id : selection_.wire_ids) {
        const std::optional<WireModel> wire = model_.GetWire(wire_id);
        if (wire.has_value() && wire_ids.insert(wire_id).second) {
            wires.push_back(*wire);
        }
    }

    if (!ExecuteCommand(std::make_unique<DeleteSelectionCommand>(
            NextCommandId(), model_, std::move(components), std::move(wires)))) {
        return false;
    }

    selection_ = {};
    pending_wire_.reset();
    NotifySelectionChanged();
    return true;
}

bool EditorController::BeginWire(PinRef pin, Point2D position) {
    if (tool_mode_ != ToolMode::kWire || !IsValidPin(pin)) {
        return false;
    }
    pending_wire_ = PendingWire{pin, position};
    return true;
}

bool EditorController::CompleteWire(PinRef pin, Point2D position) {
    if (!pending_wire_.has_value() || !IsValidPin(pin) ||
        (pending_wire_->start_pin.component_id == pin.component_id &&
         pending_wire_->start_pin.pin_id == pin.pin_id)) {
        return false;
    }

    WireModel wire;
    wire.from.pin = pending_wire_->start_pin;
    wire.from.position = pending_wire_->start_position;
    wire.to.pin = pin;
    wire.to.position = position;

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
    return model_.GetSnapshot();
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
