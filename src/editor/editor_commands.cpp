#include "src/editor/editor_commands.hpp"

namespace cse::editor {

AddComponentCommand::AddComponentCommand(CommandId command_id, model::SchematicModel& model,
                                         ComponentInstance component)
    : command_id_(command_id), model_(model), component_(std::move(component)) {}

CommandId AddComponentCommand::id() const {
    return command_id_;
}

std::string AddComponentCommand::name() const {
    return "AddComponent";
}

bool AddComponentCommand::Execute() {
    return model_.AddComponent(component_);
}

void AddComponentCommand::Undo() {
    model::ComponentRemoval removal;
    static_cast<void>(model_.RemoveComponentAndWires(component_.id, &removal));
}

MoveComponentCommand::MoveComponentCommand(CommandId command_id, model::SchematicModel& model,
                                           ComponentId component_id, Point2D old_position,
                                           Point2D new_position)
    : command_id_(command_id),
      model_(model),
      component_id_(component_id),
      old_position_(old_position),
      new_position_(new_position) {}

CommandId MoveComponentCommand::id() const {
    return command_id_;
}

std::string MoveComponentCommand::name() const {
    return "MoveComponent";
}

bool MoveComponentCommand::Execute() {
    return model_.SetComponentPosition(component_id_, new_position_);
}

void MoveComponentCommand::Undo() {
    static_cast<void>(model_.SetComponentPosition(component_id_, old_position_));
}

RotateComponentCommand::RotateComponentCommand(CommandId command_id,
                                               model::SchematicModel& model,
                                               ComponentId component_id,
                                               double old_rotation_deg,
                                               double new_rotation_deg)
    : command_id_(command_id),
      model_(model),
      component_id_(component_id),
      old_rotation_deg_(old_rotation_deg),
      new_rotation_deg_(new_rotation_deg) {}

CommandId RotateComponentCommand::id() const {
    return command_id_;
}

std::string RotateComponentCommand::name() const {
    return "RotateComponent";
}

bool RotateComponentCommand::Execute() {
    return model_.SetComponentRotation(component_id_, new_rotation_deg_);
}

void RotateComponentCommand::Undo() {
    static_cast<void>(model_.SetComponentRotation(component_id_, old_rotation_deg_));
}

AddWireCommand::AddWireCommand(CommandId command_id, model::SchematicModel& model,
                               WireModel wire)
    : command_id_(command_id), model_(model), wire_(std::move(wire)) {}

CommandId AddWireCommand::id() const {
    return command_id_;
}

std::string AddWireCommand::name() const {
    return "AddWire";
}

bool AddWireCommand::Execute() {
    return model_.AddWire(wire_);
}

void AddWireCommand::Undo() {
    WireModel removed_wire;
    static_cast<void>(model_.RemoveWire(wire_.id, &removed_wire));
}

DeleteSelectionCommand::DeleteSelectionCommand(CommandId command_id,
                                               model::SchematicModel& model,
                                               std::vector<ComponentInstance> components,
                                               std::vector<WireModel> wires)
    : command_id_(command_id),
      model_(model),
      components_(std::move(components)),
      wires_(std::move(wires)) {}

CommandId DeleteSelectionCommand::id() const {
    return command_id_;
}

std::string DeleteSelectionCommand::name() const {
    return "DeleteSelection";
}

bool DeleteSelectionCommand::Execute() {
    bool removed_anything = false;
    for (const WireModel& wire : wires_) {
        WireModel removed_wire;
        if (model_.RemoveWire(wire.id, &removed_wire)) {
            removed_anything = true;
        }
    }
    for (const ComponentInstance& component : components_) {
        model::ComponentRemoval removal;
        if (model_.RemoveComponentAndWires(component.id, &removal)) {
            removed_anything = true;
        }
    }
    return removed_anything;
}

void DeleteSelectionCommand::Undo() {
    for (const ComponentInstance& component : components_) {
        model::ComponentRemoval removal;
        removal.component = component;
        static_cast<void>(model_.RestoreComponent(removal));
    }
    for (const WireModel& wire : wires_) {
        WireModel restored_wire = wire;
        static_cast<void>(model_.AddWire(restored_wire));
    }
}

}  // namespace cse::editor
