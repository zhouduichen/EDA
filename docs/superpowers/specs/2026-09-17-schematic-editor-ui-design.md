# 原理图编辑器用户界面与编辑核心设计

## 目标

基于 CSE-Baseline-Spec v1.0.0，实现一个可运行的 C++17 / wxWidgets 电路原理图编辑器垂直切片，覆盖用户界面、预定义元件库、用户自定义元件注册、元件放置、选择、移动、删除、旋转、网格吸附、引脚连线以及撤销/重做。

## 用户请求与基线规范的边界

用户请求是本次交付范围：

- 设计原理图编辑器 GUI，提供菜单、工具栏和快捷操作；
- 元件库包含预定义电子元件，并支持用户自定义元件；
- 绘图与编辑支持元件放置、移动、连线等操作；
- 使用 wxWidgets 实现，并提供 C++ 文件。

基线文档是工程约束，不额外扩大本次功能范围：

- 遵循 UI → Editor → Model / Component → Contracts 的单向依赖；
- UI 不直接修改模型，所有编辑修改通过 Command；
- 跨模块传递 ID、DTO、Snapshot、Event、Result 或 Interface；
- 核心模型只使用逻辑坐标和标准库类型，不暴露 wxWidgets 类型；
- 公共 ID、DTO、枚举、文件格式和服务接口不擅自修改；
- 遵循 `cse` 命名空间、PascalCase 类/函数、snake_case 文件/局部变量和成员后缀 `_`。

本次明确不实现工程文件保存/加载、网表导出和数字仿真算法，但为后续模块保留 Snapshot、Query 和组件库接口边界。

## 方案选择

### 方案 A：单文件 wxWidgets 原型

实现速度最快，但 UI、模型和编辑逻辑混杂，不能满足基线的模块边界和可测试性要求。

### 方案 B：分层模块化垂直切片（采用）

以 `contracts` 定义稳定 DTO 与接口，以 `component` 提供元件定义，以 `model` 保存数据，以 `editor` 统一命令和编辑状态，以 `ui` 负责 wxWidgets 展示和输入转换。该方案能够直接运行，也能在后续接入文件、网表和仿真服务。

### 方案 C：一次完成完整工程、网表和仿真

能力最完整，但超出本次 UI/元件库/绘图编辑任务，会引入不必要的跨模块依赖和验收风险。

## 模块设计

### Contracts

冻结并复用基线中的 `ComponentId`、`WireId`、`PinId`、`Point2D`、`Rect2D`、`Direction`、`PinDirection`、`ComponentDefinition`、`ComponentInstance`、`PinRef`、`WireEndpoint`、`WireModel`、`SchematicSnapshot`、`ICommand`、`IEditorQuery`、`IComponentLibrary` 与编辑事件。

### Component

`ComponentLibrary` 保存内置定义及用户注册定义；`ComponentFactory` 根据稳定 `type_id` 创建实例、生成唯一 ID 和 Reference Designator，并初始化默认属性。内置类型包括 `logic.and`、`logic.or`、`logic.not`、`logic.xor`、`io.input`、`io.output`。用户自定义元件只需提供合法的 `ComponentDefinition`，不依赖 UI。

### Model

`SchematicModel` 保存组件和导线，负责唯一 ID 分配、增删、移动/旋转更新、快照生成和 revision 递增。模型不引用 wxWidgets。

### Editor

`EditorController` 管理 ToolMode、SelectionState、当前放置类型、网格吸附、Command 历史和查询接口。命令包含添加、删除、移动、旋转和添加导线。成功修改后发布 `ModelChangedEvent`，选择变化后发布 `SelectionChangedEvent`。

### UI

`MainFrame` 装配菜单、工具栏、左侧元件库、中央画布、右侧属性面板和状态栏。`SchematicCanvas` 将鼠标/键盘输入转换为逻辑坐标及编辑器调用，并负责绘制网格、元件、引脚、导线、选择框和临时连线。UI 只持有 Editor Facade、Component Library 和 DTO，不直接修改模型。

## 交互设计

- 菜单：文件（新建、退出）、编辑（撤销、重做、删除）、工具（选择、放置、连线、平移）、视图（放大、缩小、适应窗口）。
- 工具栏：选择、连线、删除、旋转、撤销、重做，并显示当前工具状态。
- 元件库树：按分类展示元件；双击条目进入放置模式。
- 画布左键：选择/放置/连线；拖动选中组件执行移动；右键取消当前工具。
- 快捷键：`S` 选择，`W` 连线，`P` 平移，`R` 旋转，`Delete` 删除，`Esc` 取消，`Ctrl+Z` 撤销，`Ctrl+Y` 重做。
- 逻辑坐标原点位于画布左上角，`+x` 向右、`+y` 向下，默认网格大小为 `10.0`。
- 属性面板展示单选组件的 type_id、reference、position、rotation 和 properties；本次不直接编辑属性。

## 数据流

```text
wxWidgets event
  -> UI converts to Point2D / ToolMode / ID
  -> EditorController
  -> ICommand::Execute / Undo
  -> SchematicModel
  -> ModelChangedEvent / SelectionChangedEvent
  -> Canvas, property panel, status bar refresh
```

## 错误处理

- 未注册的 `type_id` 不允许创建或放置；UI 在状态栏显示可读提示。
- 端点缺失、同一点连线或无效组件/引脚不创建导线。
- 空选择时删除、旋转和属性刷新为安全无操作。
- 普通业务失败使用 `Result` 或 `bool`，不使用异常作为跨模块控制流。
- 组件、导线和 Command ID 从 `1` 开始，`0` 为无效值，生命周期内不复用。

## 验证标准

- CMake 能发现 wxWidgets 并编译 C++17 工程；
- 元件库单元测试覆盖内置定义、查询和自定义注册；
- 模型/编辑器测试覆盖放置、移动、旋转、删除、连线和 Undo/Redo；
- 人工运行能够搭建半加器布局：A/B 输入、XOR、AND、SUM/CARRY 输出；
- UI 与核心模块无反向依赖，公共接口未被擅自破坏；
- README 说明构建、运行和快捷键。
