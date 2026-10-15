#!/bin/bash
# Compile-only syntax check for Project1 (MSVC, wxWidgets 3.2.2.1)
# Usage: compile_check.sh
set -u
export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL='*'

CL='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\bin\Hostx64\x64\cl.exe'
MSVCINC='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\include'
KIT='C:\Program Files (x86)\Windows Kits\10'
SDKV='10.0.26100.0'
WX='F:\EDA\basic\basic_tools\wxWidgets-3.2.2.1'
SRC='F:\EDA\wxWidgets_project\Project1'
OUT='F:\EDA\wxWidgets_project\_check\obj'

mkdir -p "$OUT"

"$CL" /nologo /c /EHsc /std:c++20 /utf-8 /W3 /Zc:__cplusplus \
  /D_UNICODE /DUNICODE /DWIN32 /D_WINDOWS /D_CRT_SECURE_NO_WARNINGS \
  /D_DEBUG /MDd \
  /I"$WX\include" /I"$WX\include\msvc" /I"$MSVCINC" \
  /I"$KIT\\Include\\$SDKV\\ucrt" /I"$KIT\\Include\\$SDKV\\um" \
  /I"$KIT\\Include\\$SDKV\\shared" /I"$KIT\\Include\\$SDKV\\winrt" \
  /I"$SRC" \
  /Fo"$OUT\\" \
  "$SRC\main.cpp" "$SRC\mainframe.cpp" "$SRC\canvas.cpp" "$SRC\model.cpp" "$SRC\wirenet.cpp" "$SRC\eda.cpp" "${@:1}"

echo "exit=$?"
