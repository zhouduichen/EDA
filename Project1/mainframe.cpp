///////////////////////////////////////////////////////////////////////////
// mainframe.cpp -- MainFrame implementation.
///////////////////////////////////////////////////////////////////////////

#include "mainframe.h"

#include <wx/file.h>
#include <wx/filedlg.h>
#include <wx/textdlg.h>
#include <wx/msgdlg.h>
#include <wx/filename.h>
#include <wx/propgrid/propgrid.h>
#include <wx/propgrid/property.h>
#include <wx/dcmemory.h>
#include <wx/stdpaths.h>
#include <wx/filefn.h>
#include <wx/dir.h>
#include <algorithm>
#include <string>

namespace
{
    // Where the work in progress is kept, so that closing the program and
    // starting it again does not lose anything.
    wxString SessionPath()
    {
        wxString dir = wxStandardPaths::Get().GetUserLocalDataDir();
        if (!wxDirExists(dir)) wxMkDir(dir, 0777);
        return dir + wxT("\\session.circ");
    }

    wxString MenuLabel(wxMenu* menu, int id)
    {
        wxMenuItem* mi = menu ? menu->FindItem(id) : nullptr;
        if (!mi) return wxEmptyString;
        return mi->GetItemLabel().BeforeFirst(wxT('\t'));
    }

    wxMenuItem* FindByLabel(wxMenu* menu, const wxString& label)
    {
        if (!menu) return nullptr;
        for (size_t i = 0; i < menu->GetMenuItemCount(); ++i)
        {
            wxMenuItem* mi = menu->FindItemByPosition(i);
            if (mi && mi->GetItemLabel().BeforeFirst(wxT('\t')) == label) return mi;
        }
        return nullptr;
    }

    // Generate a simple 16x16 icon for a toolbar tool.  The generated framework
    // adds the tools with wxNullBitmap, which wxMSW rejects once wxTB_TEXT is on,
    // so every tool needs a real bitmap.
    wxBitmap MakeToolIcon(int tool)
    {
        wxBitmap bmp(16, 16);
        {
            wxMemoryDC dc(bmp);
            wxColour bg = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);
            wxColour fg(40, 40, 40);
            dc.SetBackground(wxBrush(bg));
            dc.Clear();
            dc.SetPen(wxPen(fg, 1));
            dc.SetBrush(wxBrush(fg));

            switch (tool)
            {
                case TOOL_POKE:
                    dc.DrawCircle(8, 8, 4);
                    dc.SetBrush(wxBrush(bg));
                    dc.DrawCircle(8, 8, 2);
                    break;
                case TOOL_EDIT:
                {
                    wxPoint tri[3] = { wxPoint(3, 4), wxPoint(3, 12), wxPoint(11, 8) };
                    dc.DrawPolygon(3, tri, 0, 0);
                    break;
                }
                case TOOL_TEXT:
                    dc.SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
                    dc.DrawText(wxT("T"), 5, 2);
                    break;
                case TOOL_PIN_IN:
                {
                    wxPoint tri[3] = { wxPoint(2, 4), wxPoint(2, 12), wxPoint(13, 8) };
                    dc.DrawPolygon(3, tri, 0, 0);
                    break;
                }
                case TOOL_PIN_OUT:
                {
                    wxPoint tri[3] = { wxPoint(14, 4), wxPoint(14, 12), wxPoint(3, 8) };
                    dc.DrawPolygon(3, tri, 0, 0);
                    break;
                }
                case TOOL_NOT:
                {
                    wxPoint tri[3] = { wxPoint(2, 4), wxPoint(2, 12), wxPoint(10, 8) };
                    dc.DrawPolygon(3, tri, 0, 0);
                    dc.SetBrush(wxBrush(bg));
                    dc.DrawCircle(12, 8, 2);
                    break;
                }
                case TOOL_AND:
                {
                    dc.SetBrush(*wxTRANSPARENT_BRUSH);
                    wxPoint body[] = {
                        wxPoint(4, 3), wxPoint(7, 3), wxPoint(10, 4),
                        wxPoint(13, 8), wxPoint(10, 12), wxPoint(7, 13),
                        wxPoint(4, 13)
                    };
                    dc.DrawLines(7, body);
                    dc.DrawLine(4, 3, 4, 13);
                    break;
                }
                case TOOL_OR:
                {
                    dc.SetBrush(*wxTRANSPARENT_BRUSH);
                    wxPoint body[] = {
                        wxPoint(2, 3), wxPoint(5, 3), wxPoint(8, 4),
                        wxPoint(11, 6), wxPoint(13, 8), wxPoint(11, 10),
                        wxPoint(8, 12), wxPoint(5, 13), wxPoint(2, 13)
                    };
                    dc.DrawLines(9, body);
                    wxPoint back[] = {
                        wxPoint(3, 4), wxPoint(2, 6), wxPoint(1, 8),
                        wxPoint(2, 10), wxPoint(3, 12)
                    };
                    dc.DrawLines(5, back);
                    break;
                }
                default:
                    break;
            }
        }
        return bmp;
    }
}

