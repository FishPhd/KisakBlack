# Web platform: Emscripten, wasm32, pthreads. Included by CMakeLists.txt when
# KISAK_PLATFORM=Web. Creates the KisakBlack target.
#
# Foundation scope: the whole engine is compiled and boots as a headless
# dedicated server (+set dedicated 1) in the browser. The renderer, audio, video,
# input and remote networking are stubbed (src/web/compat, src/web/stubs).
#
# Configure with:  cmake --preset web      (or web-debug)
# Needs:           EMSDK env var pointing at the emsdk pinned in
#                  tools/web/emsdk-version.txt (tools/web/setup_emsdk.* installs it).

# No Bink DLL, local D3DX replacements: there is no Windows here.
# KISAK_NO_STEAM was removed from CMakeLists.txt; do not set it here.
set(KISAK_NO_BINK ON CACHE BOOL "" FORCE)
set(KISAK_NO_D3DX ON CACHE BOOL "" FORCE)

# ----- Source filtering -----
# Matched against the absolute (forward slash) source path.
set(KISAK_WEB_EXCLUDE_REGEX
    # Third-party / vendor code with no wasm build
    "/src/CubeMapGenLib/"
    "/src/vpx/"
    "/src/tracy/"
    "/src/nvapi/"
    # Developer test harness (Winsock); the web build has no remote tooling
    "/src/monkey/"
    # Voice chat over DirectSound; web voice (if ever) goes through Web Audio
    "/src/groupvoice/(play|record)_dsound\\.cpp$"
    # Platform audio drivers (src/web/stubs provides null drivers)
    "/src/sound/snd_driver_xaudio2[a-z_]*\\.cpp$"
    # MMX (__m64) skinning. Web uses the wasm SIMD loops in r_model_skin.cpp.
    "/src/gfx_d3d/r_model_skin_sse\\.cpp$"
    # OS-bound Win32 layer, replaced by src/web/*. The portable src/win32 files
    # (common, content, gamerprofile, localize, stream, tasks, workercmds, ...)
    # are kept.
    "/src/win32/win_(main|net|syscon|wndproc|input|gamepad|voice|mini_dumper|splash|shared|configure|steam)\\.cpp$"
    # Real Bink helpers (binkw32 + D3DX shader compile); bink_stub.cpp is used
    "/src/binklib/(binktextures|dx9rad3d)\\.cpp$")

foreach(_rx IN LISTS KISAK_WEB_EXCLUDE_REGEX)
  list(FILTER KISAK_SOURCES EXCLUDE REGEX "${_rx}")
endforeach()
unset(_rx)

# Platform sources: the Win32 replacement layer and the null/stub back ends.
file(GLOB_RECURSE KISAK_WEB_SOURCES CONFIGURE_DEPENDS
     "${SRC_DIR}/web/*.cpp" "${SRC_DIR}/web/*.c")

add_executable(${BIN_NAME} ${KISAK_SOURCES} ${KISAK_WEB_SOURCES}
                           "${SRC_DIR}/binklib/bink_stub.cpp"
                           # D3DX replacement: loads d3dcompiler via LoadLibraryA, which fails
                           # (returns null) on the web build, so every call returns an error.
                           "${SRC_DIR}/gfx_d3d/r_d3dx_compat.cpp")

# ----- Preprocessor defines -----
# _WIN32/WIN32 are deliberately NOT defined: third-party code (zlib, jpeg,
# libtomcrypt) must take its POSIX paths. Engine code reaches the Win32 types it
# uses through src/web/compat/windows.h.
target_compile_definitions(
  KisakBlack
  PUBLIC KISAK_WEB
         _CRT_SECURE_NO_WARNINGS)

# src/web/compat first so <windows.h>, <d3d9.h>, <xaudio2.h>, ... resolve to the
# hand-written subset headers instead of the DirectX SDK.
target_include_directories(KisakBlack BEFORE PUBLIC "${SRC_DIR}/web/compat")

# ----- Compile flags -----
# -fms-extensions -fms-compatibility -fdeclspec: decompiler / MSVC syntax
#   (__declspec, __int64, anonymous structs, __forceinline, ...). These do not
#   define _MSC_VER, so _MSC_VER-guarded MSVC-only code stays out.
# -fwasm-exceptions + -sSUPPORT_LONGJMP=wasm: native wasm setjmp/longjmp (Com_Error).
# -msimd128 -msse2: the existing SSE intrinsics map to wasm SIMD128.
# -fwrapv -fwrapv-pointer -fno-strict-aliasing -fno-strict-return: MSVC does not exploit
#   signed overflow, pointer wraparound, type-based aliasing or falling off the end of a
#   non-void function, and the decompiled code relies on all four. -fwrapv-pointer matters
#   most: idioms like `p = src - 1; *++p` (I_strncpyz, many string loops) are `inbounds`
#   GEPs out of the object to clang; with -O3 -flto the inliner turned them into poison and
#   I_strlwr(I_strncpyz(buf, ...)) became an infinite loop (DDL_Lookup_MoveTo hung boot).
# -ffp-contract=off: no fused multiply-add, so server math is reproducible.
# KISAK_WEB_AUDIT_FPTR: wasm traps on call_indirect signature mismatch (x86 tolerated casting
# callbacks to a different prototype). This swaps -w for -Wcast-function-type-strict so every
# such cast shows up in the compile log.
option(KISAK_WEB_AUDIT_FPTR "Web: report mismatched function-pointer casts" OFF)
if(KISAK_WEB_AUDIT_FPTR)
  set(KISAK_WEB_WARN_FLAGS -Wno-everything -Wcast-function-type-strict)
