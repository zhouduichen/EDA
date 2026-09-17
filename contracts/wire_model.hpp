#pragma once

#include <optional>
#include <vector>

#include "contracts/geometry.hpp"
#include "contracts/ids.hpp"

namespace cse {

struct PinRef {
    ComponentId component_id{kInvalidComponentId};
    PinId pin_id{0};
};

struct WireEndpoint {
    std::optional<PinRef> pin;
    Point2D position;
};

struct WireModel {
    WireId id{kInvalidWireId};
    WireEndpoint from;
    WireEndpoint to;
    std::vector<Point2D> route_points;
};

}  // namespace cse
