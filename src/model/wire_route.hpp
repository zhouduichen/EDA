#pragma once

#include <optional>
#include <vector>

#include "contracts/component_library.hpp"
#include "contracts/geometry.hpp"
#include "contracts/schematic_snapshot.hpp"
#include "contracts/wire_model.hpp"

namespace cse::model {

// 点到线段的距离平方。画布命中测试、建网的 T 形相接判断、走线避让共用这一份，
// 三处算出来的几何必须一致，否则"看到的线"和"连上的线"会对不上。
double DistanceToSegmentSquared(Point2D point, Point2D start, Point2D end);

// 点到线段的投影点（投影出界时钳到端点上）：把新导线吸到既有导线中段时用。
Point2D ClosestPointOnSegment(Point2D point, Point2D start, Point2D end);

// 把导线端点解析成绝对坐标：挂引脚时按引脚定义 + 元件旋转算，否则用存下来的自由坐标。
Point2D ResolveWireEndpoint(const SchematicSnapshot& snapshot, const IComponentLibrary& library,
                            const WireEndpoint& endpoint);

// 单个引脚在画布坐标系中的绝对位置（含元件旋转）。画布、建网和导出共用同一套公式，
// 避免"看到的引脚"和"导出文件里的引脚"落在不同位置。
Point2D PinAbsolutePosition(const ComponentInstance& component,
                            const ComponentDefinition& definition, const PinDefinition& pin);

// 元件旋转后的外接矩形，坐标相对元件 position。导出 KiCad 符号时用来画符号外形。
Rect2D RotatedComponentBounds(const ComponentDefinition& definition, double rotation_deg);

// 规划一条只走水平/垂直方向的正交走线，返回不含两端点的中间拐点（空 = 两点已经对齐，
// 直线本身就是正交的）。
// 为什么不是"算个拐点就完事"：两个引脚不一定同列同行，随便挑一个拐点很容易正好压在
// 旁边元件的引脚上——那样画出来像是连上了，建网时却会把两条不相干的网络并到一起。
// 所以这里按"越自然越靠前"的顺序试几种走法，挑第一条不碰任何其它引脚/导线端点的。
std::vector<Point2D> PlanWireRoute(const SchematicSnapshot& snapshot,
                                   const IComponentLibrary& library, Point2D from, Point2D to,
                                   const std::optional<PinRef>& from_pin,
                                   const std::optional<PinRef>& to_pin);

// 一条导线的完整折线：两端点 + 中间拐点。route_points 为空时按上面的规则现算；
// 存了拐点的逐段检查，元件挪动后出现的斜线段按同一套规则补直角，
// 保证画布绘制、命中测试和建网看到的永远是正交折线，而且是同一条。
std::vector<Point2D> WirePolyline(const SchematicSnapshot& snapshot,
                                  const IComponentLibrary& library, const WireModel& wire);

// 交互布线的一跳：从 anchor 画到 target，返回不含 anchor、含 target 的路径点。
// 只走正交，自动拐点同样避让其它引脚；anchor == target 时返回空。
std::vector<Point2D> PlanWireSegment(const SchematicSnapshot& snapshot,
                                     const IComponentLibrary& library, Point2D anchor,
                                     Point2D target, const std::optional<PinRef>& from_pin,
                                     const std::optional<PinRef>& to_pin);

}  // namespace cse::model
