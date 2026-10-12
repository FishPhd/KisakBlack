#include "tl_support.h"
#include <tl/tl_system.h>
#include <universal/mem_userhunk.h>
#include "common.h"
#include <win32/win_common.h>

HunkUser *s_tlHunkUser;

void __cdecl Sys_SetupTLCallbacks(int hunkMemSize)
{
    tlSystemCallbacks callbacks; // [esp+0h] [ebp-20h] BYREF

    s_tlHunkUser = Hunk_UserCreate(hunkMemSize, HU_SCHEME_DEFAULT, 4u, 0, "TL_MemAlloc support", 37);
    callbacks.ReadFile = TL_ReadFile;
    callbacks.ReleaseFile = TL_ReleaseFile;
    callbacks.CriticalError = TL_CriticalError;
    callbacks.Warning = TL_Warning;
    callbacks.DebugPrint = TL_DebugPrint;
    callbacks.MemAlloc = TL_MemAlloc;
    callbacks.MemRealloc = TL_MemRealloc;
    callbacks.MemFree = TL_MemFree;
    tlSetSystemCallbacks(&callbacks);
}

void __cdecl TL_Warning(const char *Text)
{
    Com_PrintWarning(14, "TL Warning: %s\n", Text);
}

bool __cdecl TL_ReadFile(const char *FileName, tlFileBuf *File, unsigned int Align, unsigned int Flags)
{
    TL_Warning("TL_ReadFile: no implementation");
    return 0;
}

void __cdecl TL_ReleaseFile(tlFileBuf *File)
{
    TL_Warning("TL_ReleaseFile: no implementation");
}

void __cdecl TL_DebugPrint(const char *Text)
{
    Com_Printf(14, "%s", Text);
}

void *__cdecl TL_MemAlloc(unsigned int Size, unsigned int Align, unsigned int Flags)
{
    void *v3; // [esp+8h] [ebp-Ch]

    Sys_EnterCriticalSection(CRITSECT_TL_MEMALLOC);
    v3 = Hunk_UserAlloc(s_tlHunkUser, Size, Align, 0);
    Sys_LeaveCriticalSection(CRITSECT_TL_MEMALLOC);
    return v3;
}

void __cdecl TL_MemFree(void *Ptr)
{
    Hunk_UserFree(s_tlHunkUser, Ptr);
}

void *__cdecl TL_MemRealloc(void *Ptr, unsigned int Size, unsigned int Align, unsigned int Flags)
{
    return 0;
}

void __cdecl TL_CriticalError(const char *msg)
{
    if ( !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\qcommon\\tl_support.cpp", 95, 0, msg) )
        __debugbreak();
}