MainFrame::MainFrame(wxWindow* parent)
    : MyFrame2(parent, wxID_ANY, wxT("数字逻辑电路"))
{
    m_circuits.push_back(Circuit());   // the default "main" circuit
    m_curCircuit = 0;

    LoadSession();                     // pick up last time's work, if any

    CreateStatusBar(1);

    SetupCanvas();
    BuildToolbar();
    BuildTree();
    m_treeCtrl1->Bind(wxEVT_TREE_SEL_CHANGED, &MainFrame::OnTreeSel, this);
    m_treeCtrl1->Bind(wxEVT_TREE_ITEM_ACTIVATED, &MainFrame::OnTreeDouble, this);
    m_treeCtrl1->Bind(wxEVT_TREE_ITEM_MENU,      &MainFrame::OnTreeMenu, this);
    m_propertyGrid1->Bind(wxEVT_PG_CHANGED, &MainFrame::OnPropertyChanged, this);
    SetupMenus();
    SetupLeftButtons();

    m_spinBtn1->SetRange(25, 400);
    m_spinBtn1->SetValue(100);
    m_spinBtn1->Bind(wxEVT_SPIN, &MainFrame::OnSpin, this);

    // Whatever the user leaves behind is kept, so "quit and come back" works.
    Bind(wxEVT_CLOSE_WINDOW, &MainFrame::OnClose, this);

    RelinkCircuits();       // wire up project / owner pointers once and for all
    SetCurrentTool(TOOL_EDIT);
    RefreshPropertyGrid();
    UpdateTitle();
    Status(wxT("就绪。请从左侧选择逻辑门，然后在画布上单击放置。"));
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Session: the project is kept outside of any file the user saved, so simply
// closing the program and starting it again keeps everything, custom parts
// included.
// ---------------------------------------------------------------------------

void MainFrame::LoadSession()
{
    const wxString path = SessionPath();
    if (!wxFileExists(path)) return;

    wxFile f(path);
    if (!f.IsOpened()) return;
    const size_t len = f.Length();
    std::string buf(len, '\0');
    if (len) f.Read(&buf[0], len);
    f.Close();
    wxString text = wxString::FromUTF8(buf.data(), len);

    // Optional first line: the file the project was last saved as.
    size_t nl = text.find(wxT('\n'));
    if (nl != wxString::npos)
    {
        wxString first = text.substr(0, nl);
        first.Trim(true);
        if (first.StartsWith(wxT("SESSIONFILE ")))
            m_currentFile = first.substr(12).Trim(false).Trim(true);
    }

    std::vector<Circuit> loaded;
    if (!DeserializeProject(text, loaded) || loaded.empty()) return;

    m_circuits.swap(loaded);
    m_curCircuit = 0;
}

void MainFrame::SaveSession()
{
    wxString text;
    if (!m_currentFile.IsEmpty())
        text += wxT("SESSIONFILE ") + m_currentFile + wxT("\n");
    text += SerializeProject(m_circuits);

    wxFile f(SessionPath(), wxFile::write);
    if (!f.IsOpened()) return;
    const std::string utf8 = text.utf8_string();
    f.Write(utf8.c_str(), utf8.size());
    f.Close();
}

void MainFrame::OnClose(wxCloseEvent& e)
{
    SaveSession();
    e.Skip();       // let the default handler actually close the window
}

void MainFrame::SetupCanvas()
{
    m_panel4->SetBackgroundColour(*wxWHITE);
    m_canvas = new CircuitCanvas(m_panel4);
    wxBoxSizer* sz = new wxBoxSizer(wxHORIZONTAL);
    sz->Add(m_canvas, 1, wxEXPAND);
    m_panel4->SetSizer(sz);
    m_panel4->Layout();

    m_canvas->SetCircuit(&m_circuits[m_curCircuit]);
    m_canvas->SetStatusCallback([this](const wxString& s) { Status(s); });
    m_canvas->SetSelectionCallback([this]() { OnSelectionChanged(); });
    m_canvas->SetZoomCallback([this](double z) { UpdateZoomDisplay(z); });
    m_canvas->SetSimulationCallback([this](bool on) {
        wxMenuItem* mi = FindByLabel(Simulate, wxT("启用仿真"));
        if (mi) mi->Check(on);
    });
}

void MainFrame::BuildToolbar()
{
    long style = m_toolBar4->GetWindowStyleFlag();
    m_toolBar4->SetWindowStyleFlag(style | wxTB_TEXT | wxTB_FLAT);

    auto cfg = [this](wxToolBarToolBase* t, const wxString& label,
                      const wxString& help, int tool)
    {
        int id = t->GetId();
        t->SetNormalBitmap(MakeToolIcon(tool));
        t->SetLabel(label);
        t->SetShortHelp(help);
        t->SetLongHelp(help);
        m_toolIds[id] = tool;
        Bind(wxEVT_TOOL, &MainFrame::OnTool, this, id);
    };

    cfg(change_values_within_circuit,  wxT("电平"), wxT("切换输入引脚电平"), TOOL_POKE);
    cfg(edit_selection_and_add_wires,  wxT("编辑"), wxT("选择、移动元件并绘制导线"), TOOL_EDIT);
    cfg(edit_text_in_circuit,          wxT("文本"), wxT("编辑电路中的文字"), TOOL_TEXT);
    cfg(Add_Pin,                       wxT("输入"), wxT("放置输入引脚"), TOOL_PIN_IN);
    cfg(Add_Pin2,                      wxT("输出"), wxT("放置输出引脚"), TOOL_PIN_OUT);
    cfg(Add_Not_Gate,                  wxT("非"),   wxT("放置非门"), TOOL_NOT);
    cfg(Add_And_Gate,                  wxT("与"),   wxT("放置与门"), TOOL_AND);
    cfg(Add_Or_Gate,                   wxT("或"),   wxT("放置或门"), TOOL_OR);

    m_toolBar4->Realize();
}

void MainFrame::BuildTree()
{
    m_treeCtrl1->DeleteAllItems();
    m_treeGates.clear();
    m_treeCircuits.clear();

    wxTreeItemId root = m_treeCtrl1->AddRoot(wxT("电路"));

    wxTreeItemId curId;
    for (size_t i = 0; i < m_circuits.size(); ++i)
    {
        wxTreeItemId id = m_treeCtrl1->AppendItem(root, m_circuits[i].name);
        m_treeCircuits[id] = (int)i;
        if ((int)i == m_curCircuit) curId = id;
    }

    auto addGate = [this](wxTreeItemId parent, const wxString& name, ComponentType t)
    {
        wxTreeItemId id = m_treeCtrl1->AppendItem(parent, name);
        m_treeGates[id] = t;
    };

    wxTreeItemId gates = m_treeCtrl1->AppendItem(root, wxT("逻辑门"));
    addGate(gates, wxT("与门"),   CT_And);
    addGate(gates, wxT("或门"),   CT_Or);
    addGate(gates, wxT("非门"),   CT_Not);
    addGate(gates, wxT("与非门"), CT_Nand);
    addGate(gates, wxT("或非门"), CT_Nor);
    addGate(gates, wxT("异或门"), CT_Xor);
    addGate(gates, wxT("同或门"), CT_Xnor);
    addGate(gates, wxT("缓冲器"), CT_Buffer);

    wxTreeItemId pins = m_treeCtrl1->AppendItem(root, wxT("引脚"));
    addGate(pins, wxT("输入引脚"), CT_PinIn);
    addGate(pins, wxT("输出引脚"), CT_PinOut);

    // User defined parts: every other circuit of the project can be dropped in
    // as a component.  The circuit being edited is left out on purpose -- it
    // cannot instantiate itself.
    m_treeSubs.clear();
    if (m_circuits.size() > 1)
    {
        wxTreeItemId subs = m_treeCtrl1->AppendItem(root, wxT("自定义元件"));
        for (size_t i = 0; i < m_circuits.size(); ++i)
        {
            if ((int)i == m_curCircuit) continue;
            wxTreeItemId id = m_treeCtrl1->AppendItem(subs, m_circuits[i].name);
            m_treeSubs[id] = m_circuits[i].name;
        }
        m_treeCtrl1->Expand(subs);
    }

    m_treeCtrl1->Expand(root);
    m_treeCtrl1->Expand(gates);
    m_treeCtrl1->Expand(pins);
    if (curId.IsOk()) m_treeCtrl1->SelectItem(curId);

    // Give the property grid a minimum height so it stays visible.
    m_propertyGrid1->SetMinSize(wxSize(-1, 140));
}

void MainFrame::SetupMenus()
{
    BindMenu(File,     &MainFrame::OnMenuFile);
    BindMenu(Edit,     &MainFrame::OnMenuEdit);
    BindMenu(Project,  &MainFrame::OnMenuProject);
    BindMenu(Simulate, &MainFrame::OnMenuSimulate);
    BindMenu(Window,   &MainFrame::OnMenuWindow);
    BindMenu(Help,     &MainFrame::OnMenuHelp);
}

void MainFrame::BindMenu(wxMenu* menu, void (MainFrame::*handler)(wxCommandEvent&))
{
    if (!menu) return;
    for (size_t i = 0; i < menu->GetMenuItemCount(); ++i)
    {
        wxMenuItem* mi = menu->FindItemByPosition(i);
        if (mi) Bind(wxEVT_MENU, handler, this, mi->GetId());
    }
}

void MainFrame::SetupLeftButtons()
{
    Show_Project_Circuit->SetToolTip(wxT("显示项目/电路视图"));
    Show_Project_Circuit->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        m_treeCtrl1->ExpandAll(); Status(wxT("项目视图"));
    });

    Show_Simulation->SetToolTip(wxT("显示仿真树"));
    Show_Simulation->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        Status(wxT("仿真树"));
    });

    EditViewd_Latout->SetToolTip(wxT("编辑当前布局"));
    EditViewd_Latout->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        SetCurrentTool(TOOL_EDIT); Status(wxT("编辑布局"));
    });

    EditViewed_Apperance->SetToolTip(wxT("编辑当前外观"));
    EditViewed_Apperance->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        Status(wxT("编辑外观（尚未实现）"));
    });

    Add_Circuit->SetToolTip(wxT("添加电路"));
    Add_Circuit->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { AddCircuit(); });

    Move_ViewedUP->SetToolTip(wxT("上移电路"));
    Move_ViewedUP->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { MoveCircuitUp(); });

    Move_ViewedDOWN->SetToolTip(wxT("下移电路"));
    Move_ViewedDOWN->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { MoveCircuitDown(); });

    Remove_Viewed->SetToolTip(wxT("移除电路"));
    Remove_Viewed->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { RemoveCircuit(); });
}

