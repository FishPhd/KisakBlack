// Web (Emscripten) replacement for src/win32/win_main.cpp, win_shared.cpp, win_syscon.cpp and the
// system-info half of win_configure.cpp.
//
// The browser build is a headless dedicated server. main() runs on a worker thread
// (-sPROXY_TO_PTHREAD), so the blocking Com_Frame loop is the same loop WinMain runs on desktop.
//
// Console: stdout/stderr go to the page through Module.print/printErr. Console commands come back
// through Kisak_ConsoleInput(), which may be called from the browser's main thread while the engine
// thread is running, so the queue is lock-protected.

#include <win32/win_main.h>
#include <server_mp/sv_main_mp.h>
#include <game_mp/g_main_mp.h>
#include <win32/win_net.h>
#include <win32/win_shared.h>
#include <win32/win_syscon.h>
#include <win32/win_wndproc.h>
#include <win32/win_localize.h>
#include <win32/win_configure.h>
#include <win32/win_input.h>
#include <server_mp/sv_main_mp.h>
#include <game_mp/g_main_mp.h>
#include <qcommon/threads.h>
#include <qcommon/tl_support.h>
#include <qcommon/mem_track.h>
#include <qcommon/sys_futex.h>
#include <client/cl_keys.h>
#include <client_mp/cl_main_mp.h>
#include <client/con_channels.h>
#include <clientscript/cscr_stringlist.h>
#include <universal/com_memory.h>
#include <universal/physicalmemory.h>
#include <universal/q_parse.h>
#include <universal/timing.h>
#include <tl/tl_system.h>
#include <mjpeg/mjpeg.h>
#include <mjpeg/avi.h>

#include "web_assets.h"
#include "web_graphics.h"
#include <server/sv_game.h>
#include <ui/ui_main.h>
#include <ui/ui_main_pc.h>
#include <ui/ui_utils.h>
#include <client/client.h>
#include <client/screen_placement.h>
#include <sstream>
#include <iomanip>
#include <atomic>
#include <universal/com_expressions_eval.h>
#include <cmath>
#include <ui/ui_feeders.h>

static void Web_StatusTick();
static void Web_PrefetchTick();
static void Web_HintOnce();
static void Web_ObserveTick();
static std::atomic<bool> s_agentTelemetry{false};
static const dvar_t *web_status;

#include <emscripten.h>
#include <emscripten/threading.h>
#include <emscripten/heap.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <unistd.h>

// ---------------------------------------------------------------------------------------------
// Globals the Win32 build keeps in win_main.cpp / win_wndproc.cpp
// ---------------------------------------------------------------------------------------------

const dvar_t *sys_configureGHz;
const dvar_t *sys_sysMB;
const dvar_t *sys_gpu;
const dvar_t *sys_configSum;
const dvar_t *sys_SSE;

bool g_allowMature = true;

int s_nosnd;
SysInfo sys_info;
char sys_exitCmdLine[1024];
sysEvent_t eventQue[256];
int eventHead;
int eventTail;
char sys_processSemaphoreFile[260];
char sys_cmdline[1024];
char g_open_automate_benchmark[260];

WinVars_t g_wv;

// ---------------------------------------------------------------------------------------------
// Console (win_syscon.cpp)
// ---------------------------------------------------------------------------------------------

namespace
{
    std::mutex s_consoleMutex;
    std::deque<std::string> s_consoleLines;
    char s_consoleReturned[512];
    char s_errorText[512];
}

// Called by the page (ccall/cwrap) with one console line, without the trailing newline.
extern "C" EMSCRIPTEN_KEEPALIVE void Kisak_ConsoleInput(const char *line)
{
    if (!line)
        return;
    std::lock_guard<std::mutex> lock(s_consoleMutex);
    if (s_consoleLines.size() < 256)
        s_consoleLines.emplace_back(line);
}

char *__cdecl Sys_ConsoleInput()
{
    std::lock_guard<std::mutex> lock(s_consoleMutex);
    if (s_consoleLines.empty())
        return nullptr;
    strncpy(s_consoleReturned, s_consoleLines.front().c_str(), sizeof(s_consoleReturned) - 1);
    s_consoleReturned[sizeof(s_consoleReturned) - 1] = 0;
    s_consoleLines.pop_front();
    return s_consoleReturned;
}

char *__cdecl Conbuf_CleanText(const char *source, char *target, int sizeofTarget)
{
    int out = 0;
    for (; *source && out < sizeofTarget - 1; ++source)
    {
        if (*source == '^' && source[1] >= '0' && source[1] <= '9')
        {
            ++source; // colour code
            continue;
        }
        if (*source == '\r')
            continue;
        target[out++] = *source;
    }
    target[out] = 0;
    return target;
}

