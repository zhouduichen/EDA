#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "contracts/component_factory.hpp"
#include "contracts/component_library.hpp"

namespace cse::component {

class ComponentFactory final : public IComponentFactory {
public:
    explicit ComponentFactory(const IComponentLibrary& library);

    Result<ComponentInstance> Create(const std::string& type_id,
                                      Point2D position) override;
    void SyncWithSnapshot(const SchematicSnapshot& snapshot) override;

private:
    std::string ReferencePrefix(const ComponentDefinition& definition) const;
    static bool ParseReferenceNumber(const std::string& reference, const std::string& prefix,
                                     std::uint64_t* number);

    const IComponentLibrary& library_;
    ComponentId next_component_id_{1};
    std::unordered_map<std::string, std::uint64_t> reference_counters_;
    std::unordered_set<std::string> used_references_;
};

}  // namespace cse::component
