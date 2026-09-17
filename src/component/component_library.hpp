#pragma once

#include <unordered_map>

#include "contracts/component_library.hpp"
#include "contracts/result.hpp"

namespace cse::component {

class ComponentLibrary final : public IComponentLibrary {
public:
    ComponentLibrary();

    Result<void> Register(ComponentDefinition definition);
    std::vector<ComponentDefinition> GetAll() const override;
    std::optional<ComponentDefinition> FindByTypeId(
        const std::string& type_id) const override;

private:
    void RegisterBuiltIns();

    std::unordered_map<std::string, ComponentDefinition> definitions_;
};

}  // namespace cse::component
