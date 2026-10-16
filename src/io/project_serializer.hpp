#pragma once

#include <string>

#include "contracts/result.hpp"
#include "contracts/schematic_snapshot.hpp"

namespace cse::io {

	// 工程文件格式（纯文本，每行一条记录，字段用空格分隔；属性值必要时使用引号）：
	//
	//   CSEPROJ 1
	//   COMP <id> <type_id> <reference> <x> <y> <rotation> [<key>=<value> ...]
	//   WIRE <id> <from_comp> <from_pin> <from_x> <from_y> <to_comp> <to_pin> <to_x> <to_y>
	//   END
	//
	// from_comp / to_comp 为 0 表示该端点没连到引脚（自由端点）。
	//
	// 本层不依赖 wxWidgets，也不依赖 model，只认 contracts 里的数据结构。
	Result<void> SaveProject(const SchematicSnapshot& snapshot, const std::string& path);

	Result<SchematicSnapshot> LoadProject(const std::string& path);

}  // namespace cse::io
