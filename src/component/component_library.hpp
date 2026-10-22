#pragma once

#include <unordered_map>
#include <memory>

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
    std::shared_ptr<const CompositeModule> FindComposite(
        const std::string& type_id) const override;
    Result<void> RegisterComposite(std::shared_ptr<CompositeModule> module) override;

private:
    void RegisterBuiltIns();

    std::unordered_map<std::string, ComponentDefinition> definitions_;
    std::unordered_map<std::string, std::shared_ptr<CompositeModule>> composites_;
};

}  // namespace cse::component
