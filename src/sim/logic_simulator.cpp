#include "src/sim/logic_simulator.hpp"

#include <algorithm>
#include <utility>

#include "contracts/ids.hpp"
#include "src/model/net_builder.hpp"

namespace cse::sim {

namespace {

    constexpr int kMaxIterations = 64;

    bool IsKnown(LogicValue value) {
        return value == LogicValue::kLow || value == LogicValue::kHigh;
    }

    // 门级求值都按"未知传播"语义：输入里有 X，输出不能瞎猜。
    LogicValue EvalAnd(const std::vector<LogicValue>& inputs) {
        bool any_unknown = false;
        for (LogicValue value : inputs) {
            if (value == LogicValue::kLow) {
                return LogicValue::kLow;  // 有 0 与其他输入无关
            }
            if (!IsKnown(value)) {
                any_unknown = true;
            }
        }
        return any_unknown ? LogicValue::kUnknown : LogicValue::kHigh;
    }

    LogicValue EvalOr(const std::vector<LogicValue>& inputs) {
        bool any_unknown = false;
        for (LogicValue value : inputs) {
            if (value == LogicValue::kHigh) {
                return LogicValue::kHigh;  // 有 1 与其他输入无关
            }
            if (!IsKnown(value)) {
                any_unknown = true;
            }
        }
        return any_unknown ? LogicValue::kUnknown : LogicValue::kLow;
    }

    LogicValue EvalXor(const std::vector<LogicValue>& inputs) {
        int high_count = 0;
        for (LogicValue value : inputs) {
            if (!IsKnown(value)) {
                return LogicValue::kUnknown;
            }
            if (value == LogicValue::kHigh) {
                ++high_count;
            }
        }
        return high_count % 2 == 1 ? LogicValue::kHigh : LogicValue::kLow;
    }

    LogicValue EvalNot(const std::vector<LogicValue>& inputs) {
        if (inputs.size() != 1 || !IsKnown(inputs[0])) {
            return LogicValue::kUnknown;
        }
        return inputs[0] == LogicValue::kHigh ? LogicValue::kLow : LogicValue::kHigh;
    }

