///////////////////////////////////////////////////////////////////////////
// C++ code generated with wxFormBuilder (version Jun 17 2015)
// http://www.wxformbuilder.org/
//
// PLEASE DO "NOT" EDIT THIS FILE!
///////////////////////////////////////////////////////////////////////////

#include "noname.h"

///////////////////////////////////////////////////////////////////////////

MyFrame2::MyFrame2( wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style ) : wxFrame( parent, id, title, pos, size, style )
{
	this->SetSizeHints( wxDefaultSize, wxDefaultSize );
	this->SetBackgroundColour( wxSystemSettings::GetColour( wxSYS_COLOUR_HIGHLIGHTTEXT ) );
	
	m_menubar2 = new wxMenuBar( 0 );
	File = new wxMenu();
	wxMenuItem* m_menuItem_New;
	m_menuItem_New = new wxMenuItem( File, wxID_ANY, wxString( wxT("新建") ) , wxEmptyString, wxITEM_NORMAL );
	File->Append( m_menuItem_New );
	
	wxMenuItem* m_menuItem_Open;
	m_menuItem_Open = new wxMenuItem( File, wxID_ANY, wxString( wxT("打开") ) , wxEmptyString, wxITEM_NORMAL );
	File->Append( m_menuItem_Open );
	
	wxMenuItem* m_menuItem_Open_Recent;
	m_menuItem_Open_Recent = new wxMenuItem( File, wxID_ANY, wxString( wxT("最近打开") ) , wxEmptyString, wxITEM_NORMAL );
	File->Append( m_menuItem_Open_Recent );
	
	File->AppendSeparator();
	
	wxMenuItem* m_menuItem_Close;
	m_menuItem_Close = new wxMenuItem( File, wxID_ANY, wxString( wxT("关闭") ) , wxEmptyString, wxITEM_NORMAL );
	File->Append( m_menuItem_Close );
	
	wxMenuItem* m_menuItem_Save;
	m_menuItem_Save = new wxMenuItem( File, wxID_ANY, wxString( wxT("保存") ) , wxEmptyString, wxITEM_NORMAL );
	File->Append( m_menuItem_Save );
	
	wxMenuItem* m_menuItem_Save_As;
	m_menuItem_Save_As = new wxMenuItem( File, wxID_ANY, wxString( wxT("另存为") ) , wxEmptyString, wxITEM_NORMAL );
	File->Append( m_menuItem_Save_As );
	
	File->AppendSeparator();
	
	wxMenuItem* m_menuItem_Export_Image;
	m_menuItem_Export_Image = new wxMenuItem( File, wxID_ANY, wxString( wxT("导出图像") ) , wxEmptyString, wxITEM_NORMAL );
	File->Append( m_menuItem_Export_Image );
	
	wxMenuItem* m_menuItem_Print;
	m_menuItem_Print = new wxMenuItem( File, wxID_ANY, wxString( wxT("打印") ) , wxEmptyString, wxITEM_NORMAL );
	File->Append( m_menuItem_Print );
	
	File->AppendSeparator();
	
	wxMenuItem* m_menuItem_Preferences;
	m_menuItem_Preferences = new wxMenuItem( File, wxID_ANY, wxString( wxT("首选项...") ) , wxEmptyString, wxITEM_NORMAL );
	File->Append( m_menuItem_Preferences );
	
	File->AppendSeparator();
	
	wxMenuItem* m_menuItem_Exit;
	m_menuItem_Exit = new wxMenuItem( File, wxID_ANY, wxString( wxT("退出") ) , wxEmptyString, wxITEM_NORMAL );
	File->Append( m_menuItem_Exit );
	
	m_menubar2->Append( File, wxT("文件") ); 
	
	Edit = new wxMenu();
	wxMenuItem* m_menuItem_Undo_Delete_Selection;
	m_menuItem_Undo_Delete_Selection = new wxMenuItem( Edit, wxID_ANY, wxString( wxT("撤销") ) , wxEmptyString, wxITEM_NORMAL );
	Edit->Append( m_menuItem_Undo_Delete_Selection );
	
	Edit->AppendSeparator();
	
	wxMenuItem* m_menuItem_Cut;
	m_menuItem_Cut = new wxMenuItem( Edit, wxID_ANY, wxString( wxT("剪切") ) , wxEmptyString, wxITEM_NORMAL );
	Edit->Append( m_menuItem_Cut );
	
	wxMenuItem* m_menuItem_Copy;
	m_menuItem_Copy = new wxMenuItem( Edit, wxID_ANY, wxString( wxT("复制") ) , wxEmptyString, wxITEM_NORMAL );
	Edit->Append( m_menuItem_Copy );
	
	wxMenuItem* m_menuItem_Paste;
	m_menuItem_Paste = new wxMenuItem( Edit, wxID_ANY, wxString( wxT("粘贴") ) , wxEmptyString, wxITEM_NORMAL );
	Edit->Append( m_menuItem_Paste );
	
	Edit->AppendSeparator();
	
	wxMenuItem* m_menuItem_Delete;
	m_menuItem_Delete = new wxMenuItem( Edit, wxID_ANY, wxString( wxT("删除") ) , wxEmptyString, wxITEM_NORMAL );
	Edit->Append( m_menuItem_Delete );
	
	wxMenuItem* m_menuItem_Duplicate;
	m_menuItem_Duplicate = new wxMenuItem( Edit, wxID_ANY, wxString( wxT("创建副本") ) , wxEmptyString, wxITEM_NORMAL );
	Edit->Append( m_menuItem_Duplicate );
	
	wxMenuItem* m_menuItem_Select_All;
	m_menuItem_Select_All = new wxMenuItem( Edit, wxID_ANY, wxString( wxT("全选") ) , wxEmptyString, wxITEM_NORMAL );
	Edit->Append( m_menuItem_Select_All );
	
	Edit->AppendSeparator();
	
	wxMenuItem* m_menuItem_Raise_Selection;
	m_menuItem_Raise_Selection = new wxMenuItem( Edit, wxID_ANY, wxString( wxT("上移一层") ) , wxEmptyString, wxITEM_NORMAL );
	Edit->Append( m_menuItem_Raise_Selection );
	
	wxMenuItem* m_menuItem_Lower_Selection;
	m_menuItem_Lower_Selection = new wxMenuItem( Edit, wxID_ANY, wxString( wxT("下移一层") ) , wxEmptyString, wxITEM_NORMAL );
	Edit->Append( m_menuItem_Lower_Selection );
	
	wxMenuItem* m_menuItem_Raise_To_Top;
	m_menuItem_Raise_To_Top = new wxMenuItem( Edit, wxID_ANY, wxString( wxT("置于顶层") ) , wxEmptyString, wxITEM_NORMAL );
	Edit->Append( m_menuItem_Raise_To_Top );
	
	wxMenuItem* m_menuItem_Lower_To_Bottom;
	m_menuItem_Lower_To_Bottom = new wxMenuItem( Edit, wxID_ANY, wxString( wxT("置于底层") ) , wxEmptyString, wxITEM_NORMAL );
	Edit->Append( m_menuItem_Lower_To_Bottom );
	
	Edit->AppendSeparator();
	
	wxMenuItem* m_menuItem_Add_Vertex;
	m_menuItem_Add_Vertex = new wxMenuItem( Edit, wxID_ANY, wxString( wxT("添加拐点") ) , wxEmptyString, wxITEM_NORMAL );
	Edit->Append( m_menuItem_Add_Vertex );
	
	wxMenuItem* m_menuItem_Remove_Vertex;
	m_menuItem_Remove_Vertex = new wxMenuItem( Edit, wxID_ANY, wxString( wxT("删除拐点") ) , wxEmptyString, wxITEM_NORMAL );
	Edit->Append( m_menuItem_Remove_Vertex );
	
	m_menubar2->Append( Edit, wxT("编辑") ); 
	
	Project = new wxMenu();
	wxMenuItem* m_menuItem_Add_Circuit;
	m_menuItem_Add_Circuit = new wxMenuItem( Project, wxID_ANY, wxString( wxT("添加电路...") ) , wxEmptyString, wxITEM_NORMAL );
	Project->Append( m_menuItem_Add_Circuit );
	
	wxMenuItem* m_menuItem_Load_Library;
	m_menuItem_Load_Library = new wxMenuItem( Project, wxID_ANY, wxString( wxT("加载库") ) , wxEmptyString, wxITEM_NORMAL );
	Project->Append( m_menuItem_Load_Library );
	
	wxMenuItem* m_menuItem_Unload_Libraries ;
	m_menuItem_Unload_Libraries  = new wxMenuItem( Project, wxID_ANY, wxString( wxT("卸载库...") ) , wxEmptyString, wxITEM_NORMAL );
	Project->Append( m_menuItem_Unload_Libraries  );
	
	Project->AppendSeparator();
	
	wxMenuItem* m_menuItem_Move_Circuit_Up;
	m_menuItem_Move_Circuit_Up = new wxMenuItem( Project, wxID_ANY, wxString( wxT("上移电路") ) , wxEmptyString, wxITEM_NORMAL );
	Project->Append( m_menuItem_Move_Circuit_Up );
	
	wxMenuItem* m_menuItem_Move_Circuit_Down;
	m_menuItem_Move_Circuit_Down = new wxMenuItem( Project, wxID_ANY, wxString( wxT("下移电路") ) , wxEmptyString, wxITEM_NORMAL );
	Project->Append( m_menuItem_Move_Circuit_Down );
	
	wxMenuItem* m_menuItem_Set_As_Main_Circuit;
	m_menuItem_Set_As_Main_Circuit = new wxMenuItem( Project, wxID_ANY, wxString( wxT("设为主电路") ) , wxEmptyString, wxITEM_NORMAL );
	Project->Append( m_menuItem_Set_As_Main_Circuit );
	
	wxMenuItem* m_menuItem_Remove_Circuit;
	m_menuItem_Remove_Circuit = new wxMenuItem( Project, wxID_ANY, wxString( wxT("移除电路") ) , wxEmptyString, wxITEM_NORMAL );
	Project->Append( m_menuItem_Remove_Circuit );
	
	wxMenuItem* m_menuItem_Revert_To_Default_Appearance;
	m_menuItem_Revert_To_Default_Appearance = new wxMenuItem( Project, wxID_ANY, wxString( wxT("恢复默认外观") ) , wxEmptyString, wxITEM_NORMAL );
	Project->Append( m_menuItem_Revert_To_Default_Appearance );
	
	Project->AppendSeparator();
	
	wxMenuItem* m_menuItem_View_Toolbox;
	m_menuItem_View_Toolbox = new wxMenuItem( Project, wxID_ANY, wxString( wxT("查看工具箱") ) , wxEmptyString, wxITEM_NORMAL );
	Project->Append( m_menuItem_View_Toolbox );
	
	wxMenuItem* m_menuItem_View_Simulation_Tree;
	m_menuItem_View_Simulation_Tree = new wxMenuItem( Project, wxID_ANY, wxString( wxT("查看仿真树") ) , wxEmptyString, wxITEM_NORMAL );
	Project->Append( m_menuItem_View_Simulation_Tree );
	
	wxMenuItem* m_menuItem_Edit_Circuit_Layout;
	m_menuItem_Edit_Circuit_Layout = new wxMenuItem( Project, wxID_ANY, wxString( wxT("编辑电路布局") ) , wxEmptyString, wxITEM_NORMAL );
	Project->Append( m_menuItem_Edit_Circuit_Layout );
	
	wxMenuItem* m_menuItem_Edit_Circuit_Appearance;
	m_menuItem_Edit_Circuit_Appearance = new wxMenuItem( Project, wxID_ANY, wxString( wxT("编辑电路外观") ) , wxEmptyString, wxITEM_NORMAL );
	Project->Append( m_menuItem_Edit_Circuit_Appearance );
	
	Project->AppendSeparator();
	
	wxMenuItem* m_menuItem_Analyze_Circuit;
	m_menuItem_Analyze_Circuit = new wxMenuItem( Project, wxID_ANY, wxString( wxT("分析电路") ) , wxEmptyString, wxITEM_NORMAL );
	Project->Append( m_menuItem_Analyze_Circuit );
	
	wxMenuItem* m_menuItem_Get_Circuit_Statistics;
	m_menuItem_Get_Circuit_Statistics = new wxMenuItem( Project, wxID_ANY, wxString( wxT("电路统计") ) , wxEmptyString, wxITEM_NORMAL );
	Project->Append( m_menuItem_Get_Circuit_Statistics );
	
	Project->AppendSeparator();
	
	wxMenuItem* m_menuItem_Options;
	m_menuItem_Options = new wxMenuItem( Project, wxID_ANY, wxString( wxT("选项...") ) , wxEmptyString, wxITEM_NORMAL );
	Project->Append( m_menuItem_Options );
	
	m_menubar2->Append( Project, wxT("项目") ); 
	
	Simulate = new wxMenu();
	wxMenuItem* m_menuItem_Simulation_Enabled;
	m_menuItem_Simulation_Enabled = new wxMenuItem( Simulate, wxID_ANY, wxString( wxT("启用仿真") ) , wxEmptyString, wxITEM_CHECK );
	Simulate->Append( m_menuItem_Simulation_Enabled );
	
	wxMenuItem* m_menuItem_Reset_Simulation;
	m_menuItem_Reset_Simulation = new wxMenuItem( Simulate, wxID_ANY, wxString( wxT("重置仿真") ) , wxEmptyString, wxITEM_NORMAL );
	Simulate->Append( m_menuItem_Reset_Simulation );
	
	wxMenuItem* m_menuItem_Step_Simulation;
	m_menuItem_Step_Simulation = new wxMenuItem( Simulate, wxID_ANY, wxString( wxT("单步仿真") ) , wxEmptyString, wxITEM_NORMAL );
	Simulate->Append( m_menuItem_Step_Simulation );
	
	Simulate->AppendSeparator();
	
	wxMenuItem* m_menuItem_Tick_Once;
	m_menuItem_Tick_Once = new wxMenuItem( Simulate, wxID_ANY, wxString( wxT("单步执行") ) , wxEmptyString, wxITEM_NORMAL );
	Simulate->Append( m_menuItem_Tick_Once );
	
	wxMenuItem* m_menuItem_Ticks_Enabled;
	m_menuItem_Ticks_Enabled = new wxMenuItem( Simulate, wxID_ANY, wxString( wxT("启用时钟") ) , wxEmptyString, wxITEM_NORMAL );
	Simulate->Append( m_menuItem_Ticks_Enabled );
	
	Simulate->AppendSeparator();
	
	wxMenuItem* m_menuItem_Logging;
	m_menuItem_Logging = new wxMenuItem( Simulate, wxID_ANY, wxString( wxT("日志...") ) , wxEmptyString, wxITEM_NORMAL );
	Simulate->Append( m_menuItem_Logging );
	
	m_menubar2->Append( Simulate, wxT("仿真") ); 
	
	Window = new wxMenu();
	wxMenuItem* m_menuItem_Minimize;
	m_menuItem_Minimize = new wxMenuItem( Window, wxID_ANY, wxString( wxT("最小化") ) , wxEmptyString, wxITEM_NORMAL );
	Window->Append( m_menuItem_Minimize );
	
	wxMenuItem* m_menuItem_Maximize;
	m_menuItem_Maximize = new wxMenuItem( Window, wxID_ANY, wxString( wxT("最大化") ) , wxEmptyString, wxITEM_NORMAL );
	Window->Append( m_menuItem_Maximize );
	
	wxMenuItem* m_menuItem_Close2;
	m_menuItem_Close2 = new wxMenuItem( Window, wxID_ANY, wxString( wxT("关闭") ) , wxEmptyString, wxITEM_NORMAL );
	Window->Append( m_menuItem_Close2 );
	
	Window->AppendSeparator();
	
	wxMenuItem* m_menuItem_Combinational_Analysis;
	m_menuItem_Combinational_Analysis = new wxMenuItem( Window, wxID_ANY, wxString( wxT("组合逻辑分析") ) , wxEmptyString, wxITEM_NORMAL );
	Window->Append( m_menuItem_Combinational_Analysis );
	
	wxMenuItem* m_menuItem_Preferences2;
	m_menuItem_Preferences2 = new wxMenuItem( Window, wxID_ANY, wxString( wxT("首选项") ) , wxEmptyString, wxITEM_NORMAL );
	Window->Append( m_menuItem_Preferences2 );
	
	Window->AppendSeparator();
	
	wxMenuItem* m_menuItem_Untitled;
	m_menuItem_Untitled = new wxMenuItem( Window, wxID_ANY, wxString( wxT("未命名") ) , wxEmptyString, wxITEM_NORMAL );
	Window->Append( m_menuItem_Untitled );
	
	m_menubar2->Append( Window, wxT("窗口") ); 
	
	Help = new wxMenu();
	wxMenuItem* m_menuItem_Tutorial;
	m_menuItem_Tutorial = new wxMenuItem( Help, wxID_ANY, wxString( wxT("教程") ) , wxEmptyString, wxITEM_NORMAL );
	Help->Append( m_menuItem_Tutorial );
	
	wxMenuItem* m_menuItem_Users_Guide;
	m_menuItem_Users_Guide = new wxMenuItem( Help, wxID_ANY, wxString( wxT("用户指南") ) , wxEmptyString, wxITEM_NORMAL );
	Help->Append( m_menuItem_Users_Guide );
	
	wxMenuItem* m_menuItem_Library_Reference;
	m_menuItem_Library_Reference = new wxMenuItem( Help, wxID_ANY, wxString( wxT("元件库参考") ) , wxEmptyString, wxITEM_NORMAL );
	Help->Append( m_menuItem_Library_Reference );
	
	Help->AppendSeparator();
	
	wxMenuItem* m_menuItem_About;
	m_menuItem_About = new wxMenuItem( Help, wxID_ANY, wxString( wxT("关于...") ) , wxEmptyString, wxITEM_NORMAL );
	Help->Append( m_menuItem_About );
	
	m_menubar2->Append( Help, wxT("帮助") ); 
	
	this->SetMenuBar( m_menubar2 );
	
	m_toolBar4 = this->CreateToolBar( wxTB_HORIZONTAL, wxID_ANY ); 
	m_toolBar4->SetBackgroundColour( wxSystemSettings::GetColour( wxSYS_COLOUR_INACTIVEBORDER ) );
	wxBitmap bmp( 16, 16 );
	
	change_values_within_circuit = m_toolBar4->AddTool( wxID_ANY, wxT("电平"), bmp, bmp, wxITEM_NORMAL, wxEmptyString, wxEmptyString, NULL ); 
	
	edit_selection_and_add_wires = m_toolBar4->AddTool( wxID_ANY, wxT("编辑"), bmp, bmp, wxITEM_NORMAL, wxEmptyString, wxEmptyString, NULL ); 
	
	edit_text_in_circuit = m_toolBar4->AddTool( wxID_ANY, wxT("文本"), bmp, bmp, wxITEM_NORMAL, wxEmptyString, wxEmptyString, NULL ); 
	
	Add_Pin = m_toolBar4->AddTool( wxID_ANY, wxT("输入"), bmp, bmp, wxITEM_NORMAL, wxEmptyString, wxEmptyString, NULL ); 
	
	Add_Pin2 = m_toolBar4->AddTool( wxID_ANY, wxT("输出"), bmp, bmp, wxITEM_NORMAL, wxEmptyString, wxEmptyString, NULL ); 
	
	Add_Not_Gate = m_toolBar4->AddTool( wxID_ANY, wxT("非"), bmp, bmp, wxITEM_NORMAL, wxEmptyString, wxEmptyString, NULL ); 
	
	Add_And_Gate = m_toolBar4->AddTool( wxID_ANY, wxT("与"), bmp, bmp, wxITEM_NORMAL, wxEmptyString, wxEmptyString, NULL ); 
	
	Add_Or_Gate = m_toolBar4->AddTool( wxID_ANY, wxT("或"), bmp, bmp, wxITEM_NORMAL, wxEmptyString, wxEmptyString, NULL ); 
	
	m_toolBar4->Realize(); 
	
	wxBoxSizer* bSizer3;
	bSizer3 = new wxBoxSizer( wxHORIZONTAL );
	
	left_panel = new wxPanel( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
	wxBoxSizer* bSizer4;
	bSizer4 = new wxBoxSizer( wxVERTICAL );
	
	wxBoxSizer* bSizer7;
	bSizer7 = new wxBoxSizer( wxVERTICAL );
	
	wxBoxSizer* bSizer9;
	bSizer9 = new wxBoxSizer( wxHORIZONTAL );
	
	Show_Project_Circuit = new wxBitmapButton( left_panel, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, wxBU_AUTODRAW );
	bSizer9->Add( Show_Project_Circuit, 0, wxALL, 5 );
	
	Show_Simulation = new wxBitmapButton( left_panel, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, wxBU_AUTODRAW );
	bSizer9->Add( Show_Simulation, 0, wxALL, 5 );
	
	EditViewd_Latout = new wxBitmapButton( left_panel, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, wxBU_AUTODRAW );
	bSizer9->Add( EditViewd_Latout, 0, wxALL, 5 );
	
	EditViewed_Apperance = new wxBitmapButton( left_panel, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, wxBU_AUTODRAW );
	bSizer9->Add( EditViewed_Apperance, 0, wxALL, 5 );
	
	
	bSizer7->Add( bSizer9, 1, wxEXPAND, 5 );
	
	wxBoxSizer* bSizer13;
	bSizer13 = new wxBoxSizer( wxHORIZONTAL );
	
	Add_Circuit = new wxBitmapButton( left_panel, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, wxBU_AUTODRAW );
	bSizer13->Add( Add_Circuit, 0, wxALL, 5 );
	
	Move_ViewedUP = new wxBitmapButton( left_panel, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, wxBU_AUTODRAW );
	bSizer13->Add( Move_ViewedUP, 0, wxALL, 5 );
	
	Move_ViewedDOWN = new wxBitmapButton( left_panel, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, wxBU_AUTODRAW );
	bSizer13->Add( Move_ViewedDOWN, 0, wxALL, 5 );
	
	Remove_Viewed = new wxBitmapButton( left_panel, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, wxBU_AUTODRAW );
	bSizer13->Add( Remove_Viewed, 0, wxALL, 5 );
	
	
	bSizer7->Add( bSizer13, 1, wxEXPAND, 5 );
	
	
	bSizer4->Add( bSizer7, 1, wxEXPAND, 5 );
	
	m_treeCtrl1 = new wxTreeCtrl( left_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTR_DEFAULT_STYLE );
	bSizer4->Add( m_treeCtrl1, 1, wxEXPAND, 6 );
	
	m_staticline1 = new wxStaticLine( left_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL );
	bSizer4->Add( m_staticline1, 0, wxEXPAND | wxALL, 5 );
	
	m_propertyGrid1 = new wxPropertyGrid(left_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxPG_DEFAULT_STYLE);
	bSizer4->Add( m_propertyGrid1, 0, wxEXPAND, 5 );
	
	wxBoxSizer* bSizer5;
	bSizer5 = new wxBoxSizer( wxHORIZONTAL );
	
	m_textCtrl2 = new wxTextCtrl( left_panel, wxID_ANY, wxT("100%"), wxDefaultPosition, wxDefaultSize, wxTE_READONLY );
	bSizer5->Add( m_textCtrl2, 1, wxALL, 5 );
	
	m_spinBtn1 = new wxSpinButton( left_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0 );
	bSizer5->Add( m_spinBtn1, 0, wxALIGN_CENTER_VERTICAL, 5 );
	
	
	bSizer4->Add( bSizer5, 0, wxEXPAND, 5 );
	
	
	left_panel->SetSizer( bSizer4 );
	left_panel->Layout();
	bSizer4->Fit( left_panel );
	bSizer3->Add( left_panel, 0, wxEXPAND | wxALL, 5 );
	
	m_panel4 = new wxPanel( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
	bSizer3->Add( m_panel4, 1, wxEXPAND, 5 );
	
	
	this->SetSizer( bSizer3 );
	this->Layout();
	
	this->Centre( wxBOTH );
}

MyFrame2::~MyFrame2()
{
}
