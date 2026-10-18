#include <bgame/bg_local.h>
#include <bgame/bg_pmove.h>
#include <bgame/bg_misc.h>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#endif

const dvar_s* stopspeed = nullptr;
static_assert(offsetof(playerState_s, pm_flags) == 0xc);
static_assert(offsetof(playerState_s, velocity) == 0x30);
static_assert(offsetof(pml_t, frametime) == 0x24);
static_assert(offsetof(dvar_s, current) == 0x18);

static float f32(unsigned bits) { float value; std::memcpy(&value, &bits, 4); return value; }

void pm_call(bool original, void* entry, void* global, const unsigned* input, unsigned* output) {
    playerState_s ps{}; pml_t pml{}; dvar_s stop{};
    ps.pm_flags = input[0];
    std::memcpy(ps.velocity, input + 1, 12);
    float direction[3]; std::memcpy(direction, input + 4, 12);
    const float speed = f32(input[7]), accel = f32(input[8]);
    pml.frametime = f32(input[9]); stop.current.value = f32(input[10]);
    stopspeed = &stop;
    const unsigned steps = input[11] ? input[11] : 1;
    if (steps > 1024) std::abort();
    playerState_s before = ps;
    const pml_t before_pml = pml;
#ifdef _WIN32
    if (original) {
        DWORD protection;
        if (!VirtualProtect(global, 4, PAGE_READWRITE, &protection)) std::abort();
        *static_cast<const dvar_s**>(global) = &stop;
        VirtualProtect(global, 4, protection, &protection);
    }
#endif
    for (unsigned step = 0; step < steps; ++step) {
#ifdef _WIN32
        if (original) {
            auto state = &ps; auto local = &pml; auto wish = direction;
            __asm {
                push accel
                push speed
                push state
                mov edx, local
                mov ecx, wish
                call entry
                add esp, 12
            }
        } else
#endif
        PM_Accelerate(&ps, &pml, direction, speed, accel);
    }
    std::memcpy(output, ps.velocity, 12); output[3] = ps.pm_flags;
    std::memcpy(before.velocity, ps.velocity, 12);
    if (std::memcmp(&before, &ps, sizeof(ps))) std::abort();
    if (std::memcmp(&before_pml, &pml, sizeof(pml)) || std::memcmp(direction, input + 4, 12) ||
        std::memcmp(&stop.current.value, input + 10, 4)) std::abort();
    stopspeed = nullptr;
}