void __cdecl Conbuf_AppendText(char *pMsg)
{
    if (!pMsg || !*pMsg)
        return;
    char clean[16384];
    Conbuf_CleanText(pMsg, clean, sizeof(clean));
    fputs(clean, stdout);
}

void __cdecl Conbuf_AppendTextInMainThread(char *msg)
{
    Conbuf_AppendText(msg);
}

void __cdecl Sys_SetErrorText(const char *buf)
{
    I_strncpyz(s_errorText, buf, sizeof(s_errorText));
    fprintf(stderr, "ERROR: %s\n", buf);
}

void __cdecl Sys_CreateConsole(HINSTANCE__ *) {}
void __cdecl Sys_ShowConsole() {}
void __cdecl Sys_DestroyConsole() {}

// ---------------------------------------------------------------------------------------------
// Time (win_shared.cpp)
// ---------------------------------------------------------------------------------------------

int sys_timeBase;

unsigned int __cdecl Sys_MillisecondsRaw()
{
    return timeGetTime();
}

unsigned int __cdecl Sys_Milliseconds()
{
    static int initialized = 0;
    if (!initialized)
    {
        sys_timeBase = timeGetTime();
        initialized = 1;
    }
    return timeGetTime() - sys_timeBase;
}

// The x87 `fistp` the Win32 build uses rounds to nearest even; nearbyintf does the same in the
// default rounding mode, so snapped origins match the desktop server.
void Sys_SnapVector(float *v)
{
    v[0] = nearbyintf(v[0]);
    v[1] = nearbyintf(v[1]);
    v[2] = nearbyintf(v[2]);
}

// ---------------------------------------------------------------------------------------------
// Fatal errors and exit
// ---------------------------------------------------------------------------------------------

void __cdecl Sys_DirectXFatalError()
{
    fprintf(stderr, "Sys_DirectXFatalError\n");
    exit(-1);
}

void __cdecl Sys_OutOfMemErrorInternal(const char *filename, int line)
{
    Sys_EnterCriticalSection(CRITSECT_FATAL_ERROR);
    fprintf(stderr, "Out of memory: filename '%s', line %d\n", filename, line);
    exit(-1);
}

void __cdecl Sys_QuitAndStartProcess(const char *exeName)
{
    I_strncpyz(sys_exitCmdLine, exeName, sizeof(sys_exitCmdLine));
    Cbuf_AddText(0, "quit\n");
}

void __cdecl Sys_OpenURL(const char *, int) {}

void Sys_Error(char *error, ...)
{
    char string[4100];
    va_list va;

    va_start(va, error);
    Sys_EnterCriticalSection(CRITSECT_COM_ERROR);
    com_errorEntered = 1;
    vsnprintf(string, 0x1000u, error, va);
    va_end(va);

    Sys_SetErrorText(string);
    fflush(stdout);
    fflush(stderr);
    exit(1);
}

void __cdecl Sys_Quit()
{
    Sys_EnterCriticalSection(CRITSECT_COM_ERROR);
    CL_ShutdownAll();
    IN_Shutdown();
    Key_Shutdown();
    Sys_DestroyConsole();
    Sys_NormalExit();
    Win_ShutdownLocalization();
    Dvar_Shutdown();
    Cmd_Shutdown();
    Sys_ShutdownEvents();
    SL_Shutdown(SCRIPTINSTANCE_SERVER);
    SL_Shutdown(SCRIPTINSTANCE_CLIENT);
    if (!com_errorEntered)
        track_shutdown(0);
    Con_ShutdownChannels();
    fflush(stdout);
    fflush(stderr);
    exit(0);
}

void Sys_SpawnQuitProcess() {}

void __cdecl Sys_NormalExit() {}

void Sys_NoFreeFilesError()
{
    Sys_Error((char *)"Out of file handles");
}

int __cdecl Sys_CheckCrashOrRerun()
{
    return 1;
}

int __cdecl Sys_IsGameProcess(unsigned int)
{
    return 0;
}

int Sys_GetSemaphoreFileName()
{
    sys_processSemaphoreFile[0] = 0;
    return 0;
}

// Output goes to stdout (the page's console). OutputDebugString has no destination here.
void __cdecl Sys_Print(char *msg)
{
    Conbuf_AppendTextInMainThread(msg);
}

char *__cdecl Sys_GetClipboardData()
{
    return nullptr;
}

