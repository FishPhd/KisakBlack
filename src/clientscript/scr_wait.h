#pragma once

#include <cstdint>
#include <cstring>

// The time-array key has 24 bits. A nonnegative wait must advance at least
// one tick; queuing a sub-frame wait at the current key runs it again in the
// same Scr_RunCurrentThreads pass, starving the rest of the frame.
constexpr int SCR_WAIT_TOO_LONG = 0xFFFFFF;

inline int Scr_WaitTicks(float seconds, int ticksPerSecond)
{
    uint32_t bits;
    std::memcpy(&bits, &seconds, sizeof(bits));
    if ((bits & 0x7F800000u) == 0x7F800000u)
        return SCR_WAIT_TOO_LONG;
    if (seconds < 0.0f)
        return -1;
    const double ticks = double(seconds) * ticksPerSecond;
    if (ticks + 0.5 >= SCR_WAIT_TOO_LONG)
        return SCR_WAIT_TOO_LONG;
    const int rounded = int(ticks + 0.5);
    return rounded > 0 ? rounded : 1;
}

inline int Scr_WaitTicks(int seconds, int ticksPerSecond)
{
    if (seconds < 0)
        return -1;
    const int64_t ticks = int64_t(seconds) * ticksPerSecond;
    if (ticks >= SCR_WAIT_TOO_LONG)
        return SCR_WAIT_TOO_LONG;
    return ticks > 0 ? int(ticks) : 1;
}
