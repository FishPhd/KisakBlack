// Force-included into every translation unit (C and C++) of the Web build; see
// cmake/platform_web.cmake. Holds the MSVC CRT / compiler spellings that clang's
// -fms-extensions does not provide. Keep it free of engine includes.
#pragma once

// -fms-compatibility-version (cmake/platform_web.cmake) is what turns char16_t/char32_t
// back into keywords, but it also defines the MSVC identification macros. The engine and
// libc++ must keep seeing a non-MSVC compiler, so drop them before anything else.
#undef _MSC_VER
#undef _MSC_FULL_VER
#undef _MSC_BUILD
#undef _MSC_EXTENSIONS
#undef _MSVC_LANG

// -fms-extensions makes __unaligned a keyword, but Emscripten's own xmmintrin.h declares
// `struct __unaligned`. The engine never uses the qualifier.
#define __unaligned __kisak_unaligned_unused

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <alloca.h>
#include <malloc.h>
#include <wchar.h>
#include <fcntl.h>
#include <sys/stat.h>

// ---- compiler keywords / attributes ---------------------------------------------------
// -fms-extensions already provides __declspec, __forceinline, __int8..__int64, __cdecl,
// __stdcall, __fastcall, __thiscall. Wasm has one calling convention, so the convention
// keywords are accepted and ignored.
// (__debugbreak, _alloca, __assume, _rotl/_rotr, _byteswap_ulong and the _Interlocked*
// family are clang builtins under -fms-extensions; intrin.h supplies the rest.)
#define __noop ((void)0)

// ---- CRT name differences -------------------------------------------------------------
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#define stricmp strcasecmp
#define strnicmp strncasecmp
#define _strdup strdup
#define _wcsicmp wcscasecmp
#define _wcsnicmp wcsncasecmp
#define wcsicmp wcscasecmp
#define wcsnicmp wcsncasecmp
#define _wcsdup wcsdup
#define _snprintf snprintf
#define _vsnprintf vsnprintf
#define _vsnwprintf vswprintf
#define _snwprintf swprintf
#define _fileno fileno
#define _getpid getpid
#define _isnan isnan
#define _finite isfinite
#define _copysign copysign
#define _atoi64 atoll
#define _strtoi64 strtoll
#define _strtoui64 strtoull
#define _fseeki64 fseeko
#define _ftelli64 ftello
#define _fstat fstat
#define _stat stat
#define _O_BINARY 0
#define _O_RDONLY O_RDONLY
#define _O_WRONLY O_WRONLY
#define _O_CREAT O_CREAT
#define _O_TRUNC O_TRUNC
#define O_BINARY 0
#define _MAX_PATH 260
#define _MAX_DRIVE 3
#define _MAX_DIR 256
#define _MAX_FNAME 256
#define _MAX_EXT 256
#define _countof(a) (sizeof(a) / sizeof((a)[0]))
#define _ARRAYSIZE(a) _countof(a)
#define __max(a, b) (((a) > (b)) ? (a) : (b))
#define __min(a, b) (((a) < (b)) ? (a) : (b))
#define _lrotl(v, s) _rotl(v, s)
#define _lrotr(v, s) _rotr(v, s)
#define _byteswap_ushort(x) __builtin_bswap16((uint16_t)(x))
#define _byteswap_uint64(x) __builtin_bswap64((uint64_t)(x))

static inline void *_aligned_malloc(size_t size, size_t align)
{
    if (align < 16)
        align = 16;
    return aligned_alloc(align, ((size + align - 1) / align) * align);
}
static inline void _aligned_free(void *p) { free(p); }
static inline void *_aligned_realloc(void *p, size_t size, size_t align)
{
    if (!p)
        return _aligned_malloc(size, align);
    size_t old = malloc_usable_size(p);
    void *n = _aligned_malloc(size, align);
    if (n)
    {
        memcpy(n, p, old < size ? old : size);
        free(p);
    }
    return n;
}

static inline char *_strlwr(char *s)
{
    for (char *p = s; *p; ++p)
        if (*p >= 'A' && *p <= 'Z')
            *p = (char)(*p + ('a' - 'A'));
    return s;
}
static inline char *_strupr(char *s)
{
    for (char *p = s; *p; ++p)
        if (*p >= 'a' && *p <= 'z')
            *p = (char)(*p - ('a' - 'A'));
    return s;
}
static inline char *_itoa(int value, char *buffer, int radix)
{
    if (radix == 10)
        sprintf(buffer, "%d", value);
    else if (radix == 16)
        sprintf(buffer, "%x", (unsigned)value);
    else if (radix == 8)
        sprintf(buffer, "%o", (unsigned)value);
    else
        sprintf(buffer, "%d", value);
    return buffer;
}
static inline char *_ultoa(unsigned long value, char *buffer, int radix)
{
    sprintf(buffer, radix == 16 ? "%lx" : "%lu", value);
    return buffer;
}
static inline char *_i64toa(long long value, char *buffer, int radix)
{
    sprintf(buffer, radix == 16 ? "%llx" : "%lld", value);
    return buffer;
}