// ---------------------------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------------------------

void __cdecl Sys_QueEvent(unsigned int time, sysEventType_t type, int value, int value2, int ptrLength, void *ptr)
{
    Sys_EnterCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
    sysEvent_t *ev = &eventQue[(unsigned __int8)eventHead];
    if (eventHead - eventTail >= 256)
    {
        Com_Printf(16, "Sys_QueEvent: overflow\n");
        if (ev->evPtr)
            Z_Free((char *)ev->evPtr, 11);
        ++eventTail;
    }
    ++eventHead;
    if (!time)
        time = Sys_Milliseconds();
    ev->evTime = time;
    ev->evType = type;
    ev->evValue = value;
    ev->evValue2 = value2;
    ev->evPtrLength = ptrLength;
    ev->evPtr = ptr;
    Sys_LeaveCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
}

void Sys_ShutdownEvents()
{
    Sys_EnterCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
    while (eventHead > eventTail)
    {
        sysEvent_t *ev = &eventQue[(unsigned __int8)eventTail++];
        if (ev->evPtr)
            Z_Free((char *)ev->evPtr, 11);
    }
    Sys_LeaveCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
}

void __cdecl Sys_LoadingKeepAlive() {}

sysEvent_t *__cdecl Win_GetEvent(sysEvent_t *result)
{
    Sys_EnterCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
    if (eventHead <= eventTail)
    {
        // No window messages here: console lines from the page are the only input source.
        char *s = Sys_ConsoleInput();
        if (s)
        {
            const int len = (int)strlen(s) + 1; // includes the terminator, like win_main.cpp
            char *b = (char *)Com_AllocEvent(len);
            I_strncpyz(b, s, len);
            Sys_QueEvent(0, SE_CONSOLE, 0, 0, len, b);
        }
    }

    sysEvent_t ev;
    if (eventHead <= eventTail)
    {
        memset(&ev, 0, sizeof(ev));
        ev.evTime = Sys_Milliseconds();
    }
    else
    {
        ev = eventQue[(unsigned __int8)eventTail++];
    }
    Sys_LeaveCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
    *result = ev;
    return result;
}

sysEvent_t *__cdecl Sys_GetEvent(sysEvent_t *result)
{
    return Win_GetEvent(result);
}

void __cdecl Sys_Mjpeg() {}
void __cdecl Sys_MjpegClose() {}

// ---------------------------------------------------------------------------------------------
// System info (win_configure.cpp)
// ---------------------------------------------------------------------------------------------

void __cdecl Sys_GetInfo(SysInfo *info)
{
    memcpy(info, &sys_info, sizeof(SysInfo));
}

bool __cdecl Sys_HasConfigureChecksumChanged(int checksum)
{
    Sys_RegisterInfoDvars();
    if (G_OnlyConnectingPaths())
        return false;
    if (!sys_configSum->current.integer || sys_configSum->current.integer != checksum)
        Dvar_SetInt((dvar_s *)sys_configSum, checksum);
    return false;
}

bool __cdecl Sys_ShouldUpdateForConfigChange() { return false; }
bool __cdecl Sys_ShouldUpdateForInfoChange() { return false; }

void Sys_RegisterInfoDvars()
{
    sys_configureGHz = _Dvar_RegisterFloat(
        "sys_configureGHz", 0.0f, -3.4028235e38f, 3.4028235e38f, 0x11u,
        "Normalized total CPU power, based on cpu type, count, and speed; used in autoconfigure");
    web_status = _Dvar_RegisterInt("web_status", 30, 0, 3600, 0, "Web build: seconds between status lines on the console (0 = off)");
    sys_sysMB = _Dvar_RegisterInt("sys_sysMB", 0, 0x80000000, 0x7FFFFFFF, 0x11u, "Physical memory in the system");
    sys_gpu = _Dvar_RegisterString("sys_gpu", (char *)"", 0x11u, "GPU description");
    sys_configSum = _Dvar_RegisterInt("sys_configSum", 0, 0x80000000, 0x7FFFFFFF, 0x11u, "Configuration checksum");
    sys_SSE = _Dvar_RegisterBool("sys_SSE", sys_info.SSE, 0x40u, "Operating system allows Streaming SIMD Extensions");
    _Dvar_RegisterFloat("sys_cpuGHz", (float)sys_info.cpuGHz, -3.4028235e38f, 3.4028235e38f, 0x40u, "Measured CPU speed");
    _Dvar_RegisterString("sys_cpuName", sys_info.cpuName, 0x40u, "CPU name description");
}

