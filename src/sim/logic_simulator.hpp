#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "contracts/component_library.hpp"
#include "contracts/logic_value.hpp"
#include "contracts/schematic_snapshot.hpp"

namespace cse::sim {

// 组合逻辑仿真结果：
//   net_values 与 net_builder::BuildNets 的 nets 一一对应（下标即网络序号）；
//   pin_values 的 key 用 contracts/ids.hpp 的 PinKey(component_id, pin_id)，
//   值 = 该引脚所在网络的值；
//   wire_values 给每条接了网络的导线记录电平，供画布可视化着色
//   （悬空导线不在里面）。
struct SimulationResult {
    bool stable{false};
    int iterations{0};
    std::vector<LogicValue> net_values;
    std::unordered_map<std::uint64_t, LogicValue> pin_values;
    std::unordered_map<WireId, LogicValue> wire_values;
};

// 组合逻辑仿真：
//   - io.input 元件是激励源，值取 input_states（没有的默认 kLow）；
//   - category == "logic" 的元件按 type_id 求值（and/or/xor/not），
//     未注册语义的门输出悬空（保持 kUnknown）；
//   - 一个网络上有多个驱动源且值不一致 → 冲突，记 kUnknown；
//   - 迭代传播直到不再变化；超过上限仍未稳定视为振荡，stable = false。
SimulationResult RunSimulation(const SchematicSnapshot& snapshot,
                               const IComponentLibrary& library,
                               const std::unordered_map<ComponentId, LogicValue>& input_states = {});

}  // namespace cse::sim
