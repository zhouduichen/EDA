# 组合式自定义元件 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将用户选中的内置逻辑电路封装为可重用、可保存、可仿真的组合元件。

**Architecture:** 增加可序列化的组合模块数据和引脚映射；主图快照携带模块定义；元件库负责注册模块；编辑器支持 Shift 多选和封装替换；仿真器用内部输入/输出 IO 递归映射组合模块的行为。

**Tech Stack:** C++17、wxWidgets 3.x、CMake、CTest。

## Global Constraints

- 保持现有 CSEPROJ v1/v2 文件可读。
- 首版组合模块内部只允许内置元件，不支持嵌套组合模块。
- 保持现有 `0/1/X/Z` 仿真值语义。
- 创建失败时不修改电路和元件库。

---

### Task 1: 组合模块数据结构和元件库

**Files:**
- Modify: `contracts/component_model.hpp`
- Modify: `contracts/schematic_snapshot.hpp`
- Modify: `contracts/component_library.hpp`
- Modify: `src/component/component_library.hpp`
- Modify: `src/component/component_library.cpp`
- Test: `tests/unit/core_tests.cpp`

- [ ] 定义组合模块内部快照、输入输出映射和外部符号引脚。
- [ ] 让内置元件库注册、查找和导出自定义模块，并检测 type_id 冲突。
- [ ] 测试有效注册、重复注册、非法模块拒绝。

### Task 2: 仿真组合模块

**Files:**
- Modify: `src/sim/logic_simulator.cpp`
- Modify: `src/model/net_builder.cpp`（仅在网络定义解析需要时）
- Test: `tests/unit/core_tests.cpp`

- [ ] 使用外部实例输入引脚值构造内部 `io.input` 状态。
- [ ] 递归执行内部快照并读取内部 `io.output` 引脚值。
- [ ] 将结果驱动到组合元件外部输出引脚，沿用现有冲突合并。
- [ ] 用组合 XOR 模块测试 00/01/10/11 和未知输入传播。

### Task 3: 工程文件保存/恢复

**Files:**
- Modify: `src/io/project_serializer.cpp`
- Modify: `src/io/project_serializer.hpp`
- Test: `tests/unit/core_tests.cpp`

- [ ] 增加 CSEPROJ 3 模块记录，保存定义、端口映射、内部组件和导线。
- [ ] 加载模块记录后验证引用、ID 和嵌套限制，再验证顶层图。
- [ ] 保留 v1/v2 解析路径。
- [ ] 测试含组合 XOR 定义的完整工程往返与旧版本兼容。

### Task 4: 多选和封装操作

**Files:**
- Modify: `contracts/events.hpp`（若选择事件需要变化）
- Modify: `src/editor/editor_controller.hpp`
- Modify: `src/editor/editor_controller.cpp`
- Modify: `src/ui/schematic_canvas.cpp`
- Modify: `src/ui/main_frame.hpp`
- Modify: `src/ui/main_frame.cpp`
- Modify: `src/ui/component_library_panel.hpp`
- Modify: `src/ui/component_library_panel.cpp`
- Test: `tests/unit/core_tests.cpp`

- [ ] 增加 Shift 多选并保持普通点击单选行为。
- [ ] 加入创建组合元件菜单/快捷入口和输入名称对话框。
- [ ] 验证选区有输入输出 IO、连线没有跨出选区、内部无自定义模块。
- [ ] 创建定义后以新实例替换选区，并刷新元件库。
- [ ] 测试有效封装替换及跨界/缺端口失败时主图不变。

### Task 5: 绘制和端到端验证

**Files:**
- Modify: `src/ui/schematic_canvas.cpp`
- Modify: `README.md`

- [ ] 在画布绘制自定义元件符号、名称、位号和外部引脚。
- [ ] 构建 GUI 并运行 CTest。
- [ ] 检查 `git diff --check` 和更改范围。
