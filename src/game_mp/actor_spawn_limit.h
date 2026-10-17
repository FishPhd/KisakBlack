#pragma once

// Preserve the shared engine's 16 actor slots and snapshot format. The Zombies
// director queues additional spawns while the simultaneous actor pool is full.
int Actor_SetSpawnLimit(int requested);
int Actor_GetSpawnLimit();
