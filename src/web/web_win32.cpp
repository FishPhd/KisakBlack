// Win32 API subset for the web build (Emscripten, wasm32, pthreads).
//
// Declarations live in src/web/compat/windows.h. Everything here is the thinnest correct mapping
// onto pthreads / futexes / POSIX-over-WasmFS; calls with no meaning in a browser (windows, cursor,
// gamma ramps, modules, debugger) fail or no-op.

#include <windows.h>

#include "web_assets.h"
#include <universal/com_files.h> // fs_debug

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <pthread.h>
#include <sched.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <emscripten.h>
#include <emscripten/em_asm.h>
#include <emscripten/threading.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#ifdef KISAK_PROF_PHASES
static void NoteSpin(const char *what);
#else
static inline void NoteSpin(const char *) {}
#endif

extern "C" const char *Kisak_SetOp(const char *op); // threads.cpp: per-thread "inside <op>" marker for stall diagnostics
struct FileOp
{
    const char *prev;
    explicit FileOp(const char *name) : prev(Kisak_SetOp(name)) {}
    ~FileOp() { Kisak_SetOp(prev); }
};


// ---------------------------------------------------------------------------------------------
// Per-thread state
// ---------------------------------------------------------------------------------------------

static std::atomic<DWORD> s_nextThreadId{1};
static thread_local DWORD t_threadId = 0;
static thread_local DWORD t_lastError = 0;

EXTERN_C DWORD WINAPI GetCurrentThreadId(void)
{
    if (!t_threadId)
        t_threadId = s_nextThreadId.fetch_add(1);
    return t_threadId;
}

EXTERN_C DWORD WINAPI GetLastError(void) { return t_lastError; }
EXTERN_C void WINAPI SetLastError(DWORD e) { t_lastError = e; }

static DWORD ErrnoToWin32(int e)
{
    switch (e)
    {
    case 0: return ERROR_SUCCESS;
    case ENOENT: return ERROR_FILE_NOT_FOUND;
    case ENOTDIR: return ERROR_PATH_NOT_FOUND;
    case EACCES:
    case EPERM:
    case EROFS: return ERROR_ACCESS_DENIED;
    case EEXIST: return ERROR_ALREADY_EXISTS;
    default: return ERROR_ACCESS_DENIED;
    }
}

// ---------------------------------------------------------------------------------------------
// Kernel objects
// ---------------------------------------------------------------------------------------------

enum KKind { K_THREAD = 1, K_EVENT, K_MUTEX, K_SEMAPHORE, K_FILE, K_FIND };

struct KObj
{
    KKind kind;
    std::atomic<int> refs{1};

    pthread_mutex_t lock;
    pthread_cond_t cond;

    // event
    bool manual = false;
    bool signaled = false;
    unsigned pulseGen = 0;
    int waiters = 0;
    int pulseTokens = 0;

    // semaphore
    LONG count = 0;
    LONG maxCount = 0;

    // mutex
    DWORD owner = 0;
    LONG recursion = 0;

    // thread
    pthread_t thread{};
    DWORD tid = 0;
    bool finished = false;
    DWORD exitCode = STILL_ACTIVE;
    LPTHREAD_START_ROUTINE fn = nullptr;
    void *arg = nullptr;
    LONG suspendCount = 0;

    // file
    int fd = -1;

    // find
    DIR *dir = nullptr;
    std::string dirPath;
    std::string pattern;

    explicit KObj(KKind k) : kind(k)
    {
        pthread_mutex_init(&lock, nullptr);
        pthread_cond_init(&cond, nullptr);
    }
    ~KObj()
    {
        pthread_cond_destroy(&cond);
        pthread_mutex_destroy(&lock);
    }
};

static void KRelease(KObj *o)
{
    if (o->refs.fetch_sub(1) == 1)
        delete o;
}

static KObj *KFrom(HANDLE h, KKind kind)
{
    if (!h || h == INVALID_HANDLE_VALUE || h == (HANDLE)(LONG_PTR)-2)
        return nullptr;
    KObj *o = (KObj *)h;
    return o->kind == kind ? o : nullptr;
}

static void AbsTimeout(struct timespec *ts, DWORD ms)
{
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec += ms / 1000;
    ts->tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L)
    {
        ts->tv_sec += 1;
        ts->tv_nsec -= 1000000000L;
    }
}

// cond wait with Win32 millisecond semantics; returns false on timeout. The caller holds o->lock.
static bool KWait(KObj *o, DWORD ms)
{
    if (ms == INFINITE)
    {
        pthread_cond_wait(&o->cond, &o->lock);
        return true;
    }
    struct timespec ts;
    AbsTimeout(&ts, ms);
    return pthread_cond_timedwait(&o->cond, &o->lock, &ts) != ETIMEDOUT;
}

EXTERN_C BOOL WINAPI CloseHandle(HANDLE h)
{
    FileOp fileOp_("CloseHandle");
    if (!h || h == INVALID_HANDLE_VALUE || h == (HANDLE)(LONG_PTR)-2)
        return TRUE;
    KObj *o = (KObj *)h;
    if (o->kind == K_FILE && o->fd >= 0)
    {
        close(o->fd);
        o->fd = -1;
    }
    if (o->kind == K_FIND && o->dir)
    {
        closedir(o->dir);
        o->dir = nullptr;
    }
    KRelease(o);
    return TRUE;
}

EXTERN_C BOOL WINAPI DuplicateHandle(HANDLE, HANDLE src, HANDLE, LPHANDLE dst, DWORD, BOOL, DWORD)
{
    if (src && src != INVALID_HANDLE_VALUE && src != (HANDLE)(LONG_PTR)-2)
        ((KObj *)src)->refs.fetch_add(1);
    if (dst)
        *dst = src;
    return TRUE;
}

// ---------------------------------------------------------------------------------------------
// Threads
// ---------------------------------------------------------------------------------------------

static void *ThreadEntry(void *p)
{
    KObj *o = (KObj *)p;
    // The id was handed out in CreateThread (the engine compares it with threadId[] to identify its
    // main / server / database threads).
    t_threadId = o->tid;

    // CREATE_SUSPENDED: park until ResumeThread brings the suspend count to zero.
    pthread_mutex_lock(&o->lock);
    while (o->suspendCount > 0)
        pthread_cond_wait(&o->cond, &o->lock);
    pthread_mutex_unlock(&o->lock);

    DWORD code = o->fn(o->arg);

    pthread_mutex_lock(&o->lock);
    o->exitCode = code;
    o->finished = true;
    pthread_cond_broadcast(&o->cond);
    pthread_mutex_unlock(&o->lock);
    KRelease(o); // the thread's own reference
    return nullptr;
}

EXTERN_C HANDLE WINAPI CreateThread(LPSECURITY_ATTRIBUTES, SIZE_T stackSize, LPTHREAD_START_ROUTINE fn, LPVOID arg,
                                    DWORD flags, LPDWORD outId)
{
    KObj *o = new KObj(K_THREAD);
    o->refs = 2; // handle + running thread
    o->fn = fn;
    o->arg = arg;
    o->suspendCount = (flags & CREATE_SUSPENDED) ? 1 : 0;
    o->tid = s_nextThreadId.fetch_add(1);

    // The decompiled worker threads recurse deeper than a 64 KB Win32 default stack allows for.
    if (stackSize < (1u << 20))
        stackSize = 1u << 20;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, stackSize);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    // Do not transfer #game onto the render thread. Chrome leaves that canvas black while the
    // thread never returns to the event loop. GL draws a private canvas and posts ImageBitmaps.
    int rc = pthread_create(&o->thread, &attr, ThreadEntry, o);
    pthread_attr_destroy(&attr);
    if (rc != 0)
    {
        fprintf(stderr, "CreateThread: pthread_create failed (%d)\n", rc);
        delete o;
        t_lastError = ERROR_ACCESS_DENIED;
        return nullptr;
    }
    if (outId)
        *outId = o->tid;
    return (HANDLE)o;
}

