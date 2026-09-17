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

}  // namespace cse
