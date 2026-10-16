#include "src/component/component_factory.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
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
    std::string reference;
    do {
        ++reference_counter;
        reference = prefix + std::to_string(reference_counter);
    } while (used_references_.count(reference) != 0);
    used_references_.insert(reference);

    ComponentInstance instance;
    instance.id = next_component_id_++;
    instance.type_id = type_id;
    instance.reference = std::move(reference);
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

void ComponentFactory::SyncWithSnapshot(const SchematicSnapshot& snapshot) {
    next_component_id_ = 1;
    reference_counters_.clear();
    used_references_.clear();

    for (const ComponentInstance& component : snapshot.components) {
        if (component.id != kInvalidComponentId &&
            component.id < std::numeric_limits<ComponentId>::max() &&
            component.id >= next_component_id_) {
            next_component_id_ = component.id + 1;
        }

        used_references_.insert(component.reference);
        const std::optional<ComponentDefinition> definition =
            library_.FindByTypeId(component.type_id);
        const std::string prefix = definition.has_value() ? ReferencePrefix(*definition) : "X";
        std::uint64_t number = 0;
        if (ParseReferenceNumber(component.reference, prefix, &number)) {
            std::uint64_t& counter = reference_counters_[prefix];
            counter = std::max(counter, number);
        }
    }
}

bool ComponentFactory::ParseReferenceNumber(const std::string& reference,
                                            const std::string& prefix,
                                            std::uint64_t* number) {
    if (number == nullptr || reference.size() <= prefix.size() ||
        reference.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }

    std::uint64_t parsed = 0;
    for (std::size_t index = prefix.size(); index < reference.size(); ++index) {
        const unsigned char character = static_cast<unsigned char>(reference[index]);
        if (std::isdigit(character) == 0) {
            return false;
        }
        const std::uint64_t digit = static_cast<std::uint64_t>(character - '0');
        if (parsed > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
            return false;
        }
        parsed = parsed * 10 + digit;
    }
    if (parsed == 0) {
        return false;
    }
    *number = parsed;
    return true;
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
