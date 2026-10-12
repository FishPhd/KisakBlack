// Stub implementation of the subset of the Bink API used by gfx_d3d/r_cinematic.cpp.
//
// Built only when KISAK_NO_BINK is defined (CMake option KISAK_NO_BINK, default ON). It removes the
// dependency on binkw32.dll: BinkOpen always fails, so no cinematic ever plays. r_cinematic.cpp
// short-circuits R_Cinematic_StartPlayback_Now() under KISAK_NO_BINK and marks the requested movie
// as finished so menus / loading screens that wait for a video keep progressing.
//
// __RADINEXE__ is defined by CMake so bink.h declares these functions without dllimport.

#ifdef KISAK_NO_BINK

#include <windows.h>
#include <string.h>
#include "bink.h"

static char s_binkNoError[1] = { 0 };

char PTR4* RADEXPLINK BinkGetError(void)
{
    return s_binkNoError;
}

void RADEXPLINK BinkSetError(const char PTR4* err)
{
    (void)err;
}

HBINK RADEXPLINK BinkOpen(const char PTR4* name, U32 flags)
{
    (void)name;
    (void)flags;
    return 0;
}

void RADEXPLINK BinkClose(HBINK bnk)
{
    (void)bnk;
}

void RADEXPLINK BinkSetMemory(BINKMEMALLOC a, BINKMEMFREE f)
{
    (void)a;
    (void)f;
}

S32 RADEXPLINK BinkSetSoundSystem(BINKSNDSYSOPEN open, UINTa param)
{
    (void)open;
    (void)param;
    return 0;
}

BINKSNDOPEN RADEXPLINK BinkOpenDirectSound(UINTa param)
{
    (void)param;
    return 0;
}

void RADEXPLINK BinkSetSoundTrack(U32 total_tracks, U32 PTR4* tracks)
{
    (void)total_tracks;
    (void)tracks;
}

void RADEXPLINK BinkSetIOSize(U32 iosize)
{
    (void)iosize;
}

S32 RADEXPLINK BinkControlBackgroundIO(HBINK bink, U32 control)
{
    (void)bink;
    (void)control;
    return 0;
}

S32 RADEXPLINK BinkPause(HBINK bnk, S32 pause)
{
    (void)bnk;
    (void)pause;
    return 0;
}

S32 RADEXPLINK BinkDoFrame(HBINK bnk)
{
    (void)bnk;
    return 0;
}

void RADEXPLINK BinkNextFrame(HBINK bnk)
{
    (void)bnk;
}

S32 RADEXPLINK BinkWait(HBINK bnk)
{
    (void)bnk;
    return 0;
}

S32 RADEXPLINK BinkShouldSkip(HBINK bink)
{
    (void)bink;
    return 0;
}

void RADEXPLINK BinkGetRealtime(HBINK bink, BINKREALTIME PTR4* run, U32 frames)
{
    (void)bink;
    (void)frames;
    if (run)
        memset(run, 0, sizeof(*run));
}

void RADEXPLINK BinkSetVolume(HBINK bnk, U32 trackid, S32 volume)
{
    (void)bnk;
    (void)trackid;
    (void)volume;
}

void RADEXPLINK BinkGetFrameBuffersInfo(HBINK bink, BINKFRAMEBUFFERS* fbset)
{
    (void)bink;
    (void)fbset;
}

void RADEXPLINK BinkRegisterFrameBuffers(HBINK bink, BINKFRAMEBUFFERS* fbset)
{
    (void)bink;
    (void)fbset;
}

#endif // KISAK_NO_BINK
