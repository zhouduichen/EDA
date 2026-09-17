#pragma once

namespace cse {

struct Point2D {
    double x{0.0};
    double y{0.0};
};

struct Rect2D {
    double x{0.0};
    double y{0.0};
    double width{0.0};
    double height{0.0};
};

enum class Direction {
    kLeft,
    kRight,
    kUp,
    kDown
};

constexpr double kDefaultGridSize = 10.0;

}  // namespace cse
