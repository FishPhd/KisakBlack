cmake_minimum_required(VERSION 3.21)
project(KisakReferencePilot LANGUAGES CXX)
get_filename_component(REPO "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(KISAK_RE_PACK_SOURCE "${REPO}/src/universal/com_pack.cpp" CACHE FILEPATH "Production source; overridden only by mutation self-test")
add_executable(re_runner runner.cpp link_guards.cpp pm_adapter.cpp fx_adapter.cpp math_adapter.cpp weapon_adapter.cpp rechamber_adapter.cpp rechamber_check_adapter.cpp event_adapter.cpp offhand_adapter.cpp game_debug_adapter.cpp offhand_hold_adapter.cpp melee_end_adapter.cpp
    offhand_init_adapter.cpp lowready_adapter.cpp sprint_slide_adapter.cpp weapon_pose_adapter.cpp
    melee_fire_adapter.cpp sprint_loop_adapter.cpp timed_weapon_adapter.cpp
    dtp_transition_adapter.cpp slide_transition_adapter.cpp
    slide_check_adapter.cpp
    "${KISAK_RE_PACK_SOURCE}" "${CMAKE_CURRENT_BINARY_DIR}/selected.cpp")
target_include_directories(re_runner PRIVATE "${REPO}" "${REPO}/src" "${REPO}/src/libs" "${REPO}/src/universal")
target_compile_features(re_runner PRIVATE cxx_std_20)
target_compile_definitions(re_runner PRIVATE NDEBUG KISAK_MP)
if(EMSCRIPTEN)
  target_compile_definitions(re_runner PRIVATE KISAK_WEB)
  target_include_directories(re_runner BEFORE PRIVATE "${REPO}/src/web/compat")
  target_compile_options(re_runner PRIVATE -O3 -flto -msimd128 -msse2 -ffunction-sections -fms-extensions -fms-compatibility -fms-compatibility-version=19.40 -fdeclspec -fwrapv -fwrapv-pointer -fno-strict-aliasing -fno-strict-return -ffp-contract=off -Wno-everything
    "SHELL:-include ${REPO}/src/web/compat/kisak_web_compat.h")
  target_link_options(re_runner PRIVATE -O3 -flto -sENVIRONMENT=node -sNODERAWFS=1 -sEXIT_RUNTIME=1)
else()
  if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "The native oracle requires MSVC x86 (configure with -A Win32)")
  endif()
  target_compile_definitions(re_runner PRIVATE WIN32 _CRT_SECURE_NO_WARNINGS)
  target_compile_options(re_runner PRIVATE /O2 /Gy /Gw /fp:precise /permissive)
  target_link_options(re_runner PRIVATE /OPT:REF /OPT:ICF /BASE:0x10000000 /DYNAMICBASE:NO /INCREMENTAL:NO /MAP)
endif()
