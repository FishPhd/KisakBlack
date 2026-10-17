// Policy numbers are checked at compile time. To reproduce the wasm32 failure,
// link this file with mimalloc, -sINITIAL_MEMORY=16MB, -sALLOW_MEMORY_GROWTH=1,
// and -sMAXIMUM_MEMORY equal to Web_Wasm32MaximumMemory, then run it under Node.
// Do not pass -sGROWABLE_ARRAYBUFFERS. The heap must grow past 16 MB. The same
// link with -sALLOW_MEMORY_GROWTH=0 aborts in emscripten_resize_heap.
#include "../../src/web/web_memory_policy.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/heap.h>
#endif

static_assert(Web_Wasm32MemoryBudgetValid());
static_assert(Web_Wasm32InitialMemory == 3072u * 1024u * 1024u);
static_assert(Web_Wasm32MaximumMemory == 4294901760u);

// Linked with a small initial heap and the client's growth ceiling. The byte
// count is volatile so the reservation cannot be deleted at compile time.
static volatile size_t g_reserveBytes = 32u * 1024u * 1024u;
static volatile unsigned char g_sink;

int main()
{
    const size_t bytes = g_reserveBytes;
    unsigned char *block = static_cast<unsigned char *>(malloc(bytes));
    if (!block)
    {
        fprintf(stderr, "arena reservation failed\n");
        return 1;
    }
    block[0] = 1;
    block[bytes - 1] = 2;
    g_sink = static_cast<unsigned char>(block[0] + block[bytes - 1]);
#ifdef __EMSCRIPTEN__
    const size_t heap = emscripten_get_heap_size();
    fprintf(stderr, "memory policy ok %u heap %zu\n", g_sink, heap);
    free(block);
    return g_sink == 3 && heap > 16u * 1024u * 1024u ? 0 : 1;
#else
    fprintf(stderr, "memory policy ok %u\n", g_sink);
    free(block);
    return g_sink == 3 ? 0 : 1;
#endif
}
