#pragma once

#ifdef TRACY_ENABLE
#ifndef TRACY_ON_DEMAND
#error This should be left ON
#endif

#include <tracy/public/tracy/Tracy.hpp>
#include <tracy/public/tracy/TracyC.h>
//#include <tracy/Tracy.hpp>
//#include <tracy/TracyC.h>

#define PROF_SCOPED_RUNTIME_NAME(name) ZoneScoped; ZoneName(name, strlen(name));
#define PROF_SCOPED(name) ZoneScopedN(name)
#define PROFLOAD_SCOPED(name) PROF_SCOPED(name)
#define PROF_THREADNAME(threadname) tracy::SetThreadName(threadname)
#else

#ifdef KISAK_PROF_PHASES
// Web stall diagnostics: every PROF_SCOPED also records itself as the thread's current phase
// (Kisak_FutexWaits in threads.cpp prints it), which pins a browser-only hang to a code region.
extern "C" const char *Kisak_SetOp(const char *op);
struct KisakProfPhase
{
    const char *prev;
    explicit KisakProfPhase(const char *name) : prev(Kisak_SetOp(name)) {}
    ~KisakProfPhase() { Kisak_SetOp(prev); }
};
#define KISAK_PROF_CAT2(a, b) a##b
#define KISAK_PROF_CAT(a, b) KISAK_PROF_CAT2(a, b)
#define PROF_SCOPED(name) KisakProfPhase KISAK_PROF_CAT(kisakProfPhase_, __COUNTER__)(name)
#else
#define PROF_SCOPED(name) // Disable Profiling without Tracy
#endif
#define PROF_SCOPED_RUNTIME_NAME(name)
#define PROFLOAD_SCOPED(name) PROF_SCOPED(name)
#define ZoneText(str, len)
#define ZoneTextF(fmt, ...)
#define ZoneName(str, len)
#define PROF_THREADNAME(threadname)
#define FrameMark

#endif

