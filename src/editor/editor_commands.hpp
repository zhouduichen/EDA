#pragma once

#include <memory>
#include <vector>

#include "contracts/command.hpp"
#include "contracts/component_model.hpp"
#include "contracts/wire_model.hpp"
#include "src/model/schematic_model.hpp"

namespace cse::editor {

class AddComponentCommand final : public ICommand {
public:
    AddComponentCommand(CommandId command_id, model::SchematicModel& model,
                        ComponentInstance component);

    CommandId id() const override;
    std::string name() const override;
    bool Execute() override;
    void Undo() override;

private:
    CommandId command_id_;
    model::SchematicModel& model_;
    ComponentInstance component_;
};

class MoveComponentCommand final : public ICommand {
public:
    MoveComponentCommand(CommandId command_id, model::SchematicModel& model,
                         ComponentId component_id, Point2D old_position,
                         Point2D new_position);

    CommandId id() const override;
    std::string name() const override;
    bool Execute() override;
    void Undo() override;

private:
    CommandId command_id_;
    model::SchematicModel& model_;
    ComponentId component_id_;
    Point2D old_position_;
    Point2D new_position_;
};

class RotateComponentCommand final : public ICommand {
public:
    RotateComponentCommand(CommandId command_id, model::SchematicModel& model,
                           ComponentId component_id, double old_rotation_deg,
                           double new_rotation_deg);

    CommandId id() const override;
    std::string name() const override;
    bool Execute() override;
    void Undo() override;

private:
    CommandId command_id_;
    model::SchematicModel& model_;
    ComponentId component_id_;
    double old_rotation_deg_;
    double new_rotation_deg_;
};

class AddWireCommand final : public ICommand {
public:
    AddWireCommand(CommandId command_id, model::SchematicModel& model, WireModel wire);

    CommandId id() const override;
    std::string name() const override;
    bool Execute() override;
    void Undo() override;

private:
    CommandId command_id_;
    model::SchematicModel& model_;
    WireModel wire_;
};

class DeleteSelectionCommand final : public ICommand {
public:
    DeleteSelectionCommand(CommandId command_id, model::SchematicModel& model,
                           std::vector<ComponentInstance> components,
                           std::vector<WireModel> wires,
                           std::vector<TextLabel> text_labels);

    CommandId id() const override;
    std::string name() const override;
    bool Execute() override;
    void Undo() override;

private:
    CommandId command_id_;
    model::SchematicModel& model_;
    std::vector<ComponentInstance> components_;
    std::vector<WireModel> wires_;
    std::vector<TextLabel> text_labels_;
};

// 一次移动多个元件 + 重算受影响导线的拐点。多选拖动和方向键微调共用，
// 微调时通过 UpdateTargets 合并进同一条命令，撤销一次就退回整段拖动/微调。
class MoveSelectionCommand final : public ICommand {
public:
    struct ComponentMove {
        ComponentId component_id{kInvalidComponentId};
        Point2D old_position;
        Point2D new_position;
    };
    struct WireRouteChange {
        WireId wire_id{kInvalidWireId};
        std::vector<Point2D> old_route_points;
        std::vector<Point2D> new_route_points;
    };

    MoveSelectionCommand(CommandId command_id, model::SchematicModel& model,
                         std::vector<ComponentMove> moves,
                         std::vector<WireRouteChange> wire_changes,
                         bool coalescable);

    CommandId id() const override;
    std::string name() const override;
    bool Execute() override;
    void Undo() override;

    // 连续微调合并：调用时模型已处于上一步的新状态，只更新"目标"并直接落盘；
    // old_* 保留最初值，撤销仍然一步回到起点。
    void UpdateTargets(const std::vector<Point2D>& new_positions,
                       const std::vector<WireRouteChange>& wire_changes);
    bool CoversComponents(const std::vector<ComponentId>& component_ids) const;
    // 只有"方向键微调"产生的命令允许被后续微调合并；拖动提交的命令
    // 保持独立，撤销一步只回退一次拖动，不会把之后的微调一并吞掉。
    bool Coalescable() const { return coalescable_; }

private:
    void Apply(const std::vector<Point2D>* positions,
               const std::vector<std::vector<Point2D>>* routes);

    CommandId command_id_;
    model::SchematicModel& model_;
    std::vector<ComponentMove> moves_;
    std::vector<WireRouteChange> wire_changes_;
    bool coalescable_{false};
};

// 多选整体旋转：每 90° 一档，一次命令覆盖全部选中元件。
class RotateSelectionCommand final : public ICommand {
public:
    struct ComponentRotate {
        ComponentId component_id{kInvalidComponentId};
        double old_rotation_deg{0.0};
        double new_rotation_deg{0.0};
    };

    RotateSelectionCommand(CommandId command_id, model::SchematicModel& model,
                           std::vector<ComponentRotate> rotations);

    CommandId id() const override;
    std::string name() const override;
    bool Execute() override;
    void Undo() override;

private:
    CommandId command_id_;
    model::SchematicModel& model_;
    std::vector<ComponentRotate> rotations_;
};

class AddTextLabelCommand final : public ICommand {
public:
    AddTextLabelCommand(CommandId command_id, model::SchematicModel& model, TextLabel label);

    CommandId id() const override;
    std::string name() const override;
    bool Execute() override;
    void Undo() override;

    const TextLabel& Label() const { return label_; }

private:
    CommandId command_id_;
    model::SchematicModel& model_;
    TextLabel label_;
};

class MoveTextLabelCommand final : public ICommand {
public:
    MoveTextLabelCommand(CommandId command_id, model::SchematicModel& model,
                         TextLabelId label_id, Point2D old_position, Point2D new_position);

    CommandId id() const override;
    std::string name() const override;
    bool Execute() override;
    void Undo() override;

private:
    CommandId command_id_;
    model::SchematicModel& model_;
    TextLabelId label_id_;
    Point2D old_position_;
    Point2D new_position_;
};

}  // namespace cse::editor
