#include "src/ui/component_library_panel.hpp"

#include <map>

#include <wx/sizer.h>
#include <wx/treectrl.h>

namespace cse::ui {

namespace {

class ComponentItemData final : public wxTreeItemData {
public:
    explicit ComponentItemData(std::string type_id) : type_id_(std::move(type_id)) {}

    const std::string& GetTypeId() const {
        return type_id_;
    }

private:
    std::string type_id_;
};

}  // namespace

ComponentLibraryPanel::ComponentLibraryPanel(wxWindow* parent,
                                             const IComponentLibrary& library)
    : wxPanel(parent, wxID_ANY), library_(library) {
    SetName("panel_component_library");
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    tree_component_library_ = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                             wxTR_DEFAULT_STYLE | wxTR_SINGLE);
    tree_component_library_->SetName("tree_component_library");
    sizer->Add(tree_component_library_, 1, wxEXPAND | wxALL, 6);
    SetSizer(sizer);
    tree_component_library_->Bind(wxEVT_TREE_ITEM_ACTIVATED,
                                  &ComponentLibraryPanel::OnItemActivated, this);
    PopulateTree();
}

void ComponentLibraryPanel::SetComponentActivatedHandler(ComponentActivatedHandler handler) {
    component_activated_handler_ = std::move(handler);
}

void ComponentLibraryPanel::PopulateTree() {
    tree_component_library_->DeleteAllItems();
    const wxTreeItemId root = tree_component_library_->AddRoot("元件库");
    std::map<std::string, wxTreeItemId> category_items;
    for (const ComponentDefinition& definition : library_.GetAll()) {
        wxTreeItemId category_item;
        const auto category_iterator = category_items.find(definition.category);
        if (category_iterator == category_items.end()) {
            category_item = tree_component_library_->AppendItem(root, definition.category);
            category_items.emplace(definition.category, category_item);
        } else {
            category_item = category_iterator->second;
        }
        const wxTreeItemId component_item = tree_component_library_->AppendItem(
            category_item, definition.display_name + "  [" + definition.type_id + "]");
        tree_component_library_->SetItemData(component_item,
                                              new ComponentItemData(definition.type_id));
    }
    tree_component_library_->Expand(root);
    for (const auto& [category, category_item] : category_items) {
        static_cast<void>(category);
        tree_component_library_->Expand(category_item);
    }
}

void ComponentLibraryPanel::OnItemActivated(wxTreeEvent& event) {
    auto* item_data = dynamic_cast<ComponentItemData*>(
        tree_component_library_->GetItemData(event.GetItem()));
    if (item_data != nullptr && component_activated_handler_) {
        component_activated_handler_(item_data->GetTypeId());
    }
}

}  // namespace cse::ui
