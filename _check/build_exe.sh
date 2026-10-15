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

VS='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231'
BIN="$VS\\bin\\Hostx64\\x64"
MSVCINC="$VS\\include"
MSVCLIB="$VS\\lib\\x64"
KIT='C:\Program Files (x86)\Windows Kits\10'
SDKV='10.0.26100.0'
RC="$KIT\\bin\\$SDKV\\x64\\rc.exe"     # the resource compiler lives in the SDK
# link.exe shells out to rc.exe when it embeds the manifest, so it must be
# reachable through PATH.
export PATH="/c/Program Files (x86)/Windows Kits/10/bin/$SDKV/x64:$PATH"
WX='F:\EDA\basic\basic_tools\wxWidgets-3.2.2.1'
SRC='F:\EDA\wxWidgets_project\Project1'
CHK='F:\EDA\wxWidgets_project\_check'
OUT="$CHK\\build"
DESK='C:\Users\34854\Desktop'
EXE="$DESK\\LogicSim.exe"

mkdir -p "$OUT"
rm -f "$EXE"

# NB: every /I must stay one quoted argument -- these paths contain spaces.
INC=("/I$WX\\include" "/I$WX\\include\\msvc" "/I$MSVCINC"
     "/I$KIT\\Include\\$SDKV\\ucrt" "/I$KIT\\Include\\$SDKV\\um"
     "/I$KIT\\Include\\$SDKV\\shared" "/I$KIT\\Include\\$SDKV\\winrt"
     "/I$SRC")

# ---------------------------------------------------------------- resources
# NB: every /I must stay one quoted argument -- these paths contain spaces.
echo "--- resources ---"
RCINC=("/I$WX\\include" "/I$WX\\include\\msvc"
       "/I$KIT\\Include\\$SDKV\\um" "/I$KIT\\Include\\$SDKV\\shared"
       "/I$KIT\\Include\\$SDKV\\ucrt")
"$RC" /nologo /D_UNICODE /DUNICODE "${RCINC[@]}" /fo"$OUT\\app.res" "$CHK\\app.rc"
if [ $? -ne 0 ]; then echo "rc failed"; exit 1; fi

# ------------------------------------------------------------------ compile
echo "--- compile ---"
"$BIN\\cl.exe" /nologo /c /EHsc /std:c++20 /utf-8 /W3 /O2 /Zc:__cplusplus \
  /D_UNICODE /DUNICODE /DWIN32 /D_WINDOWS /D_CRT_SECURE_NO_WARNINGS \
  /D_DEBUG /MDd "${INC[@]}" /Fo"$OUT\\" \
  "$SRC\\main.cpp" "$SRC\\mainframe.cpp" "$SRC\\canvas.cpp" \
  "$SRC\\model.cpp" "$SRC\\wirenet.cpp" "$SRC\\eda.cpp"
if [ $? -ne 0 ]; then echo "cl failed"; exit 1; fi

# --------------------------------------------------------------------- link
echo "--- link ---"
"$BIN\\link.exe" /nologo /SUBSYSTEM:WINDOWS /MACHINE:X64 \
  /MANIFEST:EMBED \
  /LIBPATH:"$WX\\lib\\vc_x64_lib" /LIBPATH:"$MSVCLIB" \
  /LIBPATH:"$KIT\\Lib\\$SDKV\\ucrt\\x64" /LIBPATH:"$KIT\\Lib\\$SDKV\\um\\x64" \
  /OUT:"$EXE" \
  "$OUT\\main.obj" "$OUT\\mainframe.obj" "$OUT\\canvas.obj" \
  "$OUT\\model.obj" "$OUT\\wirenet.obj" "$OUT\\eda.obj" "$OUT\\app.res" \
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
