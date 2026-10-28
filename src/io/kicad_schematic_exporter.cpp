#include "src/io/kicad_schematic_exporter.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <iomanip>
#include <locale>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "contracts/geometry.hpp"
#include "contracts/ids.hpp"
#include "src/model/wire_route.hpp"

namespace cse::io {

namespace {

    // 一个网格（kDefaultGridSize 单位）对应 2.54 mm（0.1 英寸），和 KiCad 常见引脚间距一致。
    constexpr double kScale = 2.54 / kDefaultGridSize;
    constexpr double kJoinEpsilon = 0.5;
    constexpr double kExactEpsilon = 1e-6;
    constexpr int kFormatVersion = 20211123;  // KiCad 6；更新的版本会自动升级。

    std::string Escape(const std::string& text) {
        std::string result;
        result.reserve(text.size());
        for (char character : text) {
            // 换行/制表符会破坏一行一个 token 的布局；引用值里用空格代替。
            if (character == '\n' || character == '\r' || character == '\t') {
                result.push_back(' ');
                continue;
            }
            if (character == '"' || character == '\\') {
                result.push_back('\\');
            }
            result.push_back(character);
        }
        return result;
    }

    std::string Coord(double value) {
        if (std::abs(value) < 0.0000005) {
            value = 0.0;
        }
        std::ostringstream stream;
        stream.imbue(std::locale::classic());  // 小数点是 '.'，不受系统区域设置影响。
        stream << std::fixed << std::setprecision(6) << value;
        return stream.str();
    }

    std::string Mm(double editor_units) {
        return Coord(editor_units * kScale);
    }

    // KiCad 只要求 UUID 唯一；用计数器生成稳定值，导出的文件可以逐字比较。
    class UuidGenerator {
    public:
        std::string Next() {
            std::ostringstream stream;
            stream << "00000000-0000-4000-8000-" << std::setw(12) << std::setfill('0')
                   << std::hex << ++counter_;
            return stream.str();
        }

    private:
        std::uint64_t counter_{0};
    };

    std::string Sanitize(const std::string& type_id) {
        std::string result;
        result.reserve(type_id.size());
        for (char character : type_id) {
            const bool safe = (character >= 'a' && character <= 'z') ||
                              (character >= 'A' && character <= 'Z') ||
                              (character >= '0' && character <= '9') || character == '_';
            result.push_back(safe ? character : '_');
        }
        return result.empty() ? "component" : result;
    }

    int QuarterTurn(double rotation_deg) {
        int quarter = static_cast<int>(std::lround(rotation_deg / 90.0)) % 4;
        if (quarter < 0) {
            quarter += 4;
        }
        return quarter;
    }

    std::string ReferencePrefix(const std::string& type_id) {
        if (type_id == "io.input") {
            return "IN";
        }
        if (type_id == "io.output") {
            return "OUT";
        }
        return "U";
    }

    // 占位封装：让 KiCad 的 F8 能先建出元件，用户再按实际器件改封装。
    // 都是 KiCad 官方库里的标准名字。
    std::string DefaultFootprint(const std::string& type_id) {
        if (type_id == "io.input" || type_id == "io.output") {
            return "Connector_PinHeader_2.54mm:PinHeader_1x01_P2.54mm_Vertical";
        }
        return "Package_DIP:DIP-14_W7.62mm";
    }

    bool ValidReference(const std::string& reference) {
        if (reference.empty() ||
            std::isalpha(static_cast<unsigned char>(reference.front())) == 0) {
            return false;
        }
        for (char character : reference) {
            if (std::isalnum(static_cast<unsigned char>(character)) == 0 &&
                character != '_' && character != '-') {
                return false;
            }
        }
        return true;
    }

    std::string ChoosePaper(double width_mm, double height_mm) {
        struct Paper {
            const char* name;
            double width;
            double height;
        };
        const Paper papers[] = {
            {"A4", 297.0, 210.0}, {"A3", 420.0, 297.0}, {"A2", 594.0, 420.0},
            {"A1", 841.0, 594.0}, {"A0", 1189.0, 841.0},
        };
        for (const Paper& paper : papers) {
            if (width_mm <= paper.width && height_mm <= paper.height) {
                return paper.name;
            }
        }
        return "A0";
    }