// ---------------------------------------------------------------------------
// Tool / circuit / property helpers
// ---------------------------------------------------------------------------

void MainFrame::SetCurrentTool(int tool)
{
    m_canvas->SetTool(tool);
    switch (tool)
    {
        case TOOL_EDIT:   Status(wxT("编辑工具：选择、移动元件并绘制导线")); break;
        case TOOL_POKE:   Status(wxT("电平工具：单击输入引脚切换其电平")); break;
        case TOOL_TEXT:   Status(wxT("文本工具：单击元件重命名，或单击空白处添加文本")); break;
        default:          Status(wxT("放置") + TypeName(TypeForToolComponent(tool)) + wxT("：单击画布放置")); break;
    }
}

// Small mapping helper (defined here to avoid exposing it in the header).
ComponentType MainFrame::TypeForToolComponent(int tool)
{
    switch (tool)
    {
        case TOOL_AND:    return CT_And;
        case TOOL_OR:     return CT_Or;
        case TOOL_NOT:    return CT_Not;
        case TOOL_NAND:   return CT_Nand;
        case TOOL_NOR:    return CT_Nor;
        case TOOL_XOR:    return CT_Xor;
        case TOOL_XNOR:   return CT_Xnor;
        case TOOL_BUFFER: return CT_Buffer;
        case TOOL_PIN_IN: return CT_PinIn;
        case TOOL_PIN_OUT:return CT_PinOut;
    }
    return CT_And;
}

