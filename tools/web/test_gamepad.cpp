// Links the real PC GPad_* and IN_GamepadsMove implementation with a fake XInput device.
#include <win32/win_gamepad.h>
#include <win32/win_input.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static dvar_s dvars[32];
static int dvarCount;
static bool connected;
static XINPUT_STATE sample{};
static int axes[6], edges[256][3];

static dvar_s *Register(const char *name) {
    dvar_s *d = &dvars[dvarCount++]; d->name = name; return d;
}
const dvar_s *_Dvar_RegisterInt(const char *n, int v, int, int, unsigned short, const char *) {
    auto d = Register(n); d->current.integer = v; return d;
}
const dvar_s *_Dvar_RegisterFloat(const char *n, float v, float, float, unsigned short, const char *) {
    auto d = Register(n); d->current.value = v; return d;
}
const dvar_s *_Dvar_RegisterBool(const char *n, bool v, unsigned short, const char *) {
    auto d = Register(n); d->current.enabled = v; return d;
}
const dvar_s *_Dvar_RegisterString(const char *n, const char *v, unsigned short, const char *) {
    auto d = Register(n); d->current.string = v; return d;
}
double Dvar_GetFloat(const char *name) {
    for (int i = 0; i < dvarCount; ++i) if (!strcmp(dvars[i].name, name)) return dvars[i].current.value;
    abort();
}
void Dvar_SetBool(dvar_s *d, bool v) { d->current.enabled = v; }
bool Assert_MyHandler(const char *, int, int, const char *, ...) { abort(); }
float Vec2Normalize(float *v) {
    float len = sqrtf(v[0]*v[0] + v[1]*v[1]);
    if (len) { v[0] /= len; v[1] /= len; } return len;
}
unsigned int Sys_Milliseconds() { return 100; }
DWORD XInputGetState(DWORD port, XINPUT_STATE *out) {
    if (port || !connected) return ERROR_DEVICE_NOT_CONNECTED;
    *out = sample; return ERROR_SUCCESS;
}
DWORD XInputGetCapabilities(DWORD port, DWORD, XINPUT_CAPABILITIES *out) {
    *out = {}; return port || !connected ? ERROR_DEVICE_NOT_CONNECTED : ERROR_SUCCESS;
}
void CL_GamepadEvent(int, unsigned int axis, int value) { axes[axis] = value; }
void CL_GamepadButtonEventForPort(int, int key, int event, unsigned int, GamePadButton) { ++edges[key][event]; }

int main() {
    GPad_InitAll();
    IN_GamepadsMove();
    assert(!GPad_IsActive(0) && !gpad_present->current.enabled);
    // Preserve PC opt-in and profile names.
    assert(!gpad_enabled->current.enabled);
    assert(!strcmp(gpad_buttonsConfig->current.string, "buttons_default"));
    assert(!strcmp(gpad_sticksConfig->current.string, "thumbstick_default"));
    connected = true;
    sample.Gamepad = {XINPUT_GAMEPAD_A, 128, 255, 32767, -32767, -32767, 32767};
    IN_GamepadsMove();
    assert(GPad_IsActive(0) && gpad_present->current.enabled);
    assert(edges[1][1] == 1 && edges[18][1] == 1 && edges[19][1] == 1);
    assert(axes[0] < 0 && axes[1] > 0 && axes[2] > 0 && axes[3] < 0);
    assert(axes[4] == 65535 && axes[5] > 27000 && axes[5] < 29000);
    IN_GamepadsMove();
    assert(edges[1][1] == 1); // Held digitals never generate duplicate down edges.
    assert(edges[18][2] == 1 && edges[19][2] == 1); // Analog buttons update while held.
    connected = false;
    IN_GamepadsMove();
    assert(!GPad_IsActive(0) && !gpad_present->current.enabled);
    assert(edges[1][0] == 1 && edges[18][0] == 1 && edges[19][0] == 1);
    for (int value : axes) assert(value == 0);
    IN_GamepadsMove();
    assert(edges[1][0] == 1); // Disconnect releases exactly once.
    connected = true;
    sample.Gamepad = {XINPUT_GAMEPAD_A, 10, 0, 1000, -1000, 0, 0};
    IN_GamepadsMove();
    assert(edges[1][1] == 2); // Reconnection is a new edge.
    for (int value : axes) assert(value == 0); // Original radial and trigger deadzones.
    sample.Gamepad = {};
    IN_GamepadsMove();
    assert(edges[1][0] == 2);
    puts("PC controller axes, deadzones, button transitions and disconnect/reconnect passed.");
}
