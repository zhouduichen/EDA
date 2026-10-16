// Release 下 NDEBUG 会把 assert 变成空操作，测试断言就全废了——
// 测试文件必须取消 NDEBUG，让断言在任何配置下都真正执行。
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <unordered_map>

#include "src/component/component_factory.hpp"
#include "src/io/kicad_netlist_exporter.hpp"
#include "src/io/project_serializer.hpp"
#include "src/component/component_library.hpp"
#include "src/editor/editor_controller.hpp"
#include "src/model/net_builder.hpp"
#include "src/model/schematic_model.hpp"
#include "src/model/wire_route.hpp"
#include "src/sim/logic_simulator.hpp"

namespace {

    void TestProjectSaveFormat() {
        cse::model::SchematicModel model;

        cse::ComponentInstance gate;
        gate.type_id = "logic.and";
        gate.reference = "U1";
        gate.position = { 100.0, 20.0 };
        gate.properties["input_count"] = "2";
        gate.properties["alpha"] = "1";   // 故意让 key 顺序和插入顺序相反
        assert(model.AddComponent(gate));

        const std::string path = "cse_test_project.cseproj";
        assert(cse::io::SaveProject(model.GetSnapshot(), path).ok);

        std::ifstream stream(path, std::ios::binary);
        assert(stream.is_open());

        std::string line;
        // 第 1 行：魔数 + 版本
        assert(std::getline(stream, line) && line == "CSEPROJ 1");
        // 第 2 行：元件。注意 alpha 排在 input_count 前面 —— 证明属性被排序了
        assert(std::getline(stream, line) &&
            line == "COMP 1 logic.and U1 100.000000 20.000000 0.000000 alpha=1 input_count=2");
        // 第 3 行：结束标记
        assert(std::getline(stream, line) && line == "END");
        stream.close();

        std::filesystem::remove(path);
    }

    void TestProjectPropertyEscapingAndValidation() {
        cse::SchematicSnapshot snapshot;
        cse::ComponentInstance input;
        input.id = 1;
        input.type_id = "io.input";
        input.reference = "IN1";
        input.position = {10.0, 20.0};
        input.properties["label"] = "Clock Input \"A\"\\main";
        input.properties["empty"] = "";
        snapshot.components.push_back(input);

        const std::string path = "cse_test_property_escape.cseproj";
        assert(cse::io::SaveProject(snapshot, path).ok);
        const cse::Result<cse::SchematicSnapshot> loaded = cse::io::LoadProject(path);
        assert(loaded.ok);
        assert(loaded.value.components.size() == 1);
        assert(loaded.value.components[0].properties.at("label") ==
               "Clock Input \"A\"\\main");
        assert(loaded.value.components[0].properties.at("empty").empty());
        std::filesystem::remove(path);

        const std::string missing_end_path = "cse_test_missing_end.cseproj";
        {
            std::ofstream stream(missing_end_path, std::ios::binary);
            assert(stream.is_open());
            stream << "CSEPROJ 1\nCOMP 1 io.input IN1 10.000000 20.000000 0.000000\n";
        }
        const cse::Result<cse::SchematicSnapshot> missing_end =
            cse::io::LoadProject(missing_end_path);
        assert(!missing_end.ok);
        assert(missing_end.code == cse::ErrorCode::kInvalidFileFormat);
        std::filesystem::remove(missing_end_path);

        const std::string duplicate_id_path = "cse_test_duplicate_id.cseproj";
        {
            std::ofstream stream(duplicate_id_path, std::ios::binary);
            assert(stream.is_open());
            stream << "CSEPROJ 1\n"
                   << "COMP 1 io.input IN1 10.000000 20.000000 0.000000\n"
                   << "COMP 1 io.output OUT1 100.000000 20.000000 0.000000\n"
                   << "END\n";
        }
        const cse::Result<cse::SchematicSnapshot> duplicate_id =
            cse::io::LoadProject(duplicate_id_path);
        assert(!duplicate_id.ok);
        assert(duplicate_id.code == cse::ErrorCode::kInvalidFileFormat);
        std::filesystem::remove(duplicate_id_path);
    }

    // ---- 测试用小工具：按 id/类型/位号/坐标造元件和导线 ----

    cse::ComponentInstance MakeComponent(cse::ComponentId id, const std::string& type_id,
                                         const std::string& reference, double x, double y) {
        cse::ComponentInstance component;
        component.id = id;
        component.type_id = type_id;
        component.reference = reference;
        component.position = { x, y };
        return component;
    }

    // from_comp/to_comp 为 0 表示该端点是自由端点（与工程文件格式约定一致）。
    cse::WireModel MakeWire(cse::WireId id, cse::ComponentId from_comp, cse::PinId from_pin,
                            cse::Point2D from_pos, cse::ComponentId to_comp, cse::PinId to_pin,
                            cse::Point2D to_pos) {
        cse::WireModel wire;
        wire.id = id;
        if (from_comp != 0) {
            wire.from.pin = cse::PinRef{ from_comp, from_pin };
        }
        wire.from.position = from_pos;
        if (to_comp != 0) {
            wire.to.pin = cse::PinRef{ to_comp, to_pin };
        }
        wire.to.position = to_pos;
        return wire;
    }

    // 引脚绝对坐标速查：io.input(50x30) OUT 在 (50,15)；io.output IN 在 (0,15)；
    // and/or/xor(70x50) A(0,15) B(0,35) Y(70,25)；not(70x50) A(0,25) Y(70,25)。旋转均为 0。

