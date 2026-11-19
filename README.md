# LogicSim · 数字逻辑电路编辑器

类 Logisim 的数字逻辑电路绘制与仿真工具，C++20 + wxWidgets 3.2.2.1，运行于 Windows / MSVC x64。
功能说明、文件格式与设计约定见 [`Project1/README.md`](Project1/README.md)。

> **本仓库不包含可执行文件，也不包含 wxWidgets 库。**
> `git clone` 之后必须在本机构建一次。仓库里的路径已经全部改为自动探测，
> 不需要再改任何脚本；`.github/workflows/build.yml` 会在干净的 Windows 环境里
> 复现「拉下来 → 构建 → 跑测试」的完整流程。

---

## 为什么以前拉下来不能直接用

| 原因 | 现在的处理 |
| --- | --- |
| 编译产物（`.exe` / `.obj` / `.lib`）被 `.gitignore` 排除，仓库里没有可运行的程序 | 提供一键构建；CI 上传构建产物供下载，但分发版仍需自行构建 |
| 依赖 wxWidgets 3.2.2.1，体积大且安装路径因机器而异 | 用 `WXWIN` 环境变量 / `-DwxWidgets_ROOT_DIR` 指定，CMake 与 `env.sh` 自动定位 |
| `_check/*.sh` 与 `Project1.vcxproj` 曾硬编码 `F:\EDA\...`、`C:\Users\...`、MSVC / SDK 版本号 | 全部去除：VS 与 SDK 由 `vswhere` + Windows Kits 自动探测，wxWidgets 由 `WXWIN` 提供 |
| `FindwxWidgets` 在本机同时存在静态 debug 与 DLL release 时会选错库目录 | `CMakeLists.txt` 按构建类型显式固定 `wxWidgets_LIB_DIR` 与 `wxWidgets_CONFIGURATION` |
| 最小 wx 桩头 `stub/wx/string.h` 对引用参数调用 `va_start`，新版 MSVC 直接编译报错 | 改为把 `va_start` 放在纯指针参数上，公共 `Format` 用变参模板转发 |

---

## 环境要求

| 组件 | 版本 / 说明 |
| --- | --- |
| 操作系统 | Windows 10/11 x64 |
| 编译器 | Visual Studio 2022 / 2026，安装「使用 C++ 的桌面开发」工作负载（MSVC + Windows SDK） |
| CMake | 3.24+（Visual Studio 自带的可直接用） |
| wxWidgets | **3.2.2.1**，官方预编译 `vc14x_x64` 开发包（下方给出确切文件名） |
| Git Bash | 仅 `_check/*.sh` 脚本需要（Git for Windows 自带） |
| g++ | 仅 `netlist_check.sh` / Linux 纯逻辑测试需要，非必需 |

> 纯逻辑层（`wirenet.cpp` / `netlist.cpp` / `model.cpp`）可用 `_check/stub/wx` 桩头在 Linux + g++ 下编译，
> 但完整 GUI 程序依赖 wxMSW，只能在 Windows 上构建。

---

## 快速开始（CMake，推荐）

### 1. 准备 wxWidgets 3.2.2.1

