#pragma once

// Thread wait/wake primitives shared by the desktop and web builds.
// Desktop: WaitOnAddress / WakeByAddress* (Windows 8+, Synchronization.lib).
// Web: emscripten_futex_wait / emscripten_futex_wake (Atomics.wait, needs -pthread).

#include <atomic>
#include <cstdint>

#ifdef KISAK_WEB
#include <emscripten/threading.h>
#include <sched.h>
#include <cmath>
#else
#include <Windows.h>
#include <intrin.h>
#endif

#define SYS_FUTEX_INFINITE 0xFFFFFFFFu

#ifdef KISAK_WEB
// Diagnostics: records which address each thread is parked on (Kisak_FutexWaits, threads.cpp).
extern "C" void Kisak_NoteWait(const void *addr, int enter);
#endif

// Blocks while *addr == expected, until woken or timeoutMs elapses. May return spuriously.
inline void Sys_FutexWait(volatile uint32_t *addr, uint32_t expected, uint32_t timeoutMs)
{
#ifdef KISAK_WEB
    Kisak_NoteWait((const void *)addr, 1);
    emscripten_futex_wait((void *)addr, expected, timeoutMs == SYS_FUTEX_INFINITE ? INFINITY : (double)timeoutMs);
    Kisak_NoteWait((const void *)addr, 0);
#else
    WaitOnAddress(addr, &expected, sizeof(expected), timeoutMs);
#endif
}

inline void Sys_FutexWakeOne(volatile uint32_t *addr)
{
#ifdef KISAK_WEB
    emscripten_futex_wake((void *)addr, 1);
#else
    WakeByAddressSingle((PVOID)addr);
#endif
}

inline void Sys_FutexWakeAll(volatile uint32_t *addr)
{
#ifdef KISAK_WEB
    emscripten_futex_wake((void *)addr, INT32_MAX);
#else
    WakeByAddressAll((PVOID)addr);
#endif
}

inline void Sys_CpuRelax()
{
#ifdef KISAK_WEB
    // wasm has no pause instruction
#else
    _mm_pause();
#endif
}

inline void Sys_YieldThread()
{
#ifdef KISAK_WEB
    sched_yield();
#else
    SwitchToThread();
#endif
}

// std::atomic<uint32_t> overloads. The futex word must be a plain 4-byte lock-free atomic.
static_assert(sizeof(std::atomic<uint32_t>) == sizeof(uint32_t) && std::atomic<uint32_t>::is_always_lock_free,
    "futex words must be lock-free 32-bit atomics");

inline void Sys_FutexWait(std::atomic<uint32_t> *addr, uint32_t expected, uint32_t timeoutMs)
{
    Sys_FutexWait(reinterpret_cast<volatile uint32_t *>(addr), expected, timeoutMs);
}

inline void Sys_FutexWakeOne(std::atomic<uint32_t> *addr)
{
    Sys_FutexWakeOne(reinterpret_cast<volatile uint32_t *>(addr));
}

inline void Sys_FutexWakeAll(std::atomic<uint32_t> *addr)
{
    Sys_FutexWakeAll(reinterpret_cast<volatile uint32_t *>(addr));
}

// Adaptive backoff for short waits: pause first, then give the core away.
inline void Sys_SpinBackoff(unsigned int &spins)
{
    if (spins < 64)
    {
        for (unsigned int i = 0; i < (1u << (spins >> 4)); ++i)
            Sys_CpuRelax();
    }
    else
    {
        Sys_YieldThread();
    }
    ++spins;
}