    void TestBuildNetsPinsAndTJunction() {
        cse::component::ComponentLibrary library;
        cse::SchematicSnapshot snapshot;
        snapshot.components.push_back(MakeComponent(1, "io.input", "IN1", 50.0, 85.0));    // OUT @ (100,100)
        snapshot.components.push_back(MakeComponent(2, "io.output", "OUT1", 150.0, 165.0)); // IN @ (150,180)
        snapshot.components.push_back(MakeComponent(3, "io.input", "IN2", 80.0, 125.0));   // OUT @ (130,140)
        snapshot.components.push_back(MakeComponent(4, "io.output", "OUT2", 120.0, 125.0)); // IN @ (170,140)

        // A：(100,100)→(200,100)，起点挂在 IN1.OUT
        snapshot.wires.push_back(MakeWire(1, 1, 1, { 100.0, 100.0 }, 0, 0, { 200.0, 100.0 }));
        // B：(150,100)→(150,180)，终点挂在 OUT1.IN；起点 T 接在 A 的中段
        snapshot.wires.push_back(MakeWire(2, 0, 0, { 150.0, 100.0 }, 2, 1, { 150.0, 180.0 }));
        // C：(130,140)→(170,140)，十字交叉穿过 B 的中段 —— 按约定不连接
        snapshot.wires.push_back(MakeWire(3, 3, 1, { 130.0, 140.0 }, 4, 1, { 170.0, 140.0 }));

        const cse::model::BuiltNetlist netlist = cse::model::BuildNets(snapshot, library);
        assert(netlist.nets.size() == 2);
        // 网络 1：IN1.OUT — A — T 接点 — B — OUT1.IN
        assert(netlist.nets[0].name == "Net-(IN1-OUT)");
        assert(netlist.nets[0].pins.size() == 2);
        assert(netlist.nets[0].pins[0].component_id == 1 && netlist.nets[0].pins[0].pin_id == 1);
        assert(netlist.nets[0].pins[1].component_id == 2 && netlist.nets[0].pins[1].pin_id == 1);
        // 网络 2：C 自己连的 IN2.OUT — OUT2.IN
        assert(netlist.nets[1].name == "Net-(IN2-OUT)");
        assert(netlist.nets[1].pins.size() == 2);
        assert(netlist.nets[1].pins[0].component_id == 3);
        assert(netlist.nets[1].pins[1].component_id == 4);
    }

    void TestBuildNetsSharedPin() {
        cse::component::ComponentLibrary library;
        cse::SchematicSnapshot snapshot;
        snapshot.components.push_back(MakeComponent(1, "io.input", "IN1", 0.0, 0.0));     // OUT @ (50,15)
        snapshot.components.push_back(MakeComponent(2, "io.output", "OUT1", 300.0, 0.0)); // IN @ (300,15)
        snapshot.components.push_back(MakeComponent(3, "io.output", "OUT2", 300.0, 60.0)); // IN @ (300,75)

        // 两条导线从同一个引脚 IN1.OUT 出发，接到不同的输出 —— 三个引脚同一网络
        snapshot.wires.push_back(MakeWire(1, 1, 1, { 50.0, 15.0 }, 2, 1, { 300.0, 15.0 }));
        snapshot.wires.push_back(MakeWire(2, 1, 1, { 50.0, 15.0 }, 3, 1, { 300.0, 75.0 }));

        const cse::model::BuiltNetlist netlist = cse::model::BuildNets(snapshot, library);
        assert(netlist.nets.size() == 1);
        assert(netlist.nets[0].pins.size() == 3);
        assert(netlist.nets[0].pins[0].component_id == 1);
        assert(netlist.nets[0].pins[1].component_id == 2);
        assert(netlist.nets[0].pins[2].component_id == 3);
    }

    void TestSimulationHalfAdder() {
        cse::component::ComponentLibrary library;
        cse::SchematicSnapshot snapshot;
        // 和任务书第 8 页 Logisim 半加器同构：XOR 求和、AND 求进位
        snapshot.components.push_back(MakeComponent(1, "io.input", "A", 0.0, 0.0));     // OUT @ (50,15)
        snapshot.components.push_back(MakeComponent(2, "io.input", "B", 0.0, 100.0));   // OUT @ (50,115)
        snapshot.components.push_back(MakeComponent(3, "logic.xor", "X1", 200.0, 0.0));   // A(200,15) B(200,35) Y(270,25)
        snapshot.components.push_back(MakeComponent(4, "logic.and", "U1", 200.0, 100.0)); // A(200,115) B(200,135) Y(270,125)
        snapshot.components.push_back(MakeComponent(5, "io.output", "S", 350.0, 10.0));   // IN @ (350,25)
        snapshot.components.push_back(MakeComponent(6, "io.output", "C", 350.0, 110.0));  // IN @ (350,125)

        snapshot.wires.push_back(MakeWire(1, 1, 1, { 50.0, 15.0 }, 3, 1, { 200.0, 15.0 }));
        snapshot.wires.push_back(MakeWire(2, 2, 1, { 50.0, 115.0 }, 3, 2, { 200.0, 35.0 }));
        snapshot.wires.push_back(MakeWire(3, 3, 3, { 270.0, 25.0 }, 5, 1, { 350.0, 25.0 }));
        snapshot.wires.push_back(MakeWire(4, 1, 1, { 50.0, 15.0 }, 4, 1, { 200.0, 115.0 }));
        snapshot.wires.push_back(MakeWire(5, 2, 1, { 50.0, 115.0 }, 4, 2, { 200.0, 135.0 }));
        snapshot.wires.push_back(MakeWire(6, 4, 3, { 270.0, 125.0 }, 6, 1, { 350.0, 125.0 }));

        std::unordered_map<cse::ComponentId, cse::LogicValue> states{
            { 1, cse::LogicValue::kHigh }, { 2, cse::LogicValue::kHigh } };
        const cse::sim::SimulationResult result = cse::sim::RunSimulation(snapshot, library, states);
        assert(result.stable);
        assert(result.pin_values.at(cse::PinKey(5, 1)) == cse::LogicValue::kLow);   // 1+1 → 和 0
        assert(result.pin_values.at(cse::PinKey(6, 1)) == cse::LogicValue::kHigh);  // 1+1 → 进位 1
        // 可视化用：每条导线都有电平 —— A/B 支路是 1，XOR.Y→S 支路是 0
        assert(result.wire_values.size() == 6);
        assert(result.wire_values.at(1) == cse::LogicValue::kHigh);
        assert(result.wire_values.at(3) == cse::LogicValue::kLow);
        assert(result.wire_values.at(6) == cse::LogicValue::kHigh);

        states[2] = cse::LogicValue::kLow;
        const cse::sim::SimulationResult second = cse::sim::RunSimulation(snapshot, library, states);
        assert(second.stable);
        assert(second.pin_values.at(cse::PinKey(5, 1)) == cse::LogicValue::kHigh);  // 1+0 → 和 1
        assert(second.pin_values.at(cse::PinKey(6, 1)) == cse::LogicValue::kLow);   // 1+0 → 进位 0
    }

