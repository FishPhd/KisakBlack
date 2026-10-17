#include "../../src/clientscript/scr_wait.h"
#include <cassert>
#include <climits>
#include <initializer_list>
#include <limits>

int main()
{
    for (int rate : {20, 60})
    {
        for (float seconds : {0.0f, 0.0001f, 0.001f, 0.01f})
            assert(Scr_WaitTicks(seconds, rate) >= 1);
        assert(Scr_WaitTicks(0, rate) == 1);
        assert(Scr_WaitTicks(1, rate) == rate);
        assert(Scr_WaitTicks(1.0f, rate) == rate);
        assert(Scr_WaitTicks(-1, rate) == -1);
        assert(Scr_WaitTicks(-0.01f, rate) == -1);
        assert(Scr_WaitTicks(INT_MAX, rate) == SCR_WAIT_TOO_LONG);
        assert(Scr_WaitTicks(std::numeric_limits<float>::infinity(), rate) == SCR_WAIT_TOO_LONG);
        assert(Scr_WaitTicks(std::numeric_limits<float>::quiet_NaN(), rate) == SCR_WAIT_TOO_LONG);
        assert(Scr_WaitTicks(std::numeric_limits<float>::max(), rate) == SCR_WAIT_TOO_LONG);
    }
    assert(Scr_WaitTicks(0.05f, 20) == 1);
    assert(Scr_WaitTicks(0.1f, 20) == 2);
    assert(Scr_WaitTicks(0.1f, 60) == 6);
    // Simulate the supply-drop loop: each reschedule must leave this tick's
    // queue, including a wrap through the 24-bit clock.
    unsigned tick = 0xFFFFFE;
    for (int i = 0; i < 100; ++i)
    {
        const unsigned next = (tick + Scr_WaitTicks(0.01f, 20)) & 0xFFFFFF;
        assert(next != tick);
        tick = next;
    }
}
