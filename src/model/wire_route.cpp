#include "src/model/wire_route.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace cse::model {

namespace {

    // 网格 10px、引脚都落在网格上，0.5 足够吸收浮点误差，
    // 又不会把相邻网格上的两个点误判成同一个点。与建网的 kJoinEpsilon 取同一个值。
    constexpr double kRouteEpsilon = 0.5;

    int NormalizeQuarterTurn(double rotation_deg) {
        int quarter_turn = static_cast<int>(std::lround(rotation_deg / 90.0)) % 4;
        if (quarter_turn < 0) {
            quarter_turn += 4;
        }
        return quarter_turn;
    }

    // 与画布同一套引脚旋转公式，保证画布和建网算出来的引脚坐标一致。
    Point2D RotateRelative(Point2D relative, Rect2D bounds, double rotation_deg) {
        const Point2D center{bounds.width / 2.0, bounds.height / 2.0};
        const double local_x = relative.x - center.x;
        const double local_y = relative.y - center.y;
        switch (NormalizeQuarterTurn(rotation_deg)) {
            case 1:
                return {center.x - local_y, center.y + local_x};
            case 2:
                return {center.x - local_x, center.y - local_y};
            case 3:
                return {center.x + local_y, center.y - local_x};
            default:
                return relative;
        }
    }

    bool SamePoint(Point2D first, Point2D second) {
        return std::abs(first.x - second.x) < kRouteEpsilon &&
               std::abs(first.y - second.y) < kRouteEpsilon;
    }

    bool IsPin(const std::optional<PinRef>& pin, ComponentId component_id, PinId pin_id) {
        return pin.has_value() && pin->component_id == component_id && pin->pin_id == pin_id;
    }

    Point2D PinPosition(const ComponentInstance& component, const ComponentDefinition& definition,
                        const PinDefinition& pin) {
        const Point2D rotated =
            RotateRelative(pin.relative_position, definition.default_bounds, component.rotation_deg);
        return {component.position.x + rotated.x, component.position.y + rotated.y};
    }

    // 走线要绕开的点：所有引脚位置（本线两端那两个除外）+ 其它导线已有的端点。
    std::vector<Point2D> CollectObstacles(const SchematicSnapshot& snapshot,
                                          const IComponentLibrary& library, Point2D from,
                                          Point2D to, const std::optional<PinRef>& from_pin,
                                          const std::optional<PinRef>& to_pin) {
        std::vector<Point2D> obstacles;
        for (const ComponentInstance& component : snapshot.components) {
            const std::optional<ComponentDefinition> definition =
                library.FindByTypeId(component.type_id);
            if (!definition.has_value()) {
                continue;
            }
            for (const PinDefinition& pin : definition->pins) {
                if (IsPin(from_pin, component.id, pin.local_id) ||
                    IsPin(to_pin, component.id, pin.local_id)) {
                    continue;
                }
                obstacles.push_back(PinPosition(component, *definition, pin));
            }
        }
        for (const WireModel& wire : snapshot.wires) {
            // 其它导线的端点不避让：端点落在本线线段上是合法的 T 形相接
            //（建网就按这条规则连网），避让反而会把本该相连的线绕开。
            // 拐点仍然避让——拐点压在本线上只是几何重叠，建网不会连。
            obstacles.insert(obstacles.end(), wire.route_points.begin(),
                             wire.route_points.end());
        }
        // 和自己两端重合的点不算障碍——导线本来就该从这两个点出发。
        obstacles.erase(std::remove_if(obstacles.begin(), obstacles.end(),
                                       [from, to](Point2D point) {
                                           return SamePoint(point, from) || SamePoint(point, to);
                                       }),
                        obstacles.end());
        return obstacles;
    }

