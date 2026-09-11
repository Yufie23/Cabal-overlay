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
    VERSION 0.1.0
    DESCRIPTION "Native Wayland overlay for Cabal Online (private server)"
    LANGUAGES CXX
)

# C++20 is required: we use std::chrono::zoned_time for the
# Europe/Berlin server clock and std::format for text output.
# CXX_EXTENSIONS OFF means "strict standard C++, no GNU extras",
# which keeps the code portable between g++ and clang++.
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

# pkg-config is how Linux libraries announce their compiler and
# linker flags. IMPORTED_TARGET gives us a clean CMake target
# (PkgConfig::GTK4) instead of raw flag strings.
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
pkg_check_modules(LAYER_SHELL REQUIRED IMPORTED_TARGET gtk4-layer-shell-0)
pkg_check_modules(TOMLPP REQUIRED IMPORTED_TARGET tomlplusplus)

# Source files. The platform backend is chosen here: today there is
# only the Wayland one, but the day a Windows build is needed this
# becomes `if(WIN32) overlay_windows.cpp else() overlay_wayland.cpp`
# and NOTHING else in the project changes — that is the whole point
# of the platform/ contract.
add_executable(cabal-overlay
    src/main.cpp
    src/clock.cpp
    src/config.cpp
    src/schedule.cpp
    src/platform/overlay_wayland.cpp
)

# Lets sources write #include "platform/overlay.h" from anywhere.
target_include_directories(cabal-overlay PRIVATE src)

target_link_libraries(cabal-overlay PRIVATE
    PkgConfig::GTK4
    PkgConfig::LAYER_SHELL
    PkgConfig::TOMLPP
)

# Treat warnings seriously from day one: -Wall -Wextra enable the
# common ones, -Wpedantic rejects non-standard C++. Warnings in
# C++ are usually real bugs the compiler is politely pointing at.
target_compile_options(cabal-overlay PRIVATE -Wall -Wextra -Wpedantic)