int MainFrame::ToolForComponentType(ComponentType type)
{
    switch (type)
    {
        case CT_And:    return TOOL_AND;
        case CT_Or:     return TOOL_OR;
        case CT_Not:    return TOOL_NOT;
        case CT_Nand:   return TOOL_NAND;
        case CT_Nor:    return TOOL_NOR;
        case CT_Xor:    return TOOL_XOR;
        case CT_Xnor:   return TOOL_XNOR;
        case CT_Buffer: return TOOL_BUFFER;
        case CT_PinIn:  return TOOL_PIN_IN;
        case CT_PinOut: return TOOL_PIN_OUT;
    }
    return TOOL_EDIT;
}

// m_circuits may reallocate (push_back / erase), which invalidates every
// Component::owner and the pointer the canvas holds.  Re-point all of them and
// re-resolve every sub-circuit instance.
void MainFrame::RelinkCircuits()
{
    for (auto& c : m_circuits)
    {
        c.project = &m_circuits;
        c.LinkComponents();
    }
    for (auto& c : m_circuits) c.RefreshSubCircuits();

    m_canvas->SetProject(&m_circuits);
    if (m_curCircuit >= 0 && m_curCircuit < (int)m_circuits.size())
        m_canvas->SetCircuit(&m_circuits[m_curCircuit]);
}

void MainFrame::SwitchCircuit(int idx)
{
    if (idx < 0 || idx >= (int)m_circuits.size()) return;
    m_curCircuit = idx;
    RelinkCircuits();
    BuildTree();
    UpdateTitle();
}