    // 走线要绕开的面：其它元件旋转后的外接矩形（不含本线两端所在的元件——
    // 导线本来就要从它们的引脚上出发，贴着边界走是正常的）。
    std::vector<Rect2D> CollectBodyObstacles(const SchematicSnapshot& snapshot,
                                             const IComponentLibrary& library,
                                             const std::optional<PinRef>& from_pin,
                                             const std::optional<PinRef>& to_pin) {
        std::vector<Rect2D> bodies;
        for (const ComponentInstance& component : snapshot.components) {
            bool is_endpoint_owner = false;
            if (from_pin.has_value() && from_pin->component_id == component.id) {
                is_endpoint_owner = true;
            }
            if (to_pin.has_value() && to_pin->component_id == component.id) {
                is_endpoint_owner = true;
            }
            if (is_endpoint_owner) {
                continue;
            }
            const std::optional<ComponentDefinition> definition =
                library.FindByTypeId(component.type_id);
            if (!definition.has_value()) {
                continue;
            }
            const Rect2D relative =
                RotatedComponentBounds(*definition, component.rotation_deg);
            bodies.push_back({component.position.x + relative.x,
                              component.position.y + relative.y, relative.width, relative.height});
        }
        return bodies;
    }

    // 线段是否穿过矩形内部（向内收缩 shrink：贴着边/引脚从边界出发不算穿过）。
    bool SegmentCrossesRect(Point2D start, Point2D end, Rect2D rect) {
        constexpr double kShrink = 0.5;
        const double left = rect.x + kShrink;
        const double right = rect.x + rect.width - kShrink;
        const double top = rect.y + kShrink;
        const double bottom = rect.y + rect.height - kShrink;
        if (right <= left || bottom <= top) {
            return false;
        }
        const auto inside = [left, right, top, bottom](Point2D point) {
            return point.x > left && point.x < right && point.y > top && point.y < bottom;
        };
        if (inside(start) || inside(end)) {
            return true;
        }
        // 与四条边做严格相交测试（共线/端点落在边上不算）。
        const auto orientation = [](Point2D a, Point2D b, Point2D c) {
            const double value = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
            return value > 0.0 ? 1 : (value < 0.0 ? -1 : 0);
        };
        const auto segments_strictly_cross = [&](Point2D a, Point2D b, Point2D c, Point2D d) {
            return orientation(a, b, c) * orientation(a, b, d) < 0 &&
                   orientation(c, d, a) * orientation(c, d, b) < 0;
        };
        const Point2D corners[4] = {{left, top}, {right, top}, {right, bottom}, {left, bottom}};
        for (int index = 0; index < 4; ++index) {
            if (segments_strictly_cross(start, end, corners[index],
                                        corners[(index + 1) % 4])) {
                return true;
            }
        }
        return false;
    }

    bool TouchesObstacle(const std::vector<Point2D>& polyline,
                         const std::vector<Point2D>& obstacles,
                         const std::vector<Rect2D>& bodies) {
        const double epsilon_squared = kRouteEpsilon * kRouteEpsilon;
        for (std::size_t index = 1; index < polyline.size(); ++index) {
            for (Point2D obstacle : obstacles) {
                if (DistanceToSegmentSquared(obstacle, polyline[index - 1], polyline[index]) <=
                    epsilon_squared) {
                    return true;
                }
            }
            for (const Rect2D& body : bodies) {
                if (SegmentCrossesRect(polyline[index - 1], polyline[index], body)) {
                    return true;
                }
            }
        }
        return false;
    }

    // 候选走法，越自然的排越前：先让位移大的那个方向当第一段（导线从引脚正前方出来），
    // 不行再折中——把中间那条竖线/横线放到两端的中间位置，绕开挡路的引脚。
    // 两端已经同行/同列时四个候选全退化成同一条直线，所以再补两组平移绕行的走法：
    // 直线上有别的引脚/元件时，至少有一条能绕开（否则两个引脚之间根本连不上）。
    std::vector<std::vector<Point2D>> RouteCandidates(Point2D from, Point2D to) {
        const Point2D middle{(from.x + to.x) / 2.0, (from.y + to.y) / 2.0};
        const std::vector<Point2D> horizontal_first{{to.x, from.y}};
        const std::vector<Point2D> vertical_first{{from.x, to.y}};
        const std::vector<Point2D> middle_column{{middle.x, from.y}, {middle.x, to.y}};
        const std::vector<Point2D> middle_row{{from.x, middle.y}, {to.x, middle.y}};
        std::vector<std::vector<Point2D>> candidates;
        if (std::abs(to.x - from.x) >= std::abs(to.y - from.y)) {
            candidates = {horizontal_first, vertical_first, middle_column, middle_row};
        } else {
            candidates = {vertical_first, horizontal_first, middle_row, middle_column};
        }
        constexpr double kDetourOffset = 2.0 * kDefaultGridSize;
        const bool same_row = std::abs(to.y - from.y) < kRouteEpsilon;
        const bool same_column = std::abs(to.x - from.x) < kRouteEpsilon;
        if (same_row) {
            candidates.push_back({{from.x, from.y - kDetourOffset},
                                  {to.x, from.y - kDetourOffset}});
            candidates.push_back({{from.x, from.y + kDetourOffset},
                                  {to.x, from.y + kDetourOffset}});
        }
        if (same_column) {
            candidates.push_back({{from.x - kDetourOffset, from.y},
                                  {from.x - kDetourOffset, to.y}});
            candidates.push_back({{from.x + kDetourOffset, from.y},
                                  {from.x + kDetourOffset, to.y}});
        }
        return candidates;
    }

