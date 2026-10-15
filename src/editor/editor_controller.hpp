#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "contracts/command.hpp"
#include "contracts/component_factory.hpp"
#include "contracts/component_library.hpp"
#include "contracts/editor_query.hpp"
#include "contracts/events.hpp"
namespace cse::model {
class SchematicModel;
}

namespace cse::editor {

struct PendingWire {
    PinRef start_pin;
    Point2D start_position;
    // 画到一半时已经定下的拐点（坐标为画布吸附后的最终坐标，不再二次吸附）。
    std::vector<Point2D> route_points;
};

class EditorController final : public IEditorQuery {
public:
    using ModelChangedHandler = std::function<void(const ModelChangedEvent&)>;
    using SelectionChangedHandler = std::function<void(const SelectionChangedEvent&)>;

    EditorController(model::SchematicModel& model, const IComponentLibrary& library,
                     IComponentFactory& factory);

    void SetToolMode(ToolMode mode);
    ToolMode GetToolMode() const override;
    void SetPlacementType(std::string type_id);
    const std::string& GetPlacementType() const;

    bool PlaceComponent(Point2D position);
    void SelectComponent(std::optional<ComponentId> component_id);
    void SelectWire(std::optional<WireId> wire_id);
    bool MoveComponent(ComponentId component_id, Point2D position);
    bool RotateSelected();
    bool DeleteSelection();

    bool BeginWire(PinRef pin, Point2D position);
    bool CompleteWire(PinRef pin, Point2D position);
    // 终点落在空白处或既有导线上（T 形相接）：position 由画布决定是否吸附。
    bool CompleteWireAtPoint(Point2D position);
    // 布线途中追加一个拐点。
    bool AddWireBend(Point2D position);
    // 退掉最后一个拐点（Backspace）。
    bool RemoveLastWireBend();
    // 在最后一个拐点收尾（回车），省掉「最后再点一下」。
    bool FinishWireAtLastBend();
    void CancelWire();
    std::optional<PendingWire> GetPendingWire() const;

    Point2D SnapToGrid(Point2D position) const;
    bool Undo();
    bool Redo();
    bool CanUndo() const;
    bool CanRedo() const;

    SchematicSnapshot GetSnapshot() const override;
    void LoadSnapshot(const SchematicSnapshot& snapshot);
    std::optional<ComponentInstance> GetComponent(ComponentId id) const override;
    SelectionState GetSelection() const override;

    void SetModelChangedHandler(ModelChangedHandler handler);
    void SetSelectionChangedHandler(SelectionChangedHandler handler);

private:
    bool IsValidPin(PinRef pin) const;
    bool FinishPendingWire(std::optional<PinRef> end_pin, Point2D position);
    bool ExecuteCommand(std::unique_ptr<ICommand> command);
    void NotifyModelChanged();
    void NotifySelectionChanged();
    CommandId NextCommandId();

    model::SchematicModel& model_;
    const IComponentLibrary& library_;
    IComponentFactory& factory_;
    ToolMode tool_mode_{ToolMode::kSelect};
    std::string placement_type_;
    SelectionState selection_;
    std::optional<PendingWire> pending_wire_;
    std::vector<std::unique_ptr<ICommand>> undo_stack_;
    std::vector<std::unique_ptr<ICommand>> redo_stack_;
    CommandId next_command_id_{1};
    ModelChangedHandler model_changed_handler_;
    SelectionChangedHandler selection_changed_handler_;
};

}  // namespace cse::editor