void MainFrame::AddCircuit()
{
    wxString name = wxGetTextFromUser(wxT("电路名称："), wxT("添加电路"), wxT("电路"));
    if (name.IsEmpty()) return;
    Circuit c;
    c.name = name;
    m_circuits.push_back(c);
    m_curCircuit = (int)m_circuits.size() - 1;
    RelinkCircuits();
    m_currentFile.Clear();
    BuildTree();
    UpdateTitle();
}

void MainFrame::MoveCircuitUp()
{
    if (m_curCircuit <= 0) return;
    std::swap(m_circuits[m_curCircuit], m_circuits[m_curCircuit - 1]);
    --m_curCircuit;
    RelinkCircuits();
    BuildTree();
    UpdateTitle();
}

void MainFrame::MoveCircuitDown()
{
    if (m_curCircuit >= (int)m_circuits.size() - 1) return;
    std::swap(m_circuits[m_curCircuit], m_circuits[m_curCircuit + 1]);
    ++m_curCircuit;
    RelinkCircuits();
    BuildTree();
    UpdateTitle();
}

void MainFrame::RemoveCircuit()
{
    if (m_circuits.size() <= 1) { Status(wxT("不能移除唯一的电路")); return; }

    const wxString name = m_circuits[m_curCircuit].name;

    // How many instances of this definition are out there?  Removing the
    // definition orphans them, so tell the user and clean them up as well.
    int refs = 0;
    for (size_t i = 0; i < m_circuits.size(); ++i)
    {
        if ((int)i == m_curCircuit) continue;
        for (const Component& c : m_circuits[i].comps)
            if (c.type == CT_SubCircuit && c.subName == name) ++refs;
    }

    wxString msg = wxString::Format(wxT("确定移除电路「%s」？"), name.c_str());
    if (refs > 0)
        msg += wxString::Format(wxT("\n有 %d 个实例正在使用它，将一并删除。"), refs);
    if (wxMessageBox(msg, wxT("移除电路"), wxYES_NO | wxICON_QUESTION) != wxYES)
    {
        Status(wxT("已取消"));
        return;
    }

    if (refs > 0) RemoveSubCircuitInstances(m_circuits, name);

    m_circuits.erase(m_circuits.begin() + m_curCircuit);
    if (m_curCircuit >= (int)m_circuits.size()) m_curCircuit = (int)m_circuits.size() - 1;
    RelinkCircuits();
    BuildTree();
    UpdateTitle();
    Status(wxString::Format(wxT("已移除电路「%s」"), name.c_str()));
}

void MainFrame::RefreshPropertyGrid()
{
    m_propertyGrid1->Clear();
    m_propCompId = -1;

    Component* sel = nullptr;
    Circuit* c = m_canvas->GetCircuit();
    if (c)
        for (auto& comp : c->comps)
            if (comp.selected) { sel = &comp; break; }

    if (!sel)
    {
        wxPGProperty* cat = m_propertyGrid1->Append(new wxPropertyCategory(wxT("选择")));
        wxPGProperty* info = m_propertyGrid1->AppendIn(cat,
            new wxStringProperty(wxT("信息"), wxT("info"), wxT("未选择元件")));
        info->ChangeFlag(wxPG_PROP_READONLY, true);
        return;
    }

    m_propCompId = sel->id;

    wxString catName = TypeName(sel->type);
    if (sel->type == CT_SubCircuit)
        catName = sel->subName.IsEmpty() ? wxString(wxT("子电路")) : (wxT("子电路 ") + sel->subName);
    wxPGProperty* cat = m_propertyGrid1->Append(new wxPropertyCategory(catName));
    m_propertyGrid1->AppendIn(cat, new wxStringProperty(wxT("标签"), wxT("label"), sel->label));

    if (sel->type == CT_SubCircuit)
    {
        wxPGProperty* sn = m_propertyGrid1->AppendIn(cat, new wxStringProperty(
            wxT("电路"), wxT("sub"),
            sel->subName.IsEmpty() ? wxString(wxT("(未指定)")) : sel->subName));
        sn->ChangeFlag(wxPG_PROP_READONLY, true);
    }

    if (IsVariableInputGate(sel->type))
    {
        wxPGProperty* in = m_propertyGrid1->AppendIn(cat,
            new wxIntProperty(wxT("输入数量"), wxT("inputs"), sel->inputs));
        in->SetAttribute(wxT("Min"), 1);
        in->SetAttribute(wxT("Max"), 8);
    }
    if (sel->type == CT_PinIn)
    {
        m_propertyGrid1->AppendIn(cat,
            new wxBoolProperty(wxT("电平"), wxT("value"), sel->value != 0));
    }
    m_propertyGrid1->Refresh();
}

