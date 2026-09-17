# Schematic Editor UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a runnable C++17 / wxWidgets schematic editor vertical slice with a baseline-compliant UI, component library, and drawing/editing workflow.

**Architecture:** Keep stable value types and interfaces in `contracts/`; implement component definitions/factory in `src/component/`, the wx-free schematic model in `src/model/`, and command-driven editor behavior in `src/editor/`. The wxWidgets layer in `src/ui/` converts events to logical coordinates and calls the editor controller; it never edits the model directly.

**Tech Stack:** C++17, wxWidgets 3.x, CMake, standard library, assert-based unit test executable.

## Global Constraints

- Use `C++17 + wxWidgets`.
- Keep the dependency direction `ui → editor → model/component → contracts`.
- UI must not directly modify `SchematicModel`.
- All editing mutations must be expressed as `ICommand` implementations.
- Core public interfaces must not contain `wxFrame`, `wxPanel`, `wxPoint`, `wxRect`, `wxString`, `wxDC`, or `wxGraphicsContext`.
- Use `Point2D`, `Rect2D`, `ComponentId`, `WireId`, `PinId`, `SchematicSnapshot`, `Result`, `ToolMode`, and `SelectionState` according to the baseline.
- Use `10.0` as the single default grid size.
- Use `.hpp` headers and the `cse` namespace with baseline naming rules.
- Do not implement project persistence, netlist export, or simulation in this task.

---

### Task 1: Freeze the contracts needed by the vertical slice

**Files:**
- Create: `contracts/ids.hpp`
- Create: `contracts/geometry.hpp`
- Create: `contracts/logic_value.hpp`
- Create: `contracts/result.hpp`
- Create: `contracts/component_model.hpp`
- Create: `contracts/wire_model.hpp`
- Create: `contracts/schematic_snapshot.hpp`
- Create: `contracts/command.hpp`
- Create: `contracts/editor_query.hpp`
- Create: `contracts/component_library.hpp`
- Create: `contracts/component_factory.hpp`
- Create: `contracts/events.hpp`

**Interfaces:**
- Produce the baseline ID aliases, geometry/value enums, component/wire/snapshot DTOs, `ICommand`, `IEditorQuery`, `IComponentLibrary`, `IComponentFactory`, and event DTOs.
- Use `std::optional`, `std::vector`, `std::unordered_map`, and `std::string`; do not include wxWidgets.

- [x] **Step 1: Add exact baseline value types and DTO fields.**
- [x] **Step 2: Add the stable command/query/library/factory interfaces.**
- [x] **Step 3: Compile a header-only smoke target with `c++ -std=c++17`.**

Expected: all headers compile without wxWidgets or project-internal implementation headers.

### Task 2: Implement the model and component services

**Files:**
- Create: `src/model/schematic_model.hpp`
- Create: `src/model/schematic_model.cpp`
- Create: `src/component/component_library.hpp`
- Create: `src/component/component_library.cpp`
- Create: `src/component/component_factory.hpp`
- Create: `src/component/component_factory.cpp`

**Interfaces:**
- `cse::model::SchematicModel` provides component/wire CRUD, movement/rotation, snapshot generation, pin/wire queries, and revision tracking.
- `cse::component::ComponentLibrary` implements `IComponentLibrary` and adds `Register()`.
- `cse::component::ComponentFactory` implements `IComponentFactory::Create()`.

- [x] **Step 1: Implement the model with monotonic IDs, revision increments, and no wxWidgets dependency.**
- [x] **Step 2: Register built-ins `logic.and`, `logic.or`, `logic.not`, `logic.xor`, `io.input`, and `io.output`.**
- [x] **Step 3: Implement custom-definition validation and registration.**
- [x] **Step 4: Implement factory-created IDs, reference prefixes, and default properties.**
- [x] **Step 5: Build model/component unit tests for lookup, custom registration, revision, and CRUD.**

Expected: model/component tests pass and the implementation remains usable without a GUI.

### Task 3: Implement command-driven editor behavior

**Files:**
- Create: `src/editor/editor_commands.hpp`
- Create: `src/editor/editor_commands.cpp`
- Create: `src/editor/editor_controller.hpp`
- Create: `src/editor/editor_controller.cpp`

**Interfaces:**
- `EditorController` manages `ToolMode`, `SelectionState`, placement type, grid snapping, wire interaction state, undo/redo, `IEditorQuery`, and typed event callbacks.
- Commands provide `AddComponentCommand`, `MoveComponentCommand`, `RotateComponentCommand`, `AddWireCommand`, and `DeleteSelectionCommand` behind `ICommand`.

- [x] **Step 1: Implement commands with `Execute()`/`Undo()` and stable command IDs.**
- [x] **Step 2: Implement controller command history and clear redo history after a new edit.**
- [x] **Step 3: Implement placement, hit-independent selection, move, rotate, delete, and wire completion APIs.**
- [x] **Step 4: Emit `ModelChangedEvent` and `SelectionChangedEvent` callbacks only after successful state changes.**
- [x] **Step 5: Add editor unit tests for execute/undo/redo, grid snapping, wire validation, and selection.**

Expected: editor tests pass without creating a wxWidgets object.

### Task 4: Build the wxWidgets UI shell and canvas editor

**Files:**
- Create: `src/ui/main_frame.hpp`
- Create: `src/ui/main_frame.cpp`
- Create: `src/ui/schematic_canvas.hpp`
- Create: `src/ui/schematic_canvas.cpp`
- Create: `src/ui/component_library_panel.hpp`
- Create: `src/ui/component_library_panel.cpp`
- Create: `src/ui/properties_panel.hpp`
- Create: `src/ui/properties_panel.cpp`
- Create: `src/app/main.cpp`

**Interfaces:**
- `MainFrame` owns the UI widgets and references the editor/library services assembled in `main.cpp`.
- `SchematicCanvas` uses logical-to-screen conversion, grid rendering, component/pin/wire hit testing, and editor calls.
- `ComponentLibraryPanel` displays `IComponentLibrary::GetAll()` and starts placement on double-click.
- `PropertiesPanel` displays a selected component snapshot without writing it.

- [x] **Step 1: Add menus, toolbar, status bar, splitter-based three-pane layout, and semantic widget names.**
- [x] **Step 2: Add canvas rendering for grid, components, pins, wires, selection, and temporary wire.**
- [x] **Step 3: Bind mouse/keyboard shortcuts `S`, `W`, `P`, `R`, `Delete`, `Esc`, `Ctrl+Z`, and `Ctrl+Y`.**
- [x] **Step 4: Connect editor callbacks to canvas/properties/status refresh.**
- [x] **Step 5: Assemble services in `main.cpp` without global state or singleton objects.**

Expected: launching the executable shows the editor and allows the half-adder placement/connection workflow.

### Task 5: Add build configuration, tests, and usage documentation

**Files:**
- Create: `CMakeLists.txt`
- Create: `README.md`
- Create: `tests/unit/core_tests.cpp`

- [x] **Step 1: Define a `cse_core` library target and a `cse_editor` wxWidgets executable target.**
- [x] **Step 2: Define a `cse_core_tests` executable linked only to core sources.**
- [x] **Step 3: Document wxWidgets prerequisites, build commands, shortcuts, and known scope limits.**
- [x] **Step 4: Configure and build with CMake; run `ctest --output-on-failure`.**
- [x] **Step 5: If wxWidgets is unavailable, still run core tests and report the UI build prerequisite explicitly.**

Expected: core tests pass; UI target builds when `wx-config`/CMake wxWidgets discovery is available; no new public contract changes occur after Task 1.
