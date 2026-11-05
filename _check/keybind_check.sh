#!/bin/bash
# Build + run the key-binding predicate test (MSVC; wx headers only, no GUI lib).
# Local-toolchain adaptation of the *_check.sh family for this machine.
set -eu
export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL='*'
cd "$(dirname "$0")"

VS='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231'
BIN="$VS\\bin\\Hostx64\\x64"
MSVCINC="$VS\\include"
MSVCLIB="$VS\\lib\\x64"
KIT='C:\Program Files (x86)\Windows Kits\10'
SDKV='10.0.26100.0'
WX='F:\EDA\basic\basic_tools\wxWidgets-3.2.2.1'
SRC='F:\EDA\wxWidgets_project\Project1'
OUT='F:\EDA\wxWidgets_project\_check\obj'

mkdir -p "$OUT"

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
