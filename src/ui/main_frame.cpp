#include "src/ui/main_frame.hpp"

#include <wx/artprov.h>
#include <wx/menu.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/statusbr.h>
#include <wx/toolbar.h>

#include "src/ui/component_library_panel.hpp"
#include "src/ui/properties_panel.hpp"
#include "src/ui/schematic_canvas.hpp"

namespace cse::ui {

MainFrame::MainFrame(editor::EditorController& editor, const IComponentLibrary& library)
    : wxFrame(nullptr, wxID_ANY, "Circuit Schematic Editor", wxDefaultPosition,
              wxSize(1280, 820)),
      editor_(editor),
      library_(library) {
    SetName("frame_main");
    BuildMenuBar();
    BuildToolBar();
    BuildContent();
    CreateStatusBar(2);
    GetStatusBar()->SetName("statusbar_main");
    const int status_widths[2] = {-1, 190};
    GetStatusBar()->SetStatusWidths(2, status_widths);

    editor_.SetModelChangedHandler([this](const ModelChangedEvent&) { RefreshViews(); });
    editor_.SetSelectionChangedHandler([this](const SelectionChangedEvent&) { RefreshViews(); });
    RefreshViews();
    Centre();
}

void MainFrame::BuildMenuBar() {
    auto* menu_file = new wxMenu();
    menu_file->Append(kMenuNew, "新建\tCtrl+N");
    menu_file->AppendSeparator();
    menu_file->Append(kMenuExit, "退出\tAlt+F4");

    auto* menu_edit = new wxMenu();
    menu_edit->Append(kMenuUndo, "撤销\tCtrl+Z");
    menu_edit->Append(kMenuRedo, "重做\tCtrl+Y");
    menu_edit->AppendSeparator();
    menu_edit->Append(kMenuDelete, "删除\tDelete");

    auto* menu_tool = new wxMenu();
    menu_tool->AppendRadioItem(kMenuSelectTool, "选择\tS");
    menu_tool->AppendRadioItem(kMenuWireTool, "连线\tW");
    menu_tool->AppendRadioItem(kMenuPanTool, "平移\tP");
    menu_tool->AppendSeparator();
    menu_tool->Append(kMenuRotate, "旋转\tR");

    auto* menu_view = new wxMenu();
    menu_view->Append(kMenuZoomIn, "放大");
    menu_view->Append(kMenuZoomOut, "缩小");
    menu_view->Append(kMenuFit, "重置视图");

    auto* menu_bar = new wxMenuBar();
    menu_bar->Append(menu_file, "文件");
    menu_bar->Append(menu_edit, "编辑");
    menu_bar->Append(menu_tool, "工具");
    menu_bar->Append(menu_view, "视图");
    SetMenuBar(menu_bar);

    Bind(wxEVT_MENU, &MainFrame::OnNew, this, kMenuNew);
    Bind(wxEVT_MENU, &MainFrame::OnExit, this, kMenuExit);
    Bind(wxEVT_MENU, &MainFrame::OnUndo, this, kMenuUndo);
    Bind(wxEVT_MENU, &MainFrame::OnRedo, this, kMenuRedo);
    Bind(wxEVT_MENU, &MainFrame::OnDelete, this, kMenuDelete);
    Bind(wxEVT_MENU, &MainFrame::OnSelectTool, this, kMenuSelectTool);
    Bind(wxEVT_MENU, &MainFrame::OnWireTool, this, kMenuWireTool);
    Bind(wxEVT_MENU, &MainFrame::OnPanTool, this, kMenuPanTool);
    Bind(wxEVT_MENU, &MainFrame::OnRotate, this, kMenuRotate);
    Bind(wxEVT_MENU, &MainFrame::OnZoomIn, this, kMenuZoomIn);
    Bind(wxEVT_MENU, &MainFrame::OnZoomOut, this, kMenuZoomOut);
    Bind(wxEVT_MENU, &MainFrame::OnFit, this, kMenuFit);
}

void MainFrame::BuildToolBar() {
    toolbar_main_ = CreateToolBar(wxTB_HORIZONTAL | wxTB_TEXT);
    toolbar_main_->SetName("toolbar_main");
    toolbar_main_->AddTool(kToolbarSelect, "选择",
                           wxArtProvider::GetBitmap(wxART_LIST_VIEW, wxART_TOOLBAR));
    toolbar_main_->AddTool(kToolbarWire, "连线",
                           wxArtProvider::GetBitmap(wxART_PLUS, wxART_TOOLBAR));
    toolbar_main_->AddTool(kToolbarPan, "平移",
                           wxArtProvider::GetBitmap(wxART_LIST_VIEW, wxART_TOOLBAR));
    toolbar_main_->AddSeparator();
    toolbar_main_->AddTool(kToolbarRotate, "旋转",
                           wxArtProvider::GetBitmap(wxART_REDO, wxART_TOOLBAR));
    toolbar_main_->AddTool(kToolbarUndo, "撤销",
                           wxArtProvider::GetBitmap(wxART_UNDO, wxART_TOOLBAR));
    toolbar_main_->AddTool(kToolbarRedo, "重做",
                           wxArtProvider::GetBitmap(wxART_REDO, wxART_TOOLBAR));
    toolbar_main_->AddTool(kToolbarDelete, "删除",
                           wxArtProvider::GetBitmap(wxART_DELETE, wxART_TOOLBAR));
    toolbar_main_->Realize();
    Bind(wxEVT_TOOL, &MainFrame::OnSelectTool, this, kToolbarSelect);
    Bind(wxEVT_TOOL, &MainFrame::OnWireTool, this, kToolbarWire);
    Bind(wxEVT_TOOL, &MainFrame::OnPanTool, this, kToolbarPan);
    Bind(wxEVT_TOOL, &MainFrame::OnRotate, this, kToolbarRotate);
    Bind(wxEVT_TOOL, &MainFrame::OnUndo, this, kToolbarUndo);
    Bind(wxEVT_TOOL, &MainFrame::OnRedo, this, kToolbarRedo);
    Bind(wxEVT_TOOL, &MainFrame::OnDelete, this, kToolbarDelete);
}

void MainFrame::BuildContent() {
    auto* horizontal_splitter = new wxSplitterWindow(this, wxID_ANY);
    horizontal_splitter->SetName("splitter_main");
    panel_component_library_ = new ComponentLibraryPanel(horizontal_splitter, library_);
    auto* content_splitter = new wxSplitterWindow(horizontal_splitter, wxID_ANY);
    content_splitter->SetName("splitter_content");
    panel_canvas_ = new SchematicCanvas(content_splitter, editor_, library_);
    panel_properties_ = new PropertiesPanel(content_splitter, editor_, library_);
    content_splitter->SplitVertically(panel_canvas_, panel_properties_, 820);
    content_splitter->SetMinimumPaneSize(180);
    horizontal_splitter->SplitVertically(panel_component_library_, content_splitter, 245);
    horizontal_splitter->SetMinimumPaneSize(180);

    auto* outer_sizer = new wxBoxSizer(wxVERTICAL);
    outer_sizer->Add(horizontal_splitter, 1, wxEXPAND);
    SetSizer(outer_sizer);

    panel_component_library_->SetComponentActivatedHandler(
        [this](const std::string& type_id) {
            editor_.SetPlacementType(type_id);
            UpdateStatus();
            panel_canvas_->SetFocus();
        });
}

void MainFrame::OnNew(wxCommandEvent& event) {
    static_cast<void>(event);
    GetStatusBar()->SetStatusText("当前版本暂未接入工程文件的新建、保存与加载", 0);
}

void MainFrame::OnExit(wxCommandEvent& event) {
    static_cast<void>(event);
    Close(true);
}

void MainFrame::OnSelectTool(wxCommandEvent& event) {
    static_cast<void>(event);
    editor_.SetToolMode(ToolMode::kSelect);
    UpdateStatus();
}

void MainFrame::OnWireTool(wxCommandEvent& event) {
    static_cast<void>(event);
    editor_.SetToolMode(ToolMode::kWire);
    UpdateStatus();
    panel_canvas_->SetFocus();
}

void MainFrame::OnPanTool(wxCommandEvent& event) {
    static_cast<void>(event);
    editor_.SetToolMode(ToolMode::kPan);
    UpdateStatus();
}

void MainFrame::OnRotate(wxCommandEvent& event) {
    static_cast<void>(event);
    static_cast<void>(editor_.RotateSelected());
    RefreshViews();
}

void MainFrame::OnDelete(wxCommandEvent& event) {
    static_cast<void>(event);
    static_cast<void>(editor_.DeleteSelection());
    RefreshViews();
}

void MainFrame::OnUndo(wxCommandEvent& event) {
    static_cast<void>(event);
    static_cast<void>(editor_.Undo());
    RefreshViews();
}

void MainFrame::OnRedo(wxCommandEvent& event) {
    static_cast<void>(event);
    static_cast<void>(editor_.Redo());
    RefreshViews();
}

void MainFrame::OnZoomIn(wxCommandEvent& event) {
    static_cast<void>(event);
    panel_canvas_->ZoomIn();
}

void MainFrame::OnZoomOut(wxCommandEvent& event) {
    static_cast<void>(event);
    panel_canvas_->ZoomOut();
}

void MainFrame::OnFit(wxCommandEvent& event) {
    static_cast<void>(event);
    panel_canvas_->FitContent();
}

void MainFrame::RefreshViews() {
    if (panel_canvas_ != nullptr) {
        panel_canvas_->Refresh();
    }
    if (panel_properties_ != nullptr) {
        panel_properties_->RefreshFromSelection();
    }
    UpdateStatus();
}

void MainFrame::UpdateStatus() {
    if (GetStatusBar() == nullptr) {
        return;
    }
    const char* tool_name = "选择";
    switch (editor_.GetToolMode()) {
        case ToolMode::kPlaceComponent:
            tool_name = "放置元件";
            break;
        case ToolMode::kWire:
            tool_name = "连线";
            break;
        case ToolMode::kPan:
            tool_name = "平移";
            break;
        case ToolMode::kSelect:
            break;
    }
    const SchematicSnapshot snapshot = editor_.GetSnapshot();
    GetStatusBar()->SetStatusText(
        wxString::Format("工具：%s | 元件：%zu | 导线：%zu | Revision：%llu", tool_name,
                         snapshot.components.size(), snapshot.wires.size(),
                         static_cast<unsigned long long>(snapshot.revision)),
        0);
    GetStatusBar()->SetStatusText("网格 10.0", 1);
}

}  // namespace cse::ui
