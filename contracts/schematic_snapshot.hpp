#pragma once

#include <cstdint>
#include <vector>

#include "contracts/component_model.hpp"
#include "contracts/wire_model.hpp"

namespace cse {

struct SchematicSnapshot {
    std::uint64_t revision{0};
    std::vector<ComponentInstance> components;
    std::vector<WireModel> wires;
};

}  // namespace cse
