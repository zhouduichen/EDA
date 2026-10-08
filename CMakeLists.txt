cmake_minimum_required(VERSION 3.20)

project(CircuitSchematicEditor VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(wxWidgets REQUIRED COMPONENTS core base)
include(${wxWidgets_USE_FILE})

# MSVC 不认 -Wall 一类的 GCC 旗标（会报 D8021），单独分支；
# /utf-8 必须加，否则源码里的 UTF-8 中文/° 字面量会按 GBK 解析。
if(MSVC)
    set(CSE_WARNING_FLAGS /W4 /utf-8)
else()
    set(CSE_WARNING_FLAGS -Wall -Wextra -Wpedantic)
endif()

set(CSE_CORE_SOURCES
    src/component/component_factory.cpp
    src/component/component_library.cpp
    src/editor/editor_commands.cpp
    src/editor/editor_controller.cpp
    src/io/kicad_netlist_exporter.cpp
    src/io/project_serializer.cpp
    src/model/net_builder.cpp
    src/model/schematic_model.cpp
    src/sim/logic_simulator.cpp
)
add_library(cse_core ${CSE_CORE_SOURCES})
target_include_directories(cse_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(cse_core PRIVATE ${CSE_WARNING_FLAGS})

add_executable(cse_editor
    src/app/main.cpp
    src/ui/component_library_panel.cpp
    src/ui/main_frame.cpp
    src/ui/properties_panel.cpp
    src/ui/schematic_canvas.cpp
    src/ui/simulation_dialog.cpp
)
target_include_directories(cse_editor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cse_editor PRIVATE cse_core ${wxWidgets_LIBRARIES})
target_compile_options(cse_editor PRIVATE ${CSE_WARNING_FLAGS})

# wxIMPLEMENT_APP 在 Windows 下生成 WinMain；不置这个标志链接器会去找 main（LNK2019）。
set_target_properties(cse_editor PROPERTIES WIN32_EXECUTABLE TRUE)

enable_testing()
add_executable(cse_core_tests tests/unit/core_tests.cpp)
target_include_directories(cse_core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cse_core_tests PRIVATE cse_core)
target_compile_options(cse_core_tests PRIVATE ${CSE_WARNING_FLAGS})
add_test(NAME cse_core_tests COMMAND cse_core_tests)
