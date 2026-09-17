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
    bool RemoveComponentAndWires(ComponentId component_id, ComponentRemoval* removal);
    bool RestoreComponent(const ComponentRemoval& removal);

    bool SetComponentPosition(ComponentId component_id, Point2D position);
    bool SetComponentRotation(ComponentId component_id, double rotation_deg);

    bool AddWire(WireModel& wire);
    bool RemoveWire(WireId wire_id, WireModel* removed_wire);

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

    std::unordered_map<ComponentId, ComponentInstance> components_;
    std::unordered_map<WireId, WireModel> wires_;
    ComponentId next_component_id_{1};
    WireId next_wire_id_{1};
    std::uint64_t revision_{0};
};

}  // namespace cse::model
