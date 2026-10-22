#pragma once

#include <string>

#include "contracts/component_library.hpp"
#include "contracts/result.hpp"
#include "contracts/schematic_snapshot.hpp"

namespace cse::io {

// 导出为 KiCad 6+ 可直接打开的 .kicad_sch 原理图。
//
// 为什么不是 .net：现代 KiCad（6/7/8/9）已经取消独立网表导入，PCB 侧改为
// “Tools -> Update PCB from Schematic”。因此要让现代 PCB 软件直接加载，
// 需要输出自包含的原理图文件。Altium 的 KiCad Importer 可以导入 .kicad_sch；
// 立创EDA需经其格式转换助手，版本兼容性以实际软件为准。
//
// 符号定义内嵌在 lib_symbols 中（不依赖外部符号库），导线、T 形接点和
// 元件旋转都按画布实际坐标导出。元件带占位封装，打开后可在 KiCad 里改成
// 实际封装，再用 F8 同步到 PCB。
Result<void> ExportKiCadSchematic(const SchematicSnapshot& snapshot,
                                  const IComponentLibrary& library,
                                  const std::string& path);

}  // namespace cse::io
