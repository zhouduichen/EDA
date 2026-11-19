#!/bin/bash
# Build + run the standalone wire-geometry / net-extraction test (console app).
set -eu
export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL='*'
# test_wirenet.cpp is referenced by a relative path below, and the object
# files live in ./obj.  Without this cd a caller from the repo root makes cl
# fail -- and, because set -e used to be missing, link.exe would silently
# reuse a stale .obj and the old test binary would report a fake PASS.
# Toolchain (VS/SDK via vswhere, wxWidgets via WXWIN or env.local.sh):
# see env.sh.  Sourced, not executed, so it may not use `exit`.
. "$(dirname "$0")/env.sh"
cd "$(dirname "$0")"


mkdir -p "$(cygpath -u "$OUT")"
# Never let a failed compile fall back to a previous build's objects.
rm -f obj/wirenet.obj obj/test_wirenet.obj obj/test_wirenet.exe

"$BIN\cl.exe" /nologo /c /EHsc /std:c++20 /utf-8 /W3 /MDd /D_DEBUG /D_UNICODE /DUNICODE /D_CRT_SECURE_NO_WARNINGS \
  /I"$WX\include" /I"$WX\include\msvc" /I"$MSVCINC" \
  /I"$KIT\\Include\\${SDKV}\\ucrt" /I"$KIT\\Include\\${SDKV}\\um" \
  /I"$KIT\\Include\\${SDKV}\\shared" /I"$KIT\\Include\\${SDKV}\\winrt" \
  /I"$SRC" /Fo"$OUT\\" "$SRC\wirenet.cpp" test_wirenet.cpp

"$BIN\link.exe" /nologo /SUBSYSTEM:CONSOLE /MACHINE:X64 \
  /LIBPATH:"$WX\\lib\\vc_x64_lib" /LIBPATH:"$MSVCLIB" \
  /LIBPATH:"$KIT\\Lib\\${SDKV}\\ucrt\\x64" /LIBPATH:"$KIT\\Lib\\${SDKV}\\um\\x64" \
  /OUT:"$OUT\\test_wirenet.exe" \
  "$OUT\wirenet.obj" "$OUT\test_wirenet.obj" \
  wxbase32ud.lib wxregexud.lib ucrtd.lib vcruntimed.lib msvcrtd.lib msvcprtd.lib \
  kernel32.lib user32.lib shell32.lib advapi32.lib ole32.lib oleaut32.lib uuid.lib

"$OUT\\test_wirenet.exe"