void MainFrame::OnSelectionChanged()
{
    RefreshPropertyGrid();
}

void MainFrame::UpdateZoomDisplay(double z)
{
    int pct = (int)wxRound(z * 100.0);
    m_textCtrl2->SetValue(wxString::Format(wxT("%d%%"), pct));
    m_spinBtn1->SetValue(pct);
}

void MainFrame::Status(const wxString& s)
{
    SetStatusText(s);
}

void MainFrame::UpdateTitle()
{
    SetTitle(wxT("数字逻辑电路 - ") + m_circuits[m_curCircuit].name);
}

// ---------------------------------------------------------------------------
// Event handlers
// ---------------------------------------------------------------------------

void MainFrame::OnTool(wxCommandEvent& e)
{
    auto it = m_toolIds.find(e.GetId());
    if (it != m_toolIds.end()) SetCurrentTool(it->second);
}

void MainFrame::OnTreeSel(wxTreeEvent& e)
{
    auto it = m_treeGates.find(e.GetItem());
    if (it != m_treeGates.end())
    {
        SetCurrentTool(ToolForComponentType(it->second));
        return;
    }

    auto sub = m_treeSubs.find(e.GetItem());
    if (sub != m_treeSubs.end())
    {
        m_canvas->SetSubCircuitToPlace(sub->second);
        SetCurrentTool(TOOL_SUBCIRCUIT);

        int pins = 0;
        for (size_t i = 0; i < m_circuits.size(); ++i)
        {
            if (m_circuits[i].name != sub->second) continue;
            for (const Component& c : m_circuits[i].comps)
                if (IsPinType(c.type)) ++pins;
        }
        if (pins == 0)
            Status(sub->second + wxT(" 没有输入/输出引脚，放置后没有可连的端口"));
        else
            Status(wxT("点击画布放置子电路：") + sub->second +
                   wxString::Format(wxT("（%d 个端口）"), pins));
    }
}

void MainFrame::OnTreeDouble(wxTreeEvent& e)
{
    auto it = m_treeCircuits.find(e.GetItem());
    if (it != m_treeCircuits.end()) { SwitchCircuit(it->second); return; }

    // A user defined part: jump to the circuit that defines it, so it can be
    // edited.  This is the "open the black box" gesture.
    auto sub = m_treeSubs.find(e.GetItem());
    if (sub != m_treeSubs.end())
    {
        for (size_t i = 0; i < m_circuits.size(); ++i)
            if (m_circuits[i].name == sub->second) { SwitchCircuit((int)i); return; }
    }
}

// Right click on a user defined part: place it, open its definition, or get
// rid of it.  Deleting is done here because it is the part the user sees.
void MainFrame::OnTreeMenu(wxTreeEvent& e)
{
    wxTreeItemId item = e.GetItem();
    if (!item.IsOk()) return;
    m_treeCtrl1->SelectItem(item);

    auto sub = m_treeSubs.find(item);
    if (sub == m_treeSubs.end()) return;

    enum { kPlace = 9101, kEdit, kDelete };
    wxMenu menu;
    menu.Append(kPlace,  wxT("放置该元件"));
    menu.Append(kEdit,   wxT("编辑定义"));
    menu.AppendSeparator();
    menu.Append(kDelete, wxT("删除该元件"));

    const int cmd = GetPopupMenuSelectionFromUser(menu, e.GetPoint());
    if (cmd == kPlace)
    {
        m_canvas->SetSubCircuitToPlace(sub->second);
        SetCurrentTool(TOOL_SUBCIRCUIT);
        Status(wxT("点击画布放置子电路：") + sub->second);
    }
    else if (cmd == kEdit)
    {
        for (size_t i = 0; i < m_circuits.size(); ++i)
            if (m_circuits[i].name == sub->second) { SwitchCircuit((int)i); break; }
    }
    else if (cmd == kDelete)
    {
        RemoveSubCircuit(sub->second);
    }
}

void MainFrame::RemoveSubCircuit(const wxString& name)
{
    for (size_t i = 0; i < m_circuits.size(); ++i)
    {
        if (m_circuits[i].name != name) continue;
        m_curCircuit = (int)i;
        RemoveCircuit();          // asks for confirmation, counts the instances
        return;
    }
}

void MainFrame::OnMenuFile(wxCommandEvent& e)
{
    wxString label = MenuLabel(File, e.GetId());

    if (label == wxT("新建"))                   DoNew();
    else if (label == wxT("打开"))             DoOpen();
    else if (label == wxT("最近打开"))         Status(wxT("没有最近打开的文件"));
    else if (label == wxT("关闭"))             DoNew();
    else if (label == wxT("保存"))             DoSave(false);
    else if (label == wxT("另存为"))           DoSave(true);
    else if (label == wxT("导出图像"))         DoExportImage();
    else if (label == wxT("打印"))             Status(wxT("打印功能尚未实现"));
    else if (label == wxT("首选项..."))        Status(wxT("首选项功能尚未实现"));
    else if (label == wxT("退出"))             Close(true);
}

