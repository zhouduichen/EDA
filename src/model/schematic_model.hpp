#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "contracts/component_model.hpp"
#include "contracts/schematic_snapshot.hpp"
#include "contracts/wire_model.hpp"

namespace cse::model {

struct ComponentRemoval {
    ComponentInstance component;
    std::vector<WireModel> wires;
};

class SchematicModel {
public:
    bool AddComponent(ComponentInstance component);
    // 只删元件本身，不动导线：删除选区时保留连线是产品语义（挂空的导线端点
    // 会退化为自由坐标，由 ResolveWireEndpoint 兜底）。
    bool RemoveComponent(ComponentId component_id);
    bool RemoveComponentAndWires(ComponentId component_id, ComponentRemoval* removal);
    bool RestoreComponent(const ComponentRemoval& removal);
    void Clear();

    bool SetComponentPosition(ComponentId component_id, Point2D position);
    bool SetComponentRotation(ComponentId component_id, double rotation_deg);

    bool AddWire(WireModel& wire);
    bool RemoveWire(WireId wire_id, WireModel* removed_wire);
    // 元件挪动后重算拐点用：整段替换导线的 route_points。
    bool SetWireRoutePoints(WireId wire_id, std::vector<Point2D> route_points);

    bool AddTextLabel(TextLabel& label);  // id 无效时分配新 id，否则原样保留
    bool RemoveTextLabel(TextLabelId label_id, TextLabel* removed_label);
    bool SetTextLabelPosition(TextLabelId label_id, Point2D position);
    bool ContainsTextLabel(TextLabelId label_id) const;
    std::optional<TextLabel> GetTextLabel(TextLabelId label_id) const;

    bool ContainsComponent(ComponentId component_id) const;
    bool ContainsWire(WireId wire_id) const;
    std::optional<ComponentInstance> GetComponent(ComponentId component_id) const;
    std::optional<WireModel> GetWire(WireId wire_id) const;
    std::vector<WireModel> GetWiresForComponent(ComponentId component_id) const;
    SchematicSnapshot GetSnapshot() const;
    std::uint64_t GetRevision() const;

private:
    void BumpRevision();
    void UpdateNextComponentId(ComponentId component_id);
    void UpdateNextWireId(WireId wire_id);
    void UpdateNextTextLabelId(TextLabelId label_id);

    std::unordered_map<ComponentId, ComponentInstance> components_;
    std::unordered_map<WireId, WireModel> wires_;
    std::unordered_map<TextLabelId, TextLabel> text_labels_;
    ComponentId next_component_id_{1};
    WireId next_wire_id_{1};
    TextLabelId next_text_label_id_{1};
    std::uint64_t revision_{0};
};

}  // namespace cse::model
