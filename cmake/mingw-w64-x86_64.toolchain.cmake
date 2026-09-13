# ─────────────────────────────────────────────────────────────
# mingw-w64-x86_64.toolchain.cmake — cross-compile for Windows
# from Linux (or configure a MinGW build without autodetection).
#
# Usage:
#   cmake -B build-win -G Ninja \
#       -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.toolchain.cmake
#
# The toolchain only provides the compiler and the target system;
# the Windows build still needs a full dependency sysroot with
# pkg-config files for gtk4, tomlplusplus and nlohmann_json built
# for MinGW. The practical source of those is an MSYS2 UCRT64
# installation; pointing PKG_CONFIG_SYSROOT_DIR at one is the
# remaining wiring on a fresh machine. When no sysroot is available,
# build on Windows itself inside MSYS2 UCRT64 instead — the same
# CMakeLists works there unchanged.
# ─────────────────────────────────────────────────────────────

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER   x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)

set(CMAKE_EXE_LINKER_FLAGS_INIT "-static-libgcc -static-libstdc++")

# Find programs (e.g. glib tools) on the host, libraries on the target.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
