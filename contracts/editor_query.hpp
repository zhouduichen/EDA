#pragma once

#include <optional>
#include <vector>

#include "contracts/ids.hpp"
#include "contracts/schematic_snapshot.hpp"

namespace cse {

enum class ToolMode {
    kSelect,
    kPlaceComponent,
    kWire,
    kPan
};

struct SelectionState {
    std::vector<ComponentId> component_ids;
    std::vector<WireId> wire_ids;
};

class IEditorQuery {
public:
    virtual ~IEditorQuery() = default;

    virtual SchematicSnapshot GetSnapshot() const = 0;
    virtual std::optional<ComponentInstance> GetComponent(ComponentId id) const = 0;
    virtual SelectionState GetSelection() const = 0;
    virtual ToolMode GetToolMode() const = 0;
};

}  // namespace cse
