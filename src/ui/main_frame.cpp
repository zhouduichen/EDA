#include "src/ui/main_frame.hpp"

#include <wx/artprov.h>
#include <wx/filedlg.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/statusbr.h>
#include <wx/textdlg.h>
#include <wx/toolbar.h>

#include <cctype>

#include "src/io/kicad_netlist_exporter.hpp"
#include "src/io/project_serializer.hpp"
#include "src/ui/component_library_panel.hpp"
#include "src/ui/properties_panel.hpp"
#include "src/ui/schematic_canvas.hpp"
#include "src/ui/simulation_dialog.hpp"

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

    editor_.SetModelChangedHandler([this](const ModelChangedEvent&) {
        RefreshViews();
        if (simulation_dialog_ != nullptr) {
            simulation_dialog_->RefreshCircuit();
        }
    });
    editor_.SetSelectionChangedHandler([this](const SelectionChangedEvent&) { RefreshViews(); });
    RefreshViews();
    Centre();
}

void MainFrame::BuildMenuBar() {
    auto* menu_file = new wxMenu();
    menu_file->Append(kMenuNew, L"新建\tCtrl+N");
    menu_file->Append(kMenuOpen, L"打开工程\tCtrl+O");
    menu_file->Append(kMenuSaveProject, L"保存工程\tCtrl+S");
    menu_file->Append(kMenuExportNetlist, L"导出网表...\tCtrl+E");
    menu_file->AppendSeparator();
    menu_file->Append(kMenuExit, L"退出\tAlt+F4");

    auto* menu_edit = new wxMenu();
    menu_edit->Append(kMenuUndo, L"撤销\tCtrl+Z");
    menu_edit->Append(kMenuRedo, L"重做\tCtrl+Y");
    menu_edit->AppendSeparator();
    menu_edit->Append(kMenuDelete, L"删除\tDelete");
    menu_edit->Append(kMenuCreateComposite, L"将选区创建为自定义元件...");

    auto* menu_tool = new wxMenu();
    menu_tool->AppendRadioItem(kMenuSelectTool, L"选择\tS");
    menu_tool->AppendRadioItem(kMenuWireTool, L"连线\tW");
    menu_tool->AppendRadioItem(kMenuPanTool, L"平移\tP");
    menu_tool->AppendSeparator();
    menu_tool->Append(kMenuRotate, L"旋转\tR");

    auto* menu_view = new wxMenu();
    menu_view->Append(kMenuZoomIn, L"放大");
    menu_view->Append(kMenuZoomOut, L"缩小");
    menu_view->Append(kMenuFit, L"重置视图");

    auto* menu_sim = new wxMenu();
    menu_sim->Append(kMenuSimulate, L"运行仿真...\tF5");

    auto* menu_bar = new wxMenuBar();
    menu_bar->Append(menu_file, L"文件");
    menu_bar->Append(menu_edit, L"编辑");
    menu_bar->Append(menu_tool, L"工具");
    menu_bar->Append(menu_view, L"视图");
    menu_bar->Append(menu_sim, L"仿真");
    SetMenuBar(menu_bar);

    Bind(wxEVT_MENU, &MainFrame::OnNew, this, kMenuNew);
    Bind(wxEVT_MENU, &MainFrame::OnOpen, this, kMenuOpen);
    Bind(wxEVT_MENU, &MainFrame::OnSaveProject, this, kMenuSaveProject);
    Bind(wxEVT_MENU, &MainFrame::OnExportNetlist, this, kMenuExportNetlist);
    Bind(wxEVT_MENU, &MainFrame::OnSimulate, this, kMenuSimulate);
    Bind(wxEVT_MENU, &MainFrame::OnExit, this, kMenuExit);
    Bind(wxEVT_MENU, &MainFrame::OnUndo, this, kMenuUndo);
    Bind(wxEVT_MENU, &MainFrame::OnRedo, this, kMenuRedo);
    Bind(wxEVT_MENU, &MainFrame::OnDelete, this, kMenuDelete);
    Bind(wxEVT_MENU, &MainFrame::OnCreateComposite, this, kMenuCreateComposite);
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
    toolbar_main_->AddTool(kToolbarSelect, L"选择",
                           wxArtProvider::GetBitmap(wxART_LIST_VIEW, wxART_TOOLBAR));
    toolbar_main_->AddTool(kToolbarWire, L"连线",
                           wxArtProvider::GetBitmap(wxART_PLUS, wxART_TOOLBAR));
    toolbar_main_->AddTool(kToolbarPan, L"平移",
                           wxArtProvider::GetBitmap(wxART_LIST_VIEW, wxART_TOOLBAR));
    toolbar_main_->AddSeparator();
    toolbar_main_->AddTool(kToolbarRotate, L"旋转",
                           wxArtProvider::GetBitmap(wxART_REDO, wxART_TOOLBAR));
    toolbar_main_->AddTool(kToolbarUndo, L"撤销",
                           wxArtProvider::GetBitmap(wxART_UNDO, wxART_TOOLBAR));
    toolbar_main_->AddTool(kToolbarRedo, L"重做",
                           wxArtProvider::GetBitmap(wxART_REDO, wxART_TOOLBAR));
    toolbar_main_->AddTool(kToolbarDelete, L"删除",
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
    editor_.LoadSnapshot(SchematicSnapshot{});
    editor_.SetToolMode(ToolMode::kSelect);
    panel_canvas_->SetFocus();
    GetStatusBar()->SetStatusText(L"已新建空白工程", 0);
}

void MainFrame::OnOpen(wxCommandEvent& event) {
    static_cast<void>(event);
    wxFileDialog dialog(this, L"打开工程", wxEmptyString, wxEmptyString,
                        L"CSE 工程文件 (*.cseproj)|*.cseproj",
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK) {
        return;
    }
    const wxString wx_path = dialog.GetPath();
    const Result<SchematicSnapshot> loaded = io::LoadProject(wx_path.ToStdString());
    if (!loaded.ok) {
        wxMessageBox(L"打开工程失败：" + wxString(loaded.message), L"错误",
                     wxOK | wxICON_ERROR, this);
        return;
    }
    editor_.LoadSnapshot(loaded.value);
    RefreshViews();
    GetStatusBar()->SetStatusText(L"已打开：" + wx_path, 0);
}

void MainFrame::OnSaveProject(wxCommandEvent& event) {
    static_cast<void>(event);
    wxFileDialog dialog(this, L"保存工程", wxEmptyString, L"工程.cseproj",
                        L"CSE 工程文件 (*.cseproj)|*.cseproj",
                        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dialog.ShowModal() != wxID_OK) {
        return;
    }
    // ToStdString() 默认按系统 ANSI 代码页（GBK）转换，与 std::ofstream 的窄字符
    // 打开行为一致，含中文的路径才能正确落盘。
    const wxString wx_path = dialog.GetPath();
    const Result<void> result = io::SaveProject(editor_.GetSnapshot(),
                                                wx_path.ToStdString());
    if (result.ok) {
        GetStatusBar()->SetStatusText(L"已保存：" + wx_path, 0);
    }
    else {
        wxMessageBox(L"保存工程失败：" + wxString(result.message), L"错误",
                     wxOK | wxICON_ERROR, this);
    }
}

void MainFrame::OnExportNetlist(wxCommandEvent& event) {
    static_cast<void>(event);
    wxFileDialog dialog(this, L"导出网表", wxEmptyString, L"网表.net",
                        L"KiCad 网表 (*.net)|*.net",
                        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dialog.ShowModal() != wxID_OK) {
        return;
    }
    const wxString wx_path = dialog.GetPath();
    const Result<void> result = io::ExportKiCadNetlist(editor_.GetSnapshot(), library_,
                                                       wx_path.ToStdString());
    if (result.ok) {
        GetStatusBar()->SetStatusText(L"已导出网表：" + wx_path, 0);
    }
    else {
        wxMessageBox(L"导出网表失败：" + wxString(result.message), L"错误",
                     wxOK | wxICON_ERROR, this);
    }
}

void MainFrame::OnSimulate(wxCommandEvent& event) {
    static_cast<void>(event);
    if (simulation_dialog_ != nullptr) {
        simulation_dialog_->Raise();
        return;
    }
    simulation_dialog_ = new SimulationDialog(
        this, editor_, library_, panel_canvas_, [this] {
            simulation_dialog_ = nullptr;
            panel_canvas_->Refresh();
        });
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

void MainFrame::OnCreateComposite(wxCommandEvent& event) {
    static_cast<void>(event);
    wxTextEntryDialog dialog(this, L"输入自定义元件显示名称：", L"创建自定义元件", L"My Circuit");
    if (dialog.ShowModal() != wxID_OK) {
        return;
    }
    const wxString display_name = dialog.GetValue().Trim();
    if (display_name.empty()) {
        return;
    }
    std::string slug;
    const std::string utf8_name = display_name.ToStdString();
    bool previous_separator = false;
    for (unsigned char character : utf8_name) {
        if (std::isalnum(character) != 0) {
            slug.push_back(static_cast<char>(std::tolower(character)));
            previous_separator = false;
        } else if (!slug.empty() && !previous_separator) {
            slug.push_back('_');
            previous_separator = true;
        }
    }
    while (!slug.empty() && slug.back() == '_') {
        slug.pop_back();
    }
    if (slug.empty()) {
        slug = "module";
    }
    std::string type_id = "custom." + slug;
    unsigned int suffix = 2;
    while (library_.FindByTypeId(type_id).has_value()) {
        type_id = "custom." + slug + "_" + std::to_string(suffix++);
    }
    const Result<ComponentId> result =
        editor_.CreateComposite(type_id, display_name.ToStdString());
    if (!result.ok) {
        wxMessageBox(L"创建自定义元件失败：" + wxString::FromUTF8(result.message),
                     L"创建失败", wxOK | wxICON_ERROR, this);
        return;
    }
    panel_component_library_->RefreshLibrary();
    panel_canvas_->SetFocus();
    GetStatusBar()->SetStatusText(L"已创建自定义元件：" + display_name, 0);
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
    const wchar_t* tool_name = L"选择";
    switch (editor_.GetToolMode()) {
        case ToolMode::kPlaceComponent:
            tool_name = L"放置元件";
            break;
        case ToolMode::kWire:
            tool_name = L"连线";
            break;
        case ToolMode::kPan:
            tool_name = L"平移";
            break;
        case ToolMode::kSelect:
            break;
    }
    const SchematicSnapshot snapshot = editor_.GetSnapshot();
    GetStatusBar()->SetStatusText(
        wxString::Format(L"工具：%s | 元件：%zu | 导线：%zu | Revision：%llu", tool_name,
                         snapshot.components.size(), snapshot.wires.size(),
                         static_cast<unsigned long long>(snapshot.revision)),
        0);
    GetStatusBar()->SetStatusText(L"网格 10.0", 1);
}

}  // namespace cse::ui
