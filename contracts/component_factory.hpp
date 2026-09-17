#pragma once

#include <string>

#include "contracts/component_model.hpp"
#include "contracts/result.hpp"

namespace cse {

class IComponentFactory {
public:
    virtual ~IComponentFactory() = default;

    virtual Result<ComponentInstance> Create(
        const std::string& type_id,
        Point2D position) = 0;
};

}  // namespace cse