else()
  set(KISAK_WEB_WARN_FLAGS -w)
endif()

set(KISAK_WEB_COMMON_FLAGS
    -pthread
    -msimd128
    -msse2
    -fms-extensions
    -fms-compatibility
    # >= 19.00 makes char16_t/char32_t keywords again; it also defines _MSC_VER, which the
    # force-included kisak_web_compat.h immediately undefines.
    -fms-compatibility-version=19.40
    -fdeclspec
    -fwasm-exceptions
    -sSUPPORT_LONGJMP=wasm
    -fwrapv
    -fwrapv-pointer
    -fno-strict-aliasing
    -fno-strict-return
    -ffp-contract=off
    # Decompiled code is warning-heavy; real problems are errors.
    ${KISAK_WEB_WARN_FLAGS}
    # enum X : __int32 { INVALID = 0xFFFFFFFF }: MSVC wraps the value to -1, clang makes it
    # a default-on error. The wrapped value is what the original binary had.
    -Wno-c++11-narrowing)

# Compile-error iteration: -O0, no debug info, no LTO (web-check preset).
option(KISAK_WEB_FAST_COMPILE "Web: -O0 without -g/-flto for quick compile checks" OFF)
if(KISAK_WEB_FAST_COMPILE)
  set(KISAK_WEB_OPT_FLAGS -O0)
else()
  set(KISAK_WEB_OPT_FLAGS $<$<CONFIG:Release>:-O3> $<$<CONFIG:Release>:-flto>
                          $<$<CONFIG:Debug>:-O1> $<$<CONFIG:Debug>:-g>)
endif()

# Third-party C (jpeg, zlib, speex, libtomcrypt) was written for permissive C compilers; the
# diagnostics below are errors in clang 16+ but only warnings in MSVC.
target_compile_options(
  KisakBlack PRIVATE $<$<COMPILE_LANGUAGE:C>:-Wno-error=implicit-function-declaration>
                     $<$<COMPILE_LANGUAGE:C>:-Wno-error=incompatible-pointer-types>
                     $<$<COMPILE_LANGUAGE:C>:-Wno-error=incompatible-function-pointer-types>
                     $<$<COMPILE_LANGUAGE:C>:-Wno-error=int-conversion>)

target_compile_options(
  KisakBlack PRIVATE ${KISAK_WEB_COMMON_FLAGS}
                     # MSVC shims -fms-extensions does not cover (every TU)
                     "SHELL:-include ${SRC_DIR}/web/compat/kisak_web_compat.h"
                     ${KISAK_WEB_OPT_FLAGS}
                     $<$<BOOL:${KISAK_WEB_MEMORY64}>:-sMEMORY64=1>)

# ----- Link flags -----
# PROXY_TO_PTHREAD: main() runs on a worker, so the blocking while(1) Com_Frame
#   loop and the futex waits run unchanged (no ASYNCIFY).
# Wasm32 starts at KISAK_WEB_INITIAL_MEMORY and may grow to one page below 4 GiB.
# A fixed heap aborts when mimalloc reserves another arena: that sbrk is larger
# than the bytes still free, and with growth disabled emscripten traps instead of
# letting mimalloc retry a smaller arena. Full WebGPU preparation of an MP map
# hits this while the break is still short of 3 GiB.
# Do not set GROWABLE_ARRAYBUFFERS. toResizableBuffer() makes the heap a
# resizable ArrayBuffer, and WebGL2 bufferData/texImage2D (and WebGPU
# queue.writeBuffer of the same HEAPU8 view) throw
# "ArrayBufferView value must not be resizable" during startup. Growth still
# works: Emscripten replaces the ordinary SharedArrayBuffer views after sbrk.
# WASMFS: the OPFS backend (persistent storage) is part of it in Emscripten 6.
# The same wasm also runs under Node (headless tests: tools/web/run_node.mjs mounts a host
# directory through WasmFS' Node backend). The glue it adds is negligible; turn it off for a
# browser-only artifact.
option(KISAK_WEB_NODE "Web: also run under Node.js (headless tests)" ON)
if(KISAK_WEB_NODE)
  set(KISAK_WEB_ENVIRONMENT "web,worker,node")
