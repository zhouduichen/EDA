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
        // 当前写入版本。没有拐点的工程仍写成 v1，这样旧版本程序还能打开；
        // 出现拐点（ROUTE 段）才升到 v2。
        constexpr int kFormatVersion = 2;
        constexpr int kMinSupportedVersion = 1;
        // 畸形文件里 ROUTE 数量可能是任意大数，先拦一道，避免按声明开数组。
        constexpr std::size_t kMaxRoutePoints = 10000;

        int VersionFor(const SchematicSnapshot& snapshot) {
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
            return Result<void>::Success();
        }

    }  // namespace

    Result<void> SaveProject(const SchematicSnapshot& snapshot, const std::string& path) {
        std::ofstream stream(path, std::ios::binary);
        if (!stream.is_open()) {
            return Result<void>::Failure(ErrorCode::kFileOpenFailed,
                "cannot open for writing: " + path);
        }

        stream << kMagic << ' ' << VersionFor(snapshot) << '\n';

        for (const ComponentInstance& component : snapshot.components) {
            stream << "COMP " << component.id << ' ' << component.type_id << ' '
                << component.reference << ' ' << FormatDouble(component.position.x) << ' '
                << FormatDouble(component.position.y) << ' '
                << FormatDouble(component.rotation_deg);
            for (const auto& [key, value] : SortedProperties(component.properties)) {
                stream << ' ' << key << '=' << FormatPropertyValue(value);
            }
            stream << '\n';
        }

        for (const WireModel& wire : snapshot.wires) {
            stream << "WIRE " << wire.id << ' ' << FormatEndpoint(wire.from) << ' '
                << FormatEndpoint(wire.to);
            if (!wire.route_points.empty()) {
                stream << " ROUTE " << wire.route_points.size();
                for (const Point2D& point : wire.route_points) {
                    stream << ' ' << FormatDouble(point.x) << ' ' << FormatDouble(point.y);
                }
            }
            stream << '\n';
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
            if (tag == "END") {
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
                snapshot.wires.push_back(std::move(wire));
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
