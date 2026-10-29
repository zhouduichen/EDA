#!/bin/bash
# Build + run the standalone wire-geometry / net-extraction test (console app).
set -u
export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL='*'

BIN='D:\Visual Studio\Visual Studio 2026\Community\VC\Tools\MSVC\14.51.36231\bin\Hostx64\x64'
MSVCINC='D:\Visual Studio\Visual Studio 2026\Community\VC\Tools\MSVC\14.51.36231\include'
MSVCLIB='D:\Visual Studio\Visual Studio 2026\Community\VC\Tools\MSVC\14.51.36231\lib\x64'
KIT='D:\Windows Kits\10'
SDKV='10.0.26100.0'
WX='C:\wxWidgets-3.2.2.1'
SRC='C:\Users\asus\Desktop\电子科技大学\EDA\HJH\CZW\EDA-wxwidgets-project1\Project1'
OUT='C:\Users\asus\Desktop\电子科技大学\EDA\HJH\CZW\EDA-wxwidgets-project1\_check\obj'

mkdir -p "$OUT"

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
