#pragma once

#include <string>

#include <wx/panel.h>

#include "contracts/component_library.hpp"
#include "src/editor/editor_controller.hpp"

class wxStaticText;
class wxListCtrl;

namespace cse::ui {

class PropertiesPanel final : public wxPanel {
public:
    PropertiesPanel(wxWindow* parent, const editor::EditorController& editor,
                    const IComponentLibrary& library);

    void RefreshFromSelection();

private:
    wxListCtrl* property_table_{nullptr};
    const editor::EditorController& editor_;
    const IComponentLibrary& library_;
};

}  // namespace cse::ui
