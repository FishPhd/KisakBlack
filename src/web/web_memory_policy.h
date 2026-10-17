#pragma once

#include <stdint.h>

// Wasm32 byte lengths are 32-bit. A 4 GiB memory reports size 0, so the
// ceiling is one Wasm page below 4 GiB (65535 pages).
//
// The client starts at Web_Wasm32InitialMemory and may grow to this ceiling.
// Mimalloc takes a fresh arena with one sbrk. That reservation is often larger
// than the bytes still free inside the initial heap, even while the program
// break is still short of 3 GiB. With growth disabled, emscripten_resize_heap
// aborts instead of returning failure, so mimalloc never retries a smaller
// arena. Growth lets the reservation succeed. It also turns off aborting
// malloc, so a request past this ceiling returns null and the smaller retry runs.
//
// The runtime trap is reported as "thread-local initialization routine for bgs"
// because that weak TLS stub is a single unreachable instruction and the linker
// folds it into emscripten's ___trap. bgs itself is a thread-local pointer.
//
// Heap views stay ordinary SharedArrayBuffers. GROWABLE_ARRAYBUFFERS /
// Memory.toResizableBuffer() makes them resizable, and WebGL2 bufferData and
// texImage2D reject that with "ArrayBufferView value must not be resizable".
constexpr uint32_t Web_Wasm32PageSize = 65536u;
constexpr uint32_t Web_Wasm32InitialMemory = 3072u * 1024u * 1024u;
constexpr uint32_t Web_Wasm32MaximumMemory = 4294901760u;

constexpr bool Web_Wasm32MemoryBudgetValid()
{
    return Web_Wasm32PageSize == 65536u &&
           Web_Wasm32InitialMemory % Web_Wasm32PageSize == 0 &&
           Web_Wasm32MaximumMemory % Web_Wasm32PageSize == 0 &&
           Web_Wasm32InitialMemory < Web_Wasm32MaximumMemory &&
           Web_Wasm32MaximumMemory / Web_Wasm32PageSize == 65535u &&
           (uint64_t)Web_Wasm32MaximumMemory + Web_Wasm32PageSize == 0x100000000ull;
}
