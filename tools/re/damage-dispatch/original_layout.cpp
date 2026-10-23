// Deliberately separate from unit tests: a dispatch regression passing does
// not establish original layout compatibility. Preserve every failing check.
#include <game_mp/g_main_mp.h>
#include <client_mp/cl_main_mp.h>
#include <server/server.h>
#include <cstddef>
static_assert(sizeof(playerState_s) == 0x26e8, "Original snapshot copy size at 0x56d63d");
static_assert(sizeof(PlayerHeldWeapon) == 0x1c, "Original held-weapon stride at 0x56e019");
static_assert(offsetof(PlayerHeldWeapon, adsZoomSelect) == 0x10, "Original zoom slot at 0x5cc909");
static_assert(sizeof(clientState_s) == 0xd8, "Original client state: field at 0xd0 plus 8-byte alignment");
static_assert(sizeof(MatchState) == 0x80, "Original snapshot match-state stride");
static_assert(sizeof(entityState_s) == 0xe0, "Original snapshot entity stride");
static_assert(SNAPSHOT_STORAGE_FRAME_COUNT == 28, "Original 0x57a3e0 snapshot capacities");
static_assert(ARCHIVED_SNAPSHOT_BUFFER_SIZE == 0x2000000, "Original archive storage size");
static_assert(ARCHIVED_SNAPSHOT_COUNTER_WRAP == 0x50000000, "Original 0x6664af writer modulus");
static_assert(sizeof(gclient_s) == 0x2a28, "Original client stride at BlackOpsMP 0x46381a");
static_assert(sizeof(clientInfo_t) == 0x5d0, "Original client-info clear size at 0x4a74d1");
static_assert(offsetof(cgs_t, corpseinfo) == 0x17e0,
              "Original corpse-info base: 32 * 0x5d0 - 0xa220 at 0x7ceaff");
static_assert(offsetof(gclient_s, flags) == 0x28e4, "Original flags at 0x6194a2");
static_assert(offsetof(gclient_s, lastStand) == 0x2a20, "Original Last Stand at 0x61950b");
static_assert(offsetof(gclient_s, sess) + offsetof(clientSession_t, cs) == 0x27b8,
              "Original clientState offset at 0x463820");
#include "original_fields.h"
