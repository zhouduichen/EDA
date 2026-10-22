#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "contracts/component_model.hpp"
#include "contracts/wire_model.hpp"

namespace cse {

struct CompositeModule;

struct SchematicSnapshot {
    std::uint64_t revision{0};
    std::vector<ComponentInstance> components;
    std::vector<WireModel> wires;
    std::vector<std::shared_ptr<CompositeModule>> composite_modules;
};

struct CompositePortMapping {
    PinId external_pin_id{0};
    ComponentId internal_component_id{kInvalidComponentId};
    PinId internal_pin_id{0};
};

struct CompositeModule {
    ComponentDefinition definition;
    SchematicSnapshot implementation;
    std::vector<CompositePortMapping> inputs;
    std::vector<CompositePortMapping> outputs;
};

}  // namespace cse