    void TestSimulationUnknownPropagation() {
        cse::component::ComponentLibrary library;
        cse::SchematicSnapshot snapshot;
        // AND 门一个输入悬空：输出必须保持未知，不能猜成 0 或 1
        snapshot.components.push_back(MakeComponent(1, "io.input", "IN1", 0.0, 0.0));    // OUT @ (50,15)
        snapshot.components.push_back(MakeComponent(2, "logic.and", "U1", 100.0, 0.0));  // A(100,15) B(100,35) Y(170,25)
        snapshot.components.push_back(MakeComponent(3, "io.output", "OUT1", 300.0, 10.0)); // IN @ (300,25)

        snapshot.wires.push_back(MakeWire(1, 1, 1, { 50.0, 15.0 }, 2, 1, { 100.0, 15.0 }));
        snapshot.wires.push_back(MakeWire(2, 2, 3, { 170.0, 25.0 }, 3, 1, { 300.0, 25.0 }));

        std::unordered_map<cse::ComponentId, cse::LogicValue> states{
            { 1, cse::LogicValue::kHigh } };
        const cse::sim::SimulationResult result = cse::sim::RunSimulation(snapshot, library, states);
        assert(result.stable);
        assert(result.pin_values.at(cse::PinKey(3, 1)) == cse::LogicValue::kUnknown);
    }

    void TestSimulationOrNotGates() {
        cse::component::ComponentLibrary library;
        cse::SchematicSnapshot snapshot;
        snapshot.components.push_back(MakeComponent(1, "io.input", "A", 0.0, 0.0));      // OUT (50,15)
        snapshot.components.push_back(MakeComponent(2, "io.input", "B", 0.0, 100.0));    // OUT (50,115)
        snapshot.components.push_back(MakeComponent(3, "logic.or", "U1", 150.0, 0.0));   // A(150,15) B(150,35) Y(220,25)
        snapshot.components.push_back(MakeComponent(4, "logic.not", "U2", 300.0, 0.0));  // A(300,25) Y(370,25)
        snapshot.components.push_back(MakeComponent(5, "io.output", "OUT1", 450.0, 10.0)); // IN (450,25)

        snapshot.wires.push_back(MakeWire(1, 1, 1, { 50.0, 15.0 }, 3, 1, { 150.0, 15.0 }));
        snapshot.wires.push_back(MakeWire(2, 2, 1, { 50.0, 115.0 }, 3, 2, { 150.0, 35.0 }));
        snapshot.wires.push_back(MakeWire(3, 3, 3, { 220.0, 25.0 }, 4, 1, { 300.0, 25.0 }));
        snapshot.wires.push_back(MakeWire(4, 4, 2, { 370.0, 25.0 }, 5, 1, { 450.0, 25.0 }));

        std::unordered_map<cse::ComponentId, cse::LogicValue> states{
            { 1, cse::LogicValue::kHigh }, { 2, cse::LogicValue::kLow } };
        const cse::sim::SimulationResult on = cse::sim::RunSimulation(snapshot, library, states);
        assert(on.stable);
        assert(on.pin_values.at(cse::PinKey(5, 1)) == cse::LogicValue::kLow);   // 1|0=1 → 取非 0

        states[1] = cse::LogicValue::kLow;
        const cse::sim::SimulationResult off = cse::sim::RunSimulation(snapshot, library, states);
        assert(off.stable);
        assert(off.pin_values.at(cse::PinKey(5, 1)) == cse::LogicValue::kHigh); // 0|0=0 → 取非 1
    }