bool __cdecl Sys_HasInfoChanged()
{
    Sys_RegisterInfoDvars();
    return sys_configureGHz->current.value > sys_info.configureGHz * 1.1
        || sys_configureGHz->current.value < sys_info.configureGHz * 0.9
        || sys_sysMB->current.integer > sys_info.sysMB + 32
        || sys_sysMB->current.integer < sys_info.sysMB - 32
        || strcmp(sys_gpu->current.string, sys_info.gpuDescription);
}

void __cdecl Sys_ArchiveInfo(int checksum)
{
    Sys_RegisterInfoDvars();
    Dvar_SetFloat((dvar_s *)sys_configureGHz, (float)sys_info.configureGHz);
    Dvar_SetInt((dvar_s *)sys_sysMB, sys_info.sysMB);
    Dvar_SetString((dvar_s *)sys_gpu, sys_info.gpuDescription);
    Dvar_SetInt((dvar_s *)sys_configSum, checksum);
}

int __cdecl Sys_SystemMemoryMB()
{
    return 1024; // the wasm linear memory is the whole budget
}

void __cdecl Sys_DetectVideoCard(int descLimit, char *description)
{
    I_strncpyz(description, "None (headless)", descLimit);
}

void __cdecl Sys_DetectCpuVendorAndName(char *vendor, char *name)
{
    I_strncpyz(vendor, "WebAssembly", 13);
    I_strncpyz(name, "WebAssembly (browser)", 49);
}

bool __cdecl Sys_SupportsSSE()
{
    return true; // SSE intrinsics map to wasm SIMD128
}

void __cdecl Sys_SetAutoConfigureGHz(SysInfo *sysInfo)
{
    const double scale = sysInfo->physicalCpuCount <= 1 ? 1.0 : sysInfo->physicalCpuCount == 2 ? 1.75 : 2.0;
    sysInfo->configureGHz = sysInfo->cpuGHz * scale;
}

void __cdecl Sys_GetPhysicalCpuCount(SysInfo *sysInfo)
{
    sysInfo->physicalCpuCount = sysInfo->logicalCpuCount;
}

double __cdecl Sys_BenchmarkGHz()
{
    return 2.0;
}

void Sys_FindInfo()
{
    sys_info.logicalCpuCount = (int)Sys_GetCpuCount();
    sys_info.physicalCpuCount = sys_info.logicalCpuCount;
    sys_info.cpuGHz = 2.0;
    sys_info.sysMB = Sys_SystemMemoryMB();
    Sys_DetectVideoCard(512, sys_info.gpuDescription);
    sys_info.SSE = Sys_SupportsSSE();
    Sys_DetectCpuVendorAndName(sys_info.cpuVendor, sys_info.cpuName);
    Sys_SetAutoConfigureGHz(&sys_info);
}

bool __cdecl PC_StartWithNoSounds()
{
    // Desktop WinMain sets this from the command line; dedicated servers still skip SND_Init
    // through IsDedicatedServer() inside SND_ShouldInit.
    s_nosnd = I_stristr(sys_cmdline, "nosnd") != nullptr;
    return s_nosnd != 0;
}

// ---------------------------------------------------------------------------------------------
// Sys_Init, startup helpers and main()
// ---------------------------------------------------------------------------------------------

cmd_function_s Sys_In_Restart_f_VAR;
cmd_function_s Sys_Net_Restart_f_VAR;
cmd_function_s Sys_Listen_f_VAR;
cmd_function_s Sys_Connect_f_VAR;

void __cdecl Sys_Init()
{
    Cmd_AddCommandInternal("in_restart", Sys_In_Restart_f, &Sys_In_Restart_f_VAR);
    Cmd_AddCommandInternal("net_restart", Sys_Net_Restart_f, &Sys_Net_Restart_f_VAR);

    Com_Printf(16, "CPU name is \"%s\"\n", sys_info.cpuName);
    Com_Printf(16, "%i logical CPU%s reported\n", sys_info.logicalCpuCount, sys_info.logicalCpuCount == 1 ? "" : "s");
    Com_Printf(16, "System memory is %i MB (wasm linear memory)\n", sys_info.sysMB);
    Com_Printf(16, "\n");
    IN_Init();
}

void __cdecl Sys_In_Restart_f()
{
    IN_Shutdown();
    IN_Init();
}

void __cdecl Sys_Net_Restart_f()
{
    NET_Restart();
}

void PrintWorkingDir()
{
    char cwd[260];
    if (getcwd(cwd, sizeof(cwd)))
        Com_Printf(16, "Working directory: %s\n", cwd);
}

