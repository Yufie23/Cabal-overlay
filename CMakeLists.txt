# ─────────────────────────────────────────────────────────────
# cabal-overlay — build definition
#
# CMake is the "package.json" of C++: it declares what we build,
# which language standard we use, and which external libraries
# we link against. Ninja (the -G flag when configuring) is the
# tool that actually runs the compiler, like make but faster.
# ─────────────────────────────────────────────────────────────

cmake_minimum_required(VERSION 3.25)

project(cabal-overlay
    VERSION 0.1.2
    DESCRIPTION "Native game overlay for Cabal Online (private server)"
    LANGUAGES CXX
)

# C++20 is required: we use std::chrono::zoned_time for the
# Europe/Berlin server clock and std::format for text output.
# CXX_EXTENSIONS OFF means "strict standard C++, no GNU extras",
# which keeps the code portable between g++, clang++ and MinGW.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Always emit compile_commands.json: the clangd language server
# (VSCode extension) reads it to give exact IntelliSense, go-to-
# definition and semantic coloring for this project.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to a Debug build (with assertions and debug symbols)
# when the developer does not pick one explicitly.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# pkg-config is how libraries announce their compiler and linker
# flags — on Linux from the system, on Windows from MSYS2's
# MinGW environment (pacman -S mingw-w64-ucrt-x86_64-gtk4 ...).
# IMPORTED_TARGET gives us a clean CMake target (PkgConfig::GTK4)
# instead of raw flag strings.
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
pkg_check_modules(TOMLPP REQUIRED IMPORTED_TARGET tomlplusplus)
pkg_check_modules(NLOHMANN REQUIRED IMPORTED_TARGET nlohmann_json)

# Source files shared by every platform: the UI, the models and
# the app logic know nothing about the host OS.
set(APP_SOURCES
    src/main.cpp
    src/app/alarms.cpp
    src/app/dgcheck.cpp
    src/app/update_check.cpp
    src/time/clock.cpp
    src/app/config.cpp
    src/model/dungeons.cpp
    src/time/schedule.cpp
    src/model/state.cpp
    src/model/task_presets.cpp
    src/model/tasks.cpp
    src/ui/goals_panel.cpp
    src/ui/settings_window.cpp
    src/ui/tutorial.cpp
)

# The platform backend is chosen here and NOTHING else in the
# project changes — that is the whole point of the platform/
# contract. Linux gets the Wayland layer-shell backend plus the
# X11 sensors; Windows gets Win32 styles and native API sensors.
if(WIN32)
    set(PLATFORM_SOURCES
        src/platform/overlay/overlay_windows.cpp
        src/platform/hotkey/hotkey_windows.cpp
        src/platform/pointer/pointer_windows.cpp
        src/platform/game_watch/game_watch_windows.cpp
        src/platform/sound/sound_windows.cpp
        src/platform/tray/tray_windows.cpp
    )
else()
    pkg_check_modules(LAYER_SHELL REQUIRED IMPORTED_TARGET gtk4-layer-shell-0)
    pkg_check_modules(CANBERRA REQUIRED IMPORTED_TARGET libcanberra)
    # X11 client library: the dgcheck pointer sensor reads pointer
    # state through the plain core protocol (XQueryPointer/
    # XQueryKeymap), no extensions, no input devices, no extra
    # permissions.
    pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11)
    set(PLATFORM_SOURCES
        src/platform/overlay/overlay_wayland.cpp
        src/platform/hotkey/hotkey_linux.cpp
        src/platform/pointer/pointer_x11.cpp
        src/platform/game_watch/game_watch_x11.cpp
        src/platform/sound/sound_canberra.cpp
        src/platform/tray/tray_none.cpp
    )
endif()

add_executable(cabal-overlay ${APP_SOURCES} ${PLATFORM_SOURCES})

if(WIN32)
    # GUI subsystem (no console window behind the overlay) and the
    # Win32 libraries the backends call directly (shell32 for the
    # tray icon's Shell_NotifyIconW).
    set_target_properties(cabal-overlay PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_libraries(cabal-overlay PRIVATE user32 gdi32 winmm shell32)
else()
    target_link_libraries(cabal-overlay PRIVATE
        PkgConfig::LAYER_SHELL
        PkgConfig::CANBERRA
        PkgConfig::X11
    )
endif()

# Lets sources write #include "platform/overlay.h" from anywhere.
target_include_directories(cabal-overlay PRIVATE src)

# The project version as a compiled-in string literal: the update
# check compares it against the newest GitHub release tag.
target_compile_definitions(cabal-overlay PRIVATE
    CABAL_OVERLAY_VERSION="${PROJECT_VERSION}")

target_link_libraries(cabal-overlay PRIVATE
    PkgConfig::GTK4
    PkgConfig::TOMLPP
    PkgConfig::NLOHMANN
)

# Treat warnings seriously from day one: -Wall -Wextra enable the
# common ones, -Wpedantic rejects non-standard C++. Warnings in
# C++ are usually real bugs the compiler is politely pointing at.
if(NOT MSVC)
    target_compile_options(cabal-overlay PRIVATE -Wall -Wextra -Wpedantic)
endif()
