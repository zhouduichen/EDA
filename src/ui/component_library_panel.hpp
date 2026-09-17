#pragma once

#include <functional>
#include <string>

#include <wx/panel.h>

#include "contracts/component_library.hpp"

class wxTreeCtrl;
class wxTreeEvent;

namespace cse::ui {

class ComponentLibraryPanel final : public wxPanel {
public:
    using ComponentActivatedHandler = std::function<void(const std::string&)>;

    ComponentLibraryPanel(wxWindow* parent, const IComponentLibrary& library);

    void SetComponentActivatedHandler(ComponentActivatedHandler handler);

private:
    void PopulateTree();
    void OnItemActivated(wxTreeEvent& event);

    const IComponentLibrary& library_;
    wxTreeCtrl* tree_component_library_{nullptr};
    ComponentActivatedHandler component_activated_handler_;
};

}  // namespace cse::ui