char __cdecl CheckRemoteSession()
{
    return 0;
}

bool __cdecl StartingDedicatedServer(char *cmdline)
{
    const char *v1 = strstr(cmdline, "dedicated");
    if (!v1)
        return false;
    const char *p = v1 + strlen("dedicated");
    while (*p == ' ')
        ++p;
    if (*p == '"')
        ++p;
    return atoi(p) != 0;
}

int __stdcall WinMain(HINSTANCE__ *, HINSTANCE__ *, char *lpCmdLine, int)
{
    g_allowMature = 1;
    Sys_InitializeCriticalSections();
    Sys_InitMainThread();
    PMem_Init();
    track_init();
    Win_InitLocalization();

    s_nosnd = I_stristr(lpCmdLine, "nosnd") != nullptr;
    Com_InitParse();
    Dvar_Init();
    Web_RegisterGraphicsDvars();
    InitTiming();
    Sys_FindInfo();
    I_strncpyz(sys_cmdline, lpCmdLine, sizeof(sys_cmdline));
    Sys_Milliseconds();
    Sys_SetupTLCallbacks(0x900000);

    MAIN_THREAD_ASYNC_EM_ASM({ if (Module.kisakBootStage) Module.kisakBootStage("engineInit"); });
    Com_Init(sys_cmdline);
    MAIN_THREAD_ASYNC_EM_ASM({ if (Module.kisakBootStage) Module.kisakBootStage("engineReady"); });
    if (!IsDedicatedServer())
        Cbuf_AddText(0, "readStats\n");
    PrintWorkingDir();

    for (;;)
    {
        if (IsDedicatedServer())
        {
            // Sleep until a packet arrives or the next server tick is due, whichever is first.
            const int msecUntilTick = SV_GetMsecUntilNextFrame();
            unsigned int idleMsec = 5;
            if (msecUntilTick >= 0 && msecUntilTick < 5)
                idleMsec = msecUntilTick > 0 ? msecUntilTick : 1;
            NET_SleepUntilPacket(idleMsec);
        }

        Com_Frame();

        Web_ObserveTick();
        Web_StatusTick();
        Web_PrefetchTick();
        Web_HintOnce();
    }
}

// Sample on the game thread: page-side reads of live UI pointers race menu rebuilds.
extern "C" EMSCRIPTEN_KEEPALIVE void Kisak_EnableAgentTelemetry(int enabled)
{
    s_agentTelemetry.store(enabled != 0);
}

static std::string Web_JsonString(const char *s)
{
    std::ostringstream out;
    out << '"';
    for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; ++p)
    {
        if (*p == '"' || *p == '\\') out << '\\' << *p;
        else if (*p < 32 || *p >= 127)
            out << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << (unsigned)*p << std::dec;
        else out << *p;
    }
    out << '"';
    return out.str();
}