    std::string PinElectricalType(PinDirection direction) {
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

    // 引脚方向随符号旋转一起转；长度是 0，方向只影响名字/编号的摆放。
    int PinAngle(const PinDefinition& pin, const Rect2D& bounds, int quarter) {
        int base = 0;
        if (pin.relative_position.x <= 0.0) {
            base = 0;  // 左边，指向右
        } else if (pin.relative_position.x >= bounds.width) {
            base = 180;  // 右边，指向左
        } else if (pin.relative_position.y <= 0.0) {
            base = 270;  // 上边，指向下
        } else if (pin.relative_position.y >= bounds.height) {
            base = 90;  // 下边，指向上
        }
        return (base - quarter * 90 + 360) % 360;
    }

    struct SymbolPinSpec {
        Point2D position;  // mm，相对元件原点
        int angle{0};
        PinId number{0};
        std::string name;
        std::string electrical_type;
    };

    struct SymbolSpec {
        std::string lib_id;
        std::string unit_name;
        std::string reference_prefix;
        std::string value;
        std::string footprint;
        double x{0.0};
        double y{0.0};
        double width{0.0};
        double height{0.0};
        std::vector<SymbolPinSpec> pins;
    };

    SymbolSpec MakeSymbolSpec(const ComponentDefinition& definition, int quarter) {
        SymbolSpec spec;
        const std::string suffix = quarter == 0 ? "" : "_r" + std::to_string(quarter * 90);
        spec.unit_name = Sanitize(definition.type_id) + suffix;
        spec.lib_id = "cse:" + spec.unit_name;
        spec.reference_prefix = ReferencePrefix(definition.type_id);
        spec.value = definition.display_name;
        spec.footprint = DefaultFootprint(definition.type_id);

        const Rect2D bounds = model::RotatedComponentBounds(definition, quarter * 90.0);
        spec.x = bounds.x * kScale;
        spec.y = bounds.y * kScale;
        spec.width = bounds.width * kScale;
        spec.height = bounds.height * kScale;

        ComponentInstance origin;
        origin.position = {0.0, 0.0};
        origin.rotation_deg = quarter * 90.0;
        for (const PinDefinition& pin : definition.pins) {
            const Point2D position = model::PinAbsolutePosition(origin, definition, pin);
            SymbolPinSpec pin_spec;
            pin_spec.position = {position.x * kScale, position.y * kScale};
            pin_spec.angle = PinAngle(pin, definition.default_bounds, quarter);
            pin_spec.number = pin.local_id;
            pin_spec.name = pin.name;
            pin_spec.electrical_type = PinElectricalType(pin.direction);
            spec.pins.push_back(std::move(pin_spec));
        }
        return spec;
    }

    bool SamePoint(Point2D first, Point2D second) {
        return std::abs(first.x - second.x) <= kJoinEpsilon &&
               std::abs(first.y - second.y) <= kJoinEpsilon;
    }

    bool ExactSamePoint(Point2D first, Point2D second) {
        return std::abs(first.x - second.x) <= kExactEpsilon &&
               std::abs(first.y - second.y) <= kExactEpsilon;
    }

    // KiCad 的连接规则要求 T 形相接处显式放 junction；导线端点重合不需要。
    // 建网用的是 0.5 单位容差，而 KiCad 按精确坐标判断；若两者不一致，
    // 导出的文件会和画布/仿真结果对不上，所以这里把"松连接"标出来并拒绝导出。
    std::vector<Point2D> CollectJunctions(const std::vector<std::vector<Point2D>>& polylines,
                                          bool* has_loose_connection) {
        *has_loose_connection = false;
        std::vector<Point2D> junctions;
        auto add = [&junctions](Point2D point) {
            for (const Point2D& existing : junctions) {
                if (SamePoint(existing, point)) {
                    return;
                }
            }
            junctions.push_back(point);
        };

        for (std::size_t first = 0; first < polylines.size(); ++first) {
            const Point2D endpoints[2] = {polylines[first].front(), polylines[first].back()};
            for (Point2D endpoint : endpoints) {
                for (std::size_t second = 0; second < polylines.size(); ++second) {
                    if (first == second) {
                        continue;
                    }
                    for (std::size_t index = 1; index < polylines[second].size(); ++index) {
                        const Point2D start = polylines[second][index - 1];
                        const Point2D end = polylines[second][index];
                        const double distance_squared =
                            model::DistanceToSegmentSquared(endpoint, start, end);
                        if (distance_squared > kJoinEpsilon * kJoinEpsilon) {
                            continue;
                        }
                        if (distance_squared > kExactEpsilon * kExactEpsilon) {
                            *has_loose_connection = true;
                        }
                        if (!SamePoint(endpoint, start) && !SamePoint(endpoint, end)) {
                            add(endpoint);
                        }
                    }
                }
            }
        }

        // 端点与端点之间：容差内但没精确重合，同样是建网连、KiCad 不连。
        for (std::size_t first = 0; first < polylines.size(); ++first) {
            for (std::size_t second = first + 1; second < polylines.size(); ++second) {
                const Point2D first_points[2] = {polylines[first].front(),
                                                 polylines[first].back()};
                const Point2D second_points[2] = {polylines[second].front(),
                                                  polylines[second].back()};
                for (Point2D first_point : first_points) {
                    for (Point2D second_point : second_points) {
                        if (SamePoint(first_point, second_point) &&
                            !ExactSamePoint(first_point, second_point)) {
                            *has_loose_connection = true;
                        }
                    }
                }
            }
        }

        // 三条以上导线共用同一个端点时也补一个 junction，避免不同版本判定不一致。
        for (std::size_t first = 0; first < polylines.size(); ++first) {
            const Point2D endpoints[2] = {polylines[first].front(), polylines[first].back()};
            for (Point2D endpoint : endpoints) {
                int count = 0;
                for (const std::vector<Point2D>& polyline : polylines) {
                    if (SamePoint(endpoint, polyline.front()) ||
                        SamePoint(endpoint, polyline.back())) {
                        ++count;
                    }
                }
                if (count >= 3) {
                    add(endpoint);
                }
            }
        }

        std::sort(junctions.begin(), junctions.end(), [](Point2D first, Point2D second) {
            return first.x != second.x ? first.x < second.x : first.y < second.y;
        });
        return junctions;
    }

    void WriteLibSymbols(std::ostream& stream, const std::map<std::pair<std::string, int>,
                                                           SymbolSpec>& symbols) {
        stream << "  (lib_symbols\n";
        for (const auto& [key, spec] : symbols) {
            static_cast<void>(key);
            // 符号库用 Y 轴向上，原理图用 Y 轴向下；放置时 KiCad 会自动把库坐标的 Y 取反。
            // 这里先把 Y 取反写进 lib_symbols，放到画布上才和编辑器坐标一致。
            const double property_x = spec.x + spec.width / 2.0;
            const double reference_y = -(spec.y - 2.54);
            const double value_y = -(spec.y + spec.height + 2.54);

            stream << "    (symbol \"" << Escape(spec.lib_id)
                   << "\" (pin_names (offset 1.016)) (in_bom yes) (on_board yes)\n";
            stream << "      (property \"Reference\" \"" << Escape(spec.reference_prefix)
                   << "\" (id 0) (at " << Coord(property_x) << ' ' << Coord(reference_y)
                   << " 0)\n        (effects (font (size 1.27 1.27)))\n      )\n";
            stream << "      (property \"Value\" \"" << Escape(spec.value)
                   << "\" (id 1) (at " << Coord(property_x) << ' ' << Coord(value_y)
                   << " 0)\n        (effects (font (size 1.27 1.27)))\n      )\n";
            stream << "      (property \"Footprint\" \"" << Escape(spec.footprint)
                   << "\" (id 2) (at 0 0 0)\n"
                      "        (effects (font (size 1.27 1.27)) hide)\n      )\n";
            stream << "      (property \"Datasheet\" \"\" (id 3) (at 0 0 0)\n"
                      "        (effects (font (size 1.27 1.27)) hide)\n      )\n";

            stream << "      (symbol \"" << Escape(spec.unit_name) << "_0_1\"\n";
            stream << "        (rectangle (start " << Coord(spec.x) << ' '
                   << Coord(-(spec.y + spec.height)) << ") (end "
                   << Coord(spec.x + spec.width) << ' ' << Coord(-spec.y)
                   << ")\n          (stroke (width 0) (type default) (color 0 0 0 0))\n"
                      "          (fill (type none))\n        )\n      )\n";

            if (!spec.pins.empty()) {
                stream << "      (symbol \"" << Escape(spec.unit_name) << "_1_1\"\n";
                for (const SymbolPinSpec& pin : spec.pins) {
                    // 垂直引脚的方向也要跟着 Y 取反（90 <-> 270）。
                    const int lib_angle = (360 - pin.angle) % 360;
                    stream << "        (pin " << pin.electrical_type << " line (at "
                           << Coord(pin.position.x) << ' ' << Coord(-pin.position.y) << ' '
                           << lib_angle << ") (length 0)\n";
                    stream << "          (name \"" << Escape(pin.name)
                           << "\" (effects (font (size 1.27 1.27))))\n";
                    stream << "          (number \"" << pin.number
                           << "\" (effects (font (size 1.27 1.27))))\n        )\n";
                }
                stream << "      )\n";
            }
            stream << "    )\n";
        }
        stream << "  )\n\n";
    }

}  // namespace

Result<void> ExportKiCadSchematic(const SchematicSnapshot& snapshot,
                                  const IComponentLibrary& library,
                                  const std::string& path) {
    std::vector<ComponentInstance> components = snapshot.components;
    std::sort(components.begin(), components.end(),
              [](const ComponentInstance& first, const ComponentInstance& second) {
                  return first.id < second.id;
              });

    std::vector<WireModel> wires = snapshot.wires;
    std::sort(wires.begin(), wires.end(), [](const WireModel& first, const WireModel& second) {
        return first.id < second.id;
    });

    std::map<std::pair<std::string, int>, SymbolSpec> symbols;
    std::unordered_set<std::string> references;

    for (const ComponentInstance& component : components) {
        if (component.id == kInvalidComponentId || !ValidReference(component.reference) ||
            !std::isfinite(component.position.x) || !std::isfinite(component.position.y) ||
            !std::isfinite(component.rotation_deg)) {
            return Result<void>::Failure(
                ErrorCode::kInvalidArgument,
                "invalid component (id/reference/position) in schematic");
        }
        if (!references.insert(component.reference).second) {
            return Result<void>::Failure(ErrorCode::kInvalidArgument,
                                         "duplicate reference: " + component.reference);
        }
        const std::optional<ComponentDefinition> definition =
            library.FindByTypeId(component.type_id);
        if (!definition.has_value()) {
            return Result<void>::Failure(
                ErrorCode::kInvalidComponent,
                "component type not found in library: " + component.type_id);
        }
        const int quarter = QuarterTurn(component.rotation_deg);
        const auto key = std::make_pair(component.type_id, quarter);
        if (symbols.count(key) == 0) {
            symbols.emplace(key, MakeSymbolSpec(*definition, quarter));
        }
    }

    // 自定义类型经过 Sanitize 后可能撞名（a.b 和 a_b 都会变成 a_b）。
    std::map<std::string, std::pair<std::string, int>> lib_id_owners;
    for (const auto& [key, spec] : symbols) {
        const auto [iterator, inserted] = lib_id_owners.emplace(spec.lib_id, key);
        if (!inserted && iterator->second != key) {
            return Result<void>::Failure(
                ErrorCode::kInvalidComponent,
                "component type ids collide after sanitizing: " + spec.lib_id);
        }
    }

    std::vector<std::vector<Point2D>> wire_polylines(wires.size());
    std::vector<std::vector<Point2D>> polylines;
    polylines.reserve(wires.size());
    for (std::size_t index = 0; index < wires.size(); ++index) {
        wire_polylines[index] = model::WirePolyline(snapshot, library, wires[index]);
        if (wire_polylines[index].size() < 2) {
            continue;
        }
        for (const Point2D& point : wire_polylines[index]) {
            if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
                return Result<void>::Failure(ErrorCode::kInvalidArgument,
                                             "invalid wire point in schematic");
            }
        }
        polylines.push_back(wire_polylines[index]);
    }

