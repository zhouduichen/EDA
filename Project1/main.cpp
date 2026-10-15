///////////////////////////////////////////////////////////////////////////
// main.cpp -- application entry point.
///////////////////////////////////////////////////////////////////////////

#include <wx/wx.h>
#include "mainframe.h"

class EdaApp : public wxApp
{
public:
    bool OnInit() override
    {
        if (!wxApp::OnInit()) return false;

        // wxWidgets needs an image handler before saving PNG files.
        wxInitAllImageHandlers();

        MainFrame* frame = new MainFrame(nullptr);
        frame->Show(true);
        SetTopWindow(frame);
        return true;
    }
};

wxIMPLEMENT_APP(EdaApp);