static void Web_ObserveTick()
{
    if (IsDedicatedServer()) return;
    static unsigned int last;
    const unsigned int now = Sys_Milliseconds();
    if (last && now - last < 100) return;
    last = now;
    const bool agent = s_agentTelemetry.load();
    UiContext *dc = &UI_UIContext_GetInfo(0)->uiDC;
    clientActive_t *cl = CL_GetLocalClientGlobals(0);
    const int connection = CL_GetLocalClientConnectionState(0);
    const bool ui = (clientUIActives[0].keyCatchers & 0x10) != 0;
    std::ostringstream out;
    out << "{\"connection\":" << connection << ",\"serverTime\":" << (cl ? cl->serverTime : 0)
        << ",\"uiActive\":" << (ui ? "true" : "false") << ",\"menus\":[";
    bool firstMenu = true, interactive = false, frontendReady = false;
    for (int m = 0; m < dc->openMenuCount; ++m)
    {
        menuDef_t *menu = dc->menuStack[m].menu;
        if (!menu) continue;
        if (!firstMenu) out << ',';
        firstMenu = false;
        out << "{\"name\":" << Web_JsonString(menu->window.name) << ",\"items\":[";
        bool firstItem = true;
        for (int i = 0; i < menu->itemCount; ++i)
        {
            itemDef_s *it = menu->items[i];
            if (!it || !Item_IsFocusDefType(it) || !Item_IsVisible(0, dc->contextIndex, it)
                || ((it->dvarFlags & 3) && !Item_EnableShowViaDvar(it, 1))) continue;
            interactive = true;
            if (menu->window.name && !strcmp(menu->window.name, "main_text")) frontendReady = true;
            if (!agent) continue;
            rectDef_s r = it->window.rect;
            ScrPlace_ApplyRect(&scrPlaceView[0], &r.x, &r.y, &r.w, &r.h, r.horzAlign, r.vertAlign);
            const auto label = [](itemDef_s *item) -> const char * {
                if (!Item_IsTextDefType(item) || !item->typeData.textDef) return "";
                textDef_s *td = item->typeData.textDef;
                const char *text = td->text;
                if (!text && td->textExpData && td->textExpData->textExp.filename)
                    text = GetExpressionResultString(0, item, &td->textExpData->textExp);
                if (!text) return "";
                return *text == '@' ? UI_SafeTranslateString(text + 1) : text;
            };
            std::string text = label(it);
            // Shipped menus often draw the label in a separate item under a blank button.
            if (text.empty()) for (int j = 0; j < menu->itemCount; ++j)
            {
                itemDef_s *other = menu->items[j];
                if (!other || fabsf(other->window.rect.x - it->window.rect.x) > 3
                    || fabsf(other->window.rect.y - it->window.rect.y) > 1
                    || !Item_IsVisible(0, dc->contextIndex, other)) continue;
                const char *candidate = label(other);
                if (*candidate) { text = candidate; break; }
            }
            if (!firstItem) out << ',';
            firstItem = false;
            out << "{\"index\":" << i << ",\"name\":" << Web_JsonString(it->window.name)
                << ",\"text\":" << Web_JsonString(text.c_str()) << ",\"rect\":[" << r.x << ',' << r.y << ',' << r.w << ',' << r.h << ']';
            if (it->dataType == 4)
            {
                listBoxDef_s *list = Item_GetListBoxDef(it);
                const int feeder = (int)list->special;
                if (feeder == 1 || feeder == 4)
                {
                    const int count = UI_FeederCount(0, dc->contextIndex, list->special, list);
                    out << ",\"list\":{\"cursor\":" << list->cursorPos[0] << ",\"rows\":[";
                    for (int row = 0; row < count && row < 128; ++row)
                    {
                        int actual = row;
                        if (feeder == 4) UI_SelectedMap(row, &actual);
                        if (row) out << ',';
                        out << "{\"index\":" << row << ",\"map\":"
                            << Web_JsonString(actual >= 0 && actual < sharedUiInfo.mapCount ? sharedUiInfo.mapList[actual].mapLoadName : "") << '}';
                    }
                    out << "]}";
                }
            }
            out << '}';
        }
        out << "]}";
    }
    out << "],\"menuInteractive\":" << (ui && interactive ? "true" : "false")
        << ",\"frontendReady\":" << (ui && frontendReady ? "true" : "false");
    if (agent && cl)
        out << ",\"origin\":[" << cl->cgameOrigin[0] << ',' << cl->cgameOrigin[1] << ',' << cl->cgameOrigin[2]
            << "],\"angles\":[" << cl->viewangles[0] << ',' << cl->viewangles[1] << ',' << cl->viewangles[2] << ']'
            << ",\"inputReady\":" << (connection == CA_ACTIVE && !(cl->snap.ps.pm_flags & 0x800) && cl->snap.ps.weaponstate != 35 ? "true" : "false");
    out << '}';
    char *copy = strdup(out.str().c_str());
    if (!copy) return;
    MAIN_THREAD_ASYNC_EM_ASM({
        try { if (Module.kisakObserve) Module.kisakObserve(JSON.parse(UTF8ToString($0))); }
        finally { _free($0); }
    }, copy);
}

// ---------------------------------------------------------------------------------------------
// Next-map prefetch: once the server is on a map, ask the page to start fetching the rotation's next
// map in the background, so the map change finds its fastfiles already in OPFS. (Per-map loading is
// what keeps the transfer to boot set + the maps actually played.)
// ---------------------------------------------------------------------------------------------

// First "map <name>" in a rotation string ("gametype tdm map mp_nuked gametype dom map mp_cairo").
static bool Web_FirstRotationMap(const char *rotation, char *out, size_t cap)
{
    if (!rotation)
        return false;
    const char *p = rotation;
    bool wantName = false;
    while (*p)
    {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
            ++p;
        const char *start = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
            ++p;
        const size_t len = (size_t)(p - start);
        if (!len)
            break;
        if (wantName)
        {
            snprintf(out, cap, "%.*s", (int)len, start);
            return true;
        }
        wantName = len == 3 && !I_strnicmp(start, "map", 3);
    }
    return false;
}

