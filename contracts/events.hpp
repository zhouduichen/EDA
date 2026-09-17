#pragma once

#include <cstdint>
#include <vector>

#include "contracts/ids.hpp"

namespace cse {

struct ComponentAddedEvent {
    ComponentId component_id{kInvalidComponentId};
};

struct ComponentRemovedEvent {
    ComponentId component_id{kInvalidComponentId};
};

struct WireAddedEvent {
    WireId wire_id{kInvalidWireId};
};

struct WireRemovedEvent {
    WireId wire_id{kInvalidWireId};
};

struct SelectionChangedEvent {
    std::vector<ComponentId> selected_components;
    std::vector<WireId> selected_wires;
};

struct ModelChangedEvent {
    std::uint64_t revision{0};
};

}  // namespace cse
