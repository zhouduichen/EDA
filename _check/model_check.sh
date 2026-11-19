#!/bin/bash
# Build + run the model layer smoke test (console app, wxBase only).
set -eu
export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL='*'
# test_model.cpp is referenced by a relative path below; see wirenet_check.sh
# for why both the cd and set -e are required to avoid stale-object fake PASS.
# Toolchain (VS/SDK via vswhere, wxWidgets via WXWIN or env.local.sh):
# see env.sh.  Sourced, not executed, so it may not use `exit`.
. "$(dirname "$0")/env.sh"
cd "$(dirname "$0")"


mkdir -p "$(cygpath -u "$OUT")"
# Never let a failed compile fall back to a previous build's objects.
rm -f obj/model.obj obj/wirenet.obj obj/test_model.obj obj/test_model.exe

"$BIN\cl.exe" /nologo /c /EHsc /std:c++20 /utf-8 /MDd /D_DEBUG /D_UNICODE /DUNICODE /D_CRT_SECURE_NO_WARNINGS \
  /I"$WX\include" /I"$WX\include\msvc" /I"$MSVCINC" \
  /I"$KIT\\Include\\${SDKV}\\ucrt" /I"$KIT\\Include\\${SDKV}\\um" \
  /I"$KIT\\Include\\${SDKV}\\shared" /I"$KIT\\Include\\${SDKV}\\winrt" \
  /I"$SRC" /Fo"$OUT\\" "$SRC\model.cpp" "$SRC\wirenet.cpp" test_model.cpp

"$BIN\link.exe" /nologo /SUBSYSTEM:CONSOLE /MACHINE:X64 \
  /LIBPATH:"$WX\\lib\\vc_x64_lib" /LIBPATH:"$MSVCLIB" \
  /LIBPATH:"$KIT\\Lib\\${SDKV}\\ucrt\\x64" /LIBPATH:"$KIT\\Lib\\${SDKV}\\um\\x64" \
  /OUT:"$OUT\\test_model.exe" \
  "$OUT\model.obj" "$OUT\wirenet.obj" "$OUT\test_model.obj" \
  wxbase32ud.lib wxregexud.lib ucrtd.lib vcruntimed.lib msvcrtd.lib msvcprtd.lib \
  kernel32.lib user32.lib shell32.lib advapi32.lib ole32.lib oleaut32.lib uuid.lib

"$OUT\\test_model.exe"
