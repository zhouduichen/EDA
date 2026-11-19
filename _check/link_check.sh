#!/bin/bash
# Link the object files produced by compile_check.sh into an exe (smoke test).
set -u
export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL='*'
# Toolchain (VS/SDK via vswhere, wxWidgets via WXWIN or env.local.sh):
# see env.sh.  Sourced, not executed, so it may not use `exit`.
. "$(dirname "$0")/env.sh"
cd "$(dirname "$0")"
# link_check consumes the objects produced by compile_check.sh; refuse to
# link a stale set.
for o in main mainframe canvas model wirenet netlist eda; do
  if [ ! -f "obj/$o.obj" ]; then
    echo "missing obj/$o.obj -- run compile_check.sh first" >&2
    exit 1
  fi
done


"$BIN\link.exe" /nologo /SUBSYSTEM:WINDOWS /MACHINE:X64 /DEBUG \
  /LIBPATH:"$WX\\lib\\vc_x64_lib" \
  /LIBPATH:"$MSVCLIB" \
  /LIBPATH:"$KIT\\Lib\\${SDKV}\\ucrt\\x64" \
  /LIBPATH:"$KIT\\Lib\\${SDKV}\\um\\x64" \
  /OUT:"$OUT\\Project1_check.exe" \
  "$OUT\main.obj" "$OUT\mainframe.obj" "$OUT\canvas.obj" "$OUT\model.obj" "$OUT\wirenet.obj" "$OUT\netlist.obj" "$OUT\eda.obj" \
  wxzlibd.lib wxtiffd.lib wxscintillad.lib wxregexud.lib wxpngd.lib wxmsw32ud_xrc.lib \
  wxmsw32ud_webview.lib wxmsw32ud_stc.lib wxmsw32ud_richtext.lib wxmsw32ud_ribbon.lib \
  wxmsw32ud_qa.lib wxmsw32ud_propgrid.lib wxmsw32ud_media.lib wxmsw32ud_html.lib \
  wxmsw32ud_gl.lib wxmsw32ud_core.lib wxmsw32ud_aui.lib wxmsw32ud_adv.lib wxjpegd.lib \
  wxexpatd.lib wxbase32ud_xml.lib wxbase32ud_net.lib wxbase32ud.lib \
  ucrtd.lib vcruntimed.lib msvcrtd.lib msvcprtd.lib \
  kernel32.lib user32.lib gdi32.lib comdlg32.lib comctl32.lib shell32.lib \
  ole32.lib oleaut32.lib uuid.lib advapi32.lib winspool.lib shlwapi.lib version.lib

rc=$?
echo "exit=$rc"
exit "$rc"
