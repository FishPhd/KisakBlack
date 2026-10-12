// Minimal <windows.h> for the Web (Emscripten, wasm32) build.
//
// Only the types, macros and function declarations the engine still references are
// provided; the implementations live in src/web/web_win32.cpp. Unlike the Windows
// SDK this header is meant to be small and explicit: if the engine starts using a
// new Win32 API, the web build fails to compile and the API is added here (and
// implemented, or stubbed, in web_win32.cpp) on purpose.
//
// _WIN32 / WIN32 are NOT defined on the web build, so code guarded by them (and
// third-party code with a POSIX path) takes its non-Windows branch.
#pragma once

#ifndef KISAK_WEB
#error "src/web/compat/windows.h is only for the Emscripten web build"
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <wchar.h>

// ----------------------------------------------------------------------------
// Calling conventions / decorations (the web target has a single convention)
// ----------------------------------------------------------------------------
#ifndef WINAPI
#define WINAPI
#define CALLBACK
#define APIENTRY
#define WINAPIV
#define NTAPI
#define CONST const
#define FAR
#define NEAR
#define PASCAL
#define STDMETHODCALLTYPE
#define STDMETHODVCALLTYPE
#define DECLSPEC_NOVTABLE
#define DECLSPEC_UUID(x)
#define DECLSPEC_SELECTANY
#define _In_
#define _In_opt_
#define _Out_
#define _Out_opt_
#define _Inout_
#define _Inout_opt_
#define _Out_writes_bytes_(x)
#define _In_reads_(x)
#define _In_reads_bytes_(x)
#define _Success_(x)
#define _Check_return_
#define _Ret_maybenull_
#define _Printf_format_string_
#define __in
#define __out
#define __inout
#define __in_opt
#define __out_opt
#define __in_bcount(x)
#define __out_bcount(x)
#define __RPC_FAR
#define UNREFERENCED_PARAMETER(P) ((void)(P))
#endif

#ifdef __cplusplus
#define EXTERN_C extern "C"
#else
#define EXTERN_C extern
#endif

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#ifndef MAX_PATH
#define MAX_PATH 260
#endif

// ----------------------------------------------------------------------------
// Base types. wasm32 is ILP32 with a 4-byte long, exactly like MSVC x86.
// ----------------------------------------------------------------------------
typedef int BOOL;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned long DWORD;
typedef unsigned int UINT;
typedef int INT;
typedef long LONG;
typedef unsigned long ULONG;
typedef short SHORT;
typedef unsigned short USHORT;
typedef char CHAR;
typedef unsigned char UCHAR;
typedef float FLOAT;
typedef double DOUBLE;
typedef long long LONGLONG;
typedef unsigned long long ULONGLONG;
typedef int8_t INT8;
typedef int16_t INT16;
typedef int32_t INT32;
typedef int64_t INT64;
typedef uint8_t UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef DWORD *PDWORD, *LPDWORD;
typedef WORD *PWORD, *LPWORD;
typedef BYTE *PBYTE, *LPBYTE;
typedef BOOL *PBOOL, *LPBOOL;
typedef INT *PINT, *LPINT;
typedef UINT *PUINT;
typedef LONG *PLONG, *LPLONG;
typedef ULONG *PULONG;
typedef LONGLONG *PLONGLONG;
typedef ULONGLONG *PULONGLONG;
typedef float *PFLOAT;
typedef void *PVOID, *LPVOID;
typedef const void *LPCVOID;
typedef char *PCHAR, *LPSTR, *PSTR;
typedef const char *LPCSTR, *PCSTR;
typedef wchar_t WCHAR;
typedef WCHAR *PWSTR, *LPWSTR;
typedef const WCHAR *PCWSTR, *LPCWSTR;
typedef char TCHAR;
typedef LPSTR LPTSTR;
typedef LPCSTR LPCTSTR;
typedef unsigned int UINT_PTR;
typedef int INT_PTR;
typedef unsigned long ULONG_PTR;
typedef long LONG_PTR;
typedef ULONG_PTR DWORD_PTR;
typedef ULONG_PTR *PULONG_PTR;
typedef size_t SIZE_T;
typedef long SSIZE_T;
typedef UINT_PTR WPARAM;
typedef LONG_PTR LPARAM;
typedef LONG_PTR LRESULT;
typedef LONG HRESULT;
typedef LONG NTSTATUS;
typedef WORD ATOM;
typedef unsigned char boolean;

