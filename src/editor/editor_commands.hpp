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
                           std::vector<WireModel> wires);

    CommandId id() const override;
    std::string name() const override;
    bool Execute() override;
    void Undo() override;

private:
    CommandId command_id_;
    model::SchematicModel& model_;
    std::vector<ComponentInstance> components_;
    std::vector<WireModel> wires_;
};

}  // namespace cse::editor
