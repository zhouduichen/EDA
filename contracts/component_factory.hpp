#pragma once

#include <string>

#include "contracts/component_model.hpp"
#include "contracts/result.hpp"
#include "contracts/schematic_snapshot.hpp"

namespace cse {

class IComponentFactory {
public:
    virtual ~IComponentFactory() = default;

    virtual Result<ComponentInstance> Create(
        const std::string& type_id,
        Point2D position) = 0;

    // 载入或新建工程后同步生成器状态。默认空实现保持外部工厂兼容。
    virtual void SyncWithSnapshot(const SchematicSnapshot& snapshot) {
        static_cast<void>(snapshot);
    }
};

}  // namespace cse