    void TestSimulationConflictAndDefaultLow() {
        cse::component::ComponentLibrary library;
        cse::SchematicSnapshot snapshot;
        snapshot.components.push_back(MakeComponent(1, "io.input", "IN1", 0.0, 0.0));    // OUT (50,15)
        snapshot.components.push_back(MakeComponent(2, "io.input", "IN2", 0.0, 100.0));  // OUT (50,115)
        snapshot.components.push_back(MakeComponent(3, "io.output", "OUT1", 300.0, 10.0)); // IN (300,25)

        // 两个输入都接到同一个自由端点 (150,15)，再连到输出 —— 同一网络
        snapshot.wires.push_back(MakeWire(1, 1, 1, { 50.0, 15.0 }, 0, 0, { 150.0, 15.0 }));
        snapshot.wires.push_back(MakeWire(2, 2, 1, { 50.0, 115.0 }, 0, 0, { 150.0, 15.0 }));
        snapshot.wires.push_back(MakeWire(3, 0, 0, { 150.0, 15.0 }, 3, 1, { 300.0, 25.0 }));

        // 一个说 1 一个说 0 → 冲突 → X
        std::unordered_map<cse::ComponentId, cse::LogicValue> states{
            { 1, cse::LogicValue::kHigh }, { 2, cse::LogicValue::kLow } };
        const cse::sim::SimulationResult conflict = cse::sim::RunSimulation(snapshot, library, states);
        assert(conflict.stable);
        assert(conflict.pin_values.at(cse::PinKey(3, 1)) == cse::LogicValue::kUnknown);

        // 不给激励时 io.input 默认输出 kLow → 一致 → 0
        const cse::sim::SimulationResult defaulted = cse::sim::RunSimulation(snapshot, library);
        assert(defaulted.stable);
        assert(defaulted.pin_values.at(cse::PinKey(3, 1)) == cse::LogicValue::kLow);
    }

    // 走线必须只走水平/垂直；而且自动拐点不能正好压在别的引脚上——
    // 半加器里 AND 就摆在 XOR 正下方，拐点随手取 (to.x, from.y) 的话
    // 会正好压在 AND.A 上，把 A、B 两条支路并成一个网络。
    void TestWireRouteIsOrthogonalAndKeepsNetsApart() {
        cse::component::ComponentLibrary library;
        cse::SchematicSnapshot snapshot;
        // 与演示工程 demo_half_adder.cseproj 同一套坐标
        snapshot.components.push_back(MakeComponent(1, "io.input", "IN1", 0.0, 0.0));
        snapshot.components.push_back(MakeComponent(2, "io.input", "IN2", 0.0, 100.0));
        snapshot.components.push_back(MakeComponent(3, "logic.xor", "X1", 200.0, 0.0));
        snapshot.components.push_back(MakeComponent(4, "logic.and", "U1", 200.0, 100.0));
        snapshot.components.push_back(MakeComponent(5, "io.output", "OUT1", 350.0, 10.0));
        snapshot.components.push_back(MakeComponent(6, "io.output", "OUT2", 350.0, 110.0));

        snapshot.wires.push_back(MakeWire(1, 1, 1, { 50.0, 15.0 }, 3, 1, { 200.0, 15.0 }));
        snapshot.wires.push_back(MakeWire(2, 2, 1, { 50.0, 115.0 }, 3, 2, { 200.0, 35.0 }));
        snapshot.wires.push_back(MakeWire(3, 3, 3, { 270.0, 25.0 }, 5, 1, { 350.0, 25.0 }));
        snapshot.wires.push_back(MakeWire(4, 1, 1, { 50.0, 15.0 }, 4, 1, { 200.0, 115.0 }));
        snapshot.wires.push_back(MakeWire(5, 2, 1, { 50.0, 115.0 }, 4, 2, { 200.0, 135.0 }));
        snapshot.wires.push_back(MakeWire(6, 4, 3, { 270.0, 125.0 }, 6, 1, { 350.0, 125.0 }));

        for (const cse::WireModel& wire : snapshot.wires) {
            const std::vector<cse::Point2D> points =
                cse::model::WirePolyline(snapshot, library, wire);
            assert(points.size() >= 2);
            for (std::size_t index = 1; index < points.size(); ++index) {
                const bool horizontal =
                    std::abs(points[index].y - points[index - 1].y) < 0.5;
                const bool vertical = std::abs(points[index].x - points[index - 1].x) < 0.5;
                assert(horizontal || vertical);   // 不允许出现斜线段
            }
        }

        // 半加器是 4 个独立网络（IN1 支路 / IN2 支路 / 和 / 进位），拐点压在 AND.A 上就会并成 2 个
        const cse::model::BuiltNetlist netlist = cse::model::BuildNets(snapshot, library);
        assert(netlist.nets.size() == 4);
        assert(netlist.nets[0].name == "Net-(IN1-OUT)");
        assert(netlist.nets[0].pins.size() == 3);   // IN1.OUT + X1.A + U1.A
        assert(netlist.nets[1].name == "Net-(IN2-OUT)");
        assert(netlist.nets[1].pins.size() == 3);   // IN2.OUT + X1.B + U1.B
    }

    // 交互布线的一跳：空图上和纯几何一致（横向偏移大先横后竖），
    // 有障碍时自动拐点必须绕开别的引脚。
    void TestPlanWireSegment() {
        cse::component::ComponentLibrary library;
        const cse::SchematicSnapshot empty;
        const std::optional<cse::PinRef> no_pin;

        // 横向偏移更大 → 先横后竖
        const std::vector<cse::Point2D> horizontal_first = cse::model::PlanWireSegment(
            empty, library, {0.0, 0.0}, {100.0, 40.0}, no_pin, no_pin);
        assert(horizontal_first.size() == 2);
        assert(horizontal_first[0].x == 100.0 && horizontal_first[0].y == 0.0);
        assert(horizontal_first[1].x == 100.0 && horizontal_first[1].y == 40.0);

        // 纵向偏移更大 → 先竖后横
        const std::vector<cse::Point2D> vertical_first = cse::model::PlanWireSegment(
            empty, library, {0.0, 0.0}, {40.0, 100.0}, no_pin, no_pin);
        assert(vertical_first.size() == 2);
        assert(vertical_first[0].x == 0.0 && vertical_first[0].y == 100.0);
        assert(vertical_first[1].x == 40.0 && vertical_first[1].y == 100.0);

        // 同一行/同一列：本来就不用拐
        const std::vector<cse::Point2D> straight =
            cse::model::PlanWireSegment(empty, library, {0.0, 0.0}, {80.0, 0.0}, no_pin, no_pin);
        assert(straight.size() == 1);
        assert(straight[0].x == 80.0 && straight[0].y == 0.0);

        // 鼠标没挪动 → 没有路径点
        assert(cse::model::PlanWireSegment(empty, library, {10.0, 10.0}, {10.0, 10.0}, no_pin,
                                           no_pin)
                   .empty());

        // 障碍避让：横向优先的拐点 (200,0) 正好压在别处一个引脚上 → 换先竖后横
        cse::SchematicSnapshot snapshot;
        snapshot.components.push_back(MakeComponent(1, "io.output", "OUT1", 200.0, -15.0)); // IN (200,0)
        const std::vector<cse::Point2D> avoided = cse::model::PlanWireSegment(
            snapshot, library, {0.0, 0.0}, {200.0, 100.0}, no_pin, no_pin);
        assert(avoided.size() == 2);
        assert(avoided[0].x == 0.0 && avoided[0].y == 100.0);
        assert(avoided[1].x == 200.0 && avoided[1].y == 100.0);
    }

