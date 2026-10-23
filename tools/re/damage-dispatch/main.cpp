// Regression tests of production G_DamageClient's dispatch decisions.
// Explicit test doubles observe the script boundary; this is NOT native-original
// execution or a claim that the damage/script/weapon dependencies are equivalent.
#include <game_mp/g_main_mp.h>
#include <game_mp/g_combat_mp.h>
#include <bgame/bg_weapons_def.h>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <initializer_list>

static_assert(offsetof(gentity_s, client) == 0x144);
static_assert(offsetof(gentity_s, takedamage) == 0x15f);
static_assert(offsetof(clientState_s, lastStandStartTime) == 0x70);

level_locals_t level{};
gclient_s::gclient_s() { std::memset(this, 0, sizeof(*this)); }
static gclient_s client;
static gentity_s target;
static int calls, lookups, multipliers, observedDamage;
static unsigned observedMod;

static void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
bool Assert_MyHandler(const char*, int, int, const char*, ...) { std::abort(); }
unsigned int BG_GetNumWeapons() { return 1; }
unsigned int G_GetWeaponIndexForEntity(const gentity_s*) { std::abort(); }
double G_GetWeaponHitLocationMultiplier(hitLocation_t hitLoc, unsigned int weapon) {
    require(hitLoc == HITLOC_HEAD && weapon == 0, "unexpected multiplier arguments");
    ++multipliers;
    return 0.5;
}
clientState_s* G_GetClientState(unsigned int clientNum) {
    require(clientNum == 0, "wrong client index");
    ++lookups;
    return &client.sess.cs;
}
void Scr_PlayerDamage(gentity_s* self, gentity_s* inflictor, gentity_s* attacker,
                     int damage, int dflags, unsigned mod, unsigned weapon,
                     float* point, float* dir, hitLocation_t hitLoc, int timeOffset) {
    require(self == &target && !inflictor && !attacker && !point && !dir,
            "script pointer arguments changed");
    require(dflags == 0x10 && weapon == 0 && hitLoc == HITLOC_HEAD && timeOffset == -25,
            "script scalar arguments changed");
    ++calls;
    observedDamage = damage;
    observedMod = mod;
}
static void reset(int lastStand, int timestamp, int now) {
    std::memset(&client, 0, sizeof(client));
    std::memset(&target, 0, sizeof(target));
    target.client = &client;
    target.takedamage = 1;
    client.sess.connected = CON_CONNECTED;
    client.lastStand = lastStand;
    client.sess.cs.lastStandStartTime = timestamp;
    level.time = now;
    calls = lookups = multipliers = observedDamage = 0;
}
static void damage(unsigned mod, int amount = 101) {
    G_DamageClient(&target, nullptr, nullptr, nullptr, nullptr, amount, 0x10,
                   mod, 0, HITLOC_HEAD, -25);
}
int main() {
    std::printf("Reconstructed client layout: size=0x%zx flags=0x%zx lastStand=0x%zx session.cs=0x%zx\n",
                sizeof(gclient_s), offsetof(gclient_s, flags), offsetof(gclient_s, lastStand),
                offsetof(gclient_s, sess) + offsetof(clientSession_t, cs));
    // Future protection suppresses the script notification, including suicide;
    // melee (7) is the original exception, and does not scale hit-location damage.
    for (unsigned mod : {1u, 2u, 7u, 13u, 14u}) {
        reset(1, 1001, 1000); damage(mod);
        require(calls == (mod == 7), "future Last Stand window failed");
        require(lookups == (mod != 7), "melee must bypass the timestamp lookup");
        require(multipliers == 0, "protected or melee damage must not be scaled");
        if (mod == 7) require(observedDamage == 101 && observedMod == 7, "melee dispatch changed");
    }
    for (int timestamp : {999, 1000, INT_MIN}) {
        reset(1, timestamp, 1000); damage(2);
        require(calls == 1 && observedDamage == 50 && multipliers == 1,
                "expired/equal window must dispatch scaled damage");
    }
    reset(0, INT_MAX, INT_MIN); damage(2);
    require(calls == 1 && lookups == 0, "ordinary players must not read Last Stand time");
    reset(1, INT_MAX, INT_MIN); damage(2);
    require(calls == 0, "timestamp comparison must be signed and avoid subtraction overflow");
    for (int reason = 0; reason < 7; ++reason) {
        reset(1, 1001, 1000);
        if (reason == 0) target.takedamage = 0;
        if (reason == 1) client.flags = 1;
        if (reason == 2) client.flags = 2;
        if (reason == 3) client.sess.connected = CON_CONNECTING;
        if (reason == 4) client.ps.pm_type = 9;
        if (reason == 5) client.ps.otherFlags = 1;
        damage(2, reason == 6 ? 0 : 101);
        require(calls == 0 && lookups == 0 && multipliers == 0, "earlier rejection gates changed");
    }
    std::puts("PASS: production damage dispatch, Last Stand boundary, melee bypass, and earlier rejection gates (unit test; no binary equivalence claim)");
}
