cmake_minimum_required(VERSION 3.16)
project(RoniaTraceTool VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(RoniaTraceTool
    src/main.cpp
    src/core/ProjectLoader.cpp
    src/core/HeaderParser.cpp
    src/core/CppImplFinder.cpp
    src/core/BraceScanner.cpp
    src/ui/MainWindow.cpp
    src/ui/MainWindow.ui
    src/ui/FunctionTableModel.cpp
    resources/style.qss
)

target_include_directories(RoniaTraceTool PRIVATE src)
target_link_libraries(RoniaTraceTool PRIVATE Qt6::Widgets)

# Copy QSS next to binary at build time
add_custom_command(TARGET RoniaTraceTool POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/style.qss
        $<TARGET_FILE_DIR:RoniaTraceTool>/style.qss)