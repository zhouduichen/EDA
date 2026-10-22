#pragma once

#include <string>

#include <wx/frame.h>

#include "contracts/component_library.hpp"
#include "src/editor/editor_controller.hpp"

class wxCommandEvent;
class wxSplitterWindow;
class wxToolBar;

namespace cse::ui {

class ComponentLibraryPanel;
class PropertiesPanel;
class SchematicCanvas;
class SimulationDialog;

enum MainFrameCommandId {
    kMenuNew = wxID_HIGHEST + 1,
    kMenuOpen,
    kMenuSaveProject,
    kMenuExportNetlist,
    kMenuExportKiCadSchematic,
    kMenuSimulate,
    kMenuExit,
    kMenuUndo,
    kMenuRedo,
    kMenuDelete,
    kMenuSelectTool,
    kMenuWireTool,
    kMenuPanTool,
    kMenuRotate,
    kMenuZoomIn,
    kMenuZoomOut,
    kMenuFit,
    kMenuCreateComposite,
    kToolbarSelect,
    kToolbarWire,
    kToolbarPan,
    kToolbarRotate,
    kToolbarUndo,
    kToolbarRedo,
    kToolbarDelete
};

class MainFrame final : public wxFrame {
public:
    MainFrame(editor::EditorController& editor, const IComponentLibrary& library);

private:
    void BuildMenuBar();
    void BuildToolBar();
    void BuildContent();
    void OnNew(wxCommandEvent& event);
    void OnOpen(wxCommandEvent& event);
    void OnSaveProject(wxCommandEvent& event);
    void OnExportNetlist(wxCommandEvent& event);
    void OnExportKiCadSchematic(wxCommandEvent& event);
    void OnSimulate(wxCommandEvent& event);
    void OnExit(wxCommandEvent& event);
    void OnSelectTool(wxCommandEvent& event);
    void OnWireTool(wxCommandEvent& event);
    void OnPanTool(wxCommandEvent& event);
    void OnRotate(wxCommandEvent& event);
    void OnDelete(wxCommandEvent& event);
    void OnCreateComposite(wxCommandEvent& event);
    void OnUndo(wxCommandEvent& event);
    void OnRedo(wxCommandEvent& event);
    void OnZoomIn(wxCommandEvent& event);
    void OnZoomOut(wxCommandEvent& event);
    void OnFit(wxCommandEvent& event);
    void RefreshViews();
    void UpdateStatus();

    editor::EditorController& editor_;
    const IComponentLibrary& library_;
    wxToolBar* toolbar_main_{nullptr};
    SchematicCanvas* panel_canvas_{nullptr};
    ComponentLibraryPanel* panel_component_library_{nullptr};
    PropertiesPanel* panel_properties_{nullptr};
    SimulationDialog* simulation_dialog_{nullptr};  // 非模态，关闭时自己销毁并回调置空
};

}  // namespace cse::ui
