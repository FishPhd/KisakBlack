# Desktop platform: MSVC x86, Win32, Direct3D 9. Included by CMakeLists.txt when
# KISAK_PLATFORM=Desktop. Creates the KisakBlack target.

set(DXSDK_DIR "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)")

# Static CRT (/MT release, /MTd debug) so no MSVCP140/VCRUNTIME140/ucrtbase DLLs
# are needed. Set before the target is created.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(${BIN_NAME} ${KISAK_SOURCES})

if(KISAK_NO_BINK)
  # Real Bink helpers (binkw32 + D3DX shader compile) are not built.
  set_source_files_properties(
    "${SRC_DIR}/binklib/binktextures.cpp" "${SRC_DIR}/binklib/dx9rad3d.cpp"
    PROPERTIES HEADER_FILE_ONLY TRUE)
  target_sources(KisakBlack PRIVATE "${SRC_DIR}/binklib/bink_stub.cpp")
else()
  # binkw32.dll stays in src/binklib. The delay-load hook opens that path.
  file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/binklib/binkw32.dll"
       _kisak_bink_dll)
  file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")
  file(
    WRITE "${CMAKE_BINARY_DIR}/generated/kisak_bink_dll.h"
    "#pragma once\n#define KISAK_BINKW32_DLL \"${_kisak_bink_dll}\"\n")
  target_sources(KisakBlack PRIVATE "${SRC_DIR}/binklib/bink_delay_load.cpp")
  target_include_directories(KisakBlack PRIVATE "${CMAKE_BINARY_DIR}/generated")
endif()

if(KISAK_NO_D3DX)
  target_sources(KisakBlack PRIVATE "${SRC_DIR}/gfx_d3d/r_d3dx_compat.cpp")
endif()

# phys_gjk.cpp specific flag (experimental)
set_source_files_properties("${SRC_DIR}/physics/phys_gjk.cpp"
                            PROPERTIES COMPILE_OPTIONS "/arch:IA32")

# ----- Preprocessor defines -----
target_compile_definitions(
  KisakBlack
  PUBLIC WIN32
         _CONSOLE
         _CRT_SECURE_NO_WARNINGS
         _WINSOCK_NO_DEPRECATED_NO_WARNINGS
         $<$<OR:$<CONFIG:Debug>,$<BOOL:${KISAK_PROFILE}>>:TRACY_ENABLE>
         $<$<OR:$<CONFIG:Debug>,$<BOOL:${KISAK_PROFILE}>>:TRACY_ON_DEMAND>)

# Decompiled code trips MSVC runtime checks (truncating casts, leftover
# uninitialised locals). /RTC1 turns each into a hard crash (failwithmessage),
# so strip it. /GS- matches Release.
string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
string(REPLACE "/RTC1" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")

target_compile_options(
  KisakBlack PRIVATE /MP /W3 /permissive /GS- $<$<CONFIG:Release>:/O2>
                     $<$<CONFIG:Debug>:/Od>)

if(KISAK_LTO)
  target_compile_options(KisakBlack PRIVATE $<$<CONFIG:Release>:/GL>)
  target_link_options(KisakBlack PRIVATE $<$<CONFIG:Release>:/LTCG>)
endif()

if(MSVC
   AND WIN32
   AND NOT MSVC_VERSION VERSION_LESS 142)
  target_link_options(KisakBlack PRIVATE $<$<CONFIG:Debug>:/INCREMENTAL>
                      $<$<CONFIG:Debug>:/SAFESEH:NO>)
  target_compile_options(KisakBlack PRIVATE $<$<CONFIG:Debug>:/ZI>)
endif()

set_target_properties(KisakBlack PROPERTIES
  WIN32_EXECUTABLE TRUE
  VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")

if(KISAK_DEPLOY_DIR)
  add_custom_command(
    TARGET KisakBlack
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:KisakBlack>"
            "${KISAK_DEPLOY_DIR}/KisakBlack_$<CONFIG>.exe"
    COMMENT "Copying KisakBlack to ${KISAK_DEPLOY_DIR}/KisakBlack_$<CONFIG>.exe")
endif()

# Called by CMakeLists.txt after the shared defines/includes are in place.
function(kisak_platform_finalize)
  target_include_directories(KisakBlack PUBLIC "${DXSDK_DIR}/Include")

  # ----- Library directories -----
  target_link_directories(
    KisakBlack
    PUBLIC
    "${DXSDK_DIR}/Lib/x86"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/libs/libvpx-1.5.0"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/binklib"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/nvapi")

  # ----- Links -----
  if(NOT KISAK_NO_BINK)
    target_link_libraries(KisakBlack PUBLIC binkw32.lib delayimp.lib)
    target_link_options(KisakBlack PRIVATE "/DELAYLOAD:binkw32.dll")
  endif()
  target_link_libraries(
    KisakBlack
    PUBLIC nvapi.lib
           d3d9.lib
           ws2_32.lib
           dxguid.lib
           dsound.lib
           winmm.lib
           # WaitOnAddress / WakeByAddress* (src/qcommon/sys_futex.h)
           Synchronization.lib
           # xinput9_1_0.dll ships with Windows (Vista+). XINPUT1_3.dll (June 2010
           # DX redist) does not.
           Xinput9_1_0.lib
           xapobase.lib
           dxerr.lib
           libvpx.lib
           # June-2010 DX SDK libs (xapobase, dxerr) reference pre-VS2015 CRT
           # symbols (_vsnprintf, etc.) that UCRT no longer exports.
           # legacy_stdio_definitions.lib provides shims.
           legacy_stdio_definitions.lib)

  if(NOT KISAK_NO_D3DX)
    target_link_libraries(KisakBlack PUBLIC $<$<CONFIG:Debug>:d3dx9d.lib>
                                            $<$<CONFIG:Release>:d3dx9.lib>)
  endif()

  # /DYNAMICBASE:NO: leaving this on crashes D3D ("directx has encountered an
  # unrecoverable error"). /LARGEADDRESSAWARE: 4GB user-mode on x86. /DEBUG: PDB
  # for both configs.
  target_link_options(
    KisakBlack
    PRIVATE
    /machine:x86
    /DYNAMICBASE:NO
    /LARGEADDRESSAWARE
    /DEBUG
    # Prebuilt static libs (libvpx, xapobase, ...) carry /DEFAULTLIB:MSVCRT.
    # Ignore those with the static CRT (LNK4098 / unresolved
    # __except_handler4_common).
    /NODEFAULTLIB:MSVCRT
    /NODEFAULTLIB:MSVCRTD
    # libvpx threading imports _beginthreadex via __imp_. Force the static CRT
    # copy in (Release otherwise misses it).
    /INCLUDE:__beginthreadex)
endfunction()