    bool has_loose_connection = false;
    std::vector<Point2D> junctions = CollectJunctions(polylines, &has_loose_connection);
    if (has_loose_connection) {
        return Result<void>::Failure(
            ErrorCode::kInvalidConnection,
            "wire endpoints connect within tolerance but are not exactly coincident; "
            "KiCad would see a different net. Move/regrid the wires before exporting.");
    }

    // KiCad 图纸原点在页面左上角，而编辑器坐标可以为负——不平移的话
    // 负坐标内容会画到页面之外，打开后"少一半图"。算出内容包围盒，
    // 整体平移到留出边距的位置（元件、导线、结点一起动，相对关系不变）。
    constexpr double kPageMargin = 20.0;
    double content_min_x = std::numeric_limits<double>::max();
    double content_min_y = std::numeric_limits<double>::max();
    for (const ComponentInstance& component : components) {
        const int quarter = QuarterTurn(component.rotation_deg);
        const SymbolSpec& spec = symbols.at(std::make_pair(component.type_id, quarter));
        content_min_x = std::min(content_min_x, component.position.x * kScale + spec.x);
        content_min_y = std::min(content_min_y, component.position.y * kScale + spec.y);
    }
    for (const std::vector<Point2D>& polyline : polylines) {
        for (const Point2D& point : polyline) {
            content_min_x = std::min(content_min_x, point.x * kScale);
            content_min_y = std::min(content_min_y, point.y * kScale);
        }
    }
    const double shift_x = content_min_x < kPageMargin ? kPageMargin - content_min_x : 0.0;
    const double shift_y = content_min_y < kPageMargin ? kPageMargin - content_min_y : 0.0;
    if (shift_x != 0.0 || shift_y != 0.0) {
        for (ComponentInstance& component : components) {
            component.position.x += shift_x / kScale;
            component.position.y += shift_y / kScale;
        }
        for (std::vector<Point2D>& polyline : wire_polylines) {
            for (Point2D& point : polyline) {
                point.x += shift_x / kScale;
                point.y += shift_y / kScale;
            }
        }
        for (Point2D& junction : junctions) {
            junction.x += shift_x / kScale;
            junction.y += shift_y / kScale;
        }
    }

