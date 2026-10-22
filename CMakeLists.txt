cmake_minimum_required(VERSION 3.16)
project(RoniaTraceTool VERSION 0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# ---- Qt ----
# Prefer Qt6; fall back to Qt5 so the project builds on older toolchains.
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

# ---- Sources ----
set(APP_SOURCES
    src/main.cpp

    # core
    src/core/FunctionInfo.h
    src/core/ProjectLoader.h
    src/core/ProjectLoader.cpp
    src/core/HeaderParser.h
    src/core/HeaderParser.cpp
    src/core/CppImplFinder.h
    src/core/CppImplFinder.cpp
    src/core/BraceScanner.h
    src/core/BraceScanner.cpp

    # new core pieces
    src/core/TextEdit.h
    src/core/IdempotencyChecker.h
    src/core/IdempotencyChecker.cpp
    src/core/EditPlanner.h
    src/core/EditPlanner.cpp
    src/core/EditApplier.h
    src/core/EditApplier.cpp

    # ui
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.ui
    src/ui/FunctionTableModel.h
    src/ui/FunctionTableModel.cpp
    src/ui/CppSyntaxHighlighter.h
    src/ui/CppSyntaxHighlighter.cpp
    src/ui/DiffPreviewDialog.h
    src/ui/DiffPreviewDialog.cpp
)

set(APP_RESOURCES
    resources/style.qss
)

add_executable(RoniaTraceTool ${APP_SOURCES} ${APP_RESOURCES})

target_include_directories(RoniaTraceTool PRIVATE src)
target_link_libraries(RoniaTraceTool PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

# ---- Platform niceties ----
if (WIN32)
    set_target_properties(RoniaTraceTool PROPERTIES WIN32_EXECUTABLE ON)
    # Uncomment if you add an .rc icon file:
    # target_sources(RoniaTraceTool PRIVATE resources/app.rc)
elseif (APPLE)
    set_target_properties(RoniaTraceTool PROPERTIES MACOSX_BUNDLE ON)
endif()

# ---- Copy QSS + trace runtime next to the binary at build time ----
add_custom_command(TARGET RoniaTraceTool POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/style.qss
        $<TARGET_FILE_DIR:RoniaTraceTool>/style.qss
    COMMENT "Copying style.qss next to executable")

# Optional: bundle the trace runtime header with the app so it's easy to
# copy into a target project. Enable if you keep it under include/trace/.
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include/trace/Trace.h")
    add_custom_command(TARGET RoniaTraceTool POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_FILE_DIR:RoniaTraceTool>/include/trace
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/include/trace/Trace.h
            $<TARGET_FILE_DIR:RoniaTraceTool>/include/trace/Trace.h
        COMMENT "Copying trace/Trace.h next to executable")
endif()