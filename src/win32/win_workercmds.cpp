#include "win_workercmds.h"
#include <tl/jobqueue/jobqueue_all.h>
#include <universal/com_workercmds.h>
#include <universal/q_shared.h>
#include <qcommon/threads.h>
#include <qcommon/common.h>
#include <gfx_d3d/r_dvars.h>
#include <algorithm>

int __cdecl SetupWorkerThread(jqBatch *batch)
{
    Sys_InitThread(batch->ParamData[0] + 2);
    return 0;
}

volatile unsigned int nuge_physicsLimit = 1;
jqModule nuge_physicsModule =
{
    .Name = "nuge_physics",
    .Type = JQ_WORKER_GENERIC,
    .Code = nuge_physicsCallback,
};
jqWorkerCmd nuge_physicsWorkerCmd = { &nuge_physicsModule, sizeof(jqBatch), 0, 0, &nuge_physicsLimit, NULL, 0u};

// no workerCmd
jqModule setup_worker_threadsModule =
{
    .Name = "setup_worker_threads",
    .Type = JQ_WORKER_GENERIC,
    .Code = SetupWorkerThread,
};

int __cdecl nuge_physicsCallback(jqBatch *batch)
{
    const char *Name; // [esp+8h] [ebp-14h]
    jqBatch *realbatch; // [esp+18h] [ebp-4h]

    realbatch = (jqBatch *)jqLockData(batch);
    Name = va("nuge_physics: %s", realbatch->Module->Name);
    PROF_SCOPED_RUNTIME_NAME(Name);
    realbatch->Module->Code(realbatch);
    jqUnlockData(batch);
    return 0;
}

void __cdecl IW_task_manager_add_batch(jqBatch *ptr)
{
    if ( !ptr->Module
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_workercmds.cpp", 51, 0, "%s", "ptr->Module") )
    {
        __debugbreak();
    }
    Sys_AddWorkerCmdInternal(&nuge_physicsWorkerCmd, (unsigned __int8 *)ptr, 0);
}

void __cdecl IW_task_manager_flush()
{
    Sys_WaitWorkerCmdInternal(&nuge_physicsWorkerCmd);
}

void __cdecl R_InitWorkerCmds()
{
    unsigned int workerCount = r_smp_worker_threads ? r_smp_worker_threads->current.integer : Sys_GetDefaultWorkerThreadsCount();
    workerCount = std::clamp(workerCount, 1u, (unsigned int)(THREAD_CONTEXT_WORKER7 - THREAD_CONTEXT_WORKER0 + 1));

    jqInit();
    // Processor bit 0 is the main thread; worker threads take bits 1..workerCount.
    jqEnableWorkers(((1u << workerCount) - 1) << 1);
    jqSetBatchDataHeapSize(0x4000u, 0x10u);
    jqStart();

    //jqBatch::jqBatch(&batch);
    jqBatch batch; // [esp+0h] [ebp-80h] BYREF
    batch.Module = &setup_worker_threadsModule;// `anonymous namespace'::setup_worker_threadsModule;

    for (unsigned int workerIndex = 0; workerIndex < workerCount; ++workerIndex)
    {
        batch.ParamData[0] = workerIndex;
        jqAddBatch(&batch, jqGetWorkerQueue(2 << workerIndex));
    }

    jqFlush(0, 0);
    Sys_InitWorkerThreadContext(workerCount);
    Sys_WorkerCmdInit();

    Com_Printf(16, "Started %u job worker threads\n", workerCount);
}

void __cdecl R_InitWorkerThreads()
{
    R_InitWorkerCmds();
}

