#pragma once

#include <optional>
#include <string>
#include <vector>

#include "contracts/component_model.hpp"

namespace cse {

class IComponentLibrary {
public:
    virtual ~IComponentLibrary() = default;

    virtual std::vector<ComponentDefinition> GetAll() const = 0;
    virtual std::optional<ComponentDefinition> FindByTypeId(
        const std::string& type_id) const = 0;
};

}  // namespace cse
