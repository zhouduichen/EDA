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
#include "contracts/result.hpp"
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

    EditorController(model::SchematicModel& model, IComponentLibrary& library,
                     IComponentFactory& factory);

    void SetToolMode(ToolMode mode);
    ToolMode GetToolMode() const override;
    void SetPlacementType(std::string type_id);
    const std::string& GetPlacementType() const;

    bool PlaceComponent(Point2D position);
    void SelectComponent(std::optional<ComponentId> component_id);
    void ToggleComponentSelection(ComponentId component_id);
    void SelectWire(std::optional<WireId> wire_id);
    void SelectTextLabel(std::optional<TextLabelId> label_id);
    void SelectAll();
    Result<ComponentId> CreateComposite(std::string type_id, std::string display_name);
    bool MoveComponent(ComponentId component_id, Point2D position);
    // 多选整体移动：coalesce 为 true 时与上一条 MoveSelection 合并成一条命令
    //（方向键连续微调撤销一次就回起点）。
    bool MoveComponents(const std::vector<ComponentId>& component_ids,
                        const std::vector<Point2D>& positions, bool coalesce_with_previous);
    bool RotateSelected();
    bool DeleteSelection();
    Result<TextLabelId> AddTextLabel(Point2D position, std::string text);
    bool MoveTextLabel(TextLabelId label_id, Point2D position);

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
    struct WireRouteChange {
        WireId wire_id{kInvalidWireId};
        std::vector<Point2D> old_route_points;
        std::vector<Point2D> new_route_points;
    };

    bool IsValidPin(PinRef pin) const;
    bool FinishPendingWire(std::optional<PinRef> end_pin, Point2D position);
    // 元件挪到新位置后，挂在它们引脚上的导线按新几何重算拐点，
    // 否则导线还按旧位置存的拐点逐段补直角，挪一下就拐出锯齿。
    std::vector<WireRouteChange> PlanWireReroutes(
        const std::vector<ComponentId>& moved_ids,
        const std::vector<Point2D>& new_positions) const;
    bool ExecuteCommand(std::unique_ptr<ICommand> command);
    void NotifyModelChanged();
    void NotifySelectionChanged();
    CommandId NextCommandId();

    model::SchematicModel& model_;
    IComponentLibrary& library_;
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