static void Web_HintOnce()
{
    static bool sent;
    if (sent || IsDedicatedServer())
        return;
    if (!com_sv_running || !com_sv_running->current.enabled || svs.time < 3000)
        return;
    sent = true;
    SV_GameSendServerCommand(-1, SV_CMD_CAN_IGNORE, "g \"F1 debug    F2 console    F11 fullscreen    Esc menu\"");
}

// Remember recent names so opening the map list and the ui_mapname poll do not ask twice.
static void Web_PrefetchMapName(const char *name)
{
    if (!name || I_strnicmp(name, "mp_", 3))
        return;
    static char recent[16][64];
    static int next;
    for (int i = 0; i < 16; ++i)
    {
        if (recent[i][0] && !I_stricmp(recent[i], name))
            return;
    }
    char *slot = recent[next++ & 15];
    I_strncpyz(slot, name, sizeof(recent[0]));
    // Manifest map fastfiles only. Menu images stay on the loose /pack/ path.
    Web_PrefetchMap(name);
}

static bool Web_IsMapListMenu(menuDef_t *menu)
{
    if (!menu)
        return false;
    const char *name = menu->window.name;
    if (name && I_stristr(name, "map") && !I_stristr(name, "heatmap") && !I_stristr(name, "loading"))
        return true;
    if (!menu->items || menu->itemCount <= 0 || menu->itemCount > 4096)
        return false;
    for (int i = 0; i < menu->itemCount; ++i)
    {
        itemDef_s *item = menu->items[i];
        if (!item || item->dataType != 4)
            continue;
        listBoxDef_s *list = Item_GetListBoxDef(item);
        if (!list)
            continue;
        const int feeder = (int)list->special;
        if (feeder == 1 || feeder == 4)
            return true;
    }
    return false;
}

static void Web_PrefetchMapListMenu(menuDef_t *menu)
{
    const dvar_t *uiMap = Dvar_FindVar("ui_mapname");
    if (uiMap)
        Web_PrefetchMapName(uiMap->current.string);
    const dvar_t *preview = Dvar_FindVar("ui_preview");
    if (preview)
        Web_PrefetchMapName(preview->current.string);
    if (!menu->items || menu->itemCount <= 0 || menu->itemCount > 4096 || sharedUiInfo.mapCount <= 0)
        return;
    for (int i = 0; i < menu->itemCount; ++i)
    {
        itemDef_s *item = menu->items[i];
        if (!item || item->dataType != 4)
            continue;
        listBoxDef_s *list = Item_GetListBoxDef(item);
        if (!list)
            continue;
        const int feeder = (int)list->special;
        int cursor = list->cursorPos[0];
        if (cursor < 0)
            cursor = 0;
        const char *load = nullptr;
        if (feeder == 1)
        {
            if (cursor < sharedUiInfo.mapCount)
                load = sharedUiInfo.mapList[cursor].mapLoadName;
        }
        else if (feeder == 4)
        {
            int actual = 0;
            const char *label = UI_SelectedMap(cursor, &actual);
            if (label && label[0] && actual >= 0 && actual < sharedUiInfo.mapCount)
                load = sharedUiInfo.mapList[actual].mapLoadName;
        }
        Web_PrefetchMapName(load);
    }
}

// The map picker writes ui_preview / the list cursor as soon as it opens. The ui_mapname poll
// below still covers the lobby, which shows the name before that menu exists.
static void Web_PrefetchOpenMapList()
{
    if (IsDedicatedServer())
        return;
    uiInfo_s *info = UI_UIContext_GetInfo(0);
    if (!info)
        return;
    UiContext *dc = &info->uiDC;
    if (dc->openMenuCount <= 0 || dc->openMenuCount > 16)
        return;
    for (int i = 0; i < dc->openMenuCount; ++i)
    {
        menuDef_t *menu = dc->menuStack[i].menu;
        if (Web_IsMapListMenu(menu))
            Web_PrefetchMapListMenu(menu);
    }
}

