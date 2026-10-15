#!/bin/bash
# Build + run the model layer smoke test (console app, wxBase only).
set -u
export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL='*'

BIN='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\bin\Hostx64\x64'
MSVCINC='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\include'
MSVCLIB='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\lib\x64'
KIT='C:\Program Files (x86)\Windows Kits\10'
SDKV='10.0.26100.0'
WX='F:\EDA\basic\basic_tools\wxWidgets-3.2.2.1'
SRC='F:\EDA\wxWidgets_project\Project1'
OUT='F:\EDA\wxWidgets_project\_check\obj'

mkdir -p "$OUT"

"$BIN\cl.exe" /nologo /c /EHsc /std:c++20 /utf-8 /MDd /D_DEBUG /D_UNICODE /DUNICODE /D_CRT_SECURE_NO_WARNINGS \
  /I"$WX\include" /I"$WX\include\msvc" /I"$MSVCINC" \
  /I"$KIT\\Include\\$SDKV\\ucrt" /I"$KIT\\Include\\$SDKV\\um" \
  /I"$KIT\\Include\\$SDKV\\shared" /I"$KIT\\Include\\$SDKV\\winrt" \
  /I"$SRC" /Fo"$OUT\\" "$SRC\model.cpp" "$SRC\wirenet.cpp" test_model.cpp

"$BIN\link.exe" /nologo /SUBSYSTEM:CONSOLE /MACHINE:X64 \
  /LIBPATH:"$WX\\lib\\vc_x64_lib" /LIBPATH:"$MSVCLIB" \
  /LIBPATH:"$KIT\\Lib\\$SDKV\\ucrt\\x64" /LIBPATH:"$KIT\\Lib\\$SDKV\\um\\x64" \
  /OUT:"$OUT\\test_model.exe" \
  "$OUT\model.obj" "$OUT\wirenet.obj" "$OUT\test_model.obj" \
  wxbase32ud.lib wxregexud.lib ucrtd.lib vcruntimed.lib msvcrtd.lib msvcprtd.lib \
  kernel32.lib user32.lib shell32.lib advapi32.lib ole32.lib oleaut32.lib uuid.lib

"$OUT\\test_model.exe"