    double content_max_x = 0.0;
    double content_max_y = 0.0;
    for (const ComponentInstance& component : components) {
        const int quarter = QuarterTurn(component.rotation_deg);
        const SymbolSpec& spec = symbols.at(std::make_pair(component.type_id, quarter));
        content_max_x = std::max(content_max_x, component.position.x * kScale + spec.x + spec.width);
        content_max_y = std::max(content_max_y,
                                 component.position.y * kScale + spec.y + spec.height);
    }
    for (const std::vector<Point2D>& polyline : polylines) {
        for (const Point2D& point : polyline) {
            content_max_x = std::max(content_max_x, point.x * kScale);
            content_max_y = std::max(content_max_y, point.y * kScale);
        }
    }
    const std::string paper = ChoosePaper(content_max_x + 20.0, content_max_y + 20.0);

    std::ofstream stream(path, std::ios::binary);
    if (!stream.is_open()) {
        return Result<void>::Failure(ErrorCode::kFileOpenFailed,
                                     "cannot open for writing: " + path);
    }

    UuidGenerator uuids;
    const std::string root_uuid = uuids.Next();

    stream << "(kicad_sch (version " << kFormatVersion << ") (generator eeschema)\n\n";
    stream << "  (uuid " << root_uuid << ")\n\n";
    stream << "  (paper \"" << paper << "\")\n\n";