typedef void *HANDLE;
typedef HANDLE *PHANDLE, *LPHANDLE;
#define DECLARE_HANDLE(name) struct name##__ { int unused; }; typedef struct name##__ *name
DECLARE_HANDLE(HWND);
DECLARE_HANDLE(HINSTANCE);
DECLARE_HANDLE(HDC);
DECLARE_HANDLE(HGLRC);
DECLARE_HANDLE(HICON);
DECLARE_HANDLE(HCURSOR);
DECLARE_HANDLE(HBRUSH);
DECLARE_HANDLE(HMENU);
DECLARE_HANDLE(HMONITOR);
DECLARE_HANDLE(HBITMAP);
DECLARE_HANDLE(HFONT);
DECLARE_HANDLE(HGDIOBJ);
DECLARE_HANDLE(HKEY);
typedef HINSTANCE HMODULE;
typedef HANDLE HLOCAL, HGLOBAL;
typedef void *FARPROC_T;
typedef int (*FARPROC)(void);
typedef DWORD LCID;
typedef DWORD COLORREF;
typedef WORD LANGID;

#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)
#define INVALID_FILE_SIZE ((DWORD)0xFFFFFFFF)
#define INVALID_SET_FILE_POINTER ((DWORD)-1)
#define INFINITE 0xFFFFFFFF
#define MAXDWORD 0xFFFFFFFF
#define MAXLONG 0x7fffffff
#define LOWORD(l) ((WORD)(((DWORD_PTR)(l)) & 0xffff))
#define HIWORD(l) ((WORD)((((DWORD_PTR)(l)) >> 16) & 0xffff))
#define LOBYTE(w) ((BYTE)(((DWORD_PTR)(w)) & 0xff))
#define HIBYTE(w) ((BYTE)((((DWORD_PTR)(w)) >> 8) & 0xff))
#define MAKELONG(a, b) ((LONG)(((WORD)(((DWORD_PTR)(a)) & 0xffff)) | ((DWORD)((WORD)(((DWORD_PTR)(b)) & 0xffff))) << 16))
#define MAKEWORD(a, b) ((WORD)(((BYTE)(((DWORD_PTR)(a)) & 0xff)) | ((WORD)((BYTE)(((DWORD_PTR)(b)) & 0xff))) << 8))

// ----------------------------------------------------------------------------
// HRESULT
// ----------------------------------------------------------------------------
#define S_OK ((HRESULT)0L)
#define S_FALSE ((HRESULT)1L)
#define E_FAIL ((HRESULT)0x80004005L)
#define E_NOTIMPL ((HRESULT)0x80004001L)
#define E_NOINTERFACE ((HRESULT)0x80004002L)
#define E_POINTER ((HRESULT)0x80004003L)
#define E_OUTOFMEMORY ((HRESULT)0x8007000EL)
#define E_INVALIDARG ((HRESULT)0x80070057L)
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#define MAKE_HRESULT(sev, fac, code) \
    ((HRESULT)(((unsigned long)(sev) << 31) | ((unsigned long)(fac) << 16) | ((unsigned long)(code))))
#define HRESULT_FROM_WIN32(x) ((HRESULT)(x) <= 0 ? (HRESULT)(x) : (HRESULT)(((x)&0x0000FFFF) | (7 << 16) | 0x80000000))

// ----------------------------------------------------------------------------
// Structures
// ----------------------------------------------------------------------------
typedef struct _GUID {
    unsigned long Data1;
    unsigned short Data2;
    unsigned short Data3;
    unsigned char Data4[8];
} GUID, IID, CLSID;
typedef const GUID &REFGUID;
typedef const IID &REFIID;
typedef const CLSID &REFCLSID;

typedef struct tagPOINT { LONG x; LONG y; } POINT, *PPOINT, *LPPOINT;
typedef struct tagRECT { LONG left; LONG top; LONG right; LONG bottom; } RECT, *PRECT, *LPRECT;
typedef struct tagSIZE { LONG cx; LONG cy; } SIZE, *PSIZE, *LPSIZE;