else()
  set(KISAK_WEB_ENVIRONMENT "web,worker")
endif()

# Default matches Web_Wasm32InitialMemory in src/web/web_memory_policy.h.
set(KISAK_WEB_INITIAL_MEMORY 3072MB CACHE STRING "wasm linear memory size")
set(KISAK_WEB_POOL_SIZE 8 CACHE STRING "pre-spawned pthread workers")

# Memory64 (wasm64) is what lifts the browser past 4 GB, up to 16 GB on Chrome/Edge 133+.
# It widens every pointer to 64 bits. Fastfiles are the 32-bit PC layout (see
# src/database/db_layout_asserts.cpp), so this build is refused there until that port exists.
# Off by default: keep `cmake --preset web` (build-web) as the comparable wasm32 client.
# On: `cmake --preset web-m64` writes build-web-m64 and does not replace build-web.
# Browsers pay a pointer-width cost (on the order of 10% in other wasm64 apps) plus the cost of
# growing the heap. Memory starts at KISAK_WEB_INITIAL_MEMORY and may grow to 16 GB; it does not
# reserve 16 GB up front.
option(KISAK_WEB_MEMORY64 "Web: wasm64 Memory64 comparison build (preset web-m64, up to 16GB)" OFF)
# Byte ceiling lives in src/web/web_memory_policy.h so the Node policy test and the link line match.
file(READ "${SRC_DIR}/web/web_memory_policy.h" _kisak_web_memory_policy)
if(NOT _kisak_web_memory_policy MATCHES "Web_Wasm32MaximumMemory = ([0-9]+)u")
  message(FATAL_ERROR "src/web/web_memory_policy.h is missing Web_Wasm32MaximumMemory")
endif()
set(KISAK_WEB_WASM32_MAXIMUM_MEMORY "${CMAKE_MATCH_1}")
if(KISAK_WEB_MEMORY64)
  set(KISAK_WEB_MEMORY_LINK_FLAGS -sMEMORY64=1 -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=16GB)
else()
  # Growth also turns off ABORTING_MALLOC, so a reservation past the ceiling returns null
  # and mimalloc retries a smaller arena instead of trapping in ___trap.
  # GROWABLE_ARRAYBUFFERS stays off: see the note above the link flags.
  set(KISAK_WEB_MEMORY_LINK_FLAGS -sALLOW_MEMORY_GROWTH=1 -sGROWABLE_ARRAYBUFFERS=0
                                  -sMAXIMUM_MEMORY=${KISAK_WEB_WASM32_MAXIMUM_MEMORY})
endif()

set(KISAK_WEB_LINK_FLAGS
    ${KISAK_WEB_COMMON_FLAGS}
    -sPROXY_TO_PTHREAD
    -sPTHREAD_POOL_SIZE=${KISAK_WEB_POOL_SIZE}
    -sINITIAL_MEMORY=${KISAK_WEB_INITIAL_MEMORY}
    ${KISAK_WEB_MEMORY_LINK_FLAGS}
    -sSTACK_SIZE=1MB
    -sDEFAULT_PTHREAD_STACK_SIZE=1MB
    -sWASMFS
    -sMALLOC=mimalloc
    -sMODULARIZE
    -sEXPORT_ES6
    -sEXPORT_NAME=KisakModule
    -sENVIRONMENT=${KISAK_WEB_ENVIRONMENT}
    -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,UTF8ToString,stringToUTF8,lengthBytesUTF8,HEAPU8,HEAP16
    -sEXPORTED_FUNCTIONS=_main,_Kisak_ConsoleInput,_malloc,_free
    -sEXIT_RUNTIME=1
    # WebGL2 only: the D3D9 emulation (src/web/gl) creates its own OffscreenCanvas on the
    # render thread and posts ImageBitmaps to the page. Pinned off: enabling it makes
    # PROXY_TO_PTHREAD call transferControlToOffscreen on Module.canvas. A placeholder
    # OffscreenCanvas has no such method, so startup dies with
    # "cannot transfer control of canvas kisak-placeholder".
    -sOFFSCREENCANVAS_SUPPORT=0
    -sMAX_WEBGL_VERSION=2
    -sMIN_WEBGL_VERSION=2
    -sGL_TRACK_ERRORS=0
    # Decompiled callbacks are cast to a nearby prototype. x86 calls them anyway; wasm call_indirect
    # traps with "function signature mismatch" (the match loading screen dies on the first one).
    -sEMULATE_FUNCTION_POINTER_CASTS=1
    # (C++ exceptions come from -fwasm-exceptions; -sNO_DISABLE_EXCEPTION_CATCHING conflicts with it)
    # Report every unresolved symbol at once instead of stopping at the first.
    -Wl,--error-limit=0)

