#!/bin/bash
# Build a standalone Windows executable and drop it on the desktop.
#
# Only the *debug* wxWidgets static libraries are installed
# (lib/vc_x64_lib/*ud.lib), so the whole toolchain is run in the matching
# flavour: /D_DEBUG + /MDd.  Everything is linked statically, so the result is
# one self-contained .exe; the only thing it still needs at run time is the
# Visual C++ debug runtime, which ships with Visual Studio.
set -u
export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL='*'

# Toolchain (VS/SDK via vswhere, wxWidgets via WXWIN or env.local.sh):
# see env.sh.  Sourced, not executed, so it may not use `exit`.
. "$(dirname "$0")/env.sh"

BLD="$CHK\\build"
EXE="$BLD\\LogicSim.exe"
# Optional convenience: drop a copy on a desktop directory of your choice
# (set DESK=... in _check/env.local.sh).
DESK="${DESK:-}"

mkdir -p "$BLD"
# EXE is a Windows-style path; rm needs the POSIX form (MSYS_NO_PATHCONV=1
# above deliberately disables automatic conversion for cl/link arguments).
# Without this a failed relink could leave a stale exe behind.
rm -f "$(cygpath -u "$EXE")"
# link.exe shells out to rc.exe when it embeds the manifest, so it must be
# reachable through PATH.
export PATH="$(cygpath -u "$KIT")/bin/$SDKV/x64:$PATH"

# NB: every /I must stay one quoted argument -- these paths contain spaces.
INC=("/I$WX\\include" "/I$WX\\include\\msvc" "/I$MSVCINC"
     "/I$KIT\\Include\\${SDKV}\\ucrt" "/I$KIT\\Include\\${SDKV}\\um"
     "/I$KIT\\Include\\${SDKV}\\shared" "/I$KIT\\Include\\${SDKV}\\winrt"
     "/I$SRC")

# ---------------------------------------------------------------- resources
# NB: every /I must stay one quoted argument -- these paths contain spaces.
echo "--- resources ---"
RCINC=("/I$WX\\include" "/I$WX\\include\\msvc"
       "/I$KIT\\Include\\${SDKV}\\um" "/I$KIT\\Include\\${SDKV}\\shared"
       "/I$KIT\\Include\\${SDKV}\\ucrt")
"$RC" /nologo /D_UNICODE /DUNICODE "${RCINC[@]}" /fo"$BLD\\app.res" "$CHK\\app.rc"
if [ $? -ne 0 ]; then echo "rc failed"; exit 1; fi

# ------------------------------------------------------------------ compile
echo "--- compile ---"
"$BIN\\cl.exe" /nologo /c /EHsc /std:c++20 /utf-8 /W3 /O2 /Zc:__cplusplus \
  /D_UNICODE /DUNICODE /DWIN32 /D_WINDOWS /D_CRT_SECURE_NO_WARNINGS \
  /D_DEBUG /MDd "${INC[@]}" /Fo"$BLD\\" \
  "$SRC\\main.cpp" "$SRC\\mainframe.cpp" "$SRC\\canvas.cpp" \
  "$SRC\\model.cpp" "$SRC\\wirenet.cpp" "$SRC\\netlist.cpp" "$SRC\\eda.cpp"
if [ $? -ne 0 ]; then echo "cl failed"; exit 1; fi

# --------------------------------------------------------------------- link
echo "--- link ---"
"$BIN\\link.exe" /nologo /SUBSYSTEM:WINDOWS /MACHINE:X64 \
  /MANIFEST:EMBED \
  /LIBPATH:"$WX\\lib\\vc_x64_lib" /LIBPATH:"$MSVCLIB" \
  /LIBPATH:"$KIT\\Lib\\${SDKV}\\ucrt\\x64" /LIBPATH:"$KIT\\Lib\\${SDKV}\\um\\x64" \
  /OUT:"$EXE" \
  "$BLD\\main.obj" "$BLD\\mainframe.obj" "$BLD\\canvas.obj" \
  "$BLD\\model.obj" "$BLD\\wirenet.obj" "$BLD\\netlist.obj" "$BLD\\eda.obj" "$BLD\\app.res" \
  wxzlibd.lib wxtiffd.lib wxscintillad.lib wxregexud.lib wxpngd.lib wxmsw32ud_xrc.lib \
  wxmsw32ud_webview.lib wxmsw32ud_stc.lib wxmsw32ud_richtext.lib wxmsw32ud_ribbon.lib \
  wxmsw32ud_qa.lib wxmsw32ud_propgrid.lib wxmsw32ud_media.lib wxmsw32ud_html.lib \
  wxmsw32ud_gl.lib wxmsw32ud_core.lib wxmsw32ud_aui.lib wxmsw32ud_adv.lib wxjpegd.lib \
  wxexpatd.lib wxbase32ud_xml.lib wxbase32ud_net.lib wxbase32ud.lib \
  ucrtd.lib vcruntimed.lib msvcrtd.lib msvcprtd.lib \
  kernel32.lib user32.lib gdi32.lib comdlg32.lib comctl32.lib shell32.lib \
  ole32.lib oleaut32.lib uuid.lib advapi32.lib winspool.lib shlwapi.lib version.lib
if [ $? -ne 0 ]; then echo "link failed"; exit 1; fi

ls -la "$EXE"
if [ -n "$DESK" ]; then
    cp -f "$(cygpath -u "$EXE")" "$(cygpath -u "$DESK")/LogicSim.exe" \
        && echo "copied to $(cygpath -u "$DESK")/LogicSim.exe"
fi