    void TestEditorWireWithBends() {
        cse::model::SchematicModel model;
        cse::component::ComponentLibrary library;
        cse::component::ComponentFactory factory(library);
        cse::editor::EditorController editor(model, library, factory);

        editor.SetPlacementType("io.input");
        assert(editor.PlaceComponent({0.0, 0.0}));
        const cse::ComponentId input_id = editor.GetSelection().component_ids.front();
        editor.SetPlacementType("io.output");
        assert(editor.PlaceComponent({300.0, 100.0}));
        const cse::ComponentId output_id = editor.GetSelection().component_ids.front();

        // 带两个拐点接过去
        editor.SetToolMode(cse::ToolMode::kWire);
        assert(editor.BeginWire({input_id, 1}, {50.0, 15.0}));
        assert(editor.AddWireBend({150.0, 15.0}));
        assert(editor.AddWireBend({150.0, 115.0}));
        assert(editor.AddWireBend({150.0, 115.0}) == false);  // 和上一个拐点重合，不加
        assert(editor.GetPendingWire().has_value());
        assert(editor.GetPendingWire()->route_points.size() == 2);
        assert(editor.RemoveLastWireBend());
        assert(editor.GetPendingWire()->route_points.size() == 1);
        assert(editor.AddWireBend({150.0, 115.0}));
        assert(editor.CompleteWire({output_id, 1}, {300.0, 115.0}));
        assert(!editor.GetPendingWire().has_value());

        const cse::SchematicSnapshot snapshot = editor.GetSnapshot();
        assert(snapshot.wires.size() == 1);
        assert(snapshot.wires[0].route_points.size() == 2);
        assert(snapshot.wires[0].route_points[0].x == 150.0);
        assert(snapshot.wires[0].route_points[0].y == 15.0);
        assert(snapshot.wires[0].route_points[1].x == 150.0);
        assert(snapshot.wires[0].route_points[1].y == 115.0);
        assert(snapshot.wires[0].to.pin.has_value());
        assert(snapshot.wires[0].to.pin->component_id == output_id);

        // 整条折线是一条撤销命令，重做后拐点原样回来
        assert(editor.Undo());
        assert(editor.GetSnapshot().wires.empty());
        assert(editor.Redo());
        assert(editor.GetSnapshot().wires.size() == 1);
        assert(editor.GetSnapshot().wires[0].route_points.size() == 2);

        // 终点落在最后一个拐点上 → 拐点升级成终点，不产生零长度段
        editor.SetToolMode(cse::ToolMode::kWire);
        assert(editor.BeginWire({output_id, 1}, {300.0, 115.0}));
        assert(editor.AddWireBend({250.0, 115.0}));
        assert(editor.CompleteWireAtPoint({250.0, 115.0}));
        const cse::SchematicSnapshot second = editor.GetSnapshot();
        assert(second.wires.size() == 2);
        assert(second.wires[1].route_points.empty());
        assert(!second.wires[1].to.pin.has_value());
        assert(second.wires[1].to.position.x == 250.0);
        assert(second.wires[1].to.position.y == 115.0);

        // 回车以最后一个拐点收尾
        editor.SetToolMode(cse::ToolMode::kWire);
        assert(editor.BeginWire({output_id, 1}, {300.0, 115.0}));
        assert(editor.AddWireBend({280.0, 60.0}));
        assert(editor.AddWireBend({200.0, 60.0}));
        assert(editor.FinishWireAtLastBend());
        const cse::SchematicSnapshot third = editor.GetSnapshot();
        assert(third.wires.size() == 3);
        assert(third.wires[2].route_points.size() == 1);
        assert(third.wires[2].route_points[0].x == 280.0);
        assert(third.wires[2].route_points[0].y == 60.0);
        assert(third.wires[2].to.position.x == 200.0);
        assert(third.wires[2].to.position.y == 60.0);

        // 只有拐点的空折线、以及起止重合的退化导线都不生成
        editor.SetToolMode(cse::ToolMode::kWire);
        assert(editor.BeginWire({input_id, 1}, {50.0, 15.0}));
        assert(editor.FinishWireAtLastBend() == false);
        assert(editor.CompleteWireAtPoint({50.0, 15.0}) == false);
        editor.CancelWire();
        assert(editor.GetSnapshot().wires.size() == 3);

        // 撤销会一并取消正在画的线：不然半成品线挂在刚被撤掉的元件上，
        // 画布上只能靠 Esc 才清得掉
        editor.SetToolMode(cse::ToolMode::kWire);
        assert(editor.BeginWire({input_id, 1}, {50.0, 15.0}));
        assert(editor.AddWireBend({150.0, 15.0}));
        assert(editor.Undo());
        assert(!editor.GetPendingWire().has_value());
        assert(editor.GetSnapshot().wires.size() == 2);
    }

