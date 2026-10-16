# 编辑器与逻辑仿真缺陷修复 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 修复当前 wxWidgets 原理图编辑器的编译、工程加载、元件编号、新建菜单和仿真窗口刷新问题，并用核心测试验证。

**Architecture:** 保持现有 contracts/component/editor/model/io/sim/ui 分层。通过工厂同步钩子让 `EditorController::LoadSnapshot` 统一重置生成器状态；工程格式继续使用当前 CSEPROJ 文本格式，仅增加兼容的属性值 tokenizer 和严格校验；仿真 UI 通过模型变更回调刷新控件，仿真核心算法不改。

**Tech Stack:** C++17、wxWidgets 3.x、CMake、CTest、现有 C++ 核心单元测试。

## Global Constraints

- 不引入第三方库。
- 保持 C++17 和 wxWidgets 3.x。
- 保留旧版 CSEPROJ 1/2 文件的读取兼容性。
- 不改变现有逻辑仿真的 `0/1/X/Z` 语义。
- 不实现未保存确认和当前文件路径管理。

---

### Task 1: 修复工厂同步和新建工程

**Files:**
- Modify: `contracts/component_factory.hpp`
- Modify: `src/component/component_factory.hpp`
- Modify: `src/component/component_factory.cpp`
- Modify: `src/editor/editor_controller.cpp`
- Modify: `src/ui/main_frame.cpp`
- Test: `tests/unit/core_tests.cpp`

**Interfaces:**
- Produces `IComponentFactory::SyncWithSnapshot(const SchematicSnapshot&)`，默认空实现。
- Produces `ComponentFactory::SyncWithSnapshot(...)`，同步下一个组件 ID、位号计数器和已用位号。

- [ ] **Step 1: Add a regression test for loading then placing.**

在 `TestLoadSnapshotIntegration()` 加载 `IN1` 后继续放置 `io.input`，断言新元件 ID 为 2、位号为 `IN2`。

- [ ] **Step 2: Run the focused test and verify the current behavior fails.**

Run: `cmake --build build -j && ./build/cse_core_tests`

Expected: the new assertion fails because the factory still generates the pre-load ID/reference.

- [ ] **Step 3: Implement snapshot synchronization.**

在接口加入默认同步钩子；具体工厂扫描快照中的最大组件 ID、按定义前缀解析最大数字后缀，保存已使用位号；`Create()` 避免生成已使用位号。`LoadSnapshot()` 完成模型替换后调用同步；`OnNew()` 调用空快照加载并切换到选择工具。

- [ ] **Step 4: Run the focused test and verify it passes.**

Run: `cmake --build build -j && ./build/cse_core_tests`

Expected: PASS。

### Task 2: 修复工程文件属性解析和严格校验

**Files:**
- Modify: `src/io/project_serializer.cpp`
- Modify: `src/io/project_serializer.hpp`
- Test: `tests/unit/core_tests.cpp`

**Interfaces:**
- Keeps `SaveProject(...)` and `LoadProject(...)` signatures unchanged.
- Adds internal property tokenization and snapshot validation only.

- [ ] **Step 1: Add regression tests.**

测试带有 `label = "Clock Input\\A"` 等价属性值的保存/加载；测试缺少 `END` 和重复组件 ID 时返回 `kInvalidFileFormat`。

- [ ] **Step 2: Run the focused test and verify the new cases fail.**

Run: `cmake --build build -j && ./build/cse_core_tests`

Expected: the quoted property does not round-trip correctly, and malformed files are currently accepted.

- [ ] **Step 3: Implement compatible parsing and validation.**

保存时仅对需要的属性值加引号并转义；加载时支持旧无引号 token 和新引号 token。要求读到 `END`，检查记录 ID 非零且不重复、必填字符串非空、坐标/旋转/拐点为有限数，并拒绝零长度导线。

- [ ] **Step 4: Run serializer tests.**

Run: `cmake --build build -j && ./build/cse_core_tests`

Expected: PASS，旧版无引号属性和 v1 工程仍可加载。

### Task 3: 修复 GUI 编译错误并刷新仿真窗口

**Files:**
- Modify: `src/ui/simulation_dialog.hpp`
- Modify: `src/ui/simulation_dialog.cpp`
- Modify: `src/ui/main_frame.cpp`
- Test: `cmake --build build -j`

**Interfaces:**
- Adds public `SimulationDialog::RefreshCircuit()`.
- Existing `Run()` and `RunSimulation(...)` signatures remain unchanged.

- [ ] **Step 1: Fix the string conversion.**

将 `ComponentLabel()` 中的 `std::string + L"..."` 改成显式 `wxString` 构造。

- [ ] **Step 2: Rebuild to verify the compiler blocker is gone.**

Run: `cmake --build build -j`

Expected: `cse_editor` and `cse_core_tests` both build successfully.

- [ ] **Step 3: Implement circuit refresh.**

保存当前输入复选框的 `ComponentId -> bool` 状态，销毁旧内容，用当前快照重建输入/输出控件，恢复仍存在的输入状态并调用 `Run()`；在主窗口模型变更回调中调用 `RefreshCircuit()`。

- [ ] **Step 4: Run full validation.**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure`

Expected: build succeeds and all tests pass。

### Task 4: Final review

**Files:**
- Modify: `README.md` only if the implemented behavior changes the documented “未实现” list.

- [ ] **Step 1: Inspect the diff.**

Run: `git diff --check && git diff --stat`

Expected: no whitespace errors and only the scoped files are changed。

- [ ] **Step 2: Verify repository state.**

Run: `git status -sb`

Expected: intended source/test/doc changes are visible and no generated build artifacts are staged。