    WriteLibSymbols(stream, symbols);

    for (const Point2D& junction : junctions) {
        stream << "  (junction (at " << Mm(junction.x) << ' ' << Mm(junction.y)
               << ") (diameter 0) (color 0 0 0 0)\n";
        stream << "    (uuid " << uuids.Next() << ")\n  )\n";
    }
    if (!junctions.empty()) {
        stream << '\n';
    }

    bool wrote_wire = false;
    for (const std::vector<Point2D>& polyline : wire_polylines) {
        for (std::size_t index = 1; index < polyline.size(); ++index) {
            const Point2D start = polyline[index - 1];
            const Point2D end = polyline[index];
            if (std::abs(start.x - end.x) < 1e-9 && std::abs(start.y - end.y) < 1e-9) {
                continue;
            }
            stream << "  (wire (pts (xy " << Mm(start.x) << ' ' << Mm(start.y) << ") (xy "
                   << Mm(end.x) << ' ' << Mm(end.y) << "))\n";
            stream << "    (stroke (width 0) (type default) (color 0 0 0 0))\n";
            stream << "    (uuid " << uuids.Next() << ")\n  )\n";
            wrote_wire = true;
        }
    }
    if (wrote_wire) {
        stream << '\n';
    }

    std::vector<std::string> symbol_uuids;
    symbol_uuids.reserve(components.size());
    for (const ComponentInstance& component : components) {
        const int quarter = QuarterTurn(component.rotation_deg);
        const SymbolSpec& spec = symbols.at(std::make_pair(component.type_id, quarter));
        const double instance_x = component.position.x * kScale;
        const double instance_y = component.position.y * kScale;
        const double reference_x = instance_x + spec.x + spec.width / 2.0;
        const double reference_y = instance_y + spec.y - 2.54;
        const double value_y = instance_y + spec.y + spec.height + 2.54;
        const std::string uuid = uuids.Next();
        symbol_uuids.push_back(uuid);

        stream << "  (symbol (lib_id \"" << Escape(spec.lib_id) << "\") (at "
               << Coord(instance_x) << ' ' << Coord(instance_y) << " 0) (unit 1)\n";
        stream << "    (in_bom yes) (on_board yes)\n";
        stream << "    (uuid " << uuid << ")\n";
        stream << "    (property \"Reference\" \"" << Escape(component.reference)
               << "\" (id 0) (at " << Coord(reference_x) << ' ' << Coord(reference_y)
               << " 0)\n      (effects (font (size 1.27 1.27)))\n    )\n";
        stream << "    (property \"Value\" \"" << Escape(spec.value)
               << "\" (id 1) (at " << Coord(reference_x) << ' ' << Coord(value_y)
               << " 0)\n      (effects (font (size 1.27 1.27)))\n    )\n";
        stream << "    (property \"Footprint\" \"" << Escape(spec.footprint)
               << "\" (id 2) (at " << Coord(instance_x) << ' ' << Coord(instance_y)
               << " 0)\n      (effects (font (size 1.27 1.27)) hide)\n    )\n";
        stream << "    (property \"Datasheet\" \"\" (id 3) (at " << Coord(instance_x) << ' '
               << Coord(instance_y) << " 0)\n      (effects (font (size 1.27 1.27)) hide)\n    )\n";
        const auto label_iterator = component.properties.find("label");
        if (label_iterator != component.properties.end() && !label_iterator->second.empty()) {
            stream << "    (property \"Label\" \"" << Escape(label_iterator->second)
                   << "\" (id 4) (at " << Coord(reference_x) << ' ' << Coord(value_y + 2.54)
                   << " 0)\n      (effects (font (size 1.27 1.27)))\n    )\n";
        }
        for (const SymbolPinSpec& pin : spec.pins) {
            stream << "    (pin \"" << pin.number << "\" (uuid " << uuids.Next() << "))\n";
        }
        stream << "  )\n";
    }
    if (!components.empty()) {
        stream << '\n';
    }

    stream << "  (sheet_instances\n    (path \"/\" (page \"1\"))\n  )\n\n";
    stream << "  (symbol_instances\n";
    for (std::size_t index = 0; index < components.size(); ++index) {
        const ComponentInstance& component = components[index];
        const int quarter = QuarterTurn(component.rotation_deg);
        const SymbolSpec& spec = symbols.at(std::make_pair(component.type_id, quarter));
        stream << "    (path \"/" << symbol_uuids[index] << "\"\n";
        stream << "      (reference \"" << Escape(component.reference) << "\") (unit 1)"
               << " (value \"" << Escape(spec.value) << "\") (footprint \""
               << Escape(spec.footprint) << "\")\n    )\n";
    }
    stream << "  )\n)\n";

    if (!stream.good()) {
        return Result<void>::Failure(ErrorCode::kFileWriteFailed, "write failed: " + path);
    }
    return Result<void>::Success();
}

}  // namespace cse::io
