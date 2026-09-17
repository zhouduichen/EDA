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

enum MainFrameCommandId {
    kMenuNew = wxID_HIGHEST + 1,
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
    void OnExit(wxCommandEvent& event);
    void OnSelectTool(wxCommandEvent& event);
    void OnWireTool(wxCommandEvent& event);
    void OnPanTool(wxCommandEvent& event);
    void OnRotate(wxCommandEvent& event);
    void OnDelete(wxCommandEvent& event);
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
};

}  // namespace cse::ui
