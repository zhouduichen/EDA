#pragma once

#include <optional>
#include <memory>
#include <string>
#include <vector>

#include "contracts/component_model.hpp"
#include "contracts/result.hpp"
#include "contracts/schematic_snapshot.hpp"

namespace cse {

class IComponentLibrary {
public:
    virtual ~IComponentLibrary() = default;

    virtual std::vector<ComponentDefinition> GetAll() const = 0;
    virtual std::optional<ComponentDefinition> FindByTypeId(
        const std::string& type_id) const = 0;

    virtual std::shared_ptr<const CompositeModule> FindComposite(
        const std::string& type_id) const {
        static_cast<void>(type_id);
        return nullptr;
    }

    virtual Result<void> RegisterComposite(std::shared_ptr<CompositeModule> module) {
        static_cast<void>(module);
        return Result<void>::Failure(ErrorCode::kInvalidComponent,
                                     "composite components are not supported by this library");
    }
};

}  // namespace cse