EXTERN_C HANDLE WINAPI GetCurrentThread(void) { return (HANDLE)(LONG_PTR)-2; }
EXTERN_C HANDLE WINAPI GetCurrentProcess(void) { return (HANDLE)(LONG_PTR)-1; }
EXTERN_C DWORD WINAPI GetCurrentProcessId(void) { return 1; }

EXTERN_C DWORD WINAPI ResumeThread(HANDLE h)
{
    KObj *o = KFrom(h, K_THREAD);
    if (!o)
        return 0;
    pthread_mutex_lock(&o->lock);
    LONG prev = o->suspendCount;
    if (prev > 0)
        --o->suspendCount;
    if (o->suspendCount == 0)
        pthread_cond_broadcast(&o->cond);
    pthread_mutex_unlock(&o->lock);
    return (DWORD)prev;
}

// A running pthread cannot be stopped from outside; SuspendThread is only used on the fatal
// error path to freeze the other engine threads, where letting them run is harmless.
EXTERN_C DWORD WINAPI SuspendThread(HANDLE) { return 0; }
EXTERN_C DWORD_PTR WINAPI SetThreadAffinityMask(HANDLE, DWORD_PTR mask) { return mask ? mask : 1; }
EXTERN_C DWORD WINAPI SetThreadIdealProcessor(HANDLE, DWORD) { return 0; }
EXTERN_C BOOL WINAPI SetThreadPriority(HANDLE, int) { return TRUE; }
EXTERN_C int WINAPI GetThreadPriority(HANDLE) { return 0; }
EXTERN_C DWORD WINAPI SetThreadExecutionState(DWORD) { return ES_CONTINUOUS; }

EXTERN_C BOOL WINAPI GetProcessAffinityMask(HANDLE, DWORD_PTR *process, DWORD_PTR *system)
{
    int cores = emscripten_num_logical_cores();
    // The engine's legacy affinity tables support up to eight CPU slots. The
    // browser schedules the actual pthreads; do not hide available cores.
    if (cores > 8) cores = 8;
    DWORD_PTR mask = (cores >= 32) ? 0xFFFFFFFFul : ((1ul << (cores > 0 ? cores : 1)) - 1);
    if (process) *process = mask;
    if (system) *system = mask;
    return TRUE;
}

EXTERN_C BOOL WINAPI GetExitCodeThread(HANDLE h, LPDWORD code)
{
    KObj *o = KFrom(h, K_THREAD);
    if (!o)
        return FALSE;
    pthread_mutex_lock(&o->lock);
    if (code)
        *code = o->finished ? o->exitCode : STILL_ACTIVE;
    pthread_mutex_unlock(&o->lock);
    return TRUE;
}

// ---------------------------------------------------------------------------------------------
// Events, mutexes, semaphores
// ---------------------------------------------------------------------------------------------

EXTERN_C HANDLE WINAPI CreateEventA(LPSECURITY_ATTRIBUTES, BOOL manualReset, BOOL initialState, LPCSTR)
{
    KObj *o = new KObj(K_EVENT);
    o->manual = manualReset != 0;
    o->signaled = initialState != 0;
    return (HANDLE)o;
}

EXTERN_C BOOL WINAPI SetEvent(HANDLE h)
{
    KObj *o = KFrom(h, K_EVENT);
    if (!o)
        return FALSE;
    pthread_mutex_lock(&o->lock);
    o->signaled = true;
    pthread_cond_broadcast(&o->cond);
    pthread_mutex_unlock(&o->lock);
    return TRUE;
}

EXTERN_C BOOL WINAPI ResetEvent(HANDLE h)
{
    KObj *o = KFrom(h, K_EVENT);
    if (!o)
        return FALSE;
    pthread_mutex_lock(&o->lock);
    o->signaled = false;
    pthread_mutex_unlock(&o->lock);
    return TRUE;
}

EXTERN_C BOOL WINAPI PulseEvent(HANDLE h)
{
    KObj *o = KFrom(h, K_EVENT);
    if (!o)
        return FALSE;
    pthread_mutex_lock(&o->lock);
    if (o->manual)
    {
        ++o->pulseGen; // releases everyone currently waiting, leaves the event unsignaled
    }
    else if (o->waiters > 0)
    {
        ++o->pulseTokens; // releases exactly one current waiter
    }
    pthread_cond_broadcast(&o->cond);
    pthread_mutex_unlock(&o->lock);
    return TRUE;
}

EXTERN_C HANDLE WINAPI CreateMutexA(LPSECURITY_ATTRIBUTES, BOOL initialOwner, LPCSTR)
{
    KObj *o = new KObj(K_MUTEX);
    if (initialOwner)
    {
        o->owner = GetCurrentThreadId();
        o->recursion = 1;
    }
    return (HANDLE)o;
}

EXTERN_C BOOL WINAPI ReleaseMutex(HANDLE h)
{
    KObj *o = KFrom(h, K_MUTEX);
    if (!o)
        return FALSE;
    pthread_mutex_lock(&o->lock);
    BOOL ok = FALSE;
    if (o->owner == GetCurrentThreadId() && o->recursion > 0)
    {
        if (--o->recursion == 0)
        {
            o->owner = 0;
            pthread_cond_broadcast(&o->cond);
        }
        ok = TRUE;
    }
    pthread_mutex_unlock(&o->lock);
    return ok;
}

EXTERN_C HANDLE WINAPI CreateSemaphoreA(LPSECURITY_ATTRIBUTES, LONG initial, LONG maximum, LPCSTR)
{
    KObj *o = new KObj(K_SEMAPHORE);
    o->count = initial;
    o->maxCount = maximum;
    return (HANDLE)o;
}

EXTERN_C BOOL WINAPI ReleaseSemaphore(HANDLE h, LONG releaseCount, LPLONG previousCount)
{
    KObj *o = KFrom(h, K_SEMAPHORE);
    if (!o)
        return FALSE;
    pthread_mutex_lock(&o->lock);
    if (previousCount)
        *previousCount = o->count;
    BOOL ok = FALSE;
    if (o->count + releaseCount <= o->maxCount)
    {
        o->count += releaseCount;
        pthread_cond_broadcast(&o->cond);
        ok = TRUE;
    }
    pthread_mutex_unlock(&o->lock);
    return ok;
}

// Tries to acquire/observe the object once. Caller holds o->lock.
static bool KTryAcquire(KObj *o, DWORD me)
{
    switch (o->kind)
    {
    case K_THREAD: return o->finished;
    case K_EVENT:
        if (o->signaled)
        {
            if (!o->manual)
                o->signaled = false;
            return true;
        }
        if (!o->manual && o->pulseTokens > 0)
        {
            --o->pulseTokens;
            return true;
        }
        return false;
    case K_MUTEX:
        if (o->recursion == 0 || o->owner == me)
        {
            o->owner = me;
            ++o->recursion;
            return true;
        }
        return false;
    case K_SEMAPHORE:
        if (o->count > 0)
        {
            --o->count;
            return true;
        }
        return false;
    default: return true;
    }
}

