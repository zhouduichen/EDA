#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "contracts/geometry.hpp"
#include "contracts/ids.hpp"
#include "contracts/logic_value.hpp"

namespace cse {

struct PinDefinition {
    PinId local_id{0};
    std::string name;
    PinDirection direction{PinDirection::kBidirectional};
    Point2D relative_position;
};

struct ComponentDefinition {
    std::string type_id;
    std::string display_name;
    std::string category;
    std::vector<PinDefinition> pins;
    Rect2D default_bounds;
};

struct ComponentInstance {
    ComponentId id{kInvalidComponentId};
    std::string type_id;
    std::string reference;
    Point2D position;
    double rotation_deg{0.0};
    std::unordered_map<std::string, std::string> properties;
};

}  // namespace cse
