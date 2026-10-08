#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

#include <unordered_map>

#include "src/component/component_factory.hpp"
#include "src/io/kicad_netlist_exporter.hpp"
#include "src/io/project_serializer.hpp"
#include "src/component/component_library.hpp"
#include "src/editor/editor_controller.hpp"
#include "src/model/net_builder.hpp"
#include "src/model/schematic_model.hpp"
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
    TestProjectSaveLoadRoundtrip();
    TestBuildNetsPinsAndTJunction();
    TestBuildNetsSharedPin();
    TestSimulationHalfAdder();
    TestSimulationUnknownPropagation();
    TestSimulationOrNotGates();
    TestSimulationConflictAndDefaultLow();
    TestLoadSnapshotIntegration();
    TestKiCadNetlistExport();
    return 0;
}