从 [wxWidgets v3.2.2.1 Release](https://github.com/wxWidgets/wxWidgets/releases/tag/v3.2.2.1)
下载这三个文件（注意 Dev 包文件名是 `3.2.2`，不是 `3.2.2.1`）：

```
wxWidgets-3.2.2.1-headers.7z
wxMSW-3.2.2_vc14x_x64_Dev.7z
wxMSW-3.2.2_vc14x_x64_ReleaseDLL.7z
```

把它们**解压到同一个目录**，例如 `C:\wxWidgets-3.2.2.1`（解压后应存在
`include\wx\wx.h` 和 `lib\vc_x64_lib\mswud\wx\setup.h`），然后设置环境变量：

```bat
set WXWIN=C:\wxWidgets-3.2.2.1
```

不想设环境变量也可以显式传给 CMake：`-DwxWidgets_ROOT_DIR=C:/wxWidgets-3.2.2.1`。

### 2. 构建

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

默认构建类型是 **Debug**：官方预编译开发包在 `lib/vc_x64_lib/mswud` 中提供静态
debug 库（`*ud.lib`），因此整个工程统一用 `/MDd` 调试运行库，链接静态 wx，
运行时不需要额外下载 wx 的 DLL。`CMakeLists.txt` 会按构建类型把这个库目录固定下来，
避免 `FindwxWidgets` 在同时存在 DLL release 库时选错目录。

### 3. 跑测试 / 启动

```bat
ctest --test-dir build -C Debug --output-on-failure
build\Debug\LogicSim.exe
```

`ctest` 会跑 4 个测试：`wirenet`（导线几何 / 建网 / 拖动拓扑保护）、
`netlist`（gEDA 网表导出）、`keybind`（快捷键表）、`model`（数据模型 / 子电路仿真）。

### 4. 用 Ninja 也可以

在「x64 Native Tools Command Prompt」里：

```bat
cmake -S . -B build-ninja -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-ninja
ctest --test-dir build-ninja --output-on-failure
```

---

## 其它构建方式

### Visual Studio 工程

设置好 `WXWIN` 后打开 `Project1/Project1.slnx`，选择 `x64` + `Debug` 生成。
`Project1.vcxproj` 的 `WX_ROOT` 优先取 `$(WXWIN)`，未设置时回退到
`C:\wxWidgets-3.2.2.1`。

该工程默认平台工具集为 `v145`（较新的 VS）。若你的 VS 只有 `v143`，用命令行覆盖即可：

```bat
msbuild Project1\Project1.slnx /p:Configuration=Debug /p:Platform=x64 /p:PlatformToolset=v143
```

### `_check/build_exe.sh`（Git Bash）

直接调用 `cl.exe` / `rc.exe` / `link.exe` 的单脚本构建，同样自动探测工具链：

```bash
export WXWIN='C:\wxWidgets-3.2.2.1'
bash _check/build_exe.sh
```

产物在 `_check/build/LogicSim.exe`。若希望顺手复制一份到某个目录，在
`_check/env.local.sh`（该文件被 `.gitignore` 排除，不会入库）里写：

```bash
WX='C:\wxWidgets-3.2.2.1'
DESK='C:\Users\你的用户名\Desktop'
```

---

## 工具链覆盖（本机差异）

`_check/env.sh` 是 `_check/*.sh` 共用的探测脚本，按以下优先级取值：

1. 调用前已 `export` 的变量；
2. `_check/env.local.sh` 里的本机赋值（不入库）；
3. 自动探测：`vswhere` 找最新 MSVC、`Windows Kits\10\Include` 找最新 SDK、
   `WXWIN` 找 wxWidgets。

| 变量 | 含义 |
| --- | --- |
| `WX` | wxWidgets 根目录（一般由 `WXWIN` 推导） |
| `VS` | MSVC 工具集目录，如 `...\VC\Tools\MSVC\14.xx.xxxxx` |
| `KIT` / `SDKV` | Windows SDK 根目录与版本号 |
| `DESK` | 可选，`build_exe.sh` 额外复制 exe 的目标目录 |

`env.sh` 找不到 wxWidgets 或 MSVC 时会直接报错并给出提示，不会静默用旧路径构建。

---

## 测试与 QA

| 命令 | 覆盖内容 | 依赖 |
| --- | --- | --- |
| `ctest --test-dir build -C Debug --output-on-failure` | CMake 注册的 4 个单元测试（推荐） | MSVC + wxWidgets |
| `bash _check/wirenet_check.sh` | 导线几何 / 建网 / 拖动拓扑保护，148 项断言 | MSVC + wxWidgets |
| `bash _check/netlist_check.sh` | gEDA `.net` / `.pcb` 导出 | 任意 g++（不需要 wxWidgets） |
| `bash _check/model_check.sh` | 数据模型 / 子电路求值 | MSVC + wxBase |
| `bash _check/compile_check.sh` / `link_check.sh` | GUI 程序完整编译 / 链接检查 | MSVC + wxWidgets |
| `bash _check/keybind_check.sh` | 快捷键绑定一致性 | MSVC + wxWidgets |
| `bash _check/build_exe.sh` | 一键重建 exe | MSVC + wxWidgets |

Linux / macOS 上也可以只验证与 GUI 无关的核心逻辑：

```bash
g++ -std=c++17 -I_check/stub -IProject1 \
    _check/test_wirenet.cpp Project1/wirenet.cpp -o /tmp/test_wirenet && /tmp/test_wirenet
bash _check/netlist_check.sh
```

---

## CI

`.github/workflows/build.yml` 在 `push` / `pull_request` / 手动触发时运行两个任务：

- **windows-msvc**：`windows-latest` 上下载并解压官方 wxWidgets 预编译包 →
  CMake 配置 → 构建 → `ctest` → 上传 `LogicSim.exe` 构建产物；
- **core-tests-linux**：`ubuntu-latest` 上用 g++ + stub 头编译运行
  `test_wirenet` 与 `test_netlist`，保证纯逻辑层不依赖 Windows。

CI 不运行需要 GUI 交互的手工验证，也不宣称覆盖网表在真实 gEDA pcb 中的加载。

---

## 已知限制

- **Debug 产物依赖调试版运行库**（`ucrtd.dll` / `msvcrtd.dll`，随 Visual Studio 安装），
  因此当前构建结果适合开发机自用，**不是可直接分发的绿色版 exe**。
  若要发给没有装 VS 的用户，需要另做 Release 构建并处理运行库分发。
- 本机与 CI 使用的官方预编译包，静态库目录为 debug（`mswud`），所以默认（也是 CI 验证过的）配置是 Debug；其它安装方式若只提供 `mswu`，`CMakeLists.txt` 会回退到实际存在的配置，但不保证运行库组合一致。
- GUI 只支持 Windows + MSVC；核心逻辑层可跨平台编译但不含界面。
- 网表导出按 gEDA pcb 格式生成 `.net` + `.pcb`；本仓库环境未安装 gEDA pcb，
  未做真机加载验证。

---

## 目录结构

```
CMakeLists.txt              根构建入口（LogicSim + 4 个测试目标）
.github/workflows/build.yml 干净环境 CI
Project1/                   产品源码、VS 工程、功能说明 README
_check/                     构建脚本、单元测试、最小 wx 桩头、app.rc
  env.sh                     共享工具链自动探测
  env.local.sh               本机覆盖（不入库）
  build_exe.sh               一键构建 exe
  *_check.sh                 各项 QA
  stub/wx/                   无 wxWidgets 时编译核心逻辑用的桩头
```
