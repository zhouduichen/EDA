#include "src/ui/properties_panel.hpp"

#include <iomanip>
#include <sstream>
#include <vector>
#include <algorithm>

#include <wx/listctrl.h>
#include <wx/sizer.h>

namespace cse::ui {

namespace {

wxString FormatPoint(Point2D point) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(1) << "(" << point.x << ", " << point.y << ")";
    return stream.str();
}

}  // namespace

PropertiesPanel::PropertiesPanel(wxWindow* parent, const editor::EditorController& editor,
                                 const IComponentLibrary& library)
    : wxPanel(parent, wxID_ANY), editor_(editor), library_(library) {
    SetName("panel_properties");
    property_table_ = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                     wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_HRULES);
    property_table_->SetName("table_properties");
    property_table_->InsertColumn(0, "属性");
    property_table_->InsertColumn(1, "值");
    property_table_->SetColumnWidth(0, 100);
    property_table_->SetColumnWidth(1, 180);
    auto* outer_sizer = new wxBoxSizer(wxVERTICAL);
    outer_sizer->Add(property_table_, 1, wxEXPAND | wxALL, 8);
    SetSizer(outer_sizer);
}

void PropertiesPanel::RefreshFromSelection() {
    property_table_->DeleteAllItems();
    const SelectionState selection = editor_.GetSelection();
    if (selection.component_ids.size() != 1) {
        return;
    }

    const std::optional<ComponentInstance> component =
        editor_.GetComponent(selection.component_ids.front());
    if (!component.has_value()) {
        return;
    }

    auto add_row = [this](const wxString& key, const wxString& value) {
        const long row = property_table_->InsertItem(property_table_->GetItemCount(), key);
        property_table_->SetItem(row, 1, value);
    };
    add_row("Type", component->type_id);
    add_row("Reference", component->reference);
    add_row("Position", FormatPoint(component->position));
    add_row("Rotation", wxString::Format("%.0f°", component->rotation_deg));

    std::vector<std::pair<std::string, std::string>> entries(component->properties.begin(),
                                                             component->properties.end());
    std::sort(entries.begin(), entries.end());
    for (const auto& [key, value] : entries) {
        add_row(key, value);
    }
    static_cast<void>(library_);
}

}  // namespace cse::ui