    LogicValue EvalGate(const std::string& type_id, const std::vector<LogicValue>& inputs) {
        if (type_id == "logic.and") {
            return EvalAnd(inputs);
        }
        if (type_id == "logic.or") {
            return EvalOr(inputs);
        }
        if (type_id == "logic.xor") {
            return EvalXor(inputs);
        }
        if (type_id == "logic.not") {
            return EvalNot(inputs);
        }
        return LogicValue::kUnknown;  // 未注册仿真语义的元件
    }

}  // namespace

SimulationResult RunSimulation(const SchematicSnapshot& snapshot,
                               const IComponentLibrary& library,
                               const std::unordered_map<ComponentId, LogicValue>& input_states) {
    SimulationResult result;
    const cse::model::BuiltNetlist netlist = cse::model::BuildNets(snapshot, library);

    // 引脚 → 网络下标，门级求值在网络粒度上传播。
    std::unordered_map<std::uint64_t, std::size_t> pin_net;
    for (std::size_t index = 0; index < netlist.nets.size(); ++index) {
        for (const PinRef& pin : netlist.nets[index].pins) {
            pin_net.emplace(PinKey(pin.component_id, pin.pin_id), index);
        }
    }

    std::vector<LogicValue> values(netlist.nets.size(), LogicValue::kUnknown);

    // 每一轮：所有驱动源先算好 → 按网络解冲突 → 与上一轮比较判断是否稳定。
    // 门读的是上一轮的网络值，链式电路靠多轮迭代逐级传播。
    for (int iteration = 1; iteration <= kMaxIterations; ++iteration) {
        struct Drive {
            std::size_t net_index;
            LogicValue value;
        };
        std::vector<Drive> drives;

        for (const ComponentInstance& component : snapshot.components) {
            const std::optional<ComponentDefinition> definition =
                library.FindByTypeId(component.type_id);
            if (!definition.has_value()) {
                continue;
            }

            if (component.type_id == "io.input") {
                const auto state_iterator = input_states.find(component.id);
                const LogicValue value = state_iterator != input_states.end()
                                             ? state_iterator->second
                                             : LogicValue::kLow;
                for (const PinDefinition& pin : definition->pins) {
                    if (pin.direction != PinDirection::kOutput) {
                        continue;
                    }
                    const auto net_iterator = pin_net.find(PinKey(component.id, pin.local_id));
                    if (net_iterator != pin_net.end()) {
                        drives.push_back({net_iterator->second, value});
                    }
                }
                continue;
            }

            const std::shared_ptr<const CompositeModule> composite =
                library.FindComposite(component.type_id);
            if (composite != nullptr) {
                std::unordered_map<ComponentId, LogicValue> inner_inputs;
                for (const CompositePortMapping& mapping : composite->inputs) {
                    const auto net_iterator = pin_net.find(
                        PinKey(component.id, mapping.external_pin_id));
                    inner_inputs[mapping.internal_component_id] =
                        net_iterator != pin_net.end() ? values[net_iterator->second]
                                                      : LogicValue::kUnknown;
                }
                const SimulationResult inner = RunSimulation(
                    composite->implementation, library, inner_inputs);
                for (const CompositePortMapping& mapping : composite->outputs) {
                    const auto external_net = pin_net.find(
                        PinKey(component.id, mapping.external_pin_id));
                    const auto internal_value = inner.pin_values.find(
                        PinKey(mapping.internal_component_id, mapping.internal_pin_id));
                    if (external_net != pin_net.end()) {
                        drives.push_back({external_net->second,
                                          internal_value != inner.pin_values.end()
                                              ? internal_value->second
                                              : LogicValue::kUnknown});
                    }
                }
                continue;
            }

            if (definition->category != "logic") {
                continue;
            }
            std::vector<LogicValue> inputs;
            for (const PinDefinition& pin : definition->pins) {
                if (pin.direction != PinDirection::kInput) {
                    continue;
                }
                const auto net_iterator = pin_net.find(PinKey(component.id, pin.local_id));
                inputs.push_back(net_iterator != pin_net.end()
                                     ? values[net_iterator->second]
                                     : LogicValue::kUnknown);  // 悬空输入按未知处理
            }
            const LogicValue output = EvalGate(component.type_id, inputs);
            for (const PinDefinition& pin : definition->pins) {
                if (pin.direction != PinDirection::kOutput) {
                    continue;
                }
                const auto net_iterator = pin_net.find(PinKey(component.id, pin.local_id));
                if (net_iterator != pin_net.end()) {
                    drives.push_back({net_iterator->second, output});
                }
            }
        }

        // 解冲突：一致 → 该值；不一致 → kUnknown；无驱动 → 保持原值。
        std::vector<LogicValue> next = values;
        std::vector<char> has_driver(values.size(), 0);
        std::vector<char> conflicted(values.size(), 0);
        for (const Drive& drive : drives) {
            if (has_driver[drive.net_index] == 0) {
                has_driver[drive.net_index] = 1;
                next[drive.net_index] = drive.value;
            } else if (next[drive.net_index] != drive.value) {
                conflicted[drive.net_index] = 1;
            }
        }
        for (std::size_t index = 0; index < next.size(); ++index) {
            if (conflicted[index] != 0) {
                next[index] = LogicValue::kUnknown;
            }
        }

        result.iterations = iteration;
        if (next == values) {
            result.stable = true;
            values = std::move(next);
            break;
        }
        values = std::move(next);
    }

    result.net_values = values;
    for (const auto& [key, net_index] : pin_net) {
        result.pin_values.emplace(key, values[net_index]);
    }
    for (const auto& [wire_id, net_index] : netlist.wire_nets) {
        result.wire_values.emplace(wire_id, values[net_index]);
    }
    return result;
}

}  // namespace cse::sim