# Keep wasm function names in an optimized build (stack dumps: tools/web/run_node.mjs --dump-stacks).
option(KISAK_WEB_TRACE_PHASES "Web: PROF_SCOPED regions publish the thread phase (Module._Kisak_FutexWaits) to locate stalls" OFF)
if(KISAK_WEB_TRACE_PHASES)
  target_compile_definitions(KisakBlack PRIVATE KISAK_PROF_PHASES)
endif()

# On for the web preset so a trap names the C++ function beside the hex offset.
# FORCE: option() would leave a cache entry that was written when this defaulted to OFF.
set(KISAK_WEB_PROFILING_FUNCS ON CACHE BOOL "Web: keep function names in the wasm name section" FORCE)
if(KISAK_WEB_PROFILING_FUNCS)
  list(APPEND KISAK_WEB_LINK_FLAGS --profiling-funcs)
endif()

# Runs inside every pthread worker (including PROXY_TO_PTHREAD's main), where the
# RuntimeError object still exists. The page only sees error === null.
set(_kisak_crash_js "${CMAKE_CURRENT_BINARY_DIR}/kisak_pthread_crash.js")
set(_kisak_crash_js_content [=[
if (typeof ENVIRONMENT_IS_PTHREAD !== "undefined" && ENVIRONMENT_IS_PTHREAD) {
  function kisakPostTrap(error, fallbackMessage) {
    if (self.__kisakTrapPosted) return;
    if (error === "unwind") return;
    if (error && typeof error === "object" && error.name === "ExitStatus") return;
    var message = "";
    var stack = "";
    if (error && typeof error === "object") {
      if (error.message != null) message = String(error.message);
      if (typeof error.stack === "string") stack = error.stack;
    } else if (typeof error === "string") {
      message = error;
    }
    if (!message && fallbackMessage != null) message = String(fallbackMessage);
    if (!message && !stack) return;
    self.__kisakTrapPosted = true;
    var flatStack = stack.replace(/[\r\n]+/g, " | ").replace(/[ \t]{2,}/g, " ").trim();
    var line = "wasm trap: " + message;
    if (flatStack) line += " | " + flatStack;
    try {
      if (typeof err === "function") err(line);
      else console.error(line);
    } catch (ignored) {
      console.error(line);
    }
  }
  if (typeof invokeEntryPoint === "function") {
    var kisakInvokeEntryPoint = invokeEntryPoint;
    invokeEntryPoint = function (ptr, arg) {
      try {
        return kisakInvokeEntryPoint(ptr, arg);
      } catch (ex) {
        kisakPostTrap(ex);
        throw ex;
      }
    };
  }
  if (typeof self.addEventListener === "function") self.addEventListener("error", function (ev) {
    var error = ev.error;
    if (error === "unwind" || (error && error.name === "ExitStatus")) return;
    kisakPostTrap(error, ev.message);
    if (ev.preventDefault) ev.preventDefault();
  });
  self.onerror = function (message, filename, lineno, colno, error) {
    if (error === "unwind" || (error && error.name === "ExitStatus")) return false;
    kisakPostTrap(error, message);
    return true;
  };
}
]=])
if(EXISTS "${_kisak_crash_js}")
  file(READ "${_kisak_crash_js}" _kisak_crash_js_prev)
else()
  set(_kisak_crash_js_prev "")
endif()
if(NOT _kisak_crash_js_prev STREQUAL _kisak_crash_js_content)
  file(WRITE "${_kisak_crash_js}" "${_kisak_crash_js_content}")
endif()
file(TO_CMAKE_PATH "${_kisak_crash_js}" _kisak_crash_js_fwd)
list(APPEND KISAK_WEB_LINK_FLAGS "--post-js=${_kisak_crash_js_fwd}")
set_property(TARGET KisakBlack APPEND PROPERTY LINK_DEPENDS "${_kisak_crash_js}")

target_link_options(KisakBlack PRIVATE ${KISAK_WEB_LINK_FLAGS}
                                       ${KISAK_WEB_OPT_FLAGS})

# kisak.mjs + kisak.wasm (+ the pthread worker script) land next to each other.
set_target_properties(KisakBlack PROPERTIES
  OUTPUT_NAME "kisak"
  SUFFIX ".mjs")

# Stage the page shell (web/index.html, kisak.js) beside the build output so
# tools/web/serve.mjs --root build-web serves a working page.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/web")
  add_custom_command(
    TARGET KisakBlack POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/web" "$<TARGET_FILE_DIR:KisakBlack>"
    COMMENT "Staging web shell")
endif()

# Called by CMakeLists.txt after the shared defines/includes are in place.
function(kisak_platform_finalize)
  # Nothing to add after the shared settings on Web: no SDK paths or import libs.
endfunction()
