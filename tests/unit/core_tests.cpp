#include <cassert>
#include <string>

#include "src/component/component_factory.hpp"
#include "src/component/component_library.hpp"
#include "src/editor/editor_controller.hpp"
#include "src/model/schematic_model.hpp"

namespace {

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
    return 0;
}
