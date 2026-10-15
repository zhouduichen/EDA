#include "src/model/net_builder.hpp"

#include <algorithm>
#include <map>
#include <numeric>
#include <unordered_map>
#include <utility>

#include "contracts/ids.hpp"
#include "src/model/wire_route.hpp"

namespace cse::model {

namespace {

    // 端点吸附容差：画布上网格 10px、端点都落在网格上，0.5 足以吸收浮点误差，
    // 又不会把相邻网格上的两个点误接到一起。
    constexpr double kJoinEpsilon = 0.5;

    // 并查集：把"接到一起"的导线/引脚并成同一个连通分量。
    class DisjointSet {
    public:
        explicit DisjointSet(std::size_t size) : parent_(size) {
            std::iota(parent_.begin(), parent_.end(), 0);
        }

        std::size_t Find(std::size_t node) {
            while (parent_[node] != node) {
                parent_[node] = parent_[parent_[node]];
                node = parent_[node];
            }
            return node;
        }

        void Union(std::size_t first, std::size_t second) {
            const std::size_t first_root = Find(first);
            const std::size_t second_root = Find(second);
            if (first_root != second_root) {
                parent_[second_root] = first_root;
            }
        }

    private:
        std::vector<std::size_t> parent_;
    };

    std::string NetName(const PinRef& pin,
                        const std::unordered_map<ComponentId, ComponentDefinition>& definitions,
                        const std::unordered_map<ComponentId, std::string>& references) {
        std::string pin_name = std::to_string(pin.pin_id);
        const auto definition_iterator = definitions.find(pin.component_id);
        if (definition_iterator != definitions.end()) {
            const auto pin_iterator = std::find_if(
                definition_iterator->second.pins.begin(), definition_iterator->second.pins.end(),
                [pin](const PinDefinition& definition_pin) {
                    return definition_pin.local_id == pin.pin_id;
                });
            if (pin_iterator != definition_iterator->second.pins.end()) {
                pin_name = pin_iterator->name;
            }
        }
        const auto reference_iterator = references.find(pin.component_id);
        const std::string reference =
            reference_iterator != references.end() ? reference_iterator->second
                                                   : std::to_string(pin.component_id);
        return "Net-(" + reference + "-" + pin_name + ")";
    }

}  // namespace

BuiltNetlist BuildNets(const SchematicSnapshot& snapshot, const IComponentLibrary& library) {
    // 1. 缓存元件定义和位号，供网络命名用。
    std::unordered_map<ComponentId, ComponentDefinition> definitions;
    std::unordered_map<ComponentId, std::string> references;
    for (const ComponentInstance& component : snapshot.components) {
        const std::optional<ComponentDefinition> definition =
            library.FindByTypeId(component.type_id);
        if (!definition.has_value()) {
            continue;
        }
        references.emplace(component.id, component.reference);
        definitions.emplace(component.id, *definition);
    }

    // 2. 编号：前 wires.size() 个节点是导线，其后每个出现过的引脚一个节点。
    std::vector<std::uint64_t> pin_keys;
    for (const WireModel& wire : snapshot.wires) {
        if (wire.from.pin.has_value()) {
            pin_keys.push_back(PinKey(wire.from.pin->component_id, wire.from.pin->pin_id));
        }
        if (wire.to.pin.has_value()) {
            pin_keys.push_back(PinKey(wire.to.pin->component_id, wire.to.pin->pin_id));
        }
    }
    std::sort(pin_keys.begin(), pin_keys.end());
    pin_keys.erase(std::unique(pin_keys.begin(), pin_keys.end()), pin_keys.end());

    std::unordered_map<std::uint64_t, std::size_t> pin_nodes;
    pin_nodes.reserve(pin_keys.size());
    const std::size_t wire_count = snapshot.wires.size();
    for (std::size_t index = 0; index < pin_keys.size(); ++index) {
        pin_nodes.emplace(pin_keys[index], wire_count + index);
    }

    DisjointSet sets(wire_count + pin_nodes.size());

    // 3. 端点挂引脚 → 导线与引脚同网。
    for (std::size_t index = 0; index < wire_count; ++index) {
        const WireModel& wire = snapshot.wires[index];
        if (wire.from.pin.has_value()) {
            sets.Union(index, pin_nodes.at(PinKey(wire.from.pin->component_id,
                                                  wire.from.pin->pin_id)));
        }
        if (wire.to.pin.has_value()) {
            sets.Union(index, pin_nodes.at(PinKey(wire.to.pin->component_id,
                                                  wire.to.pin->pin_id)));
        }
    }

    // 4. 几何合并：端点重合 / T 形相接。先把每条导线的折线算出来（挂引脚的
    //    端点用引脚绝对坐标，中间拐点走和画布同一套正交规划），再做端点-线段距离测试。
    std::vector<std::vector<Point2D>> polylines(wire_count);
    for (std::size_t index = 0; index < wire_count; ++index) {
        polylines[index] = WirePolyline(snapshot, library, snapshot.wires[index]);
    }

    const double epsilon_squared = kJoinEpsilon * kJoinEpsilon;
    auto endpoint_touches = [epsilon_squared](Point2D endpoint,
                                              const std::vector<Point2D>& polyline) {
        for (std::size_t index = 1; index < polyline.size(); ++index) {
            if (DistanceToSegmentSquared(endpoint, polyline[index - 1], polyline[index]) <=
                epsilon_squared) {
                return true;
            }
        }
        return false;
    };
    for (std::size_t first = 0; first < wire_count; ++first) {
        for (std::size_t second = first + 1; second < wire_count; ++second) {
            const bool first_touches_second =
                endpoint_touches(polylines[first].front(), polylines[second]) ||
                endpoint_touches(polylines[first].back(), polylines[second]);
            const bool second_touches_first =
                endpoint_touches(polylines[second].front(), polylines[first]) ||
                endpoint_touches(polylines[second].back(), polylines[first]);
            if (first_touches_second || second_touches_first) {
                sets.Union(first, second);
            }
        }
    }

    // 5. 收集：引脚节点按连通分量分组；纯导线分量（没有引脚）丢弃。
    std::map<std::size_t, std::vector<PinRef>> groups;
    for (const auto& [key, node] : pin_nodes) {
        groups[sets.Find(node)].push_back(
            PinRef{key >> 32, static_cast<PinId>(key & 0xFFFFFFFFULL)});
    }

    BuiltNetlist netlist;
    std::map<std::size_t, std::size_t> net_index_by_root;
    for (auto& [root, pins] : groups) {
        std::sort(pins.begin(), pins.end(), [](const PinRef& first, const PinRef& second) {
            return first.component_id != second.component_id
                       ? first.component_id < second.component_id
                       : first.pin_id < second.pin_id;
        });
        BuiltNet net;
        net.name = NetName(pins.front(), definitions, references);
        net.pins = std::move(pins);
        net_index_by_root[root] = netlist.nets.size();
        netlist.nets.push_back(std::move(net));
    }
    std::sort(netlist.nets.begin(), netlist.nets.end(),
              [](const BuiltNet& first, const BuiltNet& second) {
                  return first.pins.front().component_id != second.pins.front().component_id
                             ? first.pins.front().component_id < second.pins.front().component_id
                             : first.pins.front().pin_id < second.pins.front().pin_id;
              });
    // 排序后下标变了，按"根 → 网络"映射重建一遍，再把导线挂到网络下标上。
    for (std::size_t index = 0; index < netlist.nets.size(); ++index) {
        const PinRef& first_pin = netlist.nets[index].pins.front();
        net_index_by_root[sets.Find(pin_nodes.at(PinKey(first_pin.component_id,
                                                        first_pin.pin_id)))] = index;
    }
    for (std::size_t index = 0; index < wire_count; ++index) {
        const auto net_iterator = net_index_by_root.find(sets.Find(index));
        if (net_iterator != net_index_by_root.end()) {
            netlist.wire_nets.emplace(snapshot.wires[index].id, net_iterator->second);
        }
    }
    return netlist;
}

}  // namespace cse::model