typedef union _LARGE_INTEGER {
    struct { DWORD LowPart; LONG HighPart; };
    struct { DWORD LowPart; LONG HighPart; } u;
    LONGLONG QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;
typedef union _ULARGE_INTEGER {
    struct { DWORD LowPart; DWORD HighPart; };
    struct { DWORD LowPart; DWORD HighPart; } u;
    ULONGLONG QuadPart;
} ULARGE_INTEGER, *PULARGE_INTEGER;

typedef struct _FILETIME { DWORD dwLowDateTime; DWORD dwHighDateTime; } FILETIME, *PFILETIME, *LPFILETIME;
typedef struct _SYSTEMTIME {
    WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
} SYSTEMTIME, *PSYSTEMTIME, *LPSYSTEMTIME;

typedef struct _SECURITY_ATTRIBUTES {
    DWORD nLength;
    LPVOID lpSecurityDescriptor;
    BOOL bInheritHandle;
} SECURITY_ATTRIBUTES, *PSECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;

typedef struct _OVERLAPPED {
    ULONG_PTR Internal;
    ULONG_PTR InternalHigh;
    union {
        struct { DWORD Offset; DWORD OffsetHigh; };
        PVOID Pointer;
    };
    HANDLE hEvent;
} OVERLAPPED, *LPOVERLAPPED;
typedef void(WINAPI *LPOVERLAPPED_COMPLETION_ROUTINE)(DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered,
                                                      LPOVERLAPPED lpOverlapped);

typedef struct _SYSTEM_INFO {
    union {
        DWORD dwOemId;
        struct { WORD wProcessorArchitecture; WORD wReserved; };
    };
    DWORD dwPageSize;
    LPVOID lpMinimumApplicationAddress;
    LPVOID lpMaximumApplicationAddress;
    DWORD_PTR dwActiveProcessorMask;
    DWORD dwNumberOfProcessors;
    DWORD dwProcessorType;
    DWORD dwAllocationGranularity;
    WORD wProcessorLevel;
    WORD wProcessorRevision;
} SYSTEM_INFO, *LPSYSTEM_INFO;

typedef struct _MEMORYSTATUS {
    DWORD dwLength;
    DWORD dwMemoryLoad;
    SIZE_T dwTotalPhys;
    SIZE_T dwAvailPhys;
    SIZE_T dwTotalPageFile;
    SIZE_T dwAvailPageFile;
    SIZE_T dwTotalVirtual;
    SIZE_T dwAvailVirtual;
} MEMORYSTATUS, *LPMEMORYSTATUS;

typedef struct _MEMORYSTATUSEX {
    DWORD dwLength;
    DWORD dwMemoryLoad;
    ULONGLONG ullTotalPhys;
    ULONGLONG ullAvailPhys;
    ULONGLONG ullTotalPageFile;
    ULONGLONG ullAvailPageFile;
    ULONGLONG ullTotalVirtual;
    ULONGLONG ullAvailVirtual;
    ULONGLONG ullAvailExtendedVirtual;
} MEMORYSTATUSEX, *LPMEMORYSTATUSEX;

// ----------------------------------------------------------------------------
// COM base (the Windows SDK pulls this in through <objbase.h>)
// ----------------------------------------------------------------------------
#define interface struct
#define THIS_
#define THIS void
#define PURE = 0
#define STDMETHOD(method) virtual HRESULT method
#define STDMETHOD_(type, method) virtual type method
#define STDMETHODIMP HRESULT
#define STDMETHODIMP_(type) type
#define DECLARE_INTERFACE(iface) interface iface
#define DECLARE_INTERFACE_(iface, base) interface iface : public base
#define BEGIN_INTERFACE
#define END_INTERFACE
#define IFACEMETHOD(m) STDMETHOD(m)
#define IFACEMETHOD_(t, m) STDMETHOD_(t, m)
// -fms-extensions gives clang the __uuidof keyword, which needs declspec(uuid) on the
// types; the web build just maps it to the IID_<type> constant instead.
#define __uuidof(x) (IID_##x)

inline bool operator==(const GUID &a, const GUID &b)
{
    return a.Data1 == b.Data1 && a.Data2 == b.Data2 && a.Data3 == b.Data3 &&
           *(const unsigned long long *)a.Data4 == *(const unsigned long long *)b.Data4;
}
inline bool operator!=(const GUID &a, const GUID &b) { return !(a == b); }

interface IUnknown
{
    virtual HRESULT QueryInterface(REFIID riid, void **ppvObject) = 0;
    virtual ULONG AddRef() = 0;
    virtual ULONG Release() = 0;
};
extern const GUID IID_IUnknown;
typedef IUnknown *LPUNKNOWN;

// ----------------------------------------------------------------------------
// Interlocked / barriers. clang provides the _Interlocked* builtins under
// -fms-extensions (wrapped for unsigned pointers in kisak_web_compat.h);
// <intrin.h> (src/web/compat) supplies the rest.
// ----------------------------------------------------------------------------
#include <intrin.h>
#define InterlockedIncrement _InterlockedIncrement
#define InterlockedDecrement _InterlockedDecrement
#define InterlockedExchange _InterlockedExchange
#define InterlockedExchangeAdd _InterlockedExchangeAdd
#define InterlockedCompareExchange _InterlockedCompareExchange
#define InterlockedCompareExchange64 _InterlockedCompareExchange64
#define InterlockedExchangePointer _InterlockedExchangePointer
#define InterlockedCompareExchangePointer _InterlockedCompareExchangePointer
#define InterlockedOr _InterlockedOr
#define InterlockedAnd _InterlockedAnd
#define InterlockedXor _InterlockedXor
#define MemoryBarrier() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#define YieldProcessor() ((void)0)
typedef long long LONG64;
typedef unsigned long long ULONG64, DWORD64;

// ----------------------------------------------------------------------------
// Threads, events, mutexes, critical sections (web_win32.cpp: pthread + futex)
// ----------------------------------------------------------------------------
typedef struct _RTL_CRITICAL_SECTION {
    void *DebugInfo;
    LONG LockCount;       // futex word: 0 = free, 1 = locked, 2 = locked with waiters
    LONG RecursionCount;
    HANDLE OwningThread;  // current thread id while held
    HANDLE LockSemaphore;
    ULONG_PTR SpinCount;
} CRITICAL_SECTION, *PCRITICAL_SECTION, *LPCRITICAL_SECTION;

typedef DWORD(WINAPI *LPTHREAD_START_ROUTINE)(LPVOID lpThreadParameter);
#define CREATE_SUSPENDED 0x00000004
#define STACK_SIZE_PARAM_IS_A_RESERVATION 0x00010000
#define WAIT_OBJECT_0 0L
#define WAIT_ABANDONED 0x00000080L
#define WAIT_TIMEOUT 258L
#define WAIT_FAILED 0xFFFFFFFFL
#define STILL_ACTIVE 259L
#define MAXIMUM_WAIT_OBJECTS 64
#define THREAD_PRIORITY_LOWEST -2
#define THREAD_PRIORITY_BELOW_NORMAL -1
#define THREAD_PRIORITY_NORMAL 0
#define THREAD_PRIORITY_ABOVE_NORMAL 1
#define THREAD_PRIORITY_HIGHEST 2
#define THREAD_PRIORITY_TIME_CRITICAL 15
#define MAXIMUM_PROCESSORS 32
#define SEMAPHORE_ALL_ACCESS 0x1F0003
#define EVENT_ALL_ACCESS 0x1F0003
#define MUTEX_ALL_ACCESS 0x1F0001
#define ES_CONTINUOUS 0x80000000
#define ES_SYSTEM_REQUIRED 0x00000001
#define ES_DISPLAY_REQUIRED 0x00000002

EXTERN_C HANDLE WINAPI CreateThread(LPSECURITY_ATTRIBUTES, SIZE_T, LPTHREAD_START_ROUTINE, LPVOID, DWORD, LPDWORD);
EXTERN_C DWORD WINAPI GetCurrentThreadId(void);
EXTERN_C HANDLE WINAPI GetCurrentThread(void);
EXTERN_C HANDLE WINAPI GetCurrentProcess(void);
EXTERN_C DWORD WINAPI GetCurrentProcessId(void);
EXTERN_C DWORD WINAPI ResumeThread(HANDLE);
EXTERN_C DWORD WINAPI SuspendThread(HANDLE);
EXTERN_C DWORD_PTR WINAPI SetThreadAffinityMask(HANDLE, DWORD_PTR);
EXTERN_C DWORD WINAPI SetThreadIdealProcessor(HANDLE, DWORD);
EXTERN_C BOOL WINAPI SetThreadPriority(HANDLE, int);
EXTERN_C int WINAPI GetThreadPriority(HANDLE);
EXTERN_C BOOL WINAPI GetProcessAffinityMask(HANDLE, DWORD_PTR *, DWORD_PTR *);
EXTERN_C BOOL WINAPI GetExitCodeThread(HANDLE, LPDWORD);
EXTERN_C DWORD WINAPI SetThreadExecutionState(DWORD);
EXTERN_C BOOL WINAPI CloseHandle(HANDLE);
EXTERN_C DWORD WINAPI WaitForSingleObject(HANDLE, DWORD);
EXTERN_C DWORD WINAPI WaitForMultipleObjects(DWORD, const HANDLE *, BOOL, DWORD);
EXTERN_C HANDLE WINAPI CreateEventA(LPSECURITY_ATTRIBUTES, BOOL manualReset, BOOL initialState, LPCSTR name);
EXTERN_C BOOL WINAPI SetEvent(HANDLE);
EXTERN_C BOOL WINAPI ResetEvent(HANDLE);
EXTERN_C BOOL WINAPI PulseEvent(HANDLE);
EXTERN_C HANDLE WINAPI CreateMutexA(LPSECURITY_ATTRIBUTES, BOOL initialOwner, LPCSTR name);
EXTERN_C BOOL WINAPI ReleaseMutex(HANDLE);
EXTERN_C HANDLE WINAPI CreateSemaphoreA(LPSECURITY_ATTRIBUTES, LONG initial, LONG maximum, LPCSTR name);
EXTERN_C BOOL WINAPI ReleaseSemaphore(HANDLE, LONG releaseCount, LPLONG previousCount);
EXTERN_C void WINAPI Sleep(DWORD ms);
EXTERN_C DWORD WINAPI SleepEx(DWORD ms, BOOL alertable);
EXTERN_C BOOL WINAPI SwitchToThread(void);
EXTERN_C void WINAPI InitializeCriticalSection(LPCRITICAL_SECTION);
EXTERN_C BOOL WINAPI InitializeCriticalSectionAndSpinCount(LPCRITICAL_SECTION, DWORD);
EXTERN_C void WINAPI EnterCriticalSection(LPCRITICAL_SECTION);
EXTERN_C BOOL WINAPI TryEnterCriticalSection(LPCRITICAL_SECTION);
EXTERN_C void WINAPI LeaveCriticalSection(LPCRITICAL_SECTION);
EXTERN_C void WINAPI DeleteCriticalSection(LPCRITICAL_SECTION);
#define CreateEvent CreateEventA
#define CreateMutex CreateMutexA
#define CreateSemaphore CreateSemaphoreA

// ----------------------------------------------------------------------------
// Time
// ----------------------------------------------------------------------------
EXTERN_C BOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER *);
EXTERN_C BOOL WINAPI QueryPerformanceFrequency(LARGE_INTEGER *);
EXTERN_C void WINAPI GetSystemTime(LPSYSTEMTIME);
EXTERN_C void WINAPI GetLocalTime(LPSYSTEMTIME);
EXTERN_C void WINAPI GetSystemTimeAsFileTime(LPFILETIME);
EXTERN_C BOOL WINAPI SystemTimeToFileTime(const SYSTEMTIME *, LPFILETIME);
EXTERN_C DWORD WINAPI GetTickCount(void);
EXTERN_C DWORD WINAPI timeGetTime(void);

