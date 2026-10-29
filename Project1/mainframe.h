///////////////////////////////////////////////////////////////////////////
// mainframe.h -- MainFrame : derived from the generated MyFrame2.
//
// The generated framework (eda.h / eda.cpp) is never modified.  This class
// attaches the kernel to every control the wxFormBuilder project already
// named: m_panel4 (canvas), m_treeCtrl1 (palette), m_toolBar4 (tools),
// m_menubar2 (menus), the left-panel bitmap buttons and the property grid.
///////////////////////////////////////////////////////////////////////////

#ifndef EDA_MAINFRAME_H
#define EDA_MAINFRAME_H

#include "eda.h"
#include "canvas.h"
#include "model.h"
#include <map>
#include <vector>

class MainFrame : public MyFrame2
{
public:
    explicit MainFrame(wxWindow* parent = nullptr);

private:
    CircuitCanvas* m_canvas = nullptr;

    std::vector<Circuit> m_circuits;
    std::map<wxTreeItemId, wxString> m_treeSubs;   // "user defined parts" -> circuit name
    void RelinkCircuits();   // re-point project/owner after m_circuits changes
    int  m_curCircuit = 0;
    wxString m_currentFile;

    std::map<int, int> m_toolIds;                       // toolbar tool id -> Tool
    std::map<wxTreeItemId, ComponentType> m_treeGates;  // palette node -> type
    std::map<wxTreeItemId, int> m_treeCircuits;         // circuit node -> index

    int m_propCompId = -1;

    // Setup.
    void SetupCanvas();
    void BuildToolbar();
    void BuildTree();
    void SetupMenus();
    void SetupLeftButtons();
    void BindMenu(wxMenu* menu, void (MainFrame::*handler)(wxCommandEvent&));

    // Tool / circuit / property helpers.
    ComponentType TypeForToolComponent(int tool);
    int ToolForComponentType(ComponentType type);
    void SetCurrentTool(int tool);
    void SwitchCircuit(int idx);
    void AddCircuit();
    void MoveCircuitUp();
    void MoveCircuitDown();
    void RemoveCircuit();
    void RefreshPropertyGrid();
    void OnSelectionChanged();
    void UpdateZoomDisplay(double z);
    void Status(const wxString& s);
    void UpdateTitle();

    // Handlers.
    void OnTool(wxCommandEvent& e);
    void OnTreeSel(wxTreeEvent& e);
    void OnTreeDouble(wxTreeEvent& e);
    void OnMenuFile(wxCommandEvent& e);
    void OnMenuEdit(wxCommandEvent& e);
    void OnMenuProject(wxCommandEvent& e);
    void OnMenuSimulate(wxCommandEvent& e);
    void OnMenuWindow(wxCommandEvent& e);
    void OnMenuHelp(wxCommandEvent& e);
    void OnPropertyChanged(wxPropertyGridEvent& e);
    void OnSpin(wxSpinEvent& e);

    // File helpers.
    void DoNew();
    void LoadSession();     // restore the work left over from last time
    void SaveSession();
    void OnClose(wxCloseEvent& e);
    void OnTreeMenu(wxTreeEvent& e);
    void RemoveSubCircuit(const wxString& name);   // delete a user defined part
    void DoOpen();
    void DoSave(bool saveAs);
    void DoExportImage();
    void DoExportNetlist();
    void DoStatistics();
};

#endif // EDA_MAINFRAME_H
