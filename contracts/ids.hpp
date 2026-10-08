#pragma once

#include <cstdint>

namespace cse {

using ComponentId = std::uint64_t;
using WireId = std::uint64_t;
using NodeId = std::uint64_t;
using PinId = std::uint32_t;
using CommandId = std::uint64_t;

constexpr ComponentId kInvalidComponentId = 0;
constexpr WireId kInvalidWireId = 0;
constexpr NodeId kInvalidNodeId = 0;

// 把 (component_id, pin_id) 打包成一个 uint64，用作 unordered_map 的 key。
inline std::uint64_t PinKey(ComponentId component_id, PinId pin_id) {
    return (component_id << 32) | static_cast<std::uint64_t>(pin_id);
}

}  // namespace cse
