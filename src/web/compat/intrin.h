// <intrin.h> for the web build: the MSVC intrinsics clang does not already provide.
// Under -fms-extensions clang has _Interlocked{Increment,Decrement,ExchangeAdd,Exchange,
// CompareExchange,CompareExchange64,Or,And,Xor,ExchangePointer,...}, __popcnt, _rotl, _rotr,
// _byteswap_ulong, __debugbreak, _alloca and __assume as builtins, so only the rest is
// defined here.
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C++" {
#endif

static inline long long _InterlockedExchangeAdd64(volatile long long *p, long long v)
{
    return __atomic_fetch_add(p, v, __ATOMIC_SEQ_CST);
}

static inline unsigned char _BitScanForward(unsigned long *index, unsigned long mask)
{
    if (!mask)
        return 0;
    *index = (unsigned long)__builtin_ctz((unsigned)mask);
    return 1;
}
static inline unsigned char _BitScanReverse(unsigned long *index, unsigned long mask)
{
    if (!mask)
        return 0;
    *index = (unsigned long)(31 - __builtin_clz((unsigned)mask));
    return 1;
}

static inline void _ReadWriteBarrier(void) { __atomic_signal_fence(__ATOMIC_SEQ_CST); }
static inline void _mm_mfence(void) { __atomic_thread_fence(__ATOMIC_SEQ_CST); }
static inline void __faststorefence(void) { __atomic_thread_fence(__ATOMIC_SEQ_CST); }
static inline void __nop(void) {}
static inline unsigned long long __rdtsc(void) { return 0; }
static inline void __cpuid(int info[4], int leaf)
{
    (void)leaf;
    info[0] = info[1] = info[2] = info[3] = 0;
}

#ifdef __cplusplus
}
#endif
