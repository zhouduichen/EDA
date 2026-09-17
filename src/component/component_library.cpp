#include "src/component/component_library.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <unordered_set>

namespace cse::component {

namespace {

bool IsLowercaseTypeId(const std::string& type_id) {
    const std::size_t separator = type_id.find('.');
    if (separator == std::string::npos || separator == 0 || separator + 1 >= type_id.size() ||
        type_id.find('.', separator + 1) != std::string::npos) {
        return false;
    }

    for (char character : type_id) {
        if (character == '.') {
            continue;
        }
        if (std::islower(static_cast<unsigned char>(character)) == 0 &&
            std::isdigit(static_cast<unsigned char>(character)) == 0 && character != '_') {
            return false;
        }
    }
    return true;
}

PinDefinition Pin(PinId id, const char* name, PinDirection direction, double x, double y) {
    PinDefinition definition;
    definition.local_id = id;
    definition.name = name;
    definition.direction = direction;
    definition.relative_position = {x, y};
    return definition;
}

ComponentDefinition Gate(const char* type_id, const char* display_name,
                         std::vector<PinDefinition> pins) {
    ComponentDefinition definition;
    definition.type_id = type_id;
    definition.display_name = display_name;
    definition.category = "logic";
    definition.pins = std::move(pins);
    definition.default_bounds = {0.0, 0.0, 70.0, 50.0};
    return definition;
}

}  // namespace

ComponentLibrary::ComponentLibrary() {
    RegisterBuiltIns();
}

Result<void> ComponentLibrary::Register(ComponentDefinition definition) {
    if (!IsLowercaseTypeId(definition.type_id) || definition.display_name.empty() ||
        definition.category.empty() || !std::isfinite(definition.default_bounds.width) ||
        !std::isfinite(definition.default_bounds.height) || definition.default_bounds.width <= 0.0 ||
        definition.default_bounds.height <= 0.0) {
        return Result<void>::Failure(ErrorCode::kInvalidComponent,
                                     "component definition has invalid metadata");
    }
    if (definitions_.count(definition.type_id) != 0) {
        return Result<void>::Failure(ErrorCode::kAlreadyExists,
                                     "component type_id is already registered");
    }

    std::unordered_set<PinId> pin_ids;
    for (const PinDefinition& pin : definition.pins) {
        if (pin.local_id == 0 || pin.name.empty() || !pin_ids.insert(pin.local_id).second ||
            !std::isfinite(pin.relative_position.x) ||
            !std::isfinite(pin.relative_position.y)) {
            return Result<void>::Failure(ErrorCode::kInvalidComponent,
                                         "component definition has invalid pins");
        }
    }

    definitions_.emplace(definition.type_id, std::move(definition));
    return Result<void>::Success();
}

std::vector<ComponentDefinition> ComponentLibrary::GetAll() const {
    std::vector<ComponentDefinition> definitions;
    definitions.reserve(definitions_.size());
    for (const auto& [type_id, definition] : definitions_) {
        static_cast<void>(type_id);
        definitions.push_back(definition);
    }
    std::sort(definitions.begin(), definitions.end(), [](const ComponentDefinition& first,
                                                         const ComponentDefinition& second) {
        if (first.category != second.category) {
            return first.category < second.category;
        }
        return first.type_id < second.type_id;
    });
    return definitions;
}

std::optional<ComponentDefinition> ComponentLibrary::FindByTypeId(
    const std::string& type_id) const {
    const auto iterator = definitions_.find(type_id);
    if (iterator == definitions_.end()) {
        return std::nullopt;
    }
    return iterator->second;
}

void ComponentLibrary::RegisterBuiltIns() {
    const std::vector<ComponentDefinition> built_ins = {
        Gate("logic.and", "AND", {
                  Pin(1, "A", PinDirection::kInput, 0.0, 15.0),
                  Pin(2, "B", PinDirection::kInput, 0.0, 35.0),
                  Pin(3, "Y", PinDirection::kOutput, 70.0, 25.0),
              }),
        Gate("logic.or", "OR", {
                  Pin(1, "A", PinDirection::kInput, 0.0, 15.0),
                  Pin(2, "B", PinDirection::kInput, 0.0, 35.0),
                  Pin(3, "Y", PinDirection::kOutput, 70.0, 25.0),
              }),
        Gate("logic.xor", "XOR", {
                  Pin(1, "A", PinDirection::kInput, 0.0, 15.0),
                  Pin(2, "B", PinDirection::kInput, 0.0, 35.0),
                  Pin(3, "Y", PinDirection::kOutput, 70.0, 25.0),
              }),
        [&] {
            ComponentDefinition definition = Gate("logic.not", "NOT", {
                Pin(1, "A", PinDirection::kInput, 0.0, 25.0),
                Pin(2, "Y", PinDirection::kOutput, 70.0, 25.0),
            });
            definition.default_bounds.height = 50.0;
            return definition;
        }(),
        [&] {
            ComponentDefinition definition;
            definition.type_id = "io.input";
            definition.display_name = "Input";
            definition.category = "io";
            definition.default_bounds = {0.0, 0.0, 50.0, 30.0};
            definition.pins = {Pin(1, "OUT", PinDirection::kOutput, 50.0, 15.0)};
            return definition;
        }(),
        [&] {
            ComponentDefinition definition;
            definition.type_id = "io.output";
            definition.display_name = "Output";
            definition.category = "io";
            definition.default_bounds = {0.0, 0.0, 50.0, 30.0};
            definition.pins = {Pin(1, "IN", PinDirection::kInput, 0.0, 15.0)};
            return definition;
        }(),
    };

    for (ComponentDefinition definition : built_ins) {
        static_cast<void>(Register(std::move(definition)));
    }
}

}  // namespace cse::component
