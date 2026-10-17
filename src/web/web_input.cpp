// Browser input. The page captures keyboard / mouse / focus events and calls the exported functions
// below from the browser's main thread; they only push into a lock-free ring. IN_Frame (game thread,
// once per frame) drains the ring and feeds the same engine entry points the Win32 window procedure
// and win_input.cpp use (Sys_QueEvent SE_KEY / SE_CHAR, CL_MouseEvent).
#include <win32/win_gamepad.h>
#include <win32/win_input.h>
#include <win32/win_local.h>
#include <win32/win_main.h>
#include <win32/win_wndproc.h>

#include <client/cl_keys.h>
#include <client_mp/cl_input_mp.h>
#include <client_mp/cl_main_mp.h>
#include <qcommon/common.h>
#include <universal/dvar.h>
#include "web_graphics.h"

#include <emscripten.h>

#include <atomic>
#include <cstdint>

namespace
{
enum WebInType : int
{
    WIN_KEY = 1,   // a = engine key code, b = down
    WIN_CHAR,      // a = character
    WIN_MOUSE_DXY, // a = dx, b = dy (pointer-locked deltas)
    WIN_MOUSE_POS, // a = x, b = y in back-buffer pixels
    WIN_FOCUS,     // a = focused
    WIN_FULLSCREEN, // a = actual browser fullscreen state
    WIN_GAMEPAD     // a = connected, gamepad = standard-layout XInput sample
};

struct WebInEvent
{
    int type, a, b;
    XINPUT_GAMEPAD gamepad{};
};

constexpr uint32_t kRing = 2048; // power of two
WebInEvent s_ring[kRing];
std::atomic<uint32_t> s_head{0}; // written by the page
std::atomic<uint32_t> s_tail{0}; // written by the game thread

std::atomic<int> s_wantsCapture{0};
std::atomic<int> s_cursorVisible{1};

int s_mouseX, s_mouseY;
int s_hasWindow;
bool s_positionEvent;
XINPUT_STATE s_gamepad{}; // Only accessed by the game thread after draining the ring.
bool s_gamepadConnected;

void Push(const WebInEvent &event)
{
    const uint32_t h = s_head.load(std::memory_order_relaxed);
    if (h - s_tail.load(std::memory_order_acquire) >= kRing)
        return; // full: drop (only happens when the game thread is stalled)
    s_ring[h & (kRing - 1)] = event;
    s_head.store(h + 1, std::memory_order_release);
}

void Push(int type, int a, int b) { Push({type, a, b}); }

void Activate(bool active)
{
    g_wv.activeApp = active ? 1 : 0;
    g_wv.isMinimized = 0;
    Key_ClearStates(0);
    IN_Activate(g_wv.activeApp);
}
} // namespace

bool Web_HasMousePositionEvent() { return s_positionEvent; }

extern "C"
{
EMSCRIPTEN_KEEPALIVE void Kisak_Key(int engineKey, int down) { Push(WIN_KEY, engineKey, down); }
EMSCRIPTEN_KEEPALIVE void Kisak_Char(int ch) { Push(WIN_CHAR, ch, 0); }
EMSCRIPTEN_KEEPALIVE void Kisak_MouseMove(int dx, int dy) { Push(WIN_MOUSE_DXY, dx, dy); }
EMSCRIPTEN_KEEPALIVE void Kisak_MousePos(int x, int y) { Push(WIN_MOUSE_POS, x, y); }
EMSCRIPTEN_KEEPALIVE void Kisak_Focus(int focused) { Push(WIN_FOCUS, focused, 0); }
EMSCRIPTEN_KEEPALIVE void Kisak_FullscreenChanged(int enabled) { Push(WIN_FULLSCREEN, enabled, 0); }
EMSCRIPTEN_KEEPALIVE int Kisak_WantsMouseCapture() { return s_wantsCapture.load(std::memory_order_relaxed); }
EMSCRIPTEN_KEEPALIVE int Kisak_CursorVisible() { return s_cursorVisible.load(std::memory_order_relaxed); }
EMSCRIPTEN_KEEPALIVE void Kisak_Gamepad(int connected, int buttons, int lt, int rt, int lx, int ly, int rx, int ry)
{
    WebInEvent event{WIN_GAMEPAD, connected, 0};
    event.gamepad = {static_cast<WORD>(buttons), static_cast<BYTE>(lt), static_cast<BYTE>(rt),
        static_cast<SHORT>(lx), static_cast<SHORT>(ly), static_cast<SHORT>(rx), static_cast<SHORT>(ry)};
    Push(event);
}
}

