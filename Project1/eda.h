///////////////////////////////////////////////////////////////////////////
// C++ code generated with wxFormBuilder (version Jun 17 2015)
// http://www.wxformbuilder.org/
//
// PLEASE DO "NOT" EDIT THIS FILE!
///////////////////////////////////////////////////////////////////////////

#ifndef __NONAME_H__
#define __NONAME_H__

#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/string.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/icon.h>
#include <wx/menu.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/toolbar.h>
#include <wx/bmpbuttn.h>
#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/treectrl.h>
#include <wx/statline.h>
#include <wx/propgrid/propgrid.h>
#include <wx/propgrid/advprops.h>
#include <wx/textctrl.h>
#include <wx/spinbutt.h>
#include <wx/panel.h>
#include <wx/frame.h>

///////////////////////////////////////////////////////////////////////////


///////////////////////////////////////////////////////////////////////////////
/// Class MyFrame2
///////////////////////////////////////////////////////////////////////////////
class MyFrame2 : public wxFrame 
{
	private:
	
	protected:
		wxMenuBar* m_menubar2;
		wxMenu* File;
		wxMenu* Edit;
		wxMenu* Project;
		wxMenu* Simulate;
		wxMenu* Window;
		wxMenu* Help;
		wxToolBar* m_toolBar4;
		wxToolBarToolBase* change_values_within_circuit; 
		wxToolBarToolBase* edit_selection_and_add_wires; 
		wxToolBarToolBase* edit_text_in_circuit; 
		wxToolBarToolBase* Add_Pin; 
		wxToolBarToolBase* Add_Pin2; 
		wxToolBarToolBase* Add_Not_Gate; 
		wxToolBarToolBase* Add_And_Gate; 
		wxToolBarToolBase* Add_Or_Gate; 
		wxPanel* left_panel;
		wxBitmapButton* Show_Project_Circuit;
		wxBitmapButton* Show_Simulation;
		wxBitmapButton* EditViewd_Latout;
		wxBitmapButton* EditViewed_Apperance;
		wxBitmapButton* Add_Circuit;
		wxBitmapButton* Move_ViewedUP;
		wxBitmapButton* Move_ViewedDOWN;
		wxBitmapButton* Remove_Viewed;
		wxTreeCtrl* m_treeCtrl1;
		wxStaticLine* m_staticline1;
		wxPropertyGrid* m_propertyGrid1;
		wxTextCtrl* m_textCtrl2;
		wxSpinButton* m_spinBtn1;
		wxPanel* m_panel4;
	
	public:
		
		MyFrame2( wxWindow* parent, wxWindowID id = wxID_ANY, const wxString& title = wxEmptyString, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize( 1000,600 ), long style = wxDEFAULT_FRAME_STYLE|wxTAB_TRAVERSAL );
		
		~MyFrame2();
	
};

#endif //__NONAME_H__
