cmake_minimum_required(VERSION 3.24)

project(LogicSim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The prebuilt wxWidgets 3.2.2.1 developer package ships static *debug*
# libraries (lib/vc_x64_lib/*ud.lib) and DLL *release* ones, and the project
# has always been built in the debug flavour -- keep that the default.
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
# The official prebuilt 3.2.2.1 packages pair flavours with build types
# (lib/vc_x64_lib/mswud = static debug, lib/vc_x64_dll/mswu = DLL release),
# so the debug/release decision follows CMAKE_BUILD_TYPE.
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

# FindwxWidgets' find_path() ranks configuration NAMES above the PATHS order,
# so with a prebuilt tree that has BOTH directories it always lands on
# mswu (release DLL) even for a Debug build.  Pin the library directory
# ourselves when the classic lib/vc_x64_{lib,dll} layout is present; vcpkg /
# custom installs do not use that layout and keep the module's normal search.
if(NOT wxWidgets_LIB_DIR AND wxWidgets_ROOT_DIR)
    foreach(_wx_dir vc_x64_lib vc_x64_dll)
        if(EXISTS "${wxWidgets_ROOT_DIR}/lib/${_wx_dir}/${_LogicSim_wx_cfg}/wx/setup.h")
            set(wxWidgets_LIB_DIR "${wxWidgets_ROOT_DIR}/lib/${_wx_dir}"
                CACHE PATH "wxWidgets library directory")
            set(_LogicSim_wx_found_cfg "${_LogicSim_wx_cfg}")
            break()
        endif()
    endforeach()
    # Only one configuration installed?  Take whichever it actually has, and
    # tell FindwxWidgets about it -- otherwise the module silently picks the
    # last directory it finds instead of the flavour we just selected.
    if(NOT wxWidgets_LIB_DIR)
        foreach(_wx_dir vc_x64_lib vc_x64_dll)
            foreach(_wx_any_cfg mswu mswud mswd msw)
                if(EXISTS "${wxWidgets_ROOT_DIR}/lib/${_wx_dir}/${_wx_any_cfg}/wx/setup.h")
                    set(wxWidgets_LIB_DIR "${wxWidgets_ROOT_DIR}/lib/${_wx_dir}"
                        CACHE PATH "wxWidgets library directory")
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