DWORD XInputGetState(DWORD port, XINPUT_STATE *state)
{
    if (port || !s_gamepadConnected)
        return ERROR_DEVICE_NOT_CONNECTED;
    *state = s_gamepad;
    if (!IN_IsForegroundWindow())
        state->Gamepad = {}; // Background tabs must never retain movement or firing.
    return ERROR_SUCCESS;
}

DWORD XInputGetCapabilities(DWORD port, DWORD, XINPUT_CAPABILITIES *caps)
{
    if (port || !s_gamepadConnected)
        return ERROR_DEVICE_NOT_CONNECTED;
    *caps = {};
    caps->Type = XINPUT_DEVTYPE_GAMEPAD;
    caps->SubType = 1; // XINPUT_DEVSUBTYPE_GAMEPAD
    return ERROR_SUCCESS;
}

void Web_SetMainWindow(HWND hwnd)
{
    g_wv.hWnd = hwnd;
    s_hasWindow = hwnd != nullptr;
    if (hwnd)
        Activate(true);
}

// ---------------------------------------------------------------------------------------------
// win_input.h
// ---------------------------------------------------------------------------------------------
const dvar_t *in_mouse;
int in_appactive;

void __cdecl IN_Init()
{
    in_mouse = _Dvar_RegisterBool("in_mouse", 1, 0x21u, "Initialize the mouse drivers");
    IN_StartupGamepads();
}

void __cdecl IN_Shutdown() {}
void __cdecl IN_Startup() {}
void __cdecl IN_StartupMouse() {}
void __cdecl IN_StartupGamepads() { GPad_InitAll(); }
void __cdecl IN_Activate(int active) { in_appactive = active; }
void __cdecl IN_ActivateMouse(int) {}
void __cdecl IN_DeactivateMouse() {}
void __cdecl IN_RecenterMouse() {}
void __cdecl IN_MouseEvent(int) {}
void __cdecl IN_SetCursorPos(unsigned int, unsigned int) {}

void __cdecl IN_ShowSystemCursor(bool show)
{
    s_cursorVisible.store(show ? 1 : 0, std::memory_order_relaxed);
}

bool __cdecl IN_IsForegroundWindow()
{
    return s_hasWindow && g_wv.activeApp;
}

void __cdecl IN_Frame()
{
    s_positionEvent = false;
    int dx = 0, dy = 0;
    uint32_t t = s_tail.load(std::memory_order_relaxed);
    const uint32_t h = s_head.load(std::memory_order_acquire);
    for (; t != h; ++t)
    {
        const WebInEvent e = s_ring[t & (kRing - 1)];
        switch (e.type)
        {
        case WIN_KEY:
            if (e.a > 0 && e.a < 256 && s_hasWindow)
                Sys_QueEvent(0, SE_KEY, e.a, e.b ? 1 : 0, 0, nullptr);
            break;
        case WIN_CHAR:
            if (s_hasWindow)
                Sys_QueEvent(0, SE_CHAR, e.a, 0, 0, nullptr);
            break;
        case WIN_MOUSE_DXY:
            dx += e.a;
            dy += e.b;
            break;
        case WIN_MOUSE_POS:
            s_mouseX = e.a;
            s_mouseY = e.b;
            s_positionEvent = true;
            break;
        case WIN_FOCUS:
            if (s_hasWindow)
            {
                // The page re-asserts focus on mouse motion. Clearing keys on a
                // repeat drops held movement until the key is pressed again.
                const bool active = e.a != 0;
                if ((g_wv.activeApp != 0) == active)
                    break;
                if (active)
                    s_positionEvent = true;
                Activate(active);
            }
            break;
        case WIN_FULLSCREEN:
            Web_BrowserFullscreenChanged(e.a != 0);
            break;
        case WIN_GAMEPAD:
            s_gamepadConnected = e.a != 0;
            s_gamepad.Gamepad = s_gamepadConnected ? e.gamepad : XINPUT_GAMEPAD{};
            ++s_gamepad.dwPacketNumber;
            break;
        }
    }
    s_tail.store(t, std::memory_order_release);

    if (!s_hasWindow)
        return;
    IN_GamepadsMove();
    if (in_mouse && !in_mouse->current.enabled)
        return;
    if (IN_IsForegroundWindow())
    {
        g_wv.recenterMouse = CL_MouseEvent(s_mouseX, s_mouseY, dx, dy);
        s_wantsCapture.store(g_wv.recenterMouse ? 1 : 0, std::memory_order_relaxed);
    }
    else
    {
        s_wantsCapture.store(0, std::memory_order_relaxed);
    }
}