    // 拐点随导线一起存盘（CSEPROJ 2 的 ROUTE 段），老格式照常能打开。
    void TestProjectRoutePointPersistence() {
        cse::SchematicSnapshot snapshot;
        cse::WireModel wire;
        wire.id = 3;
        wire.from.pin = cse::PinRef{1, 1};
        wire.from.position = {50.0, 15.0};
        wire.to.position = {300.0, 115.0};
        wire.route_points = {{150.0, 15.0}, {150.0, 115.0}};
        snapshot.wires.push_back(wire);

        const std::string path = "cse_test_route_points.cseproj";
        assert(cse::io::SaveProject(snapshot, path).ok);

        std::ifstream stream(path, std::ios::binary);
        assert(stream.is_open());
        std::string line;
        assert(std::getline(stream, line) && line == "CSEPROJ 2");
        assert(std::getline(stream, line) &&
            line == "WIRE 3 1 1 50.000000 15.000000 0 0 300.000000 115.000000 "
                    "ROUTE 2 150.000000 15.000000 150.000000 115.000000");
        assert(std::getline(stream, line) && line == "END");
        stream.close();

        const cse::Result<cse::SchematicSnapshot> loaded = cse::io::LoadProject(path);
        assert(loaded.ok);
        assert(loaded.value.wires.size() == 1);
        assert(loaded.value.wires[0].route_points.size() == 2);
        assert(loaded.value.wires[0].route_points[0].x == 150.0);
        assert(loaded.value.wires[0].route_points[1].y == 115.0);
        assert(loaded.value.wires[0].from.pin.has_value());
        assert(!loaded.value.wires[0].to.pin.has_value());
        std::filesystem::remove(path);

        // 老工程（v1、没有 ROUTE 段）照常能打开
        const std::string legacy_path = "cse_test_legacy_project.cseproj";
        {
            std::ofstream legacy(legacy_path, std::ios::binary);
            assert(legacy.is_open());
            legacy << "CSEPROJ 1\n"
                   << "WIRE 4 1 1 50.000000 15.000000 0 0 300.000000 115.000000\n"
                   << "END\n";
        }
        const cse::Result<cse::SchematicSnapshot> legacy_loaded = cse::io::LoadProject(legacy_path);
        assert(legacy_loaded.ok);
        assert(legacy_loaded.value.wires.size() == 1);
        assert(legacy_loaded.value.wires[0].route_points.empty());
        std::filesystem::remove(legacy_path);
    }

    // 折线导线中段被 T 形接上，也要算同一个网络。
    void TestBuildNetsBentWireTJunction() {
        cse::component::ComponentLibrary library;
        cse::SchematicSnapshot snapshot;
        snapshot.components.push_back(MakeComponent(1, "io.input", "IN1", 50.0, 85.0));      // OUT (100,100)
        snapshot.components.push_back(MakeComponent(2, "io.output", "OUT1", 150.0, 165.0));  // IN (150,180)

        // 折线 A：(100,100) → 拐点 (200,100) → (200,180)，起点挂在 IN1.OUT 上
        cse::WireModel wire = MakeWire(1, 1, 1, {100.0, 100.0}, 0, 0, {200.0, 180.0});
        wire.route_points = {{200.0, 100.0}};
        snapshot.wires.push_back(wire);
        // 折线 B：从 OUT1.IN 出发，终点落在 A 竖直段的中点 (200,140)
        snapshot.wires.push_back(MakeWire(2, 2, 1, {150.0, 180.0}, 0, 0, {200.0, 140.0}));

        const cse::model::BuiltNetlist netlist = cse::model::BuildNets(snapshot, library);
        assert(netlist.nets.size() == 1);
        assert(netlist.nets[0].pins.size() == 2);
        assert(netlist.nets[0].pins[0].component_id == 1);
        assert(netlist.nets[0].pins[1].component_id == 2);
        assert(netlist.wire_nets.size() == 2);
    }

    // 元件挪动后端点变了，存下来的拐点可能出现斜线段：折线必须自动补直角。
    void TestWirePolylineRepairsAfterMove() {
        cse::component::ComponentLibrary library;
        cse::SchematicSnapshot snapshot;
        snapshot.components.push_back(MakeComponent(1, "io.input", "IN1", 0.0, 0.0));  // OUT (50,15)

        cse::WireModel wire = MakeWire(1, 1, 1, {50.0, 15.0}, 0, 0, {300.0, 215.0});
        wire.route_points = {{150.0, 15.0}, {150.0, 115.0}};  // 布线时定下的拐点
        snapshot.wires.push_back(wire);

        // 终点相当于被往下挪了 100：最后一段 (150,115)→(300,215) 是斜线，要补拐点
        const std::vector<cse::Point2D> points = cse::model::WirePolyline(snapshot, library, wire);
        assert(points.size() == 5);
        assert(points[0].x == 50.0 && points[0].y == 15.0);
        assert(points[1].x == 150.0 && points[1].y == 15.0);
        assert(points[2].x == 150.0 && points[2].y == 115.0);
        assert(points[3].x == 300.0 && points[3].y == 115.0);
        assert(points[4].x == 300.0 && points[4].y == 215.0);
        for (std::size_t index = 1; index < points.size(); ++index) {
            const bool horizontal = std::abs(points[index].y - points[index - 1].y) < 0.5;
            const bool vertical = std::abs(points[index].x - points[index - 1].x) < 0.5;
            assert(horizontal || vertical);
        }
    }

