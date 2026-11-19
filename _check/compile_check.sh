#!/bin/bash
# Compile-only syntax check for Project1 (MSVC, wxWidgets 3.2.2.1)
# Usage: compile_check.sh
set -u
export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL='*'
# Toolchain (VS/SDK via vswhere, wxWidgets via WXWIN or env.local.sh):
# see env.sh.  Sourced, not executed, so it may not use `exit`.
. "$(dirname "$0")/env.sh"
cd "$(dirname "$0")"


mkdir -p "$(cygpath -u "$OUT")"
# link_check.sh consumes these objects; clear them first so a failed compile
# can never be papered over by linking a previous build's .obj files.
rm -f obj/main.obj obj/mainframe.obj obj/canvas.obj obj/model.obj \
      obj/wirenet.obj obj/netlist.obj obj/eda.obj

"$BIN\\cl.exe" /nologo /c /EHsc /std:c++20 /utf-8 /W3 /Zc:__cplusplus \
  /D_UNICODE /DUNICODE /DWIN32 /D_WINDOWS /D_CRT_SECURE_NO_WARNINGS \
  /D_DEBUG /MDd \
  /I"$WX\include" /I"$WX\include\msvc" /I"$MSVCINC" \
  /I"$KIT\\Include\\${SDKV}\\ucrt" /I"$KIT\\Include\\${SDKV}\\um" \
  /I"$KIT\\Include\\${SDKV}\\shared" /I"$KIT\\Include\\${SDKV}\\winrt" \
  /I"$SRC" \
  /Fo"$OUT\\" \
  "$SRC\main.cpp" "$SRC\mainframe.cpp" "$SRC\canvas.cpp" "$SRC\model.cpp" "$SRC\wirenet.cpp" "$SRC\netlist.cpp" "$SRC\eda.cpp" "${@:1}"

rc=$?
echo "exit=$rc"
exit "$rc"
