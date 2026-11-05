///////////////////////////////////////////////////////////////////////////
// keybindings.h -- key-code predicates shared by the canvas and its tests.
//
// Kept free of GUI dependencies on purpose: the handler in canvas.cpp and the
// standalone keybind_check test must agree on exactly one definition of what
// "delete the selection" means.
///////////////////////////////////////////////////////////////////////////

#pragma once

#include <wx/defs.h>

namespace eda
{
    // Backspace is treated exactly like Delete: on Windows both keys are
    // expected to remove the current selection.  Text labels are edited in a
    // modal wxGetTextFromUser dialog, which owns the keyboard focus, so this
    // predicate never steals Backspace from a text edit.
    //
    // OnKey is bound to both wxEVT_KEY_DOWN and wxEVT_CHAR, so a key may be
    // delivered twice.  DeleteSelection() returns immediately when nothing is
    // selected, which makes the second delivery a harmless no-op.
    inline bool IsDeleteKey(int keyCode)
    {
        return keyCode == WXK_DELETE ||
               keyCode == WXK_NUMPAD_DELETE ||
               keyCode == WXK_BACK;
    }
}
