// Loads binkw32.dll from src/binklib when KISAK_NO_BINK is off.
//
// The import library records the DLL name only, so the Windows loader would
// otherwise require a copy next to the exe. Delay-load lets this hook open the
// in-tree file first. A failed LoadLibrary falls through to the normal search.

#include <windows.h>
#include <delayimp.h>

#include "kisak_bink_dll.h"

static FARPROC WINAPI KisakBinkDelayNotify(unsigned notify, PDelayLoadInfo info)
{
    if (notify == dliNotePreLoadLibrary && info != nullptr && info->szDll != nullptr)
    {
        if (lstrcmpiA(info->szDll, "binkw32.dll") == 0)
            return reinterpret_cast<FARPROC>(LoadLibraryA(KISAK_BINKW32_DLL));
    }
    return nullptr;
}

extern "C" const PfnDliHook __pfnDliNotifyHook2 = KisakBinkDelayNotify;
