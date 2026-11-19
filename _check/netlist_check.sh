#!/bin/bash
# Build + run the netlist build/export test.
#
# This check needs no wxWidgets and no MSVC: model.h / netlist.cpp / wirenet.cpp
# are compiled against the minimal stub headers in stub/wx, so any machine with
# a C++ compiler can run it.  The MSVC-based *_check.sh scripts get their
# toolchain paths from env.sh (VS/SDK autodetection, wxWidgets via WXWIN).
set -eu
cd "$(dirname "$0")"

GXX="${GXX:-g++}"
mkdir -p obj

"$GXX" -std=c++17 -Istub -I../Project1 \
  ../Project1/netlist.cpp ../Project1/wirenet.cpp ../Project1/model.cpp test_netlist.cpp \
  -o obj/test_netlist.exe

./obj/test_netlist.exe