void MainFrame::OnMenuEdit(wxCommandEvent& e)
{
    wxString label = MenuLabel(Edit, e.GetId());

    if (label == wxT("撤销"))                       m_canvas->Undo();
    else if (label == wxT("剪切"))                  m_canvas->CutSelection();
    else if (label == wxT("复制"))                  m_canvas->CopySelection();
    else if (label == wxT("粘贴"))                  m_canvas->PasteSelection();
    else if (label == wxT("删除"))                  m_canvas->DeleteSelection();
    else if (label == wxT("创建副本"))              m_canvas->DuplicateSelection();
    else if (label == wxT("全选"))                  m_canvas->SelectAll();
    else if (label == wxT("添加拐点"))              m_canvas->AddVertexToSelectedWire();
    else if (label == wxT("删除拐点"))              m_canvas->RemoveVertexFromSelectedWire();
    else if (label == wxT("上移一层"))              Status(wxT("上移一层功能尚未实现"));
    else if (label == wxT("下移一层"))              Status(wxT("下移一层功能尚未实现"));
    else if (label == wxT("置于顶层"))              Status(wxT("置于顶层功能尚未实现"));
    else if (label == wxT("置于底层"))              Status(wxT("置于底层功能尚未实现"));
}

void MainFrame::OnMenuProject(wxCommandEvent& e)
{
    wxString label = MenuLabel(Project, e.GetId());

    if (label == wxT("添加电路..."))              AddCircuit();
    else if (label == wxT("上移电路"))            MoveCircuitUp();
    else if (label == wxT("下移电路"))            MoveCircuitDown();
    else if (label == wxT("设为主电路"))          SwitchCircuit(0);
    else if (label == wxT("移除电路"))            RemoveCircuit();
    else if (label == wxT("查看工具箱"))          m_treeCtrl1->ExpandAll();
    else if (label == wxT("查看仿真树"))          Status(wxT("仿真树"));
    else if (label == wxT("电路统计"))            DoStatistics();
    else if (label == wxT("恢复默认外观"))        Status(wxT("已恢复默认外观"));
    else Status(label + wxT("（尚未实现）"));
}

void MainFrame::DoStatistics()
{
    Circuit* c = m_canvas->GetCircuit();
    if (!c) return;
    int gates = 0, pins = 0;
    for (auto& comp : c->comps)
    {
        if (IsGateType(comp.type)) ++gates;
        else ++pins;
    }
    wxString msg = wxString::Format(
        wxT("电路：%s\n逻辑门：%d\n引脚：%d\n导线：%d\n文本标签：%d"),
        c->name, gates, pins, (int)c->wires.size(), (int)c->texts.size());
    wxMessageBox(msg, wxT("电路统计"), wxOK | wxICON_INFORMATION);
}

void MainFrame::OnMenuSimulate(wxCommandEvent& e)
{
    wxString label = MenuLabel(Simulate, e.GetId());

    if (label == wxT("启用仿真"))
    {
        bool on = !m_canvas->IsSimulationEnabled();
        m_canvas->SetSimulationEnabled(on);
        wxMenuItem* mi = FindByLabel(Simulate, label);
        if (mi) mi->Check(on);
        Status(on ? wxT("仿真已启用") : wxT("仿真已关闭"));
    }
    else if (label == wxT("重置仿真"))           m_canvas->ResetSimulation();
    else if (label == wxT("单步仿真"))           m_canvas->StepSimulation();
    else if (label == wxT("单步执行"))           m_canvas->StepSimulation();
    else if (label == wxT("启用时钟"))           Status(wxT("组合逻辑电路不需要时钟"));
    else if (label == wxT("日志..."))            Status(wxT("日志功能尚未实现"));
}

void MainFrame::OnMenuWindow(wxCommandEvent& e)
{
    wxString label = MenuLabel(Window, e.GetId());

    if (label == wxT("最小化"))                  Iconize(true);
    else if (label == wxT("最大化"))             Maximize(true);
    else if (label == wxT("关闭"))               Close(true);
    else if (label == wxT("组合逻辑分析"))       Status(wxT("组合逻辑分析尚未实现"));
    else if (label == wxT("首选项"))             Status(wxT("首选项功能尚未实现"));
    else Status(label);
}

