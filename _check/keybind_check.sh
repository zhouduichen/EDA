#!/bin/bash
# Build + run the key-binding predicate test (MSVC; wx headers only, no GUI lib).
# Local-toolchain adaptation of the *_check.sh family for this machine.
set -eu
export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL='*'
# Toolchain (VS/SDK via vswhere, wxWidgets via WXWIN or env.local.sh):
# see env.sh.  Sourced, not executed, so it may not use `exit`.
. "$(dirname "$0")/env.sh"
cd "$(dirname "$0")"


mkdir -p "$(cygpath -u "$OUT")"

"$BIN\\cl.exe" /nologo /c /EHsc /std:c++20 /utf-8 /W3 \
  /D_UNICODE /DUNICODE /DWIN32 /D_WINDOWS /D_CRT_SECURE_NO_WARNINGS /D_DEBUG /MDd \
  /I"$WX\\include" /I"$WX\\include\\msvc" /I"$MSVCINC" \
  /I"$KIT\\Include\\$SDKV\\ucrt" /I"$KIT\\Include\\$SDKV\\um" \
  /I"$KIT\\Include\\$SDKV\\shared" /I"$KIT\\Include\\$SDKV\\winrt" \
  /I"$SRC" \
  /Fo"$OUT\\" test_keybind.cpp

"$BIN\\link.exe" /nologo /SUBSYSTEM:CONSOLE /MACHINE:X64 \
  /LIBPATH:"$WX\\lib\\vc_x64_lib" /LIBPATH:"$MSVCLIB" \
  /LIBPATH:"$KIT\\Lib\\$SDKV\\ucrt\\x64" /LIBPATH:"$KIT\\Lib\\$SDKV\\um\\x64" \
  /OUT:"$OUT\\test_keybind.exe" "$OUT\\test_keybind.obj" \
  ucrtd.lib vcruntimed.lib msvcrtd.lib msvcprtd.lib kernel32.lib

"$OUT\\test_keybind.exe"
