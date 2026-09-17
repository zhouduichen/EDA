#pragma once

namespace cse {

enum class PinDirection {
    kInput,
    kOutput,
    kBidirectional
};

enum class LogicValue {
    kLow,
    kHigh,
    kUnknown,
    kHighImpedance
};

}  // namespace cse