    std::vector<Point2D> BuildPolyline(Point2D from, const std::vector<Point2D>& route_points,
                                       Point2D to) {
        std::vector<Point2D> polyline;
        polyline.reserve(route_points.size() + 2);
        polyline.push_back(from);
        polyline.insert(polyline.end(), route_points.begin(), route_points.end());
        polyline.push_back(to);
        return polyline;
    }

    // 把 previous→point 这一段补成正交后追加到折线；两端重合时跳过，避免零长度段。
    void AppendOrthogonalSegment(std::vector<Point2D>& polyline, Point2D previous, Point2D point,
                                 const SchematicSnapshot& snapshot, const IComponentLibrary& library,
                                 const std::optional<PinRef>& from_pin,
                                 const std::optional<PinRef>& to_pin) {
        if (SamePoint(previous, point)) {
            return;
        }
        const std::vector<Point2D> elbows =
            PlanWireRoute(snapshot, library, previous, point, from_pin, to_pin);
        polyline.insert(polyline.end(), elbows.begin(), elbows.end());
        polyline.push_back(point);
    }

}  // namespace

double DistanceToSegmentSquared(Point2D point, Point2D start, Point2D end) {
    const double dx = end.x - start.x;
    const double dy = end.y - start.y;
    const double length_squared = dx * dx + dy * dy;
    if (length_squared <= std::numeric_limits<double>::epsilon()) {
        const double ex = point.x - start.x;
        const double ey = point.y - start.y;
        return ex * ex + ey * ey;
    }
    const double parameter = std::clamp(
        ((point.x - start.x) * dx + (point.y - start.y) * dy) / length_squared, 0.0, 1.0);
    const double px = start.x + parameter * dx - point.x;
    const double py = start.y + parameter * dy - point.y;
    return px * px + py * py;
}

Point2D ClosestPointOnSegment(Point2D point, Point2D start, Point2D end) {
    const double dx = end.x - start.x;
    const double dy = end.y - start.y;
    const double length_squared = dx * dx + dy * dy;
    if (length_squared <= std::numeric_limits<double>::epsilon()) {
        return start;
    }
    const double parameter = std::clamp(
        ((point.x - start.x) * dx + (point.y - start.y) * dy) / length_squared, 0.0, 1.0);
    return {start.x + parameter * dx, start.y + parameter * dy};
}

Point2D ResolveWireEndpoint(const SchematicSnapshot& snapshot, const IComponentLibrary& library,
                            const WireEndpoint& endpoint) {
    if (!endpoint.pin.has_value()) {
        return endpoint.position;
    }
    const auto component_iterator =
        std::find_if(snapshot.components.begin(), snapshot.components.end(),
                     [&endpoint](const ComponentInstance& component) {
                         return component.id == endpoint.pin->component_id;
                     });
    if (component_iterator == snapshot.components.end()) {
        return endpoint.position;
    }
    const std::optional<ComponentDefinition> definition =
        library.FindByTypeId(component_iterator->type_id);
    if (!definition.has_value()) {
        return endpoint.position;
    }
    const auto pin_iterator =
        std::find_if(definition->pins.begin(), definition->pins.end(),
                     [&endpoint](const PinDefinition& pin) {
                         return pin.local_id == endpoint.pin->pin_id;
                     });
    if (pin_iterator == definition->pins.end()) {
        return endpoint.position;
    }
    return PinPosition(*component_iterator, *definition, *pin_iterator);
}

