cmake_minimum_required(VERSION 3.20)

project(CircuitSchematicEditor VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(wxWidgets REQUIRED COMPONENTS core base)
include(${wxWidgets_USE_FILE})

set(CSE_CORE_SOURCES
    src/component/component_factory.cpp
    src/component/component_library.cpp
    src/editor/editor_commands.cpp
    src/editor/editor_controller.cpp
    src/model/schematic_model.cpp
)

add_library(cse_core ${CSE_CORE_SOURCES})
target_include_directories(cse_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(cse_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(cse_editor
    src/app/main.cpp
    src/ui/component_library_panel.cpp
    src/ui/main_frame.cpp
    src/ui/properties_panel.cpp
    src/ui/schematic_canvas.cpp
)
target_include_directories(cse_editor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cse_editor PRIVATE cse_core ${wxWidgets_LIBRARIES})
target_compile_options(cse_editor PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_executable(cse_core_tests tests/unit/core_tests.cpp)
target_include_directories(cse_core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cse_core_tests PRIVATE cse_core)
target_compile_options(cse_core_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME cse_core_tests COMMAND cse_core_tests)
