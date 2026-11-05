cmake_minimum_required(VERSION 3.24)
project(WhatInput VERSION 0.2.0 LANGUAGES C CXX RC)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT WIN32)
  message(FATAL_ERROR "WhatInput requires Windows")
endif()
if(MSVC)
  add_compile_options(/utf-8 /W4)
endif()
add_library(sqlite STATIC third_party/sqlite3.c)
target_include_directories(sqlite PUBLIC third_party)
target_compile_definitions(sqlite PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION)
add_library(core STATIC src/core/core.cpp)
target_include_directories(core PUBLIC src)
target_link_libraries(core PUBLIC sqlite)
find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets)
set(CMAKE_AUTOMOC ON)
configure_file(src/app/windows.rc.in "${CMAKE_CURRENT_BINARY_DIR}/WhatInput.rc" @ONLY)
qt_add_executable(WhatInput WIN32
  src/app/main.cpp
  src/capture/capture.cpp src/capture/capture.h
  src/engine/engine.cpp src/engine/engine.h
  src/platform/application_labels.cpp src/platform/application_labels.h
  src/ui/window.cpp src/ui/window.h
  src/ui/keyboard/keyboard_heatmap.cpp src/ui/keyboard/keyboard_heatmap.h
  src/ui/mouse/mouse_statistics.cpp src/ui/mouse/mouse_statistics.h
  src/ui/mouse/heatmap_renderer.cpp src/ui/mouse/heatmap_renderer.h
  src/ui/gamepad/gamepad_heatmap.cpp src/ui/gamepad/gamepad_heatmap.h
  src/ui/history/activity_chart.cpp src/ui/history/activity_chart.h
  "${CMAKE_CURRENT_BINARY_DIR}/WhatInput.rc"
)
qt_add_resources(WhatInput application_icons PREFIX "/assets" BASE assets
  FILES assets/WhatInput.ico assets/ICON.png)
target_compile_definitions(WhatInput PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(WhatInput PRIVATE core Qt6::Widgets user32 advapi32 xinput9_1_0)
install(TARGETS WhatInput RUNTIME DESTINATION .)
install(FILES README.md THIRD_PARTY.md DESTINATION .)
install(DIRECTORY assets/ DESTINATION assets FILES_MATCHING PATTERN "*.png" PATTERN "*.ico")
