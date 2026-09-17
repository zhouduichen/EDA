#include "src/component/component_factory.hpp"

#include <utility>

namespace cse::component {

ComponentFactory::ComponentFactory(const IComponentLibrary& library) : library_(library) {}

Result<ComponentInstance> ComponentFactory::Create(const std::string& type_id,
                                                    Point2D position) {
    const std::optional<ComponentDefinition> definition = library_.FindByTypeId(type_id);
    if (!definition.has_value()) {
        return Result<ComponentInstance>::Failure(ErrorCode::kNotFound,
                                                  "component type_id is not registered");
    }

    const std::string prefix = ReferencePrefix(*definition);
    std::uint64_t& reference_counter = reference_counters_[prefix];
    ++reference_counter;

    ComponentInstance instance;
    instance.id = next_component_id_++;
    instance.type_id = type_id;
    instance.reference = prefix + std::to_string(reference_counter);
    instance.position = position;
    instance.rotation_deg = 0.0;

    if (type_id == "logic.not") {
        instance.properties["input_count"] = "1";
    } else if (type_id == "logic.and" || type_id == "logic.or" || type_id == "logic.xor") {
        instance.properties["input_count"] = "2";
    }
    if (type_id == "io.input" || type_id == "io.output") {
        instance.properties["label"] = definition->display_name;
    }

    return Result<ComponentInstance>::Success(std::move(instance));
}

std::string ComponentFactory::ReferencePrefix(const ComponentDefinition& definition) const {
    if (definition.type_id == "io.input") {
        return "IN";
    }
    if (definition.type_id == "io.output") {
        return "OUT";
    }
    if (definition.category == "logic") {
        return "U";
    }
    return "X";
}

}  // namespace cse::component