    void TestLoadSnapshotIntegration() {
        cse::model::SchematicModel model;
        cse::component::ComponentLibrary library;
        cse::component::ComponentFactory factory(library);
        cse::editor::EditorController editor(model, library, factory);

        editor.SetPlacementType("io.input");
        assert(editor.PlaceComponent({ 10.0, 20.0 }));
        const std::string path = "cse_test_loadsnap.cseproj";
        assert(cse::io::SaveProject(editor.GetSnapshot(), path).ok);

        // 故意把图改脏（多放一个门），再从文件整体加载回 1 个元件的状态
        editor.SetPlacementType("logic.and");
        assert(editor.PlaceComponent({ 100.0, 20.0 }));
        assert(editor.GetSnapshot().components.size() == 2);
        assert(editor.CanUndo());

        const cse::Result<cse::SchematicSnapshot> loaded = cse::io::LoadProject(path);
        assert(loaded.ok);
        editor.LoadSnapshot(loaded.value);
        assert(editor.GetSnapshot().components.size() == 1);
        assert(editor.GetSnapshot().components.front().reference == "IN1");
        assert(!editor.CanUndo() && !editor.CanRedo());  // 加载后历史作废

        // 加载后继续放置，ID 和位号必须从当前工程接着分配，而不是回到 1。
        editor.SetPlacementType("io.input");
        assert(editor.PlaceComponent({200.0, 20.0}));
        const cse::ComponentInstance placed =
            editor.GetComponent(editor.GetSelection().component_ids.front()).value();
        assert(placed.id == 2);
        assert(placed.reference == "IN2");
        std::filesystem::remove(path);
    }

    void TestKiCadNetlistExport() {
        cse::component::ComponentLibrary library;
        cse::SchematicSnapshot snapshot;
        snapshot.components.push_back(MakeComponent(1, "io.input", "IN1", 0.0, 0.0));    // OUT @ (50,15)
        snapshot.components.push_back(MakeComponent(2, "logic.not", "U1", 100.0, 0.0));  // A(100,25) Y(170,25)
        snapshot.components.push_back(MakeComponent(3, "io.output", "OUT1", 300.0, 10.0)); // IN @ (300,25)

        snapshot.wires.push_back(MakeWire(1, 1, 1, { 50.0, 15.0 }, 2, 1, { 100.0, 25.0 }));
        snapshot.wires.push_back(MakeWire(2, 2, 2, { 170.0, 25.0 }, 3, 1, { 300.0, 25.0 }));

        const std::string path = "cse_test_netlist.net";
        assert(cse::io::ExportKiCadNetlist(snapshot, library, path).ok);

        std::ifstream stream(path, std::ios::binary);
        assert(stream.is_open());
        const std::string content((std::istreambuf_iterator<char>(stream)),
                                  std::istreambuf_iterator<char>());
        stream.close();
        std::filesystem::remove(path);

        assert(content.find("(export (version \"E\")") == 0);
        assert(content.find("(comp (ref \"IN1\")") != std::string::npos);
        assert(content.find("(value \"Input\")") != std::string::npos);
        assert(content.find("(name \"Net-(IN1-OUT)\")") != std::string::npos);
        assert(content.find("(node (ref \"U1\") (pin \"1\") (pinfunction \"A\") (pintype \"input\"))") !=
               std::string::npos);
        assert(content.find("(node (ref \"OUT1\") (pin \"1\") (pinfunction \"IN\") (pintype \"input\"))") !=
               std::string::npos);
    }

