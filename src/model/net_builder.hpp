#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "contracts/component_library.hpp"
#include "contracts/schematic_snapshot.hpp"

namespace cse::model {

// 网络（net）：电气上连在一起的引脚集合。连接规则沿用画布约定：
//   1. 导线端点显式挂在引脚上（PinRef）→ 归入该引脚所在网络；
//   2. 两条导线的端点重合 → 同一网络；
//   3. 导线端点落在另一条导线的线段中间（T 形相接）→ 同一网络；
//   4. 两条导线线段中部交叉（十字交叉）→ 不连接。
// 只由导线组成、没连任何引脚的网络对网表和仿真都没意义，不会出现在结果里。
struct BuiltNet {
    std::string name;
    std::vector<PinRef> pins;
};

struct BuiltNetlist {
    std::vector<BuiltNet> nets;
    // 导线 → 所属网络下标（只含有引脚的网络；悬空导线不在里面），
    // 供仿真给画布上的导线着色。
    std::unordered_map<WireId, std::size_t> wire_nets;
};

// 纯查询，不修改模型。library 用于把 PinRef 解析成绝对坐标（含元件旋转）。
// 结果确定性：每个网络的引脚按 (component_id, pin_id) 排序，
// 网络之间按第一个引脚排序，同名规则见 BuildNets 实现。
BuiltNetlist BuildNets(const SchematicSnapshot& snapshot, const IComponentLibrary& library);

}  // namespace cse::model
