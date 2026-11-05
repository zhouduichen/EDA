///////////////////////////////////////////////////////////////////////////
// test_keybind.cpp -- checks the exact predicate used by CircuitCanvas::OnKey.
//
// This is not a copy of the rule under test: it includes keybindings.h, the
// same header the canvas calls, so a regression in the key mapping fails here.
///////////////////////////////////////////////////////////////////////////

#include "keybindings.h"

#include <cstdio>

namespace
{
    int g_failed = 0;

    void Check(int key, const char* name, bool expected)
    {
        const bool got = eda::IsDeleteKey(key);
        std::printf("  %-22s expect=%-5s got=%-5s %s\n",
                    name,
                    expected ? "true" : "false",
                    got      ? "true" : "false",
                    got == expected ? "OK" : "FAIL");
        if (got != expected) ++g_failed;
    }
}

int main()
{
    std::printf("keybind test (eda::IsDeleteKey, same function as canvas.cpp)\n");

    // All three keys must delete the selection.
    Check(WXK_BACK,          "WXK_BACK",          true);
    Check(WXK_DELETE,        "WXK_DELETE",        true);
    Check(WXK_NUMPAD_DELETE, "WXK_NUMPAD_DELETE", true);

    // Backspace must not turn unrelated editing / navigation keys into deletes.
    Check('A',        "'A'",        false);
    Check('Z',        "'Z'",        false);
    Check(WXK_ESCAPE, "WXK_ESCAPE", false);
    Check(WXK_RETURN, "WXK_RETURN", false);
    Check(WXK_TAB,    "WXK_TAB",    false);
    Check(WXK_LEFT,   "WXK_LEFT",   false);
    Check(WXK_SPACE,  "WXK_SPACE",  false);
    Check(0,          "none",       false);

    std::printf("keybind test: %s (%d failed)\n",
                g_failed ? "FAILED" : "ALL OK", g_failed);
    return g_failed ? 1 : 0;
}
