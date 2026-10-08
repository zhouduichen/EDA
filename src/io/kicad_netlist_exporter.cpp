#include "src/io/kicad_netlist_exporter.hpp"

#include <ctime>
#include <fstream>
#include <unordered_map>

#include "src/model/net_builder.hpp"

namespace cse::io {

    namespace {

        std::string Escape(const std::string& text) {
            std::string result;
            result.reserve(text.size());
            for (char character : text) {
                if (character == '"' || character == '\\') {
                    result.push_back('\\');
                }
                result.push_back(character);
            }
            return result;
        }

        const char* PinType(PinDirection direction) {
            switch (direction) {
                case PinDirection::kInput:
                    return "input";
                case PinDirection::kOutput:
                    return "output";
                case PinDirection::kBidirectional:
                    return "bidirectional";
            }
            return "passive";
        }

        std::string CurrentDate() {
            std::time_t now = std::time(nullptr);
            std::tm local_time{};
#ifdef _WIN32
            localtime_s(&local_time, &now);
#else
            localtime_r(&now, &local_time);
#endif
            char buffer[32] = {};
            std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &local_time);
            return buffer;
        }

    }  // namespace

    Result<void> ExportKiCadNetlist(const SchematicSnapshot& snapshot,
                                    const IComponentLibrary& library,
                                    const std::string& path) {
        const cse::model::BuiltNetlist netlist = cse::model::BuildNets(snapshot, library);

        std::unordered_map<ComponentId, ComponentInstance> components;
        for (const ComponentInstance& component : snapshot.components) {
            components.emplace(component.id, component);
        }

        std::ofstream stream(path, std::ios::binary);
        if (!stream.is_open()) {
            return Result<void>::Failure(ErrorCode::kFileOpenFailed,
                "cannot open for writing: " + path);
        }

        stream << "(export (version \"E\")\n";
        stream << "\t(design\n";
        stream << "\t\t(source \"" << Escape(path) << "\")\n";
        stream << "\t\t(date \"" << CurrentDate() << "\")\n";
        stream << "\t\t(tool \"CircuitSchematicEditor\")\n";
        stream << "\t)\n";

        stream << "\t(components\n";
        for (const ComponentInstance& component : snapshot.components) {
            const std::optional<ComponentDefinition> definition =
                library.FindByTypeId(component.type_id);
            const std::string value =
                definition.has_value() ? definition->display_name : component.type_id;
            stream << "\t\t(comp (ref \"" << Escape(component.reference) << "\")\n";
            stream << "\t\t\t(value \"" << Escape(value) << "\")\n";
            stream << "\t\t\t(footprint \"\")\n";
            stream << "\t\t\t(libsource (lib \"cse\") (part \"" << Escape(component.type_id)
                << "\") (description \"\"))\n";
            stream << "\t\t\t(sheetpath (names \"/\") (tstamps \"/\"))\n";
            stream << "\t\t)\n";
        }
        stream << "\t)\n";

        stream << "\t(nets\n";
        int code = 0;
        for (const cse::model::BuiltNet& net : netlist.nets) {
            ++code;
            stream << "\t\t(net (code \"" << code << "\") (name \"" << Escape(net.name)
                << "\")\n";
            for (const PinRef& pin : net.pins) {
                const auto component_iterator = components.find(pin.component_id);
                if (component_iterator == components.end()) {
                    // 引脚所属元件已经不存在了（理论上建网时已过滤），只写引用。
                    stream << "\t\t\t(node (ref \"?\") (pin \""
                        << pin.pin_id << "\"))\n";
                    continue;
                }
                const std::optional<ComponentDefinition> definition =
                    library.FindByTypeId(component_iterator->second.type_id);
                if (!definition.has_value()) {
                    stream << "\t\t\t(node (ref \""
                        << Escape(component_iterator->second.reference) << "\") (pin \""
                        << pin.pin_id << "\"))\n";
                    continue;
                }
                const auto pin_iterator = std::find_if(
                    definition->pins.begin(), definition->pins.end(),
                    [pin](const PinDefinition& definition_pin) {
                        return definition_pin.local_id == pin.pin_id;
                    });
                if (pin_iterator == definition->pins.end()) {
                    stream << "\t\t\t(node (ref \""
                        << Escape(component_iterator->second.reference) << "\") (pin \""
                        << pin.pin_id << "\"))\n";
                    continue;
                }
                stream << "\t\t\t(node (ref \"" << Escape(component_iterator->second.reference)
                    << "\") (pin \"" << pin.pin_id << "\") (pinfunction \""
                    << Escape(pin_iterator->name) << "\") (pintype \""
                    << PinType(pin_iterator->direction) << "\"))\n";
            }
            stream << "\t\t)\n";
        }
        stream << "\t)\n";
        stream << ")\n";

        if (!stream.good()) {
            return Result<void>::Failure(ErrorCode::kFileWriteFailed, "write failed: " + path);
        }
        return Result<void>::Success();
    }

}  // namespace cse::io
