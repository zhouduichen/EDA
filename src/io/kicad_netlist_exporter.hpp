#pragma once

#include <string>

#include "contracts/component_library.hpp"
#include "contracts/result.hpp"
#include "contracts/schematic_snapshot.hpp"

namespace cse::io {

// 导出 KiCad 格式网表（s-expression，version "E"），
// 可在 KiCad PCB 编辑器里用「文件 → 导入 → 网表」加载，进入后续 PCB 设计流程。
// (value) 取元件显示名，(libsource (part)) 用 type_id；
// 网络名由 net_builder 生成，形如 Net-(U1-A)。
Result<void> ExportKiCadNetlist(const SchematicSnapshot& snapshot,
                                const IComponentLibrary& library, const std::string& path);

}  // namespace cse::io