EXTERN_C DWORD WINAPI WaitForSingleObject(HANDLE h, DWORD ms)
{
    if (!h || h == INVALID_HANDLE_VALUE || h == (HANDLE)(LONG_PTR)-2)
        return WAIT_FAILED;
    KObj *o = (KObj *)h;
    const DWORD me = GetCurrentThreadId();

    pthread_mutex_lock(&o->lock);
    DWORD result;
    if (KTryAcquire(o, me))
    {
        result = WAIT_OBJECT_0;
    }
    else if (ms == 0)
    {
        result = WAIT_TIMEOUT;
    }
    else
    {
        const unsigned gen = o->pulseGen;
        ++o->waiters;
        result = WAIT_TIMEOUT;
        // Absolute deadline, so spurious wakeups do not extend the wait.
        struct timespec deadline;
        if (ms != INFINITE)
            AbsTimeout(&deadline, ms);
        for (;;)
        {
            bool woke = true;
            if (ms == INFINITE)
                pthread_cond_wait(&o->cond, &o->lock);
            else
                woke = pthread_cond_timedwait(&o->cond, &o->lock, &deadline) != ETIMEDOUT;
            if (o->kind == K_EVENT && o->manual && o->pulseGen != gen)
            {
                result = WAIT_OBJECT_0; // PulseEvent released this waiter
                break;
            }
            if (KTryAcquire(o, me))
            {
                result = WAIT_OBJECT_0;
                break;
            }
            if (!woke)
                break;
        }
        --o->waiters;
    }
    pthread_mutex_unlock(&o->lock);
    return result;
}

EXTERN_C DWORD WINAPI WaitForMultipleObjects(DWORD n, const HANDLE *handles, BOOL waitAll, DWORD ms)
{
    // Not on a hot path anywhere in the engine: poll.
    const double start = emscripten_get_now();
    for (;;)
    {
        if (waitAll)
        {
            bool all = true;
            for (DWORD i = 0; i < n; ++i)
                if (WaitForSingleObject(handles[i], 0) != WAIT_OBJECT_0)
                    all = false;
            if (all)
                return WAIT_OBJECT_0;
        }
        else
        {
            for (DWORD i = 0; i < n; ++i)
                if (WaitForSingleObject(handles[i], 0) == WAIT_OBJECT_0)
                    return WAIT_OBJECT_0 + i;
        }
        if (ms != INFINITE && emscripten_get_now() - start >= (double)ms)
            return WAIT_TIMEOUT;
        NoteSpin("WaitForMultipleObjects");
        usleep(500);
    }
}

// ---------------------------------------------------------------------------------------------
// Sleep / yield. Overlapped "APCs" are queued by ReadFileEx and delivered by SleepEx.
// ---------------------------------------------------------------------------------------------

struct PendingApc
{
    LPOVERLAPPED_COMPLETION_ROUTINE routine;
    DWORD error;
    DWORD bytes;
    LPOVERLAPPED overlapped;
};
static thread_local std::vector<PendingApc> *t_apcs = nullptr;

static bool RunApcs()
{
    if (!t_apcs || t_apcs->empty())
        return false;
    std::vector<PendingApc> run;
    run.swap(*t_apcs);
    for (const PendingApc &a : run)
        a.routine(a.error, a.bytes, a.overlapped);
    return true;
}

#ifdef KISAK_PROF_PHASES
// Stall diagnostics: a thread that keeps polling through Sleep / emulated waits for 15 s prints its own C stack
// once (needs function names, i.e. the web-check preset). This is how browser-only deadlocks are located,
// because Chrome cannot pause workers that sit in Atomics.wait.
static void NoteSpin(const char *what)
{
    static thread_local double lastCall = 0, spinStart = 0;
    static thread_local bool dumped = false;
    const double now = emscripten_get_now();
    if (now - lastCall > 250.0)
    {
        spinStart = now;
        dumped = false;
    }
    lastCall = now;
    if (!dumped && now - spinStart > 15000.0)
    {
        dumped = true;
        char stack[6000];
        emscripten_get_callstack(EM_LOG_C_STACK | EM_LOG_DEMANGLE | EM_LOG_NO_PATHS, stack, sizeof(stack));
        fprintf(stderr, "web: SPIN thread=%p in %s for 15s:\n%s\n", (void *)pthread_self(), what, stack);
    }
}
#endif

EXTERN_C void WINAPI Sleep(DWORD ms)
{
    NoteSpin("Sleep");
    if (ms == 0)
    {
        sched_yield();
        return;
    }
    if (ms == INFINITE)
    {
        for (;;)
            usleep(1000000);
    }
    usleep((useconds_t)ms * 1000u);
}

EXTERN_C DWORD WINAPI SleepEx(DWORD ms, BOOL alertable)
{
    if (alertable && RunApcs())
        return WAIT_IO_COMPLETION;
    if (alertable && ms == INFINITE)
        return 0; // nothing queued: an infinite alertable sleep with no pending I/O would deadlock
    Sleep(ms);
    return 0;
}

EXTERN_C BOOL WINAPI SwitchToThread(void)
{
    sched_yield();
    return TRUE;
}

// ---------------------------------------------------------------------------------------------
// Critical sections: recursive lock over a futex word (Drepper "mutex2": 0 free, 1 locked, 2 contended)
// ---------------------------------------------------------------------------------------------

static inline LONG CasLong(volatile LONG *p, LONG expected, LONG desired)
{
    __atomic_compare_exchange_n(p, &expected, desired, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
    return expected;
}

EXTERN_C void WINAPI InitializeCriticalSection(LPCRITICAL_SECTION cs)
{
    memset(cs, 0, sizeof(*cs));
}

EXTERN_C BOOL WINAPI InitializeCriticalSectionAndSpinCount(LPCRITICAL_SECTION cs, DWORD)
{
    memset(cs, 0, sizeof(*cs));
    return TRUE;
}

EXTERN_C void WINAPI DeleteCriticalSection(LPCRITICAL_SECTION cs)
{
    memset(cs, 0, sizeof(*cs));
}

EXTERN_C void WINAPI EnterCriticalSection(LPCRITICAL_SECTION cs)
{
    const HANDLE me = (HANDLE)(uintptr_t)GetCurrentThreadId();
    if (__atomic_load_n(&cs->OwningThread, __ATOMIC_RELAXED) == me)
    {
        ++cs->RecursionCount;
        return;
    }

    volatile LONG *state = &cs->LockCount;
    LONG c = CasLong(state, 0, 1);
    if (c != 0)
    {
        // Short spin first: most holds in the engine are a handful of instructions.
        for (int i = 0; i < 32 && c != 0; ++i)
        {
            if (__atomic_load_n(state, __ATOMIC_RELAXED) == 0)
                c = CasLong(state, 0, 1);
        }
        if (c != 0)
        {
            do
            {
                if (c == 2 || CasLong(state, 1, 2) != 0)
                    emscripten_futex_wait((void *)state, 2, INFINITY);
            } while ((c = CasLong(state, 0, 2)) != 0);
        }
    }
    __atomic_store_n(&cs->OwningThread, me, __ATOMIC_RELAXED);
    cs->RecursionCount = 1;
}

EXTERN_C BOOL WINAPI TryEnterCriticalSection(LPCRITICAL_SECTION cs)
{
    const HANDLE me = (HANDLE)(uintptr_t)GetCurrentThreadId();
    if (__atomic_load_n(&cs->OwningThread, __ATOMIC_RELAXED) == me)
    {
        ++cs->RecursionCount;
        return TRUE;
    }
    if (CasLong(&cs->LockCount, 0, 1) != 0)
        return FALSE;
    __atomic_store_n(&cs->OwningThread, me, __ATOMIC_RELAXED);
    cs->RecursionCount = 1;
    return TRUE;
}

EXTERN_C void WINAPI LeaveCriticalSection(LPCRITICAL_SECTION cs)
{
    if (--cs->RecursionCount > 0)
        return;
    __atomic_store_n(&cs->OwningThread, (HANDLE)nullptr, __ATOMIC_RELAXED);
    if (__atomic_fetch_sub(&cs->LockCount, 1, __ATOMIC_RELEASE) != 1)
    {
        __atomic_store_n(&cs->LockCount, 0, __ATOMIC_RELEASE);
        emscripten_futex_wake((void *)&cs->LockCount, 1);
    }
}

// ---------------------------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------------------------

static inline unsigned long long MonotonicNs()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000000000ull + (unsigned long long)ts.tv_nsec;
}

