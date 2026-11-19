cmake_minimum_required(VERSION 3.24)

project(LogicSim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The project has always been built in the debug flavour; keep that the
# default.  wxWidgets trees differ: a source build offers static debug libs
# (lib/vc_x64_lib/*ud.lib), the official prebuilt packages offer DLL import
# libs for both mswud (debug) and mswu (release).  The library directory is
# discovered below, so both layouts work.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# wxWidgets 3.2.2.1
#
# Where to find it, in order:
#   1. the WXWIN environment variable (the one wxWidgets itself documents)
#   2. the standard wxWidgets_ROOT_DIR CMake variable
#
# The sources use core/base GUI plus the property grid and XRC; the remaining
# libs those two depend on are resolved by FindwxWidgets.  The self-contained
# static flavour is preferred; the DLL flavour is the fallback.
#
# wxWidgets names its configurations mswud (debug) / mswu (release) in both
# source builds and the official prebuilt packages, so the debug/release
# decision follows CMAKE_BUILD_TYPE.
if(CMAKE_BUILD_TYPE MATCHES "^[Dd]ebug")
    set(_LogicSim_wx_cfg "mswud")
else()
    set(_LogicSim_wx_cfg "mswu")
endif()

if(NOT wxWidgets_ROOT_DIR AND DEFINED ENV{WXWIN})
    file(TO_CMAKE_PATH "$ENV{WXWIN}" _wxwin_root)
    set(wxWidgets_ROOT_DIR "${_wxwin_root}" CACHE PATH "wxWidgets 3.2.2.1 tree")
    unset(_wxwin_root)
endif()
# Accept the Windows-style path a `set WXWIN=...` / -D usually supplies.
if(wxWidgets_ROOT_DIR)
    file(TO_CMAKE_PATH "${wxWidgets_ROOT_DIR}" _wxwin_norm)
    set(wxWidgets_ROOT_DIR "${_wxwin_norm}" CACHE PATH "wxWidgets 3.2.2.1 tree" FORCE)
    unset(_wxwin_norm)
endif()

# FindwxWidgets' find_path() ranks configuration NAMES above the PATHS order,
# so with a prebuilt tree that has BOTH directories it always lands on
# mswu (release) even for a Debug build.  Pin the library directory ourselves.
#
# Do not assume the directory name: a source build uses vc_x64_lib / vc_x64_dll,
# while the official prebuilt vc14x packages use vc14x_x64_dll only.  Scan
# lib/*, prefer a static tree over a DLL one, and take the configuration that
# matches the build type.  vcpkg / custom installs keep the module's normal
# search when no classic lib/<dir>/<cfg>/wx/setup.h layout is present.
if(NOT wxWidgets_LIB_DIR AND wxWidgets_ROOT_DIR)
    file(GLOB _LogicSim_wx_libs LIST_DIRECTORIES true
         "${wxWidgets_ROOT_DIR}/lib/*")
    set(_LogicSim_wx_static "")
    set(_LogicSim_wx_shared "")
    foreach(_wx_dir IN LISTS _LogicSim_wx_libs)
        if(NOT IS_DIRECTORY "${_wx_dir}")
            continue()
        endif()
        get_filename_component(_wx_name "${_wx_dir}" NAME)
        if(_wx_name MATCHES "[dD][lL][lL]")
            list(APPEND _LogicSim_wx_shared "${_wx_dir}")
        else()
            list(APPEND _LogicSim_wx_static "${_wx_dir}")
        endif()
    endforeach()

    foreach(_wx_dir IN LISTS _LogicSim_wx_static _LogicSim_wx_shared)
        if(EXISTS "${_wx_dir}/${_LogicSim_wx_cfg}/wx/setup.h")
            set(wxWidgets_LIB_DIR "${_wx_dir}" CACHE PATH "wxWidgets library directory")
            set(_LogicSim_wx_found_cfg "${_LogicSim_wx_cfg}")
            break()
        endif()
    endforeach()
    # Only one configuration installed?  Take whichever it actually has, and
    # tell FindwxWidgets about it -- otherwise the module silently picks the
    # last directory it finds instead of the flavour we just selected.
    if(NOT wxWidgets_LIB_DIR)
        foreach(_wx_dir IN LISTS _LogicSim_wx_static _LogicSim_wx_shared)
            foreach(_wx_any_cfg mswu mswud mswd msw)
                if(EXISTS "${_wx_dir}/${_wx_any_cfg}/wx/setup.h")
                    set(wxWidgets_LIB_DIR "${_wx_dir}" CACHE PATH "wxWidgets library directory")
                    set(_LogicSim_wx_found_cfg "${_wx_any_cfg}")
                    break()
                endif()
            endforeach()
            if(wxWidgets_LIB_DIR)
                break()
            endif()
        endforeach()
    endif()
    if(_LogicSim_wx_found_cfg AND NOT wxWidgets_CONFIGURATION)
        set(wxWidgets_CONFIGURATION "${_LogicSim_wx_found_cfg}"
            CACHE STRING "wxWidgets configuration")
    endif()
    unset(_LogicSim_wx_libs)
    unset(_LogicSim_wx_static)
    unset(_LogicSim_wx_shared)
    unset(_wx_dir)
    unset(_wx_name)
endif()

set(_LogicSim_wx_components core base adv html xml xrc propgrid)

set(wxWidgets_USE_UNICODE ON)
find_package(wxWidgets QUIET COMPONENTS ${_LogicSim_wx_components})

if(NOT wxWidgets_FOUND)
    message(FATAL_ERROR
"wxWidgets 3.2.2.1 was not found.

Get the prebuilt 3.2.2.1 developer package (or build it from source), then
point the build at your tree, e.g.:

    set WXWIN=C:\\wxWidgets-3.2.2.1
    cmake -B build

or

    cmake -B build -DwxWidgets_ROOT_DIR=C:/wxWidgets-3.2.2.1
")
endif()

message(STATUS "wxWidgets ${wxWidgets_VERSION} found: ${wxWidgets_LIB_DIR}")

# ---------------------------------------------------------------------------
# The application
#
# _check/app.rc pulls in wx/msw/wx.rc: the common-controls v6 manifest and
# the standard wx cursors every wx application needs.
add_executable(LogicSim WIN32
    Project1/main.cpp
    Project1/mainframe.cpp
    Project1/canvas.cpp
    Project1/model.cpp
    Project1/wirenet.cpp
    Project1/netlist.cpp
    Project1/eda.cpp
    _check/app.rc
)

target_include_directories(LogicSim PRIVATE
    Project1
    ${wxWidgets_INCLUDE_DIRS}
)

target_compile_definitions(LogicSim PRIVATE
    _UNICODE UNICODE WIN32 _WINDOWS _CRT_SECURE_NO_WARNINGS
    ${wxWidgets_DEFINITIONS}
)

target_link_libraries(LogicSim PRIVATE ${wxWidgets_LIBRARIES})

# ---------------------------------------------------------------------------
# Unit tests (console applications)
#
# test_wirenet / test_netlist build against the minimal wx stub headers in
# _check/stub, so they run on a machine that has no wxWidgets at all; the
# keybind and model tests need the real (base) headers.
enable_testing()

add_executable(test_wirenet
    _check/test_wirenet.cpp
    Project1/wirenet.cpp
)
target_include_directories(test_wirenet PRIVATE _check/stub Project1)
add_test(NAME wirenet COMMAND test_wirenet)

add_executable(test_netlist
    _check/test_netlist.cpp
    Project1/netlist.cpp
    Project1/wirenet.cpp
    Project1/model.cpp
)
target_include_directories(test_netlist PRIVATE _check/stub Project1)
add_test(NAME netlist COMMAND test_netlist)

add_executable(test_keybind _check/test_keybind.cpp)
target_include_directories(test_keybind PRIVATE
    Project1
    ${wxWidgets_INCLUDE_DIRS}
)
target_compile_definitions(test_keybind PRIVATE
    _UNICODE UNICODE _CRT_SECURE_NO_WARNINGS
    ${wxWidgets_DEFINITIONS}
)
add_test(NAME keybind COMMAND test_keybind)

add_executable(test_model
    _check/test_model.cpp
    Project1/model.cpp
    Project1/wirenet.cpp
)
target_include_directories(test_model PRIVATE
    Project1
    ${wxWidgets_INCLUDE_DIRS}
)
target_compile_definitions(test_model PRIVATE
    _UNICODE UNICODE _CRT_SECURE_NO_WARNINGS
    ${wxWidgets_DEFINITIONS}
)
target_link_libraries(test_model PRIVATE ${wxWidgets_LIBRARIES})
add_test(NAME model COMMAND test_model)
