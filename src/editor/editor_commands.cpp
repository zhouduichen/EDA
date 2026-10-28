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
    // AddWire 会把传进去的 wire move 进模型，所以拿一份副本去调用：
    // 命令自己要留着完整几何（含拐点）才能原样重做，
    // 顺便把模型分配的 id 记回来，撤销时才找得到这条导线。
    WireModel request = wire_;
    if (!model_.AddWire(request)) {
        return false;
    }
    wire_.id = request.id;
    return true;
}

void AddWireCommand::Undo() {
    WireModel removed_wire;
    static_cast<void>(model_.RemoveWire(wire_.id, &removed_wire));
}

DeleteSelectionCommand::DeleteSelectionCommand(CommandId command_id,
                                               model::SchematicModel& model,
                                               std::vector<ComponentInstance> components,
                                               std::vector<WireModel> wires,
                                               std::vector<TextLabel> text_labels)
    : command_id_(command_id),
      model_(model),
      components_(std::move(components)),
      wires_(std::move(wires)),
      text_labels_(std::move(text_labels)) {}

CommandId DeleteSelectionCommand::id() const {
    return command_id_;
}

std::string DeleteSelectionCommand::name() const {
    return "DeleteSelection";
}

// 只删显式选中的东西：元件不再连带删除挂在上面的导线，
// 否则"删一个元件，半张图的线都消失"远超用户的预期。
bool DeleteSelectionCommand::Execute() {
    bool removed_anything = false;
    for (const WireModel& wire : wires_) {
        WireModel removed_wire;
        if (model_.RemoveWire(wire.id, &removed_wire)) {
            removed_anything = true;
        }
    }
    for (const ComponentInstance& component : components_) {
        if (model_.RemoveComponent(component.id)) {
            removed_anything = true;
        }
    }
    for (const TextLabel& label : text_labels_) {
        if (model_.RemoveTextLabel(label.id, nullptr)) {
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
    for (const TextLabel& label : text_labels_) {
        TextLabel restored_label = label;
        static_cast<void>(model_.AddTextLabel(restored_label));
    }
}

MoveSelectionCommand::MoveSelectionCommand(CommandId command_id, model::SchematicModel& model,
                                           std::vector<ComponentMove> moves,
                                           std::vector<WireRouteChange> wire_changes,
                                           bool coalescable)
    : command_id_(command_id),
      model_(model),
      moves_(std::move(moves)),
      wire_changes_(std::move(wire_changes)),
      coalescable_(coalescable) {}

CommandId MoveSelectionCommand::id() const {
    return command_id_;
}

std::string MoveSelectionCommand::name() const {
    return "MoveSelection";
}

void MoveSelectionCommand::Apply(const std::vector<Point2D>* positions,
                                 const std::vector<std::vector<Point2D>>* routes) {
    for (std::size_t index = 0; index < moves_.size(); ++index) {
        const Point2D position = positions != nullptr ? (*positions)[index]
                                                      : moves_[index].old_position;
        static_cast<void>(model_.SetComponentPosition(moves_[index].component_id, position));
    }
    for (std::size_t index = 0; index < wire_changes_.size(); ++index) {
        const std::vector<Point2D>& route =
            routes != nullptr ? (*routes)[index] : wire_changes_[index].old_route_points;
        static_cast<void>(model_.SetWireRoutePoints(wire_changes_[index].wire_id, route));
    }
}

bool MoveSelectionCommand::Execute() {
    std::vector<Point2D> positions;
    positions.reserve(moves_.size());
    for (const ComponentMove& move : moves_) {
        positions.push_back(move.new_position);
    }
    std::vector<std::vector<Point2D>> routes;
    routes.reserve(wire_changes_.size());
    for (const WireRouteChange& change : wire_changes_) {
        routes.push_back(change.new_route_points);
    }
    Apply(&positions, &routes);
    return true;
}

void MoveSelectionCommand::Undo() {
    Apply(nullptr, nullptr);
}

void MoveSelectionCommand::UpdateTargets(const std::vector<Point2D>& new_positions,
                                         const std::vector<WireRouteChange>& wire_changes) {
    for (std::size_t index = 0; index < moves_.size(); ++index) {
        moves_[index].new_position = new_positions[index];
        static_cast<void>(model_.SetComponentPosition(moves_[index].component_id,
                                                      new_positions[index]));
    }
    wire_changes_.resize(wire_changes.size());
    for (std::size_t index = 0; index < wire_changes.size(); ++index) {
        wire_changes_[index].wire_id = wire_changes[index].wire_id;
        wire_changes_[index].new_route_points = wire_changes[index].new_route_points;
        static_cast<void>(model_.SetWireRoutePoints(wire_changes[index].wire_id,
                                                    wire_changes[index].new_route_points));
    }
}

bool MoveSelectionCommand::CoversComponents(const std::vector<ComponentId>& component_ids) const {
    if (component_ids.size() != moves_.size()) {
        return false;
    }
    for (std::size_t index = 0; index < moves_.size(); ++index) {
        if (moves_[index].component_id != component_ids[index]) {
            return false;
        }
    }
    return true;
}

RotateSelectionCommand::RotateSelectionCommand(CommandId command_id, model::SchematicModel& model,
                                               std::vector<ComponentRotate> rotations)
    : command_id_(command_id),
      model_(model),
      rotations_(std::move(rotations)) {}

CommandId RotateSelectionCommand::id() const {
    return command_id_;
}

std::string RotateSelectionCommand::name() const {
    return "RotateSelection";
}

bool RotateSelectionCommand::Execute() {
    for (const ComponentRotate& rotation : rotations_) {
        static_cast<void>(model_.SetComponentRotation(rotation.component_id,
                                                      rotation.new_rotation_deg));
    }
    return true;
}

void RotateSelectionCommand::Undo() {
    for (const ComponentRotate& rotation : rotations_) {
        static_cast<void>(model_.SetComponentRotation(rotation.component_id,
                                                      rotation.old_rotation_deg));
    }
}

AddTextLabelCommand::AddTextLabelCommand(CommandId command_id, model::SchematicModel& model,
                                         TextLabel label)
    : command_id_(command_id), model_(model), label_(std::move(label)) {}

CommandId AddTextLabelCommand::id() const {
    return command_id_;
}

std::string AddTextLabelCommand::name() const {
    return "AddTextLabel";
}

bool AddTextLabelCommand::Execute() {
    return model_.AddTextLabel(label_);
}

void AddTextLabelCommand::Undo() {
    static_cast<void>(model_.RemoveTextLabel(label_.id, nullptr));
}

MoveTextLabelCommand::MoveTextLabelCommand(CommandId command_id, model::SchematicModel& model,
                                           TextLabelId label_id, Point2D old_position,
                                           Point2D new_position)
    : command_id_(command_id),
      model_(model),
      label_id_(label_id),
      old_position_(old_position),
      new_position_(new_position) {}

CommandId MoveTextLabelCommand::id() const {
    return command_id_;
}

std::string MoveTextLabelCommand::name() const {
    return "MoveTextLabel";
}

bool MoveTextLabelCommand::Execute() {
    return model_.SetTextLabelPosition(label_id_, new_position_);
}

void MoveTextLabelCommand::Undo() {
    static_cast<void>(model_.SetTextLabelPosition(label_id_, old_position_));
}

}  // namespace cse::editor