EXTERN_C BOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER *li)
{
    li->QuadPart = (LONGLONG)MonotonicNs();
    return TRUE;
}

EXTERN_C BOOL WINAPI QueryPerformanceFrequency(LARGE_INTEGER *li)
{
    li->QuadPart = 1000000000ll;
    return TRUE;
}

EXTERN_C DWORD WINAPI GetTickCount(void) { return (DWORD)(MonotonicNs() / 1000000ull); }
EXTERN_C DWORD WINAPI timeGetTime(void) { return (DWORD)(MonotonicNs() / 1000000ull); }

static void FillSystemTime(const struct tm *t, long msec, LPSYSTEMTIME st)
{
    st->wYear = (WORD)(t->tm_year + 1900);
    st->wMonth = (WORD)(t->tm_mon + 1);
    st->wDayOfWeek = (WORD)t->tm_wday;
    st->wDay = (WORD)t->tm_mday;
    st->wHour = (WORD)t->tm_hour;
    st->wMinute = (WORD)t->tm_min;
    st->wSecond = (WORD)t->tm_sec;
    st->wMilliseconds = (WORD)msec;
}

EXTERN_C void WINAPI GetSystemTime(LPSYSTEMTIME st)
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    time_t s = tv.tv_sec;
    struct tm t;
    gmtime_r(&s, &t);
    FillSystemTime(&t, tv.tv_usec / 1000, st);
}

EXTERN_C void WINAPI GetLocalTime(LPSYSTEMTIME st)
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    time_t s = tv.tv_sec;
    struct tm t;
    localtime_r(&s, &t);
    FillSystemTime(&t, tv.tv_usec / 1000, st);
}

static const unsigned long long kEpochDiff100ns = 116444736000000000ull; // 1601 -> 1970

EXTERN_C void WINAPI GetSystemTimeAsFileTime(LPFILETIME ft)
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    unsigned long long v = (unsigned long long)tv.tv_sec * 10000000ull + (unsigned long long)tv.tv_usec * 10ull + kEpochDiff100ns;
    ft->dwLowDateTime = (DWORD)v;
    ft->dwHighDateTime = (DWORD)(v >> 32);
}

EXTERN_C BOOL WINAPI SystemTimeToFileTime(const SYSTEMTIME *st, LPFILETIME ft)
{
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = st->wYear - 1900;
    t.tm_mon = st->wMonth - 1;
    t.tm_mday = st->wDay;
    t.tm_hour = st->wHour;
    t.tm_min = st->wMinute;
    t.tm_sec = st->wSecond;
    time_t s = timegm(&t);
    unsigned long long v = (unsigned long long)s * 10000000ull + (unsigned long long)st->wMilliseconds * 10000ull + kEpochDiff100ns;
    ft->dwLowDateTime = (DWORD)v;
    ft->dwHighDateTime = (DWORD)(v >> 32);
    return TRUE;
}

EXTERN_C LONG WINAPI CompareFileTime(const FILETIME *a, const FILETIME *b)
{
    unsigned long long x = ((unsigned long long)a->dwHighDateTime << 32) | a->dwLowDateTime;
    unsigned long long y = ((unsigned long long)b->dwHighDateTime << 32) | b->dwLowDateTime;
    return x < y ? -1 : (x > y ? 1 : 0);
}

static void UnixToFileTime(time_t t, FILETIME *ft)
{
    unsigned long long v = (unsigned long long)t * 10000000ull + kEpochDiff100ns;
    ft->dwLowDateTime = (DWORD)v;
    ft->dwHighDateTime = (DWORD)(v >> 32);
}

// ---------------------------------------------------------------------------------------------
// Memory. wasm linear memory is fixed-size and lazily backed by the host, so "reserve" and
// "commit" are the same thing here: one zeroed, 64 KB-aligned block per VirtualAlloc.
// ---------------------------------------------------------------------------------------------

namespace
{
    struct VRegion
    {
        char *base;
        size_t size;
        void *orig;
    };
    std::mutex s_vmMutex;
    std::map<uintptr_t, VRegion> s_vm;
}

EXTERN_C LPVOID WINAPI VirtualAlloc(LPVOID addr, SIZE_T size, DWORD type, DWORD)
{
    if (addr)
    {
        // Commit inside an existing reservation: already backed.
        std::lock_guard<std::mutex> lock(s_vmMutex);
        auto it = s_vm.upper_bound((uintptr_t)addr);
        if (it != s_vm.begin())
        {
            --it;
            if ((uintptr_t)addr >= it->first && (uintptr_t)addr + size <= it->first + it->second.size)
                return addr;
        }
        t_lastError = ERROR_INVALID_HANDLE;
        return nullptr;
    }
    if (!size)
        return nullptr;

    const size_t kAlign = 65536;
    void *orig = calloc(1, size + kAlign);
    if (!orig)
    {
        t_lastError = ERROR_ACCESS_DENIED;
        return nullptr;
    }
    char *base = (char *)(((uintptr_t)orig + kAlign - 1) & ~(uintptr_t)(kAlign - 1));
    std::lock_guard<std::mutex> lock(s_vmMutex);
    s_vm[(uintptr_t)base] = VRegion{base, size, orig};
    return base;
}

EXTERN_C BOOL WINAPI VirtualFree(LPVOID addr, SIZE_T, DWORD type)
{
    if (type & MEM_DECOMMIT)
        return TRUE; // pages stay backed; callers re-commit before touching them
    std::lock_guard<std::mutex> lock(s_vmMutex);
    auto it = s_vm.find((uintptr_t)addr);
    if (it == s_vm.end())
        return FALSE;
    free(it->second.orig);
    s_vm.erase(it);
    return TRUE;
}

EXTERN_C BOOL WINAPI VirtualProtect(LPVOID, SIZE_T, DWORD, PDWORD old)
{
    if (old)
        *old = PAGE_READWRITE;
    return TRUE;
}

EXTERN_C SIZE_T WINAPI VirtualQuery(LPCVOID addr, PMEMORY_BASIC_INFORMATION mbi, SIZE_T)
{
    memset(mbi, 0, sizeof(*mbi));
    std::lock_guard<std::mutex> lock(s_vmMutex);
    auto it = s_vm.upper_bound((uintptr_t)addr);
    if (it != s_vm.begin())
    {
        --it;
        const uintptr_t start = it->first;
        // Windows rounds every reservation up to whole pages; Z_VirtualFree walks regions by RegionSize and
        // asserts that each query lands exactly on a region start.
        const uintptr_t end = start + ((it->second.size + 4095) & ~(uintptr_t)4095);
        if ((uintptr_t)addr >= start && (uintptr_t)addr < end)
        {
            mbi->BaseAddress = (PVOID)((uintptr_t)addr & ~(uintptr_t)4095);
            mbi->AllocationBase = (PVOID)start;
            mbi->AllocationProtect = PAGE_READWRITE;
            mbi->RegionSize = end - (uintptr_t)mbi->BaseAddress;
            mbi->State = MEM_COMMIT;
            mbi->Protect = PAGE_READWRITE;
            mbi->Type = MEM_PRIVATE;
            return sizeof(*mbi);
        }
    }
    mbi->BaseAddress = (PVOID)((uintptr_t)addr & ~(uintptr_t)4095);
    mbi->RegionSize = 4096;
    mbi->State = MEM_COMMIT; // the heap and static data are all committed linear memory
    mbi->Protect = PAGE_READWRITE;
    mbi->Type = MEM_PRIVATE;
    return sizeof(*mbi);
}