Point2D PinAbsolutePosition(const ComponentInstance& component,
                            const ComponentDefinition& definition, const PinDefinition& pin) {
    return PinPosition(component, definition, pin);
}

Rect2D RotatedComponentBounds(const ComponentDefinition& definition, double rotation_deg) {
    const Rect2D& bounds = definition.default_bounds;
    const Point2D corners[4] = {
        RotateRelative({0.0, 0.0}, bounds, rotation_deg),
        RotateRelative({bounds.width, 0.0}, bounds, rotation_deg),
        RotateRelative({bounds.width, bounds.height}, bounds, rotation_deg),
        RotateRelative({0.0, bounds.height}, bounds, rotation_deg),
    };

    double min_x = corners[0].x;
    double max_x = corners[0].x;
    double min_y = corners[0].y;
    double max_y = corners[0].y;
    for (const Point2D& corner : corners) {
        min_x = std::min(min_x, corner.x);
        max_x = std::max(max_x, corner.x);
        min_y = std::min(min_y, corner.y);
        max_y = std::max(max_y, corner.y);
    }
    return {min_x, min_y, max_x - min_x, max_y - min_y};
}

std::vector<Point2D> PlanWireRoute(const SchematicSnapshot& snapshot,
                                   const IComponentLibrary& library, Point2D from, Point2D to,
                                   const std::optional<PinRef>& from_pin,
                                   const std::optional<PinRef>& to_pin) {
    if (SamePoint(from, to)) {
        return {};
    }

    const std::vector<Point2D> obstacles =
        CollectObstacles(snapshot, library, from, to, from_pin, to_pin);
    const std::vector<Rect2D> bodies =
        CollectBodyObstacles(snapshot, library, from_pin, to_pin);
    std::vector<std::vector<Point2D>> candidates = RouteCandidates(from, to);
    // 与端点重合的拐点是零长度段（同行/同列时天然出现），先滤掉。
    for (std::vector<Point2D>& candidate : candidates) {
        candidate.erase(std::remove_if(candidate.begin(), candidate.end(),
                                       [from, to](Point2D point) {
                                           return SamePoint(point, from) ||
                                                  SamePoint(point, to);
                                       }),
                        candidate.end());
    }
    for (const std::vector<Point2D>& candidate : candidates) {
        if (!TouchesObstacle(BuildPolyline(from, candidate, to), obstacles, bodies)) {
            return candidate;
        }
    }
    // 每条走法都躲不开（画得实在太挤）：退回最自然的那条，至少保证是正交的。
    return candidates.front();
}

std::vector<Point2D> PlanWireSegment(const SchematicSnapshot& snapshot,
                                     const IComponentLibrary& library, Point2D anchor,
                                     Point2D target, const std::optional<PinRef>& from_pin,
                                     const std::optional<PinRef>& to_pin) {
    std::vector<Point2D> path;
    if (SamePoint(anchor, target)) {
        return path;  // 鼠标没挪动，没有路径点
    }
    path = PlanWireRoute(snapshot, library, anchor, target, from_pin, to_pin);
    path.push_back(target);
    return path;
}

std::vector<Point2D> WirePolyline(const SchematicSnapshot& snapshot,
                                  const IComponentLibrary& library, const WireModel& wire) {
    const Point2D from = ResolveWireEndpoint(snapshot, library, wire.from);
    const Point2D to = ResolveWireEndpoint(snapshot, library, wire.to);
    if (wire.route_points.empty()) {
        return BuildPolyline(from,
                             PlanWireRoute(snapshot, library, from, to, wire.from.pin, wire.to.pin),
                             to);
    }

    // 布线时定下的拐点原样保留；但元件挪动后端点变了，某一段可能变成斜线，
    // 逐段用同一套正交规划补拐点——画布和建网于是永远看到正交折线。
    std::vector<Point2D> polyline;
    polyline.reserve(wire.route_points.size() + 3);
    polyline.push_back(from);
    Point2D previous = from;
    for (const Point2D& point : wire.route_points) {
        AppendOrthogonalSegment(polyline, previous, point, snapshot, library, wire.from.pin,
                                wire.to.pin);
        previous = point;
    }
    AppendOrthogonalSegment(polyline, previous, to, snapshot, library, wire.from.pin, wire.to.pin);
    return polyline;
}

}  // namespace cse::model
