#include "src/io/project_serializer.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cse::io {

    namespace {

        constexpr const char* kMagic = "CSEPROJ";
        // v2 增加导线拐点，v3 增加自定义组合元件定义，v4 增加文本标注。
        constexpr int kFormatVersion = 4;
        constexpr int kMinSupportedVersion = 1;
        // 畸形文件里 ROUTE 数量可能是任意大数，先拦一道，避免按声明开数组。
        constexpr std::size_t kMaxRoutePoints = 10000;

        int VersionFor(const SchematicSnapshot& snapshot) {
            if (!snapshot.composite_modules.empty() || !snapshot.text_labels.empty()) {
                return 4;
            }
            for (const WireModel& wire : snapshot.wires) {
                if (!wire.route_points.empty()) {
                    return 2;
                }
            }
            return 1;
        }

        std::string FormatDouble(double value) {
            std::ostringstream stream;
            stream << std::fixed << std::setprecision(6) << value;
            return stream.str();
        }

        // 把一个导线端点写成 5 个字段：<comp_id> <pin_id> <x> <y>
        // comp_id 为 0 表示这个端点没有连到引脚。
        std::string FormatEndpoint(const WireEndpoint& endpoint) {
            std::ostringstream stream;
            if (endpoint.pin.has_value()) {
                stream << endpoint.pin->component_id << ' ' << endpoint.pin->pin_id;
            }
            else {
                stream << kInvalidComponentId << ' ' << 0;
            }
            stream << ' ' << FormatDouble(endpoint.position.x) << ' '
                << FormatDouble(endpoint.position.y);
            return stream.str();
        }

        // properties 是 unordered_map，遍历顺序不固定。
        // 必须排序后再写，否则同一个工程每次保存出来的文件都不一样。
        std::vector<std::pair<std::string, std::string>> SortedProperties(
            const std::unordered_map<std::string, std::string>& properties) {
            std::vector<std::pair<std::string, std::string>> entries(properties.begin(),
                properties.end());
            std::sort(entries.begin(), entries.end());
            return entries;
        }

        bool NeedsQuotedValue(const std::string& value) {
            if (value.empty()) {
                return true;
            }
            return std::any_of(value.begin(), value.end(), [](char character) {
                return std::isspace(static_cast<unsigned char>(character)) != 0 ||
                       character == '"' || character == '\\';
            });
        }

        std::string FormatPropertyValue(const std::string& value) {
            if (!NeedsQuotedValue(value)) {
                return value;
            }

            std::string escaped = "\"";
            for (char character : value) {
                switch (character) {
                    case '\\':
                        escaped += "\\\\";
                        break;
                    case '"':
                        escaped += "\\\"";
                        break;
                    case '\n':
                        escaped += "\\n";
                        break;
                    case '\r':
                        escaped += "\\r";
                        break;
                    case '\t':
                        escaped += "\\t";
                        break;
                    default:
                        escaped.push_back(character);
                        break;
                }
            }
            escaped.push_back('"');
            return escaped;
        }

        std::vector<std::string> TokenizeFields(const std::string& text, bool* valid) {
            std::vector<std::string> fields;
            *valid = true;
            std::size_t index = 0;
            while (index < text.size()) {
                while (index < text.size() &&
                       std::isspace(static_cast<unsigned char>(text[index])) != 0) {
                    ++index;
                }
                if (index == text.size()) {
                    break;
                }

                std::string field;
                while (index < text.size() &&
                       std::isspace(static_cast<unsigned char>(text[index])) == 0) {
                    if (text[index] != '"') {
                        field.push_back(text[index++]);
                        continue;
                    }

                    ++index;
                    bool closed = false;
                    while (index < text.size()) {
                        const char character = text[index++];
                        if (character == '"') {
                            closed = true;
                            break;
                        }
                        if (character != '\\') {
                            field.push_back(character);
                            continue;
                        }
                        if (index == text.size()) {
                            *valid = false;
                            return {};
                        }
                        const char escaped = text[index++];
                        switch (escaped) {
                            case 'n':
                                field.push_back('\n');
                                break;
                            case 'r':
                                field.push_back('\r');
                                break;
                            case 't':
                                field.push_back('\t');
                                break;
                            case '\\':
                                field.push_back('\\');
                                break;
                            case '"':
                                field.push_back('"');
                                break;
                            default:
                                field.push_back(escaped);
                                break;
                        }
                    }
                    if (!closed) {
                        *valid = false;
                        return {};
                    }
                }
                fields.push_back(std::move(field));
            }
            return fields;
        }

        Result<void> ValidateSnapshot(const SchematicSnapshot& snapshot) {
            std::unordered_set<ComponentId> component_ids;
            for (const ComponentInstance& component : snapshot.components) {
                if (component.id == kInvalidComponentId || component.type_id.empty() ||
                    component.reference.empty() || !std::isfinite(component.position.x) ||
                    !std::isfinite(component.position.y) ||
                    !std::isfinite(component.rotation_deg) ||
                    !component_ids.insert(component.id).second) {
                    return Result<void>::Failure(ErrorCode::kInvalidFileFormat,
                                                 "invalid or duplicate component");
                }
            }

            std::unordered_set<WireId> wire_ids;
            for (const WireModel& wire : snapshot.wires) {
                if (wire.id == kInvalidWireId || !wire_ids.insert(wire.id).second ||
                    !std::isfinite(wire.from.position.x) ||
                    !std::isfinite(wire.from.position.y) ||
                    !std::isfinite(wire.to.position.x) ||
                    !std::isfinite(wire.to.position.y) ||
                    (wire.from.position.x == wire.to.position.x &&
                     wire.from.position.y == wire.to.position.y)) {
                    return Result<void>::Failure(ErrorCode::kInvalidFileFormat,
                                                 "invalid or duplicate wire");
                }
                for (const Point2D& point : wire.route_points) {
                    if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
                        return Result<void>::Failure(ErrorCode::kInvalidFileFormat,
                                                     "invalid wire route point");
                    }
                }
            }
            std::unordered_set<TextLabelId> text_label_ids;
            for (const TextLabel& label : snapshot.text_labels) {
                if (label.id == kInvalidTextLabelId || !text_label_ids.insert(label.id).second ||
                    label.text.empty() || !std::isfinite(label.position.x) ||
                    !std::isfinite(label.position.y)) {
                    return Result<void>::Failure(ErrorCode::kInvalidFileFormat,
                                                 "invalid or duplicate text label");
                }
            }
            std::unordered_set<std::string> module_ids;
            for (const auto& module : snapshot.composite_modules) {
                if (module == nullptr || module->definition.type_id.empty() ||
                    module->definition.type_id.rfind("custom.", 0) != 0 ||
                    module->definition.display_name.empty() ||
                    module->definition.category != "custom" ||
                    !std::isfinite(module->definition.default_bounds.width) ||
                    !std::isfinite(module->definition.default_bounds.height) ||
                    module->definition.default_bounds.width <= 0.0 ||
                    module->definition.default_bounds.height <= 0.0 ||
                    !module_ids.insert(module->definition.type_id).second ||
                    module->inputs.empty() || module->outputs.empty() ||
                    module->implementation.components.empty()) {
                    return Result<void>::Failure(ErrorCode::kInvalidFileFormat,
                                                 "invalid or duplicate composite module");
                }
                const Result<void> inner_validation = ValidateSnapshot(module->implementation);
                if (!inner_validation.ok || !module->implementation.composite_modules.empty()) {
                    return Result<void>::Failure(ErrorCode::kInvalidFileFormat,
                                                 "invalid nested composite implementation");
                }
                std::unordered_set<PinId> module_pin_ids;
                for (const PinDefinition& pin : module->definition.pins) {
                    if (pin.local_id == 0 || pin.name.empty() ||
                        !module_pin_ids.insert(pin.local_id).second ||
                        (pin.direction != PinDirection::kInput &&
                         pin.direction != PinDirection::kOutput) ||
                        !std::isfinite(pin.relative_position.x) ||
                        !std::isfinite(pin.relative_position.y)) {
                        return Result<void>::Failure(ErrorCode::kInvalidFileFormat,
                                                     "invalid composite pin");
                    }
                }
                std::unordered_set<PinId> mapped_pins;
                std::unordered_set<ComponentId> inner_component_ids;
                for (const ComponentInstance& component : module->implementation.components) {
                    if (component.type_id.rfind("custom.", 0) == 0) {
                        return Result<void>::Failure(ErrorCode::kInvalidFileFormat,
                                                     "nested composites are unsupported");
                    }
                    inner_component_ids.insert(component.id);
                }
                const auto validate_mappings = [&](const std::vector<CompositePortMapping>& mappings,
                                                   PinDirection direction) {
                    for (const CompositePortMapping& mapping : mappings) {
                        const auto external_pin = std::find_if(
                            module->definition.pins.begin(), module->definition.pins.end(),
                            [&mapping](const PinDefinition& pin) {
                                return pin.local_id == mapping.external_pin_id;
                            });
                        if (external_pin == module->definition.pins.end() ||
                            external_pin->direction != direction ||
                            !mapped_pins.insert(mapping.external_pin_id).second ||
                            inner_component_ids.count(mapping.internal_component_id) == 0 ||
                            mapping.internal_pin_id == 0) {
                            return false;
                        }
                    }
                    return true;
                };
                if (!validate_mappings(module->inputs, PinDirection::kInput) ||
                    !validate_mappings(module->outputs, PinDirection::kOutput) ||
                    mapped_pins.size() != module->definition.pins.size()) {
                    return Result<void>::Failure(ErrorCode::kInvalidFileFormat,
                                                 "invalid composite port mapping");
                }
            }
            return Result<void>::Success();
        }

    }  // namespace

    Result<void> SaveProject(const SchematicSnapshot& snapshot, const std::string& path) {
        const Result<void> validation = ValidateSnapshot(snapshot);
        if (!validation.ok) {
            return validation;
        }
        std::ofstream stream(path, std::ios::binary);
        if (!stream.is_open()) {
            return Result<void>::Failure(ErrorCode::kFileOpenFailed,
                "cannot open for writing: " + path);
        }

        stream << kMagic << ' ' << VersionFor(snapshot) << '\n';

        auto write_component = [&stream](const ComponentInstance& component) {
            stream << "COMP " << component.id << ' ' << component.type_id << ' '
                   << component.reference << ' ' << FormatDouble(component.position.x) << ' '
                   << FormatDouble(component.position.y) << ' '
                   << FormatDouble(component.rotation_deg);
            for (const auto& [key, value] : SortedProperties(component.properties)) {
                stream << ' ' << key << '=' << FormatPropertyValue(value);
            }
            stream << '\n';
        };
        auto write_wire = [&stream](const WireModel& wire) {
            stream << "WIRE " << wire.id << ' ' << FormatEndpoint(wire.from) << ' '
                   << FormatEndpoint(wire.to);
            if (!wire.route_points.empty()) {
                stream << " ROUTE " << wire.route_points.size();
                for (const Point2D& point : wire.route_points) {
                    stream << ' ' << FormatDouble(point.x) << ' ' << FormatDouble(point.y);
                }
            }
            stream << '\n';
        };

        for (const auto& module : snapshot.composite_modules) {
            if (module == nullptr) {
                continue;
            }
            const ComponentDefinition& definition = module->definition;
            stream << "MODULE " << std::quoted(definition.type_id) << ' '
                   << std::quoted(definition.display_name) << ' '
                   << FormatDouble(definition.default_bounds.width) << ' '
                   << FormatDouble(definition.default_bounds.height) << '\n';
            for (const PinDefinition& pin : definition.pins) {
                stream << "MPIN " << pin.local_id << ' ' << static_cast<int>(pin.direction)
                       << ' ' << std::quoted(pin.name) << ' '
                       << FormatDouble(pin.relative_position.x) << ' '
                       << FormatDouble(pin.relative_position.y) << '\n';
            }
            for (const CompositePortMapping& mapping : module->inputs) {
                stream << "INMAP " << mapping.external_pin_id << ' '
                       << mapping.internal_component_id << ' ' << mapping.internal_pin_id << '\n';
            }
            for (const CompositePortMapping& mapping : module->outputs) {
                stream << "OUTMAP " << mapping.external_pin_id << ' '
                       << mapping.internal_component_id << ' ' << mapping.internal_pin_id << '\n';
            }
            for (const ComponentInstance& component : module->implementation.components) {
                write_component(component);
            }
            for (const WireModel& wire : module->implementation.wires) {
                write_wire(wire);
            }
            stream << "ENDMODULE\n";
        }

        for (const ComponentInstance& component : snapshot.components) {
            write_component(component);
        }

        for (const WireModel& wire : snapshot.wires) {
            write_wire(wire);
        }

        // 文本内容走 std::quoted：空格/引号都安全；换行已在录入时拍平成空格。
        for (const TextLabel& label : snapshot.text_labels) {
            stream << "TEXT " << label.id << ' ' << FormatDouble(label.position.x) << ' '
                   << FormatDouble(label.position.y) << ' ' << std::quoted(label.text) << '\n';
        }

        stream << "END\n";

        if (!stream.good()) {
            return Result<void>::Failure(ErrorCode::kFileWriteFailed, "write failed: " + path);
        }
        return Result<void>::Success();
    }

    Result<SchematicSnapshot> LoadProject(const std::string& path) {
        std::ifstream stream(path, std::ios::binary);
        if (!stream.is_open()) {
            return Result<SchematicSnapshot>::Failure(ErrorCode::kFileOpenFailed,
                "cannot open for reading: " + path);
        }

        std::string line;
        if (!std::getline(stream, line)) {
            return Result<SchematicSnapshot>::Failure(ErrorCode::kInvalidFileFormat,
                "missing header: " + path);
        }
        {
            std::istringstream header(line);
            std::string magic;
            int version = 0;
            if (!(header >> magic >> version) || magic != kMagic) {
                return Result<SchematicSnapshot>::Failure(ErrorCode::kInvalidFileFormat,
                    "bad header: " + line);
            }
            if (version < kMinSupportedVersion || version > kFormatVersion) {
                return Result<SchematicSnapshot>::Failure(ErrorCode::kUnsupportedFileVersion,
                    "unsupported version: " + std::to_string(version));
            }
        }

        SchematicSnapshot snapshot;
        std::shared_ptr<CompositeModule> current_module;
        bool found_end = false;
        int line_number = 1;
        while (std::getline(stream, line)) {
            ++line_number;
            if (line.empty()) {
                continue;
            }
            std::istringstream row(line);
            std::string tag;
            if (!(row >> tag)) {
                continue;
            }
            if (tag == "MODULE") {
                if (current_module != nullptr) {
                    return Result<SchematicSnapshot>::Failure(ErrorCode::kInvalidFileFormat,
                        "nested MODULE at line " + std::to_string(line_number));
                }
                current_module = std::make_shared<CompositeModule>();
                current_module->definition.category = "custom";
                if (!(row >> std::quoted(current_module->definition.type_id)
                          >> std::quoted(current_module->definition.display_name)
                          >> current_module->definition.default_bounds.width
                          >> current_module->definition.default_bounds.height)) {
                    return Result<SchematicSnapshot>::Failure(ErrorCode::kInvalidFileFormat,
                        "bad MODULE record at line " + std::to_string(line_number));
                }
                continue;
            }
            if (tag == "ENDMODULE") {
                if (current_module == nullptr) {
                    return Result<SchematicSnapshot>::Failure(ErrorCode::kInvalidFileFormat,
                        "unexpected ENDMODULE at line " + std::to_string(line_number));
                }
                snapshot.composite_modules.push_back(current_module);
                current_module.reset();
                continue;
            }
            if (tag == "MPIN") {
                PinDefinition pin;
                int direction = -1;
                if (current_module == nullptr ||
                    !(row >> pin.local_id >> direction >> std::quoted(pin.name)
                          >> pin.relative_position.x >> pin.relative_position.y) ||
                    (direction != static_cast<int>(PinDirection::kInput) &&
                     direction != static_cast<int>(PinDirection::kOutput))) {
                    return Result<SchematicSnapshot>::Failure(ErrorCode::kInvalidFileFormat,
                        "bad MPIN record at line " + std::to_string(line_number));
                }
                pin.direction = static_cast<PinDirection>(direction);
                current_module->definition.pins.push_back(std::move(pin));
                continue;
            }
            if (tag == "INMAP" || tag == "OUTMAP") {
                CompositePortMapping mapping;
                if (current_module == nullptr ||
                    !(row >> mapping.external_pin_id >> mapping.internal_component_id
                          >> mapping.internal_pin_id) || mapping.external_pin_id == 0 ||
                    mapping.internal_component_id == kInvalidComponentId ||
                    mapping.internal_pin_id == 0) {
                    return Result<SchematicSnapshot>::Failure(ErrorCode::kInvalidFileFormat,
                        "bad port mapping at line " + std::to_string(line_number));
                }
                (tag == "INMAP" ? current_module->inputs : current_module->outputs)
                    .push_back(mapping);
                continue;
            }
            if (tag == "END") {
                if (current_module != nullptr) {
                    return Result<SchematicSnapshot>::Failure(ErrorCode::kInvalidFileFormat,
                        "missing ENDMODULE before END");
                }
                found_end = true;
                break;
            }
            if (tag == "COMP") {
                ComponentInstance component;
                if (!(row >> component.id >> component.type_id >> component.reference
                        >> component.position.x >> component.position.y
                        >> component.rotation_deg)) {
                    return Result<SchematicSnapshot>::Failure(ErrorCode::kInvalidFileFormat,
                        "bad COMP record at line " + std::to_string(line_number));
                }
                std::string property_text;
                std::getline(row, property_text);
                bool fields_valid = true;
                const std::vector<std::string> fields =
                    TokenizeFields(property_text, &fields_valid);
                if (!fields_valid) {
                    return Result<SchematicSnapshot>::Failure(ErrorCode::kInvalidFileFormat,
                        "bad quoted property at line " + std::to_string(line_number));
                }
                for (const std::string& token : fields) {
                    const std::size_t equals = token.find('=');
                    if (equals == std::string::npos || equals == 0) {
                        return Result<SchematicSnapshot>::Failure(ErrorCode::kInvalidFileFormat,
                            "bad property at line " + std::to_string(line_number));
                    }
                    component.properties[token.substr(0, equals)] = token.substr(equals + 1);
                }
                if (current_module != nullptr) {
                    current_module->implementation.components.push_back(std::move(component));
                } else {
                    snapshot.components.push_back(std::move(component));
                }
            }
            else if (tag == "TEXT") {
                TextLabel label;
                if (current_module != nullptr ||
                    !(row >> label.id >> label.position.x >> label.position.y) ||
                    label.id == kInvalidTextLabelId) {
                    return Result<SchematicSnapshot>::Failure(ErrorCode::kInvalidFileFormat,
                        "bad TEXT record at line " + std::to_string(line_number));
                }
                if (!(row >> std::quoted(label.text)) || label.text.empty()) {
                    return Result<SchematicSnapshot>::Failure(ErrorCode::kInvalidFileFormat,
                        "bad TEXT content at line " + std::to_string(line_number));
                }
                std::string extra;
                if (row >> extra) {
                    return Result<SchematicSnapshot>::Failure(ErrorCode::kInvalidFileFormat,
                        "extra TEXT field at line " + std::to_string(line_number));
                }
                snapshot.text_labels.push_back(std::move(label));
            }
            else if (tag == "WIRE") {
                WireModel wire;
                ComponentId from_component = kInvalidComponentId;
                ComponentId to_component = kInvalidComponentId;
                PinId from_pin = 0;
                PinId to_pin = 0;
                if (!(row >> wire.id >> from_component >> from_pin
                        >> wire.from.position.x >> wire.from.position.y
                        >> to_component >> to_pin
                        >> wire.to.position.x >> wire.to.position.y)) {
                    return Result<SchematicSnapshot>::Failure(ErrorCode::kInvalidFileFormat,
                        "bad WIRE record at line " + std::to_string(line_number));
                }
                if (from_component != kInvalidComponentId) {
                    wire.from.pin = PinRef{from_component, from_pin};
                }
                if (to_component != kInvalidComponentId) {
                    wire.to.pin = PinRef{to_component, to_pin};
                }
                // 可选尾段：ROUTE <数量> <x> <y> ...，画布上手工拐的弯存在这里。
                std::string trailing;
                if (row >> trailing) {
                    if (trailing != "ROUTE") {
                        return Result<SchematicSnapshot>::Failure(ErrorCode::kInvalidFileFormat,
                            "unknown WIRE field at line " + std::to_string(line_number));
                    }
                    std::size_t route_count = 0;
                    if (!(row >> route_count) || route_count > kMaxRoutePoints) {
                        return Result<SchematicSnapshot>::Failure(ErrorCode::kInvalidFileFormat,
                            "bad ROUTE count at line " + std::to_string(line_number));
                    }
                    wire.route_points.reserve(route_count);
                    for (std::size_t index = 0; index < route_count; ++index) {
                        Point2D point;
                        if (!(row >> point.x >> point.y)) {
                            return Result<SchematicSnapshot>::Failure(
                                ErrorCode::kInvalidFileFormat,
                                "bad ROUTE point at line " + std::to_string(line_number));
                        }
                        wire.route_points.push_back(point);
                    }
                    std::string extra;
                    if (row >> extra) {
                        return Result<SchematicSnapshot>::Failure(
                            ErrorCode::kInvalidFileFormat,
                            "extra WIRE field at line " + std::to_string(line_number));
                    }
                }
                if (current_module != nullptr) {
                    current_module->implementation.wires.push_back(std::move(wire));
                } else {
                    snapshot.wires.push_back(std::move(wire));
                }
            }
            else {
                return Result<SchematicSnapshot>::Failure(ErrorCode::kInvalidFileFormat,
                    "unknown record at line " + std::to_string(line_number));
            }
        }
        if (!found_end) {
            return Result<SchematicSnapshot>::Failure(ErrorCode::kInvalidFileFormat,
                "missing END record: " + path);
        }
        const Result<void> validation = ValidateSnapshot(snapshot);
        if (!validation.ok) {
            return Result<SchematicSnapshot>::Failure(validation.code, validation.message);
        }
        return Result<SchematicSnapshot>::Success(std::move(snapshot));
    }

}  // namespace cse::io