EXTERN_C HANDLE WINAPI GetProcessHeap(void) { return (HANDLE)(LONG_PTR)1; }

EXTERN_C LPVOID WINAPI HeapAlloc(HANDLE, DWORD flags, SIZE_T size)
{
    return (flags & HEAP_ZERO_MEMORY) ? calloc(1, size) : malloc(size);
}

EXTERN_C BOOL WINAPI HeapFree(HANDLE, DWORD, LPVOID p)
{
    free(p);
    return TRUE;
}

EXTERN_C SIZE_T WINAPI HeapSize(HANDLE, DWORD, LPCVOID p)
{
    return p ? malloc_usable_size((void *)p) : 0;
}

EXTERN_C HGLOBAL WINAPI GlobalAlloc(UINT flags, SIZE_T size)
{
    return (flags & GMEM_ZEROINIT) ? calloc(1, size) : malloc(size);
}
EXTERN_C HGLOBAL WINAPI GlobalFree(HGLOBAL h) { free(h); return nullptr; }
EXTERN_C LPVOID WINAPI GlobalLock(HGLOBAL h) { return h; }
EXTERN_C BOOL WINAPI GlobalUnlock(HGLOBAL) { return TRUE; }

EXTERN_C void WINAPI GlobalMemoryStatus(LPMEMORYSTATUS ms)
{
    memset(ms, 0, sizeof(*ms));
    ms->dwLength = sizeof(*ms);
    ms->dwTotalPhys = ms->dwAvailPhys = 1024u * 1024u * 1024u;
    ms->dwTotalVirtual = ms->dwAvailVirtual = 1536u * 1024u * 1024u;
    ms->dwTotalPageFile = ms->dwAvailPageFile = 1536u * 1024u * 1024u;
}

EXTERN_C BOOL WINAPI GlobalMemoryStatusEx(LPMEMORYSTATUSEX ms)
{
    memset(ms, 0, sizeof(*ms));
    ms->dwLength = sizeof(*ms);
    ms->ullTotalPhys = ms->ullAvailPhys = 1024ull * 1024ull * 1024ull;
    ms->ullTotalVirtual = ms->ullAvailVirtual = 1536ull * 1024ull * 1024ull;
    ms->ullTotalPageFile = ms->ullAvailPageFile = 1536ull * 1024ull * 1024ull;
    return TRUE;
}

EXTERN_C void WINAPI GetSystemInfo(LPSYSTEM_INFO si)
{
    memset(si, 0, sizeof(*si));
    int cores = emscripten_num_logical_cores();
    if (cores > 8) cores = 8;
    si->dwPageSize = 4096;
    si->dwAllocationGranularity = 65536;
    si->dwNumberOfProcessors = cores > 0 ? cores : 1;
    si->lpMinimumApplicationAddress = (LPVOID)0x10000;
    si->lpMaximumApplicationAddress = (LPVOID)0x7FFEFFFF;
    si->dwActiveProcessorMask = (1ul << (si->dwNumberOfProcessors < 32 ? si->dwNumberOfProcessors : 31)) - 1;
}

// ---------------------------------------------------------------------------------------------
// Files (POSIX over WasmFS). Paths use '\' in places; they are normalized, and a case-insensitive
// retry covers the Windows habit of ignoring case.
// ---------------------------------------------------------------------------------------------

static std::string NormalizePath(const char *path)
{
    std::string p(path ? path : "");
    for (char &c : p)
        if (c == '\\')
            c = '/';
    return p;
}

// Resolve each path component case-insensitively against the directory contents. Returns false if
// any component has no match.
static bool ResolveCaseInsensitive(const std::string &in, std::string &out)
{
    out.clear();
    size_t i = 0;
    if (!in.empty() && in[0] == '/')
    {
        out = "/";
        i = 1;
    }
    while (i < in.size())
    {
        size_t j = in.find('/', i);
        if (j == std::string::npos)
            j = in.size();
        std::string comp = in.substr(i, j - i);
        i = j + 1;
        if (comp.empty() || comp == ".")
            continue;

        std::string dirPath = out.empty() ? "." : out;
        std::string probe = (out.empty() || out == "/") ? out + comp : out + "/" + comp;
        struct stat st;
        if (stat(probe.c_str(), &st) == 0)
        {
            out = probe;
            continue;
        }
        DIR *d = opendir(dirPath.c_str());
        if (!d)
            return false;
        bool found = false;
        while (struct dirent *e = readdir(d))
        {
            if (strcasecmp(e->d_name, comp.c_str()) == 0)
            {
                probe = (out.empty() || out == "/") ? out + e->d_name : out + "/" + e->d_name;
                found = true;
                break;
            }
        }
        closedir(d);
        if (!found)
            return false;
        out = probe;
    }
    return true;
}

static std::string ResolvePath(const char *path, bool mustExist)
{
    std::string p = NormalizePath(path);
    struct stat st;
    if (stat(p.c_str(), &st) == 0 || !mustExist)
        return p;
    std::string r;
    if (ResolveCaseInsensitive(p, r))
        return r;
    return p;
}

const char *Web_FixPath(const char *in, char *out, size_t cap)
{
    const std::string p = ResolvePath(in, true);
    snprintf(out, cap, "%s", p.c_str());
    return out;
}

EXTERN_C HANDLE WINAPI CreateFileA(LPCSTR path, DWORD access, DWORD, LPSECURITY_ATTRIBUTES, DWORD disposition, DWORD,
                                   HANDLE)
{
    FileOp fileOp_("CreateFileA");
    int flags = 0;
    if ((access & GENERIC_WRITE) && (access & GENERIC_READ))
        flags = O_RDWR;
    else if (access & GENERIC_WRITE)
        flags = O_WRONLY;
    else
        flags = O_RDONLY;

    const bool creating = disposition == CREATE_NEW || disposition == CREATE_ALWAYS || disposition == OPEN_ALWAYS;
    switch (disposition)
    {
    case CREATE_NEW: flags |= O_CREAT | O_EXCL; break;
    case CREATE_ALWAYS: flags |= O_CREAT | O_TRUNC; break;
    case OPEN_ALWAYS: flags |= O_CREAT; break;
    case TRUNCATE_EXISTING: flags |= O_TRUNC; break;
    default: break;
    }

    std::string p = ResolvePath(path, !creating);
    int fd = open(p.c_str(), flags, 0666);
    if (fd < 0)
    {
        if (fs_debug && fs_debug->current.integer >= 2)
            fprintf(stderr, "web: open(%s) failed errno=%d (asked '%s')\n", p.c_str(), errno, path);
        t_lastError = ErrnoToWin32(errno);
        return INVALID_HANDLE_VALUE;
    }
    KObj *o = new KObj(K_FILE);
    o->fd = fd;
    t_lastError = (disposition == CREATE_ALWAYS || disposition == OPEN_ALWAYS) ? ERROR_SUCCESS : ERROR_SUCCESS;
    return (HANDLE)o;
}

EXTERN_C BOOL WINAPI ReadFile(HANDLE h, LPVOID buf, DWORD n, LPDWORD bytesRead, LPOVERLAPPED ov)
{
    FileOp fileOp_("ReadFile");
    KObj *o = KFrom(h, K_FILE);
    if (!o)
    {
        t_lastError = ERROR_INVALID_HANDLE;
        return FALSE;
    }
    ssize_t got;
    if (ov)
    {
        const off_t off = ((off_t)ov->OffsetHigh << 32) | ov->Offset;
        got = pread(o->fd, buf, n, off);
    }
    else
    {
        got = read(o->fd, buf, n);
    }
    if (got < 0)
    {
        t_lastError = ErrnoToWin32(errno);
        if (bytesRead)
            *bytesRead = 0;
        return FALSE;
    }
    if (bytesRead)
        *bytesRead = (DWORD)got;
    return TRUE;
}

