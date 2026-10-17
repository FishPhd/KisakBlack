#pragma once

// Keep runnable job workers bounded: the game and render threads also need CPU
// time. Browser core counts do not distinguish Apple performance/efficiency cores.
constexpr unsigned Web_DefaultJobWorkers(unsigned cores)
{
    return cores <= 3 ? 1 : cores <= 6 ? cores - 2 : 4;
}