    void TestProjectSaveLoadRoundtrip() {
        cse::SchematicSnapshot snapshot;

        cse::ComponentInstance gate;
        gate.id = 1;
        gate.type_id = "logic.and";
        gate.reference = "U1";
        gate.position = { 100.0, 20.0 };
        gate.rotation_deg = 90.0;
        gate.properties["input_count"] = "2";
        snapshot.components.push_back(gate);

        cse::WireModel wire;
        wire.id = 7;
        wire.from.pin = cse::PinRef{ 1, 0 };   // 起点连在 U1 的 0 号引脚上
        wire.from.position = { 100.0, 20.0 };
        wire.to.position = { 180.0, 60.0 };    // 终点是自由端点，没连引脚
        snapshot.wires.push_back(wire);

        const std::string path = "cse_test_roundtrip.cseproj";
        assert(cse::io::SaveProject(snapshot, path).ok);

        const cse::Result<cse::SchematicSnapshot> loaded = cse::io::LoadProject(path);
        assert(loaded.ok);
        assert(loaded.value.components.size() == 1);
        assert(loaded.value.components[0].id == 1);
        assert(loaded.value.components[0].type_id == "logic.and");
        assert(loaded.value.components[0].reference == "U1");
        assert(loaded.value.components[0].position.x == 100.0);
        assert(loaded.value.components[0].rotation_deg == 90.0);
        assert(loaded.value.components[0].properties.at("input_count") == "2");
        assert(loaded.value.wires.size() == 1);
        assert(loaded.value.wires[0].id == 7);
        assert(loaded.value.wires[0].from.pin.has_value());
        assert(loaded.value.wires[0].from.pin->component_id == 1);
        assert(loaded.value.wires[0].from.pin->pin_id == 0);
        assert(loaded.value.wires[0].from.position.x == 100.0);
        assert(!loaded.value.wires[0].to.pin.has_value());
        assert(loaded.value.wires[0].to.position.y == 60.0);

        std::filesystem::remove(path);
    }

cse::ComponentDefinition MakeCustomDefinition() {
    cse::ComponentDefinition definition;
    definition.type_id = "custom.buffer";
    definition.display_name = "Buffer";
    definition.category = "custom";
    definition.default_bounds = {0.0, 0.0, 60.0, 40.0};
    definition.pins = {
        {1, "IN", cse::PinDirection::kInput, {0.0, 20.0}},
        {2, "OUT", cse::PinDirection::kOutput, {60.0, 20.0}},
    };
    return definition;
}

void TestComponentLibraryAndFactory() {
    cse::component::ComponentLibrary library;
    assert(library.GetAll().size() == 6);
    assert(library.FindByTypeId("logic.and").has_value());
    assert(library.FindByTypeId("missing.type").has_value() == false);

    const cse::Result<void> registration = library.Register(MakeCustomDefinition());
    assert(registration.ok);
    assert(library.Register(MakeCustomDefinition()).ok == false);

    cse::component::ComponentFactory factory(library);
    const cse::Result<cse::ComponentInstance> input = factory.Create("io.input", {10.0, 20.0});
    const cse::Result<cse::ComponentInstance> gate = factory.Create("logic.and", {100.0, 20.0});
    assert(input.ok && gate.ok);
    assert(input.value.id == 1);
    assert(gate.value.id == 2);
    assert(input.value.reference == "IN1");
    assert(gate.value.reference == "U1");
    assert(gate.value.properties.at("input_count") == "2");
    assert(factory.Create("unknown.type", {0.0, 0.0}).ok == false);
}

void TestModelCrudAndRevision() {
    cse::model::SchematicModel model;
    cse::ComponentInstance component;
    component.type_id = "logic.and";
    component.reference = "U1";
    assert(model.AddComponent(component));
    assert(model.GetRevision() == 1);
    const cse::ComponentId component_id = model.GetSnapshot().components.front().id;
    assert(model.SetComponentPosition(component_id, {40.0, 50.0}));
    assert(model.SetComponentRotation(component_id, 90.0));
    assert(model.GetRevision() == 3);

    cse::WireModel wire;
    wire.from.pin = cse::PinRef{component_id, 1};
    wire.from.position = {40.0, 65.0};
    wire.to.position = {140.0, 65.0};
    assert(model.AddWire(wire));
    assert(model.GetSnapshot().wires.size() == 1);

    cse::model::ComponentRemoval removal;
    assert(model.RemoveComponentAndWires(component_id, &removal));
    assert(model.GetSnapshot().components.empty());
    assert(model.GetSnapshot().wires.empty());
    assert(model.RestoreComponent(removal));
    assert(model.GetSnapshot().components.size() == 1);
    assert(model.GetSnapshot().wires.size() == 1);
}

void TestEditorCommandsAndWireWorkflow() {
    cse::model::SchematicModel model;
    cse::component::ComponentLibrary library;
    cse::component::ComponentFactory factory(library);
    cse::editor::EditorController editor(model, library, factory);

    editor.SetPlacementType("io.input");
    assert(editor.PlaceComponent({12.0, 17.0}));
    const cse::ComponentId input_id = editor.GetSelection().component_ids.front();
    assert(editor.GetComponent(input_id)->position.x == 10.0);
    assert(editor.GetComponent(input_id)->position.y == 20.0);

    editor.SetPlacementType("logic.and");
    assert(editor.PlaceComponent({100.0, 20.0}));
    const cse::ComponentId gate_id = editor.GetSelection().component_ids.front();
    assert(editor.MoveComponent(gate_id, {103.0, 58.0}));
    assert(editor.GetComponent(gate_id)->position.y == 60.0);
    assert(editor.Undo());
    assert(editor.GetComponent(gate_id)->position.y == 20.0);
    assert(editor.Redo());
    assert(editor.GetComponent(gate_id)->position.y == 60.0);
    assert(editor.RotateSelected());
    assert(editor.GetComponent(gate_id)->rotation_deg == 90.0);

    editor.SetToolMode(cse::ToolMode::kWire);
    assert(editor.BeginWire({input_id, 1}, {60.0, 35.0}));
    assert(editor.CompleteWire({gate_id, 1}, {100.0, 75.0}));
    assert(editor.GetSnapshot().wires.size() == 1);
    assert(editor.Undo());
    assert(editor.GetSnapshot().wires.empty());
    assert(editor.Redo());
    assert(editor.GetSnapshot().wires.size() == 1);

    editor.SelectComponent(gate_id);
    assert(editor.DeleteSelection());
    assert(editor.GetSnapshot().components.size() == 1);
    assert(editor.GetSnapshot().wires.empty());
    assert(editor.Undo());
    assert(editor.GetSnapshot().components.size() == 2);
    assert(editor.GetSnapshot().wires.size() == 1);
}

}  // namespace

int main() {
    TestComponentLibraryAndFactory();
    TestModelCrudAndRevision();
    TestEditorCommandsAndWireWorkflow();
    TestProjectSaveFormat();          // ← 加这一行
    TestProjectPropertyEscapingAndValidation();
    TestProjectSaveLoadRoundtrip();
    TestBuildNetsPinsAndTJunction();
    TestBuildNetsSharedPin();
    TestSimulationHalfAdder();
    TestSimulationUnknownPropagation();
    TestSimulationOrNotGates();
    TestSimulationConflictAndDefaultLow();
    TestWireRouteIsOrthogonalAndKeepsNetsApart();
    TestPlanWireSegment();
    TestEditorWireWithBends();
    TestProjectRoutePointPersistence();
    TestBuildNetsBentWireTJunction();
    TestWirePolylineRepairsAfterMove();
    TestLoadSnapshotIntegration();
    TestKiCadNetlistExport();
    return 0;
}