// MSVC "secure CRT" variants the engine calls directly.
#ifndef KISAK_NO_SECURE_CRT_SHIMS
static inline int strcpy_s(char *dst, size_t size, const char *src)
{
    size_t n = strlen(src);
    if (n >= size)
    {
        if (size)
            dst[0] = 0;
        return 34; /* ERANGE */
    }
    memcpy(dst, src, n + 1);
    return 0;
}
static inline int strcat_s(char *dst, size_t size, const char *src)
{
    size_t dl = strlen(dst), sl = strlen(src);
    if (dl + sl >= size)
        return 34;
    memcpy(dst + dl, src, sl + 1);
    return 0;
}
static inline int sprintf_s(char *dst, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(dst, size, fmt, ap);
    va_end(ap);
    return r;
}
static inline int vsprintf_s(char *dst, size_t size, const char *fmt, va_list ap)
{
    return vsnprintf(dst, size, fmt, ap);
}
static inline int _vsnprintf_s(char *dst, size_t size, size_t, const char *fmt, va_list ap)
{
    return vsnprintf(dst, size, fmt, ap);
}
static inline int fopen_s(FILE **f, const char *name, const char *mode)
{
    *f = fopen(name, mode);
    return *f ? 0 : 2;
}
#endif

// 64-bit time_t CRT names (time_t is already 64-bit in Emscripten's libc).
#include <time.h>
#include <errno.h>
typedef long long __time64_t;
#define _time64(p) ((__time64_t)time((time_t *)(p)))
#define _localtime64(p) localtime((const time_t *)(p))
#define _gmtime64(p) gmtime((const time_t *)(p))
#define _ctime64(p) ctime((const time_t *)(p))
#define _errno() (&errno)
#define _putenv(s) putenv((char *)(s))
#define _tzset() tzset()
static inline int _vscprintf(const char *fmt, va_list ap)
{
    va_list c;
    va_copy(c, ap);
    int n = vsnprintf(NULL, 0, fmt, c);
    va_end(c);
    return n;
}

// _findfirst/_findnext/_findclose (wildcard directory listing) - web_win32.cpp.
struct _finddata64i32_t
{
    unsigned attrib;
    __time64_t time_create;
    __time64_t time_access;
    __time64_t time_write;
    unsigned long size;
    char name[260];
};
#ifdef __cplusplus
extern "C" {
#endif
intptr_t _findfirst64i32(const char *filespec, struct _finddata64i32_t *fileinfo);
int _findnext64i32(intptr_t handle, struct _finddata64i32_t *fileinfo);
int _findclose(intptr_t handle);
#ifdef __cplusplus
}
#endif
#define _finddata_t _finddata64i32_t
#define _findfirst _findfirst64i32
#define _findnext _findnext64i32

// The 8087 control word is meaningless on wasm (always IEEE round-to-nearest, double
// rounding does not exist). The engine's _controlfp / _clearfp calls become no-ops.
#define _MCW_PC 0x00030000
#define _PC_24 0x00020000
#define _PC_53 0x00010000
#define _PC_64 0x00000000
#define _MCW_RC 0x00000300
#define _RC_NEAR 0x00000000
#define _RC_CHOP 0x00000300
#define _MCW_EM 0x0008001F
#define _EM_INVALID 0x00000010
#define _EM_ZERODIVIDE 0x00000008
#define _EM_OVERFLOW 0x00000004
#define _EM_UNDERFLOW 0x00000002
#define _EM_INEXACT 0x00000001
#define _EM_DENORMAL 0x00080000
static inline unsigned int _controlfp(unsigned int newv, unsigned int mask)
{
    (void)newv;
    (void)mask;
    return _PC_53 | _RC_NEAR;
}
static inline unsigned int _clearfp(void) { return 0; }
static inline unsigned int _statusfp(void) { return 0; }

// MMX state reset (the MMX skinning path is not compiled on web).
#ifdef __cplusplus
static inline void _m_empty(void) {}
#endif

#include <ctype.h>
#include <float.h>
#include <limits.h>
#include <unistd.h>

// MSVC's FILE is struct _iobuf; the decompiled code names it directly.
typedef FILE _iobuf;

// The engine calls the underscore form of setjmp. Emscripten's SjLj lowering keys on the
// plain name, so route it there.
#include <setjmp.h>
#define _setjmp setjmp

