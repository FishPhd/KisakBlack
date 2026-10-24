// These are private fixture bindings for the real extracted dvar lookup path.
// A missing showevents entry causes production Dvar_GetBool to return false;
// no diagnostic lookup result is synthesized by the harness.
#include <bgame/bg_local.h>
#include <universal/dvar.h>
#include <win32/win_common.h>
#include <cstdlib>
#include <cstring>

FastCriticalSection g_dvarCritSect{};
dvar_s* dvarHashTable[1024]{};

void re_event_reset()
{
    g_dvarCritSect.readCount = 0;
    g_dvarCritSect.writeCount = 0;
    std::memset(dvarHashTable, 0, sizeof(dvarHashTable));
}

void re_event_check()
{
    if (g_dvarCritSect.readCount || g_dvarCritSect.writeCount) std::abort();
    for (auto entry : dvarHashTable)
        if (entry) std::abort();
}
