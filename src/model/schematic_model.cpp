#include "src/model/schematic_model.hpp"

#include <algorithm>
#include <cmath>

namespace cse::model {

namespace {

bool SamePoint(Point2D first, Point2D second) {
    constexpr double kEpsilon = 0.000001;
    return std::abs(first.x - second.x) < kEpsilon &&
           std::abs(first.y - second.y) < kEpsilon;
}

bool WireUsesComponent(const WireModel& wire, ComponentId component_id) {
    const bool from_matches = wire.from.pin.has_value() &&
                              wire.from.pin->component_id == component_id;
    const bool to_matches = wire.to.pin.has_value() &&
                            wire.to.pin->component_id == component_id;
    return from_matches || to_matches;
}

}  // namespace

bool SchematicModel::AddComponent(ComponentInstance component) {
    if (component.type_id.empty() || component.reference.empty()) {
        return false;
    }
    if (component.id == kInvalidComponentId) {
        component.id = next_component_id_;
    }

    if (components_.count(component.id) != 0) {
        return false;
    }

    UpdateNextComponentId(component.id);
    components_.emplace(component.id, std::move(component));
    BumpRevision();
    return true;
}

bool SchematicModel::RemoveComponentAndWires(ComponentId component_id,
                                              ComponentRemoval* removal) {
    const auto component_iterator = components_.find(component_id);
    if (component_iterator == components_.end() || removal == nullptr) {
        return false;
    }

    removal->component = component_iterator->second;
    removal->wires.clear();

    for (auto wire_iterator = wires_.begin(); wire_iterator != wires_.end();) {
        if (WireUsesComponent(wire_iterator->second, component_id)) {
            removal->wires.push_back(wire_iterator->second);
            wire_iterator = wires_.erase(wire_iterator);
        } else {
            ++wire_iterator;
        }
    }

    components_.erase(component_iterator);
    BumpRevision();
    return true;
}

bool SchematicModel::RestoreComponent(const ComponentRemoval& removal) {
    if (removal.component.id == kInvalidComponentId ||
        removal.component.type_id.empty() || removal.component.reference.empty() ||
        components_.count(removal.component.id) != 0) {
        return false;
    }

    for (const WireModel& wire : removal.wires) {
        if (wire.id == kInvalidWireId || wires_.count(wire.id) != 0 ||
            SamePoint(wire.from.position, wire.to.position)) {
            return false;
        }
    }

    components_.emplace(removal.component.id, removal.component);
    UpdateNextComponentId(removal.component.id);
    for (const WireModel& wire : removal.wires) {
        wires_.emplace(wire.id, wire);
        UpdateNextWireId(wire.id);
    }

    BumpRevision();
    return true;
}

bool SchematicModel::SetComponentPosition(ComponentId component_id, Point2D position) {
    const auto iterator = components_.find(component_id);
    if (iterator == components_.end()) {
        return false;
    }

    if (iterator->second.position.x == position.x && iterator->second.position.y == position.y) {
        return true;
    }

    iterator->second.position = position;
    BumpRevision();
    return true;
}

bool SchematicModel::SetComponentRotation(ComponentId component_id, double rotation_deg) {
    const auto iterator = components_.find(component_id);
    if (iterator == components_.end() || !std::isfinite(rotation_deg)) {
        return false;
    }

    if (iterator->second.rotation_deg == rotation_deg) {
        return true;
    }

    iterator->second.rotation_deg = rotation_deg;
    BumpRevision();
    return true;
}

bool SchematicModel::AddWire(WireModel& wire) {
    if (wire.id == kInvalidWireId) {
        wire.id = next_wire_id_;
    }
    if (wires_.count(wire.id) != 0 || SamePoint(wire.from.position, wire.to.position)) {
        return false;
    }

    UpdateNextWireId(wire.id);
    wires_.emplace(wire.id, std::move(wire));
    BumpRevision();
    return true;
}

bool SchematicModel::RemoveWire(WireId wire_id, WireModel* removed_wire) {
    const auto iterator = wires_.find(wire_id);
    if (iterator == wires_.end()) {
        return false;
    }

    if (removed_wire != nullptr) {
        *removed_wire = iterator->second;
    }
    wires_.erase(iterator);
    BumpRevision();
    return true;
}

bool SchematicModel::ContainsComponent(ComponentId component_id) const {
    return components_.count(component_id) != 0;
}

bool SchematicModel::ContainsWire(WireId wire_id) const {
    return wires_.count(wire_id) != 0;
}

std::optional<ComponentInstance> SchematicModel::GetComponent(ComponentId component_id) const {
    const auto iterator = components_.find(component_id);
    if (iterator == components_.end()) {
        return std::nullopt;
    }
    return iterator->second;
}

std::optional<WireModel> SchematicModel::GetWire(WireId wire_id) const {
    const auto iterator = wires_.find(wire_id);
    if (iterator == wires_.end()) {
        return std::nullopt;
    }
    return iterator->second;
}

std::vector<WireModel> SchematicModel::GetWiresForComponent(ComponentId component_id) const {
    std::vector<WireModel> result;
    for (const auto& [wire_id, wire] : wires_) {
        static_cast<void>(wire_id);
        if (WireUsesComponent(wire, component_id)) {
            result.push_back(wire);
        }
    }
    std::sort(result.begin(), result.end(), [](const WireModel& first, const WireModel& second) {
        return first.id < second.id;
    });
    return result;
}

SchematicSnapshot SchematicModel::GetSnapshot() const {
    SchematicSnapshot snapshot;
    snapshot.revision = revision_;
    snapshot.components.reserve(components_.size());
    snapshot.wires.reserve(wires_.size());

    for (const auto& [component_id, component] : components_) {
        static_cast<void>(component_id);
        snapshot.components.push_back(component);
    }
    for (const auto& [wire_id, wire] : wires_) {
        static_cast<void>(wire_id);
        snapshot.wires.push_back(wire);
    }

    std::sort(snapshot.components.begin(), snapshot.components.end(),
              [](const ComponentInstance& first, const ComponentInstance& second) {
                  return first.id < second.id;
              });
    std::sort(snapshot.wires.begin(), snapshot.wires.end(),
              [](const WireModel& first, const WireModel& second) {
                  return first.id < second.id;
              });
    return snapshot;
}

std::uint64_t SchematicModel::GetRevision() const {
    return revision_;
}

void SchematicModel::BumpRevision() {
    ++revision_;
}

void SchematicModel::UpdateNextComponentId(ComponentId component_id) {
    if (component_id >= next_component_id_) {
        next_component_id_ = component_id + 1;
    }
}

void SchematicModel::UpdateNextWireId(WireId wire_id) {
    if (wire_id >= next_wire_id_) {
        next_wire_id_ = wire_id + 1;
    }
}

}  // namespace cse::model