// Reads synchronously; the completion routine runs when the thread next waits alertably, which is
// how the fastfile loader (SleepEx(INFINITE, TRUE)) observes completion.
EXTERN_C BOOL WINAPI ReadFileEx(HANDLE h, LPVOID buf, DWORD n, LPOVERLAPPED ov, LPOVERLAPPED_COMPLETION_ROUTINE routine)
{
    KObj *o = KFrom(h, K_FILE);
    if (!o)
    {
        t_lastError = ERROR_INVALID_HANDLE;
        return FALSE;
    }
    const off_t off = ((off_t)ov->OffsetHigh << 32) | ov->Offset;
    ssize_t got = pread(o->fd, buf, n, off);
    if (got < 0)
    {
        t_lastError = ErrnoToWin32(errno);
        return FALSE;
    }
    if (got == 0 && n != 0)
    {
        t_lastError = ERROR_HANDLE_EOF;
        return FALSE;
    }
    ov->Internal = 0;
    ov->InternalHigh = (ULONG_PTR)got;
    if (!t_apcs)
        t_apcs = new std::vector<PendingApc>();
    t_apcs->push_back(PendingApc{routine, 0, (DWORD)got, ov});
    return TRUE;
}

EXTERN_C BOOL WINAPI GetOverlappedResult(HANDLE, LPOVERLAPPED ov, LPDWORD transferred, BOOL)
{
    if (transferred)
        *transferred = (DWORD)ov->InternalHigh;
    return TRUE;
}

EXTERN_C BOOL WINAPI WriteFile(HANDLE h, LPCVOID buf, DWORD n, LPDWORD written, LPOVERLAPPED ov)
{
    FileOp fileOp_("WriteFile");
    KObj *o = KFrom(h, K_FILE);
    if (!o)
    {
        t_lastError = ERROR_INVALID_HANDLE;
        return FALSE;
    }
    ssize_t put;
    if (ov)
    {
        const off_t off = ((off_t)ov->OffsetHigh << 32) | ov->Offset;
        put = pwrite(o->fd, buf, n, off);
    }
    else
    {
        put = write(o->fd, buf, n);
    }
    if (put < 0)
    {
        t_lastError = ErrnoToWin32(errno);
        if (written)
            *written = 0;
        return FALSE;
    }
    if (written)
        *written = (DWORD)put;
    return TRUE;
}

EXTERN_C DWORD WINAPI GetFileSize(HANDLE h, LPDWORD high)
{
    FileOp fileOp_("GetFileSize");
    KObj *o = KFrom(h, K_FILE);
    struct stat st;
    if (!o || fstat(o->fd, &st) != 0)
    {
        t_lastError = ERROR_INVALID_HANDLE;
        return INVALID_FILE_SIZE;
    }
    if (high)
        *high = (DWORD)((unsigned long long)st.st_size >> 32);
    return (DWORD)st.st_size;
}

EXTERN_C DWORD WINAPI SetFilePointer(HANDLE h, LONG lo, PLONG hi, DWORD method)
{
    FileOp fileOp_("SetFilePointer");
    KObj *o = KFrom(h, K_FILE);
    if (!o)
        return INVALID_SET_FILE_POINTER;
    off_t off = (off_t)(DWORD)lo;
    if (hi)
        off |= (off_t)(*hi) << 32;
    else
        off = (off_t)lo;
    int whence = method == FILE_BEGIN ? SEEK_SET : (method == FILE_CURRENT ? SEEK_CUR : SEEK_END);
    off_t r = lseek(o->fd, off, whence);
    if (r < 0)
        return INVALID_SET_FILE_POINTER;
    if (hi)
        *hi = (LONG)((unsigned long long)r >> 32);
    return (DWORD)r;
}

EXTERN_C BOOL WINAPI FlushFileBuffers(HANDLE) { return TRUE; }

EXTERN_C BOOL WINAPI DeleteFileA(LPCSTR path)
{
    FileOp fileOp_("DeleteFileA");
    std::string p = ResolvePath(path, true);
    if (unlink(p.c_str()) != 0)
    {
        t_lastError = ErrnoToWin32(errno);
        return FALSE;
    }
    return TRUE;
}

EXTERN_C DWORD WINAPI GetFileAttributesA(LPCSTR path)
{
    FileOp fileOp_("GetFileAttributesA");
    std::string p = ResolvePath(path, true);
    struct stat st;
    if (stat(p.c_str(), &st) != 0)
    {
        // A file the page can still fetch counts as present (existence checks before a load).
        if (Web_RemoteKnown(p.c_str(), nullptr))
            return FILE_ATTRIBUTE_ARCHIVE;
        t_lastError = ErrnoToWin32(errno);
        return INVALID_FILE_ATTRIBUTES;
    }
    DWORD a = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE;
    if (!(st.st_mode & S_IWUSR))
        a |= FILE_ATTRIBUTE_READONLY;
    return a;
}

EXTERN_C BOOL WINAPI SetFileAttributesA(LPCSTR, DWORD) { return TRUE; }

EXTERN_C BOOL WINAPI CreateDirectoryA(LPCSTR path, LPSECURITY_ATTRIBUTES)
{
    FileOp fileOp_("CreateDirectoryA");
    std::string p = NormalizePath(path);
    if (mkdir(p.c_str(), 0777) != 0)
    {
        t_lastError = ErrnoToWin32(errno);
        return FALSE;
    }
    return TRUE;
}

EXTERN_C BOOL WINAPI RemoveDirectoryA(LPCSTR path)
{
    std::string p = ResolvePath(path, true);
    if (rmdir(p.c_str()) != 0)
    {
        t_lastError = ErrnoToWin32(errno);
        return FALSE;
    }
    return TRUE;
}

EXTERN_C int WINAPI MoveFileExA(const char *from, const char *to, unsigned long)
{
    FileOp fileOp_("MoveFileExA");
    std::string a = ResolvePath(from, true);
    std::string b = NormalizePath(to);
    if (rename(a.c_str(), b.c_str()) == 0)
        return TRUE;

    // Some WasmFS backends (OPFS) cannot rename; copy over the destination and remove the source.
    const int renameErrno = errno;
    FILE *in = fopen(a.c_str(), "rb");
    if (in)
    {
        FILE *out = fopen(b.c_str(), "wb");
        if (out)
        {
            char buf[16384];
            size_t n;
            bool ok = true;
            while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
                if (fwrite(buf, 1, n, out) != n)
                {
                    ok = false;
                    break;
                }
            ok = (fclose(out) == 0) && ok;
            fclose(in);
            if (ok)
            {
                unlink(a.c_str());
                return TRUE;
            }
        }
        else
            fclose(in);
    }
    t_lastError = ErrnoToWin32(renameErrno);
    return FALSE;
}

EXTERN_C DWORD WINAPI GetCurrentDirectoryA(DWORD n, LPSTR buf)
{
    char tmp[4096];
    if (!getcwd(tmp, sizeof(tmp)))
        return 0;
    size_t len = strlen(tmp);
    if (len + 1 > n)
        return (DWORD)(len + 1);
    memcpy(buf, tmp, len + 1);
    return (DWORD)len;
}

