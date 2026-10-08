#include "src/io/project_serializer.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

namespace cse::io {

    namespace {

        constexpr const char* kMagic = "CSEPROJ";
        constexpr int kFormatVersion = 1;

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

    }  // namespace

    Result<void> SaveProject(const SchematicSnapshot& snapshot, const std::string& path) {
        std::ofstream stream(path, std::ios::binary);
        if (!stream.is_open()) {
            return Result<void>::Failure(ErrorCode::kFileOpenFailed,
                "cannot open for writing: " + path);
        }

        stream << kMagic << ' ' << kFormatVersion << '\n';

        for (const ComponentInstance& component : snapshot.components) {
            stream << "COMP " << component.id << ' ' << component.type_id << ' '
                << component.reference << ' ' << FormatDouble(component.position.x) << ' '
                << FormatDouble(component.position.y) << ' '
                << FormatDouble(component.rotation_deg);
            for (const auto& [key, value] : SortedProperties(component.properties)) {
                stream << ' ' << key << '=' << value;
            }
            stream << '\n';
        }

        for (const WireModel& wire : snapshot.wires) {
            stream << "WIRE " << wire.id << ' ' << FormatEndpoint(wire.from) << ' '
                << FormatEndpoint(wire.to) << '\n';
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
            if (version != kFormatVersion) {
                return Result<SchematicSnapshot>::Failure(ErrorCode::kUnsupportedFileVersion,
                    "unsupported version: " + std::to_string(version));
            }
        }

        SchematicSnapshot snapshot;
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
            if (tag == "END") {
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
                std::string token;
                while (row >> token) {
                    const std::size_t equals = token.find('=');
                    if (equals == std::string::npos || equals == 0) {
                        return Result<SchematicSnapshot>::Failure(ErrorCode::kInvalidFileFormat,
                            "bad property at line " + std::to_string(line_number));
                    }
                    component.properties[token.substr(0, equals)] = token.substr(equals + 1);
                }
                snapshot.components.push_back(std::move(component));
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
                snapshot.wires.push_back(std::move(wire));
            }
            else {
                return Result<SchematicSnapshot>::Failure(ErrorCode::kInvalidFileFormat,
                    "unknown record at line " + std::to_string(line_number));
            }
        }
        return Result<SchematicSnapshot>::Success(std::move(snapshot));
    }

}  // namespace cse::io