// ----------------------------------------------------------------------------
// Memory
// ----------------------------------------------------------------------------
#define MEM_COMMIT 0x00001000
#define MEM_RESERVE 0x00002000
#define MEM_DECOMMIT 0x00004000
#define MEM_RELEASE 0x00008000
#define MEM_RESET 0x00080000
#define MEM_TOP_DOWN 0x00100000
#define MEM_LARGE_PAGES 0x20000000
#define MEM_PHYSICAL 0x00400000
#define MEM_FREE 0x00010000
#define MEM_PRIVATE 0x00020000
#define PAGE_NOACCESS 0x01
#define PAGE_READONLY 0x02
#define PAGE_READWRITE 0x04
#define PAGE_EXECUTE_READWRITE 0x40
#define HEAP_ZERO_MEMORY 0x00000008
#define HEAP_NO_SERIALIZE 0x00000001
#define GMEM_FIXED 0x0000
#define GMEM_MOVEABLE 0x0002
#define GMEM_ZEROINIT 0x0040
#define GPTR 0x0040
#define LMEM_FIXED 0x0000
#define LMEM_ZEROINIT 0x0040
typedef struct _MEMORY_BASIC_INFORMATION {
    PVOID BaseAddress;
    PVOID AllocationBase;
    DWORD AllocationProtect;
    SIZE_T RegionSize;
    DWORD State;
    DWORD Protect;
    DWORD Type;
} MEMORY_BASIC_INFORMATION, *PMEMORY_BASIC_INFORMATION;
EXTERN_C LPVOID WINAPI VirtualAlloc(LPVOID, SIZE_T, DWORD allocationType, DWORD protect);
EXTERN_C BOOL WINAPI VirtualFree(LPVOID, SIZE_T, DWORD freeType);
EXTERN_C BOOL WINAPI VirtualProtect(LPVOID, SIZE_T, DWORD, PDWORD);
EXTERN_C SIZE_T WINAPI VirtualQuery(LPCVOID, PMEMORY_BASIC_INFORMATION, SIZE_T);
EXTERN_C HANDLE WINAPI GetProcessHeap(void);
EXTERN_C LPVOID WINAPI HeapAlloc(HANDLE, DWORD, SIZE_T);
EXTERN_C BOOL WINAPI HeapFree(HANDLE, DWORD, LPVOID);
EXTERN_C SIZE_T WINAPI HeapSize(HANDLE, DWORD, LPCVOID);
EXTERN_C HGLOBAL WINAPI GlobalAlloc(UINT, SIZE_T);
EXTERN_C HGLOBAL WINAPI GlobalFree(HGLOBAL);
EXTERN_C LPVOID WINAPI GlobalLock(HGLOBAL);
EXTERN_C BOOL WINAPI GlobalUnlock(HGLOBAL);
EXTERN_C void WINAPI GlobalMemoryStatus(LPMEMORYSTATUS);
EXTERN_C BOOL WINAPI GlobalMemoryStatusEx(LPMEMORYSTATUSEX);
EXTERN_C void WINAPI GetSystemInfo(LPSYSTEM_INFO);