EXTERN_C DWORD WINAPI GetFullPathNameA(LPCSTR name, DWORD n, LPSTR buf, LPSTR *filePart)
{
    std::string p = NormalizePath(name);
    if (p.empty() || p[0] != '/')
    {
        char cwd[4096];
        if (!getcwd(cwd, sizeof(cwd)))
            cwd[0] = 0;
        p = std::string(cwd) + "/" + p;
    }
    if (p.size() + 1 > n)
        return (DWORD)(p.size() + 1);
    memcpy(buf, p.c_str(), p.size() + 1);
    if (filePart)
    {
        const char *slash = strrchr(buf, '/');
        *filePart = slash ? (LPSTR)slash + 1 : buf;
    }
    return (DWORD)p.size();
}

// ---- directory enumeration -------------------------------------------------------------------

static bool NextMatch(KObj *o, std::string &name, struct stat &st)
{
    while (struct dirent *e = readdir(o->dir))
    {
        if (fnmatch(o->pattern.c_str(), e->d_name, FNM_CASEFOLD) != 0)
            continue;
        name = e->d_name;
        const std::string full = o->dirPath.empty() ? name : o->dirPath + "/" + name;
        if (stat(full.c_str(), &st) != 0)
            memset(&st, 0, sizeof(st));
        return true;
    }
    return false;
}

static KObj *OpenFind(const char *spec)
{
    std::string p = NormalizePath(spec);
    std::string dir, pat;
    size_t slash = p.find_last_of('/');
    if (slash == std::string::npos)
    {
        dir = ".";
        pat = p;
    }
    else
    {
        dir = slash == 0 ? "/" : p.substr(0, slash);
        pat = p.substr(slash + 1);
    }
    if (pat == "*.*" || pat.empty())
        pat = "*";

    DIR *d = opendir(dir.c_str());
    if (!d)
    {
        std::string r;
        if (ResolveCaseInsensitive(dir, r))
            d = opendir(r.c_str());
        if (d)
            dir = r;
    }
    if (!d)
    {
        t_lastError = ERROR_PATH_NOT_FOUND;
        return nullptr;
    }
    KObj *o = new KObj(K_FIND);
    o->dir = d;
    o->dirPath = dir == "." ? "" : dir;
    o->pattern = pat;
    return o;
}

EXTERN_C HANDLE WINAPI FindFirstFileA(LPCSTR spec, LPWIN32_FIND_DATAA data)
{
    FileOp fileOp_("FindFirstFileA");
    KObj *o = OpenFind(spec);
    if (!o)
        return INVALID_HANDLE_VALUE;
    std::string name;
    struct stat st;
    if (!NextMatch(o, name, st))
    {
        CloseHandle((HANDLE)o);
        t_lastError = ERROR_FILE_NOT_FOUND;
        return INVALID_HANDLE_VALUE;
    }
    memset(data, 0, sizeof(*data));
    data->dwFileAttributes = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE;
    data->nFileSizeLow = (DWORD)st.st_size;
    data->nFileSizeHigh = (DWORD)((unsigned long long)st.st_size >> 32);
    UnixToFileTime(st.st_mtime, &data->ftLastWriteTime);
    UnixToFileTime(st.st_atime, &data->ftLastAccessTime);
    UnixToFileTime(st.st_ctime, &data->ftCreationTime);
    strncpy(data->cFileName, name.c_str(), MAX_PATH - 1);
    return (HANDLE)o;
}

EXTERN_C BOOL WINAPI FindNextFileA(HANDLE h, LPWIN32_FIND_DATAA data)
{
    KObj *o = KFrom(h, K_FIND);
    if (!o)
        return FALSE;
    std::string name;
    struct stat st;
    if (!NextMatch(o, name, st))
    {
        t_lastError = ERROR_NO_MORE_FILES;
        return FALSE;
    }
    memset(data, 0, sizeof(*data));
    data->dwFileAttributes = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE;
    data->nFileSizeLow = (DWORD)st.st_size;
    data->nFileSizeHigh = (DWORD)((unsigned long long)st.st_size >> 32);
    UnixToFileTime(st.st_mtime, &data->ftLastWriteTime);
    UnixToFileTime(st.st_atime, &data->ftLastAccessTime);
    UnixToFileTime(st.st_ctime, &data->ftCreationTime);
    strncpy(data->cFileName, name.c_str(), MAX_PATH - 1);
    return TRUE;
}

EXTERN_C BOOL WINAPI FindClose(HANDLE h)
{
    return CloseHandle(h);
}

EXTERN_C intptr_t _findfirst64i32(const char *spec, struct _finddata64i32_t *info)
{
    KObj *o = OpenFind(spec);
    if (!o)
        return -1;
    std::string name;
    struct stat st;
    if (!NextMatch(o, name, st))
    {
        CloseHandle((HANDLE)o);
        return -1;
    }
    memset(info, 0, sizeof(*info));
    info->attrib = S_ISDIR(st.st_mode) ? 0x10 : 0;
    info->time_write = st.st_mtime;
    info->time_access = st.st_atime;
    info->time_create = st.st_ctime;
    info->size = (unsigned long)st.st_size;
    strncpy(info->name, name.c_str(), sizeof(info->name) - 1);
    return (intptr_t)o;
}

EXTERN_C int _findnext64i32(intptr_t h, struct _finddata64i32_t *info)
{
    KObj *o = KFrom((HANDLE)h, K_FIND);
    if (!o)
        return -1;
    std::string name;
    struct stat st;
    if (!NextMatch(o, name, st))
        return -1;
    memset(info, 0, sizeof(*info));
    info->attrib = S_ISDIR(st.st_mode) ? 0x10 : 0;
    info->time_write = st.st_mtime;
    info->time_access = st.st_atime;
    info->time_create = st.st_ctime;
    info->size = (unsigned long)st.st_size;
    strncpy(info->name, name.c_str(), sizeof(info->name) - 1);
    return 0;
}

EXTERN_C int _findclose(intptr_t h)
{
    CloseHandle((HANDLE)h);
    return 0;
}

// ---------------------------------------------------------------------------------------------
// Process, module, diagnostics
// ---------------------------------------------------------------------------------------------

EXTERN_C void WINAPI OutputDebugStringA(LPCSTR) {}
EXTERN_C BOOL WINAPI IsDebuggerPresent(void) { return FALSE; }
EXTERN_C void WINAPI DebugBreak(void) { __builtin_trap(); }

EXTERN_C void WINAPI RaiseException(DWORD code, DWORD, DWORD, const ULONG_PTR *)
{
    fprintf(stderr, "RaiseException(0x%08lx)\n", (unsigned long)code);
    fflush(stderr);
    abort();
}

EXTERN_C void WINAPI ExitProcess(UINT code)
{
    fflush(stdout);
    fflush(stderr);
    exit((int)code);
}

EXTERN_C int WINAPI MessageBoxA(HWND, LPCSTR text, LPCSTR caption, UINT)
{
    fprintf(stderr, "[%s] %s\n", caption ? caption : "", text ? text : "");
    return IDOK;
}

EXTERN_C HMODULE WINAPI LoadLibraryA(LPCSTR) { t_lastError = ERROR_FILE_NOT_FOUND; return nullptr; }
EXTERN_C HMODULE WINAPI GetModuleHandleA(LPCSTR) { return nullptr; }
EXTERN_C BOOL WINAPI FreeLibrary(HMODULE) { return TRUE; }
EXTERN_C FARPROC WINAPI GetProcAddress(HMODULE, LPCSTR) { return nullptr; }

EXTERN_C DWORD WINAPI GetModuleFileNameA(HMODULE, LPSTR buf, DWORD n)
{
    // The engine derives its install path from the module path, so the "executable" sits in the game root.
    char path[300];
    snprintf(path, sizeof(path), "%s/kisak.exe", Web_GameRoot());
    const char *name = path;
    DWORD len = (DWORD)strlen(name);
    if (len + 1 > n)
        len = n ? n - 1 : 0;
    if (n)
    {
        memcpy(buf, name, len);
        buf[len] = 0;
    }
    return len;
}