// <intrin.h>/<windows.h> on MSVC bring the SSE intrinsics (_mm_prefetch, __m128, ...) in.
#include <xmmintrin.h>

// libc names that collide with engine identifiers (declared before these macros, so the
// libc declarations keep their real names; engine code from here on gets the renamed ones).
#define random kisak_random
#define index kisak_index

#define _ui64toa(v, buf, radix) _i64toa((long long)(v), (buf), (radix))
typedef int (*_CoreCrtNonSecureSearchSortCompareFunction)(const void *, const void *);

#ifdef __cplusplus
// MSVC accepts `volatile unsigned *` for the long-typed _Interlocked* intrinsics (the tl
// thread code relies on it); clang's builtins are strictly `volatile long *`. Route the
// 32/64-bit forms through width-checked templates. All results match the MSVC contract
// (Increment/Decrement return the new value, the rest return the old value).
namespace kisak_ilk
{
template <class T> inline long Inc(volatile T *p) { static_assert(sizeof(T) == 4, "32-bit only"); return __atomic_add_fetch((volatile int *)p, 1, __ATOMIC_SEQ_CST); }
template <class T> inline long Dec(volatile T *p) { static_assert(sizeof(T) == 4, "32-bit only"); return __atomic_sub_fetch((volatile int *)p, 1, __ATOMIC_SEQ_CST); }
template <class T> inline long XAdd(volatile T *p, long v) { static_assert(sizeof(T) == 4, "32-bit only"); return __atomic_fetch_add((volatile int *)p, (int)v, __ATOMIC_SEQ_CST); }
template <class T> inline long Xchg(volatile T *p, long v) { static_assert(sizeof(T) == 4, "32-bit only"); return __atomic_exchange_n((volatile int *)p, (int)v, __ATOMIC_SEQ_CST); }
template <class T> inline long Or(volatile T *p, long v) { static_assert(sizeof(T) == 4, "32-bit only"); return __atomic_fetch_or((volatile int *)p, (int)v, __ATOMIC_SEQ_CST); }
template <class T> inline long And(volatile T *p, long v) { static_assert(sizeof(T) == 4, "32-bit only"); return __atomic_fetch_and((volatile int *)p, (int)v, __ATOMIC_SEQ_CST); }
template <class T> inline long Xor(volatile T *p, long v) { static_assert(sizeof(T) == 4, "32-bit only"); return __atomic_fetch_xor((volatile int *)p, (int)v, __ATOMIC_SEQ_CST); }
template <class T> inline long Cmpxchg(volatile T *p, long exchange, long comparand)
{
    static_assert(sizeof(T) == 4, "32-bit only");
    int expected = (int)comparand;
    __atomic_compare_exchange_n((volatile int *)p, &expected, (int)exchange, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return expected;
}
template <class T> inline long long Cmpxchg64(volatile T *p, long long exchange, long long comparand)
{
    static_assert(sizeof(T) == 8, "64-bit only");
    long long expected = comparand;
    __atomic_compare_exchange_n((volatile long long *)p, &expected, exchange, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return expected;
}
} // namespace kisak_ilk
#define _InterlockedIncrement kisak_ilk::Inc
#define _InterlockedDecrement kisak_ilk::Dec
#define _InterlockedExchangeAdd kisak_ilk::XAdd
#define _InterlockedExchange kisak_ilk::Xchg
#define _InterlockedOr kisak_ilk::Or
#define _InterlockedAnd kisak_ilk::And
#define _InterlockedXor kisak_ilk::Xor
#define _InterlockedCompareExchange kisak_ilk::Cmpxchg
#define _InterlockedCompareExchange64 kisak_ilk::Cmpxchg64

#include <new>
#include <type_traits>
// MSVC <windows.h> defines min/max as macros; the engine relies on them with mixed types.
// Templates give the same usual-arithmetic-conversion result without clobbering std::min.
template <class A, class B> constexpr typename std::common_type<A, B>::type min(A a, B b) { return b < a ? b : a; }
template <class A, class B> constexpr typename std::common_type<A, B>::type max(A a, B b) { return a < b ? b : a; }
#endif

// MSVC's rand() returns 0..32767 and the engine hard-codes that range (`rand() / 32767.0`,
// `rand() / 32768.0` in GaussianRandom's rejection loop, ...). musl returns 0..2^31-1, which turned
// R_CreateParticleCloudBuffer into an infinite loop. Wrap it after every libc header that declares rand.
#ifdef __cplusplus
extern "C" {
#endif
static inline int kisak_rand15(void) { return rand() & 0x7FFF; }
#ifdef __cplusplus
}
#endif
#undef RAND_MAX
#define RAND_MAX 0x7FFF
#define rand() kisak_rand15()