// ----------------------------------------------------------------------------
// Files (web_win32.cpp: POSIX over WasmFS)
// ----------------------------------------------------------------------------
#define GENERIC_READ 0x80000000L
#define GENERIC_WRITE 0x40000000L
#define FILE_SHARE_READ 0x00000001
#define FILE_SHARE_WRITE 0x00000002
#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define TRUNCATE_EXISTING 5
#define FILE_ATTRIBUTE_READONLY 0x00000001
#define FILE_ATTRIBUTE_HIDDEN 0x00000002
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define FILE_ATTRIBUTE_ARCHIVE 0x00000020
#define FILE_ATTRIBUTE_NORMAL 0x00000080
#define FILE_FLAG_OVERLAPPED 0x40000000
#define FILE_FLAG_SEQUENTIAL_SCAN 0x08000000
#define FILE_FLAG_RANDOM_ACCESS 0x10000000
#define FILE_FLAG_NO_BUFFERING 0x20000000
#define FILE_FLAG_WRITE_THROUGH 0x80000000
#define FILE_BEGIN 0
#define FILE_CURRENT 1
#define FILE_END 2
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#define ERROR_SUCCESS 0L
#define ERROR_FILE_NOT_FOUND 2L
#define ERROR_PATH_NOT_FOUND 3L
#define ERROR_ACCESS_DENIED 5L
#define ERROR_INVALID_HANDLE 6L
#define ERROR_NO_MORE_FILES 18L
#define ERROR_HANDLE_EOF 38L
#define ERROR_ALREADY_EXISTS 183L
#define ERROR_IO_PENDING 997L
#define ERROR_DEVICE_NOT_CONNECTED 1167L
typedef struct _WIN32_FIND_DATAA {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
    DWORD dwReserved0;
    DWORD dwReserved1;
    CHAR cFileName[MAX_PATH];
    CHAR cAlternateFileName[14];
} WIN32_FIND_DATAA, *LPWIN32_FIND_DATAA;
typedef WIN32_FIND_DATAA WIN32_FIND_DATA;
EXTERN_C HANDLE WINAPI CreateFileA(LPCSTR, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES, DWORD disposition, DWORD flags, HANDLE templateFile);
EXTERN_C BOOL WINAPI ReadFile(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
EXTERN_C BOOL WINAPI WriteFile(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
EXTERN_C DWORD WINAPI GetFileSize(HANDLE, LPDWORD);
EXTERN_C DWORD WINAPI SetFilePointer(HANDLE, LONG, PLONG, DWORD);
EXTERN_C BOOL WINAPI FlushFileBuffers(HANDLE);
EXTERN_C BOOL WINAPI DeleteFileA(LPCSTR);
EXTERN_C DWORD WINAPI GetFileAttributesA(LPCSTR);
EXTERN_C BOOL WINAPI SetFileAttributesA(LPCSTR, DWORD);
EXTERN_C BOOL WINAPI CreateDirectoryA(LPCSTR, LPSECURITY_ATTRIBUTES);
EXTERN_C BOOL WINAPI RemoveDirectoryA(LPCSTR);
EXTERN_C DWORD WINAPI GetCurrentDirectoryA(DWORD, LPSTR);
EXTERN_C HANDLE WINAPI FindFirstFileA(LPCSTR, LPWIN32_FIND_DATAA);
EXTERN_C BOOL WINAPI FindNextFileA(HANDLE, LPWIN32_FIND_DATAA);
EXTERN_C BOOL WINAPI FindClose(HANDLE);
#define CreateFile CreateFileA
#define DeleteFile DeleteFileA
#define GetFileAttributes GetFileAttributesA

// ----------------------------------------------------------------------------
// Process / module / diagnostics
// ----------------------------------------------------------------------------
#define EXCEPTION_NONCONTINUABLE 0x1
#define EXCEPTION_EXECUTE_HANDLER 1
#define EXCEPTION_CONTINUE_SEARCH 0
#define CP_ACP 0
#define CP_UTF8 65001
#define MB_OK 0x00000000L
#define MB_OKCANCEL 0x00000001L
#define MB_YESNO 0x00000004L
#define MB_ICONERROR 0x00000010L
#define MB_ICONQUESTION 0x00000020L
#define MB_ICONWARNING 0x00000030L
#define MB_ICONINFORMATION 0x00000040L
#define MB_SETFOREGROUND 0x00010000L
#define MB_TOPMOST 0x00040000L
#define IDOK 1
#define IDCANCEL 2
#define IDYES 6
#define IDNO 7
struct _EXCEPTION_POINTERS;
typedef struct _EXCEPTION_RECORD {
    DWORD ExceptionCode;
    DWORD ExceptionFlags;
    struct _EXCEPTION_RECORD *ExceptionRecord;
    PVOID ExceptionAddress;
    DWORD NumberParameters;
    ULONG_PTR ExceptionInformation[15];
} EXCEPTION_RECORD, *PEXCEPTION_RECORD;
typedef struct _CONTEXT { DWORD ContextFlags; DWORD Eip; DWORD Esp; DWORD Ebp; } CONTEXT, *PCONTEXT;
typedef struct _EXCEPTION_POINTERS {
    PEXCEPTION_RECORD ExceptionRecord;
    PCONTEXT ContextRecord;
} EXCEPTION_POINTERS, *PEXCEPTION_POINTERS, *LPEXCEPTION_POINTERS;
typedef LONG(WINAPI *LPTOP_LEVEL_EXCEPTION_FILTER)(struct _EXCEPTION_POINTERS *);
typedef struct _LUID { DWORD LowPart; LONG HighPart; } LUID;
typedef struct _LUID_AND_ATTRIBUTES { LUID Luid; DWORD Attributes; } LUID_AND_ATTRIBUTES;
typedef struct _TOKEN_PRIVILEGES { DWORD PrivilegeCount; LUID_AND_ATTRIBUTES Privileges[1]; } TOKEN_PRIVILEGES, *PTOKEN_PRIVILEGES;
EXTERN_C DWORD WINAPI GetLastError(void);
EXTERN_C void WINAPI SetLastError(DWORD);
EXTERN_C void WINAPI OutputDebugStringA(LPCSTR);
EXTERN_C BOOL WINAPI IsDebuggerPresent(void);
EXTERN_C void WINAPI DebugBreak(void);
EXTERN_C void WINAPI RaiseException(DWORD, DWORD, DWORD, const ULONG_PTR *);
EXTERN_C void WINAPI ExitProcess(UINT);
EXTERN_C int WINAPI MessageBoxA(HWND, LPCSTR text, LPCSTR caption, UINT type);
EXTERN_C HMODULE WINAPI LoadLibraryA(LPCSTR);
EXTERN_C HMODULE WINAPI GetModuleHandleA(LPCSTR);
EXTERN_C DWORD WINAPI GetModuleFileNameA(HMODULE, LPSTR, DWORD);
EXTERN_C FARPROC WINAPI GetProcAddress(HMODULE, LPCSTR);
EXTERN_C BOOL WINAPI FreeLibrary(HMODULE);
EXTERN_C int WINAPI MultiByteToWideChar(UINT, DWORD, LPCSTR, int, LPWSTR, int);
EXTERN_C int WINAPI WideCharToMultiByte(UINT, DWORD, LPCWSTR, int, LPSTR, int, LPCSTR, LPBOOL);
EXTERN_C HRESULT WINAPI CoInitializeEx(LPVOID, DWORD);
EXTERN_C void WINAPI CoUninitialize(void);
#define COINIT_MULTITHREADED 0x0
#define COINIT_APARTMENTTHREADED 0x2
#define MessageBox MessageBoxA

// ----------------------------------------------------------------------------
// Window system. The web build has no native window, so these exist only so the
// renderer / client code that still names them compiles; every call fails or no-ops.
// ----------------------------------------------------------------------------
#define SM_CXSCREEN 0
#define SM_CYSCREEN 1
#define SM_CXFULLSCREEN 16
#define SM_CYFULLSCREEN 17
#define GWL_STYLE (-16)
#define GWL_EXSTYLE (-20)
#define WS_OVERLAPPED 0x00000000L
#define WS_POPUP 0x80000000L
#define WS_CAPTION 0x00C00000L
#define WS_SYSMENU 0x00080000L
#define WS_VISIBLE 0x10000000L
#define WS_EX_TOPMOST 0x00000008L
#define SWP_NOSIZE 0x0001
#define SWP_NOMOVE 0x0002
#define SWP_NOZORDER 0x0004
#define SWP_SHOWWINDOW 0x0040
#define HWND_TOP ((HWND)0)
#define HWND_TOPMOST ((HWND)-1)
#define HWND_NOTOPMOST ((HWND)-2)
#define SW_HIDE 0
#define SW_SHOW 5
#define SW_RESTORE 9
#define PM_REMOVE 0x0001
#define MONITOR_DEFAULTTONULL 0
#define MONITOR_DEFAULTTOPRIMARY 1
#define MONITOR_DEFAULTTONEAREST 2
#define HORZRES 8
#define VERTRES 10
#define BITSPIXEL 12
#define CF_TEXT 1
#define DISP_CHANGE_SUCCESSFUL 0
#define CDS_FULLSCREEN 0x00000004
#define CCHDEVICENAME 32
typedef struct tagMSG { HWND hwnd; UINT message; WPARAM wParam; LPARAM lParam; DWORD time; POINT pt; } MSG, *LPMSG;
typedef struct tagMONITORINFO { DWORD cbSize; RECT rcMonitor; RECT rcWork; DWORD dwFlags; } MONITORINFO, *LPMONITORINFO;
typedef struct tagMONITORINFOEXA { MONITORINFO mi; CHAR szDevice[CCHDEVICENAME]; } MONITORINFOEXA, *LPMONITORINFOEXA;
typedef struct _devicemodeA { BYTE reserved[156]; } DEVMODEA, *LPDEVMODEA;
EXTERN_C HWND WINAPI GetDesktopWindow(void);
EXTERN_C LONG WINAPI GetWindowLongA(HWND, int);
EXTERN_C LONG WINAPI SetWindowLongA(HWND, int, LONG);
EXTERN_C BOOL WINAPI SetWindowPos(HWND, HWND, int, int, int, int, UINT);
EXTERN_C BOOL WINAPI ShowWindow(HWND, int);
EXTERN_C BOOL WINAPI DestroyWindow(HWND);
EXTERN_C BOOL WINAPI AdjustWindowRectEx(LPRECT, DWORD, BOOL, DWORD);
EXTERN_C HWND WINAPI CreateWindowExA(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
EXTERN_C int WINAPI GetWindowTextA(HWND, LPSTR, int);
EXTERN_C HMONITOR WINAPI MonitorFromWindow(HWND, DWORD);
EXTERN_C BOOL WINAPI GetMonitorInfoA(HMONITOR, LPMONITORINFO);
EXTERN_C int WINAPI ShowCursor(BOOL);
EXTERN_C int WINAPI GetSystemMetrics(int);
EXTERN_C HDC WINAPI GetDC(HWND);
EXTERN_C int WINAPI ReleaseDC(HWND, HDC);
EXTERN_C int WINAPI GetDeviceCaps(HDC, int);
EXTERN_C BOOL WINAPI SetDeviceGammaRamp(HDC, LPVOID);
EXTERN_C LONG WINAPI ChangeDisplaySettingsA(LPDEVMODEA, DWORD);
EXTERN_C BOOL WINAPI PeekMessageA(LPMSG, HWND, UINT, UINT, UINT);
EXTERN_C BOOL WINAPI GetMessageA(LPMSG, HWND, UINT, UINT);
EXTERN_C BOOL WINAPI TranslateMessage(const MSG *);
EXTERN_C LRESULT WINAPI DispatchMessageA(const MSG *);
EXTERN_C BOOL WINAPI OpenClipboard(HWND);
EXTERN_C BOOL WINAPI CloseClipboard(void);
EXTERN_C BOOL WINAPI EmptyClipboard(void);
EXTERN_C HANDLE WINAPI SetClipboardData(UINT, HANDLE);

// ----------------------------------------------------------------------------
// Misc macros / types the SDK headers provide indirectly
// ----------------------------------------------------------------------------
#define VOID void
typedef unsigned char byte; // rpcndr.h
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))
#define RTL_NUMBER_OF(a) ARRAYSIZE(a)
#define InterlockedExchange64(p, v) __atomic_exchange_n((volatile long long *)(p), (long long)(v), __ATOMIC_SEQ_CST)
#define InterlockedExchangeAdd64(p, v) _InterlockedExchangeAdd64((volatile long long *)(p), (long long)(v))
typedef BOOL(CALLBACK *WNDENUMPROC)(HWND, LPARAM);
EXTERN_C HWND WINAPI GetActiveWindow(void);
EXTERN_C BOOL WINAPI EnumWindows(WNDENUMPROC, LPARAM);
EXTERN_C DWORD WINAPI GetWindowThreadProcessId(HWND, LPDWORD);
EXTERN_C BOOL WINAPI ReadFileEx(HANDLE, LPVOID, DWORD, LPOVERLAPPED, LPOVERLAPPED_COMPLETION_ROUTINE);
EXTERN_C BOOL WINAPI GetOverlappedResult(HANDLE, LPOVERLAPPED, LPDWORD, BOOL);

// More window-system / process / file calls (stubs in web_win32.cpp)
typedef const RECT *LPCRECT;
typedef BOOL(CALLBACK *MONITORENUMPROC)(HMONITOR, HDC, LPRECT, LPARAM);
typedef struct _RGNDATAHEADER { DWORD dwSize; DWORD iType; DWORD nCount; DWORD nRgnSize; RECT rcBound; } RGNDATAHEADER;
typedef struct _RGNDATA { RGNDATAHEADER rdh; char Buffer[1]; } RGNDATA, *PRGNDATA, *LPRGNDATA;
EXTERN_C HANDLE WINAPI OpenProcess(DWORD, BOOL, DWORD);
EXTERN_C BOOL WINAPI ClientToScreen(HWND, LPPOINT);
EXTERN_C BOOL WINAPI EnumThreadWindows(DWORD, WNDENUMPROC, LPARAM);
EXTERN_C BOOL WINAPI EnumDisplayMonitors(HDC, LPCRECT, MONITORENUMPROC, LPARAM);
EXTERN_C HMONITOR WINAPI MonitorFromPoint(POINT, DWORD);
EXTERN_C BOOL WINAPI IsWindow(HWND);
EXTERN_C HWND WINAPI SetFocus(HWND);
EXTERN_C BOOL WINAPI DuplicateHandle(HANDLE, HANDLE, HANDLE, LPHANDLE, DWORD, BOOL, DWORD);
EXTERN_C LONG WINAPI CompareFileTime(const FILETIME *, const FILETIME *);
EXTERN_C DWORD WINAPI GetFullPathNameA(LPCSTR, DWORD, LPSTR, LPSTR *);
EXTERN_C int WINAPI MoveFileExA(const char *, const char *, unsigned long);
#define MOVEFILE_REPLACE_EXISTING 0x00000001
#define MOVEFILE_COPY_ALLOWED 0x00000002
#define MOVEFILE_WRITE_THROUGH 0x00000008
#define WAIT_IO_COMPLETION 192L
EXTERN_C DWORD WINAPI SleepEx(DWORD ms, BOOL alertable);