EXTERN_C HANDLE WINAPI OpenProcess(DWORD, BOOL, DWORD) { return nullptr; }

// ShlObj.h declares this with C++ linkage. Every special folder is the persistent profile directory
// (mounted from OPFS by the page shell).
#include <ShlObj.h>
HRESULT SHGetFolderPathA(HWND, int, HANDLE, DWORD, LPSTR path)
{
    strcpy(path, "/profile");
    return S_OK;
}

EXTERN_C int WINAPI MultiByteToWideChar(UINT, DWORD, LPCSTR src, int srcLen, LPWSTR dst, int dstLen)
{
    if (srcLen < 0)
        srcLen = (int)strlen(src) + 1;
    if (!dst)
        return srcLen;
    int n = srcLen < dstLen ? srcLen : dstLen;
    for (int i = 0; i < n; ++i)
        dst[i] = (WCHAR)(unsigned char)src[i];
    return n;
}

EXTERN_C int WINAPI WideCharToMultiByte(UINT, DWORD, LPCWSTR src, int srcLen, LPSTR dst, int dstLen, LPCSTR, LPBOOL used)
{
    if (srcLen < 0)
        srcLen = (int)wcslen(src) + 1;
    if (used)
        *used = FALSE;
    if (!dst)
        return srcLen;
    int n = srcLen < dstLen ? srcLen : dstLen;
    for (int i = 0; i < n; ++i)
        dst[i] = src[i] < 256 ? (char)src[i] : '?';
    return n;
}

EXTERN_C HRESULT WINAPI CoInitializeEx(LPVOID, DWORD) { return S_OK; }
EXTERN_C void WINAPI CoUninitialize(void) {}

const GUID IID_IUnknown = {0x00000000, 0x0000, 0x0000, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};

// ---------------------------------------------------------------------------------------------
// Window system: no window exists, every call fails or no-ops.
// ---------------------------------------------------------------------------------------------

// A single fake window and monitor: the page owns the real canvas. The sizes are generous so the
// engine accepts any r_customMode the page asks for; the device creates its own back buffer.
extern void Web_SetMainWindow(HWND hwnd); // web_main.cpp (mirrors it into g_wv.hWnd)

static HWND const kFakeWindow = (HWND)(uintptr_t)0x1000;
static HMONITOR const kFakeMonitor = (HMONITOR)(uintptr_t)0x2000;
static std::atomic<bool> s_webWindowExists{false};
static const int kWebMonitorW = 7680, kWebMonitorH = 4320;

EXTERN_C HWND WINAPI GetDesktopWindow(void) { return kFakeWindow; }
EXTERN_C LONG WINAPI GetWindowLongA(HWND, int) { return 0; }
EXTERN_C LONG WINAPI SetWindowLongA(HWND, int, LONG) { return 0; }
EXTERN_C BOOL WINAPI SetWindowPos(HWND, HWND, int, int, int, int, UINT) { return TRUE; }
EXTERN_C BOOL WINAPI ShowWindow(HWND, int) { return TRUE; }
EXTERN_C BOOL WINAPI DestroyWindow(HWND h)
{
    if (h != kFakeWindow)
        return FALSE;
    s_webWindowExists = false;
    Web_SetMainWindow(nullptr);
    return TRUE;
}
EXTERN_C BOOL WINAPI AdjustWindowRectEx(LPRECT, DWORD, BOOL, DWORD) { return TRUE; }
EXTERN_C BOOL WINAPI AdjustWindowRect(LPRECT, DWORD, BOOL) { return TRUE; }
EXTERN_C HWND WINAPI CreateWindowExA(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID)
{
    s_webWindowExists = true;
    Web_SetMainWindow(kFakeWindow);
    return kFakeWindow;
}
EXTERN_C int WINAPI GetWindowTextA(HWND, LPSTR buf, int n)
{
    if (buf && n > 0)
        buf[0] = 0;
    return 0;
}
EXTERN_C HMONITOR WINAPI MonitorFromWindow(HWND, DWORD) { return kFakeMonitor; }
EXTERN_C HMONITOR WINAPI MonitorFromPoint(POINT, DWORD) { return kFakeMonitor; }
EXTERN_C BOOL WINAPI GetMonitorInfoA(HMONITOR, LPMONITORINFO mi)
{
    if (!mi)
        return FALSE;
    mi->rcMonitor = {0, 0, kWebMonitorW, kWebMonitorH};
    mi->rcWork = mi->rcMonitor;
    mi->dwFlags = 1;
    return TRUE;
}
EXTERN_C BOOL WINAPI EnumDisplayMonitors(HDC, LPCRECT, MONITORENUMPROC proc, LPARAM data)
{
    if (proc)
    {
        RECT r = {0, 0, kWebMonitorW, kWebMonitorH};
        proc(kFakeMonitor, nullptr, &r, data);
    }
    return TRUE;
}
// Win32 keeps a display counter (cursor visible while >= 0); RB_SwapBuffers loops until it reaches the wanted value.
EXTERN_C int WINAPI ShowCursor(BOOL show)
{
    static std::atomic<int> count{0};
    return show ? count.fetch_add(1) + 1 : count.fetch_sub(1) - 1;
}
EXTERN_C int WINAPI GetSystemMetrics(int index)
{
    if (index == 0)
        return kWebMonitorW;
    if (index == 1)
        return kWebMonitorH;
    return 0;
}
EXTERN_C HDC WINAPI GetDC(HWND) { return nullptr; }
EXTERN_C int WINAPI ReleaseDC(HWND, HDC) { return 1; }
EXTERN_C int WINAPI GetDeviceCaps(HDC, int) { return 0; }
EXTERN_C BOOL WINAPI SetDeviceGammaRamp(HDC, LPVOID) { return FALSE; }
EXTERN_C LONG WINAPI ChangeDisplaySettingsA(LPDEVMODEA, DWORD) { return -1; }
EXTERN_C BOOL WINAPI PeekMessageA(LPMSG, HWND, UINT, UINT, UINT) { return FALSE; }
EXTERN_C BOOL WINAPI GetMessageA(LPMSG, HWND, UINT, UINT) { return FALSE; }
EXTERN_C BOOL WINAPI TranslateMessage(const MSG *) { return FALSE; }
EXTERN_C LRESULT WINAPI DispatchMessageA(const MSG *) { return 0; }
EXTERN_C BOOL WINAPI OpenClipboard(HWND) { return FALSE; }
EXTERN_C BOOL WINAPI CloseClipboard(void) { return TRUE; }
EXTERN_C BOOL WINAPI EmptyClipboard(void) { return TRUE; }
EXTERN_C HANDLE WINAPI SetClipboardData(UINT, HANDLE) { return nullptr; }
EXTERN_C HWND WINAPI GetActiveWindow(void) { return s_webWindowExists ? kFakeWindow : nullptr; }
EXTERN_C BOOL WINAPI EnumWindows(WNDENUMPROC, LPARAM) { return TRUE; }
EXTERN_C DWORD WINAPI GetWindowThreadProcessId(HWND, LPDWORD pid)
{
    if (pid)
        *pid = 0;
    return 0;
}
EXTERN_C BOOL WINAPI ClientToScreen(HWND, LPPOINT) { return TRUE; }
EXTERN_C BOOL WINAPI EnumThreadWindows(DWORD, WNDENUMPROC, LPARAM) { return TRUE; }
EXTERN_C BOOL WINAPI IsWindow(HWND h) { return h == kFakeWindow && s_webWindowExists; }
EXTERN_C HWND WINAPI SetFocus(HWND h) { return h; }
