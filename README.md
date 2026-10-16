# Circuit Schematic Editor

基于 C++17 和 wxWidgets 的电路原理图编辑器 UI 垂直切片，参考任务书和 Logisim 的界面排布，遵循 `CSE_Baseline_Spec_v1.0.0` 的模块边界与公共 DTO 协议。

## 已实现

- 菜单栏、工具栏、状态栏和三栏工作区（左元件库、中点阵画布、右属性表）；
- 元件库分类树，内置 AND、OR、NOT、XOR、Input、Output；
- `ComponentLibrary::Register()` 用户自定义元件注册接口；
- 元件放置、选择、网格吸附移动（拖动时元件跟着鼠标走）、方向键微调、旋转、删除；
- 引脚连线：默认正交走线，途中可打拐点、可接到既有导线中段（T 形相接）；
  自动拐点会避让其它引脚，拐点随工程文件保存；
- AND、OR、XOR、NOT 逻辑门图形和输入/输出图形；
- 撤销/重做；
- 工程新建、打开/保存、KiCad 网表导出；
- AND、OR、XOR、NOT 组合逻辑仿真，支持未知值和多驱动冲突显示；
- C++ 核心单元测试。

## 构建运行

需要安装 wxWidgets 3.x、CMake 和 C++17 编译器。

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/cse_editor
```

macOS 如果生成了应用包，可运行 `build/cse_editor.app/Contents/MacOS/cse_editor`。

## 操作方式

- 双击左侧元件库条目，然后在画布点击：放置元件；
- `S`：选择；`W`：连线；`P`：平移；`R`：旋转；
- `Delete`：删除；`Esc`：取消连线；
- `Ctrl+Z` / `Ctrl+Y`：撤销 / 重做；
- 鼠标滚轮：以光标为中心缩放；
- 选择元件后拖动：元件实时跟着鼠标走（保持按下时的抓取点，不会跳到光标下），
  位置吸附到 `10.0` 网格，连着它的导线一起跟着动；松手才写进模型（一次拖动 = 一步撤销）；
  选中后方向键一次挪一格；
- 连线：`W` 后从引脚起笔，途中点击空白处打拐点，点引脚 / 既有导线中段（T 形相接）收尾；
  双击空白或 `Enter` 就地收尾，`Backspace` 退掉最后一个拐点；
  自动拐点由 `model::PlanWireRoute` 挑一条不碰到其它引脚的直角走法。

## 模块边界

`contracts` 保存公共 DTO/接口；`component` 保存元件定义和工厂；`model` 保存 wxWidgets 无关的原理图数据
（含 `wire_route` 的正交走线规划，画布、命中测试和建网共用同一份折线）；`editor` 通过 Command 管理编辑行为；
`ui` 只负责 wxWidgets 输入转换和绘制。

本版本未实现：已有导线上拐点的拖动调整（拐点在布线时确定）。
