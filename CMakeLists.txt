cmake_minimum_required(VERSION 3.16)

# Desktop: 32-bit via the environment (vcvarsamd64_x86.bat + Ninja). Do not set
# CMAKE_GENERATOR_PLATFORM: Ninja rejects a platform specification.
# Web: configure with the Emscripten toolchain (cmake --preset web).
project(KisakBlack)

set(BIN_NAME "KisakBlack")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ----- Platform -----
# Desktop = MSVC x86 + Direct3D 9 (default, unchanged).
# Web     = Emscripten wasm32. The whole engine is compiled; the renderer, audio,
#           video, input and remote networking are stubbed and the executable boots
#           as a headless dedicated server in the browser (see src/web/).
set(KISAK_PLATFORM "Desktop" CACHE STRING
    "Desktop (MSVC x86, D3D9) or Web (Emscripten wasm32)")
set_property(CACHE KISAK_PLATFORM PROPERTY STRINGS Desktop Web)
if(NOT KISAK_PLATFORM STREQUAL "Desktop" AND NOT KISAK_PLATFORM STREQUAL "Web")
  message(FATAL_ERROR
          "KISAK_PLATFORM must be Desktop or Web (got '${KISAK_PLATFORM}')")
endif()

# DLL-free: stub Bink so cinematics finish immediately. No binkw32.dll. Turn OFF
# to link the real Bink path. Desktop only.
option(KISAK_NO_BINK
       "Build without binkw32 (stub Bink API, cinematics disabled)" ON)

# DLL-free: replace D3DX9 with src/gfx_d3d/r_d3dx_compat.cpp. Turn OFF to link
# the real d3dx9/d3dx9d. Desktop only.
option(KISAK_NO_D3DX "Build without d3dx9 (local D3DX replacements)" ON)

# Tracy (on demand) in Release builds. Debug always has it. Desktop only.
option(KISAK_PROFILE "Enable the Tracy profiler in non-Debug builds" OFF)

# Whole-program optimization for Release (/GL + /LTCG). Slower link. Desktop only.
option(KISAK_LTO "Enable link-time code generation in Release" OFF)

# Copy the built exe here as KisakBlack_<Config>.exe after each build, e.g. the
# folder holding main/ and zone/. The game resolves its data relative to the exe.
# Desktop only.
set(KISAK_DEPLOY_DIR "" CACHE PATH "Copy the exe here after building")

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(TL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tl")
set(DW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/DemonWare")

# KISAK_SOURCES: every translation unit. The platform file filters it and then
# creates the KisakBlack target.
include(cmake_files.cmake)

if(KISAK_PLATFORM STREQUAL "Web")
  if(NOT EMSCRIPTEN)
    message(FATAL_ERROR
            "KISAK_PLATFORM=Web needs the Emscripten toolchain: cmake --preset web "
            "(install it with tools/web/setup_emsdk.ps1 or .sh, then set EMSDK).")
  endif()
  include(cmake/platform_web.cmake)
else()
  if(EMSCRIPTEN)
    message(FATAL_ERROR "The Emscripten toolchain needs -DKISAK_PLATFORM=Web")
  endif()
  include(cmake/platform_desktop.cmake)
endif()

# ----- Settings shared by both platforms -----
target_compile_definitions(
  KisakBlack
  PUBLIC KISAK_MP
         $<$<BOOL:${KISAK_NO_D3DX}>:KISAK_NO_D3DX>
         $<$<BOOL:${KISAK_NO_BINK}>:KISAK_NO_BINK>
         # bink.h declares the Bink API without dllimport so bink_stub.cpp can
         # define it.
         $<$<BOOL:${KISAK_NO_BINK}>:__RADINEXE__>
         $<$<CONFIG:Debug>:_DEBUG>
         $<$<CONFIG:Release>:NDEBUG>)

target_include_directories(
  KisakBlack
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
         "${CMAKE_CURRENT_SOURCE_DIR}/src"
         "${CMAKE_CURRENT_SOURCE_DIR}/src/libs"
         "${CMAKE_CURRENT_SOURCE_DIR}/src/libs/libtomcrypt-1.17/src/headers"
         "${CMAKE_CURRENT_SOURCE_DIR}/src/libs/libtommath-1.0"
         "${CMAKE_CURRENT_SOURCE_DIR}/src/libs/libvpx-1.5.0/include")

# Platform include paths, libraries and link options that must come after the
# shared ones above (defined by cmake/platform_*.cmake).
kisak_platform_finalize()

message(STATUS "KisakBlack platform: ${KISAK_PLATFORM}")