static void Web_PrefetchTick()
{
    Web_PrefetchOpenMapList();

    static unsigned int s_lastCheck;
    static char s_lastMap[64];
    const unsigned int now = Sys_Milliseconds();
    if (now - s_lastCheck < 2000u)
        return;
    s_lastCheck = now;
    // While still in the frontend, the private-match lobby writes ui_mapname before the server exists.
    if (!com_sv_running || !com_sv_running->current.enabled)
    {
        const dvar_t *uiMap = Dvar_FindVar("ui_mapname");
        const char *picked = uiMap ? uiMap->current.string : nullptr;
        if (picked && !I_strnicmp(picked, "mp_", 3) && I_stricmp(picked, s_lastMap))
        {
            I_strncpyz(s_lastMap, picked, sizeof(s_lastMap));
            Web_PrefetchMapName(picked);
        }
        return;
    }

    const char *cur = Dvar_GetString("mapname");
    if (!cur || !*cur || !strcmp(cur, s_lastMap))
        return;
    I_strncpyz(s_lastMap, cur, sizeof(s_lastMap));

    // The engine consumes sv_mapRotationCurrent as it advances, so its head is the map after this one;
    // when it has run dry the rotation wraps to the start of sv_mapRotation.
    char next[64];
    if (!Web_FirstRotationMap(Dvar_GetString("sv_mapRotationCurrent"), next, sizeof(next)) &&
        !Web_FirstRotationMap(Dvar_GetString("sv_mapRotation"), next, sizeof(next)))
        return;
    if (!I_stricmp(next, cur))
        return;
    Web_PrefetchMapName(next);
}

// ---------------------------------------------------------------------------------------------
// Periodic status line (web_status seconds, 0 = off): uptime, server time, players and the heap
// high-water mark, so a long run shows memory growth without attaching a profiler.
// ---------------------------------------------------------------------------------------------

static void Web_StatusTick()
{
    static unsigned int s_lastMsec;
    static unsigned int s_startMsec;
    const unsigned int now = Sys_Milliseconds();
    if (!s_startMsec)
        s_startMsec = now ? now : 1;
    if (!web_status || web_status->current.integer <= 0)
        return;
    if (now - s_lastMsec < (unsigned int)web_status->current.integer * 1000u)
        return;
    s_lastMsec = now;

    int humans = 0, bots = 0;
    if (com_sv_running && com_sv_running->current.enabled)
    {
        for (int i = 0; i < com_maxclients->current.integer; ++i)
        {
            if (svs.clients[i].header.state < CS_CONNECTED)
                continue;
            if (svs.clients[i].header.netchan.remoteAddress.type == NA_BOT)
                ++bots;
            else
                ++humans;
        }
    }
    const unsigned int heapMB = (unsigned int)(((size_t)sbrk(0)) >> 20);
    const unsigned int totalMB = (unsigned int)(emscripten_get_heap_size() >> 20);
    fprintf(stderr, "web: status up=%us svs.time=%d clients=%d bots=%d heap-high-water=%uMB of %uMB\n",
            (now - s_startMsec) / 1000u, svs.time, humans, bots, heapMB, totalMB);
}
// PROXY_TO_PTHREAD runs main() on a worker. A wasm trap is a JS RuntimeError there; the page's
// error event has error === null. Post message and stack through err() (Module.printErr, the F2 log)
// before that forward. self.__kisakTrapPosted is shared with the --post-js hook so a trap is one line.
EM_JS(void, Kisak_InstallCrashHook, (), {
    if (typeof self === "undefined" || !self.addEventListener || self.__kisakCrashHook) return;
    self.__kisakCrashHook = true;
    self.addEventListener("error", function (ev) {
        if (self.__kisakTrapPosted) {
            if (ev.preventDefault) ev.preventDefault();
            return;
        }
        var error = ev.error;
        if (!error || error === "unwind" || error.name === "ExitStatus") return;
        var message = error.message != null ? String(error.message) : String(ev.message || "");
        var stack = typeof error.stack === "string" ? error.stack : "";
        var flatStack = stack.replace(/[\\r\\n]+/g, " | ").replace(/[ \\t]{2,}/g, " ").trim();
        var line = "wasm trap: " + message;
        if (flatStack) line += " | " + flatStack;
        self.__kisakTrapPosted = true;
        try {
            if (typeof err === "function") err(line);
            else console.error(line);
        } catch (ignored) {
            console.error(line);
        }
        if (ev.preventDefault) ev.preventDefault();
    });
});

int main(int argc, char **argv)
{
    Kisak_InstallCrashHook();
    // Arguments come from the page (Module.arguments). Without an explicit "dedicated" setting the
    // build starts as a headless server (the original foundation behaviour); the page's Play button
    // passes "+set dedicated 0" to start the client.
    std::string cmd = Web_InitFilesystem(argc, argv);
    if (!strstr(cmd.c_str(), "dedicated"))
        cmd += " +set dedicated 1";

    char line[1024];
    I_strncpyz(line, cmd.c_str(), sizeof(line));
    return WinMain(nullptr, nullptr, line, 0);
}