void MainFrame::OnMenuHelp(wxCommandEvent& e)
{
    wxString label = MenuLabel(Help, e.GetId());

    if (label == wxT("关于..."))
    {
        wxMessageBox(
            wxT("数字逻辑电路绘图与仿真软件\n\n")
            wxT("基于 wxWidgets 开发的轻量数字逻辑电路编辑器。\n")
            wxT("可从左侧放置逻辑门，连接导线并运行仿真。"),
            wxT("关于"), wxOK | wxICON_INFORMATION);
    }
    else Status(label + wxT("（尚未实现）"));
}

void MainFrame::OnPropertyChanged(wxPropertyGridEvent& e)
{
    wxPGProperty* p = e.GetProperty();
    if (!p) return;

    Circuit* c = m_canvas->GetCircuit();
    Component* comp = c ? c->FindComponent(m_propCompId) : nullptr;
    if (!comp) return;

    wxString name = p->GetName();
    if (name == wxT("label"))
    {
        comp->label = p->GetValueAsString();
    }
    else if (name == wxT("inputs"))
    {
        int n = p->GetValue().GetInteger();
        m_canvas->SetSelectedInputs(n);
        Status(wxString::Format(wxT("输入数量已调整为 %d"), n));
    }
    else if (name == wxT("value"))
    {
        comp->value = p->GetValue().GetBool() ? 1 : 0;
        m_canvas->SetSimulationEnabled(true);
    }
    m_canvas->Refresh();
}

void MainFrame::OnSpin(wxSpinEvent&)
{
    m_canvas->SetZoom(m_spinBtn1->GetValue() / 100.0);
}

// ---------------------------------------------------------------------------
// File actions
// ---------------------------------------------------------------------------

void MainFrame::DoNew()
{
    if (wxMessageBox(wxT("是否清空当前工程（所有电路）？"), wxT("新建"),
                     wxYES_NO | wxICON_QUESTION) != wxYES)
        return;
    m_circuits.clear();
    m_circuits.push_back(Circuit());
    m_curCircuit = 0;
    m_currentFile.Clear();
    RelinkCircuits();
    BuildTree();
    UpdateTitle();
    Status(wxT("已新建工程"));
}

void MainFrame::DoOpen()
{
    wxFileDialog dlg(this, wxT("打开电路"), wxT(""), wxT(""),
                     wxT("电路文件 (*.circ)|*.circ|所有文件 (*.*)|*.*"),
                     wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() != wxID_OK) return;

    wxString path = dlg.GetPath();
    wxFile f(path);
    if (!f.IsOpened())
    {
        wxMessageBox(wxT("无法打开文件。"), wxT("错误"), wxOK | wxICON_ERROR);
        return;
    }
    size_t len = f.Length();
    std::string buf(len, '\0');
    if (len) f.Read(&buf[0], len);
    f.Close();
    wxString text = wxString::FromUTF8(buf.data(), len);

    std::vector<Circuit> loaded;
    if (!DeserializeProject(text, loaded) || loaded.empty())
    {
        wxMessageBox(wxT("文件不是有效的电路文件。"), wxT("错误"), wxOK | wxICON_ERROR);
        return;
    }

    m_circuits.swap(loaded);
    m_curCircuit = 0;
    RelinkCircuits();
    m_currentFile = path;
    BuildTree();
    UpdateTitle();
    Status(wxT("已打开 ") + path);
}

void MainFrame::DoSave(bool saveAs)
{
    wxString path = m_currentFile;
    if (saveAs || path.IsEmpty())
    {
        wxFileDialog dlg(this, wxT("保存电路"), wxT(""), wxT("电路.circ"),
                         wxT("电路文件 (*.circ)|*.circ|所有文件 (*.*)|*.*"),
                         wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (dlg.ShowModal() != wxID_OK) return;
        path = dlg.GetPath();
        if (!path.Lower().EndsWith(wxT(".circ"))) path += wxT(".circ");
    }

    wxString text = SerializeProject(m_circuits);
    wxFile f(path, wxFile::write);
    if (!f.IsOpened())
    {
        wxMessageBox(wxT("无法写入文件。"), wxT("错误"), wxOK | wxICON_ERROR);
        return;
    }
    std::string utf8 = text.utf8_string();
    f.Write(utf8.c_str(), utf8.size());
    f.Close();

    m_currentFile = path;
    m_circuits[m_curCircuit].name = wxFileName(path).GetName();
    BuildTree();
    UpdateTitle();
    Status(wxT("已保存至 ") + path);
}

void MainFrame::DoExportImage()
{
    wxFileDialog dlg(this, wxT("导出图像"), wxT(""), wxT("电路.png"),
                     wxT("PNG 图像 (*.png)|*.png"),
                     wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() != wxID_OK) return;
    wxString path = dlg.GetPath();
    if (!path.Lower().EndsWith(wxT(".png"))) path += wxT(".png");
    m_canvas->ExportImage(path);
}
