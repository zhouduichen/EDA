# Circuit Schematic Editor

基于 C++17 和 wxWidgets 的电路原理图编辑器 UI 垂直切片，参考任务书和 Logisim 的界面排布，遵循 `CSE_Baseline_Spec_v1.0.0` 的模块边界与公共 DTO 协议。

## 已实现

- 菜单栏、工具栏、状态栏和三栏工作区（左元件库、中点阵画布、右属性表）；
- 元件库分类树，内置 AND、OR、NOT、XOR、Input、Output；
- `ComponentLibrary::Register()` 用户自定义元件注册接口；
- 元件放置、选择、网格吸附移动、旋转、删除；
- 引脚连线、临时连线预览、导线选择；
- AND、OR、XOR、NOT 逻辑门图形和输入/输出图形；
- 撤销/重做；
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
- 选择元件后拖动：移动并自动吸附到 `10.0` 网格。

## 模块边界

`contracts` 保存公共 DTO/接口；`component` 保存元件定义和工厂；`model` 保存 wxWidgets 无关的原理图数据；`editor` 通过 Command 管理编辑行为；`ui` 只负责 wxWidgets 输入转换和绘制。

本版本暂未实现 `.cseproj` 保存/加载、网表导出和数字逻辑仿真。
