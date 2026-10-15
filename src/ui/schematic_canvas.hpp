#pragma once

#include <optional>
#include <vector>

#include <wx/panel.h>

#include "contracts/component_library.hpp"
#include "contracts/geometry.hpp"
#include "contracts/ids.hpp"
#include "contracts/wire_model.hpp"
#include "src/editor/editor_controller.hpp"
#include "src/sim/logic_simulator.hpp"

class wxKeyEvent;
class wxMouseCaptureLostEvent;
class wxMouseEvent;
class wxPaintEvent;
class wxDC;

namespace cse::ui {

class SchematicCanvas final : public wxPanel {
public:
    SchematicCanvas(wxWindow* parent, editor::EditorController& editor,
                    const IComponentLibrary& library);

    void ZoomIn();
    void ZoomOut();
    void FitContent();

    // 仿真可视化：传入结果后导线按电平着色、IO 元件显示电平徽标；
    // 传 nullopt 恢复普通显示。
    void SetSimulationResult(std::optional<cse::sim::SimulationResult> result);
    bool IsSimulating() const { return simulation_.has_value(); }

private:
    struct PinHit {
        PinRef pin;
        Point2D position;
    };

    // 连线模式下鼠标落点的类型：优先引脚，其次既有导线（T 形相接），
    // 最后才是空白处（按网格吸附）。
    struct WireTarget {
        enum class Kind { kNone, kPin, kWire, kEmpty };

        Kind kind{Kind::kNone};
        PinRef pin;
        Point2D position;
    };

    void OnPaint(wxPaintEvent& event);
    void OnMouseLeftDown(wxMouseEvent& event);
    void OnMouseLeftUp(wxMouseEvent& event);
    void OnMouseLeftDoubleClick(wxMouseEvent& event);
    void OnMouseMove(wxMouseEvent& event);
    void OnMouseRightDown(wxMouseEvent& event);
    void OnMouseWheel(wxMouseEvent& event);
    void OnMouseCaptureLost(wxMouseCaptureLostEvent& event);
    void OnKeyDown(wxKeyEvent& event);

    Point2D ScreenToModel(wxPoint screen_point) const;
    wxPoint ModelToScreen(Point2D model_point) const;
    Point2D GetPinPosition(const ComponentInstance& component,
                           const ComponentDefinition& definition,
                           const PinDefinition& pin) const;
    Point2D RotateRelative(Point2D relative, Rect2D bounds, double rotation_deg) const;
    std::optional<ComponentId> HitComponent(Point2D model_point) const;
    std::optional<ComponentId> HitComponent(Point2D model_point,
                                            const SchematicSnapshot& snapshot) const;
    std::optional<PinHit> HitPin(Point2D model_point) const;
    std::optional<PinHit> HitPin(Point2D model_point, const SchematicSnapshot& snapshot) const;
    std::optional<WireId> HitWire(Point2D model_point) const;
    // 离光标最近的那条导线上的投影点：用于在导线中段接线（T 形相接）。
    std::optional<Point2D> HitWirePoint(Point2D model_point,
                                        const SchematicSnapshot& snapshot) const;
    WireTarget ComputeWireTarget(Point2D model_point,
                                 const SchematicSnapshot& snapshot) const;
    // 端点解析、折线拐点全部走 model::wire_route：画布、命中测试、建网共用同一份几何。
    std::vector<Point2D> WirePoints(const WireModel& wire,
                                    const SchematicSnapshot& snapshot) const;
    bool IsComponentSelected(ComponentId component_id) const;
    bool IsWireSelected(WireId wire_id) const;
    // 拖动预览：把拖动中的元件换成预览坐标，挂在它引脚上的导线于是跟着走。
    void ApplyDragPreview(SchematicSnapshot& snapshot) const;
    bool HasDragPreview() const;
    void BeginComponentDrag(ComponentId component_id, Point2D press_model);
    void EndComponentDrag();
    // 右键或捕获丢失打断拖动：这次移动不提交，元件留在原处。
    void AbortComponentDrag();
    // 待画折线到 target 这一跳的正交路径（含自动避让），提交和预览共用。
    std::vector<Point2D> PlanSegmentTo(const editor::PendingWire& pending_wire,
                                       const WireTarget& target) const;
    void CommitWireSegment(const WireTarget& target);
    void UpdateHoverCursor(Point2D model_point);
    void DrawGrid(wxDC& dc);
    void DrawWires(wxDC& dc, const SchematicSnapshot& snapshot);
    void DrawComponents(wxDC& dc, const SchematicSnapshot& snapshot);
    void DrawDragOriginMarker(wxDC& dc, const SchematicSnapshot& snapshot);
    void DrawTemporaryWire(wxDC& dc);
    void RefreshEditorViews();

    wxColour WireColour(LogicValue value) const;
    std::optional<LogicValue> PinValue(ComponentId component_id, PinId pin_id) const;

    editor::EditorController& editor_;
    const IComponentLibrary& library_;
    double zoom_{1.0};
    double offset_x_{40.0};
    double offset_y_{40.0};
    Point2D last_mouse_model_;
    bool dragging_component_{false};
    ComponentId dragging_component_id_{kInvalidComponentId};
    Point2D drag_origin_position_;
    Point2D drag_press_model_;
    std::optional<Point2D> drag_preview_position_;
    std::optional<ComponentId> hover_component_id_;
    Point2D pan_start_offset_;
    wxPoint pan_start_screen_;
    WireTarget wire_target_;
    std::optional<cse::sim::SimulationResult> simulation_;
};

}  // namespace cse::ui
