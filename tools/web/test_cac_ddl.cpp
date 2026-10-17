// Link against the game's ddl_api/lookup/buffer/converter objects with em++.
// The small schema preserves the nested arrays used by the shipped stats.ddl.
#include "../../src/ddl/ddl_api.h"
#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

bool Assert_MyHandler(const char *file, int line, int, const char *fmt, ...)
{
    fprintf(stderr, "Unexpected engine assertion %s:%d: %s\n", file, line, fmt);
    abort();
}
void Com_Printf(int, const char *, ...) {}
void Com_PrintError(int, const char *, ...) {}
void Com_PrintWarning(int, const char *, ...) {}
enum errorParm_t { TestError };
void Com_Error(errorParm_t, const char *, ...) { abort(); }
void I_strncpyz(char *dst, const char *src, int size)
{
    snprintf(dst, size, "%s", src);
}
int I_strcmp(const char *a, const char *b) { return strcmp(a, b); }
char *I_strlwr(char *s)
{
    for (char *p = s; *p; ++p) *p = (char)tolower((unsigned char)*p);
    return s;
}

static ddlMemberDef_t leaf(const char *name, int offset, int count = 1)
{
    return { name, count, offset, 2, 0, 0, 1, 1, 1, count, -1, 2 };
}

int main()
{
    ddlMemberDef_t item[] = { leaf("isproversionunlocked", 0, 3), leaf("new", 3), leaf("purchased", 4) };
    ddlMemberDef_t weapon[] = { leaf("purchasedattachments", 0, 32), leaf("purchasedoptions", 32, 72) };
    ddlMemberDef_t rootMembers[] = {
        { "itemstats", 5 * 256, 0, 6, 1, 0, 0, 0, 0, 256, -1, 2 },
        { "weaponstats", 104 * 64, 5 * 256, 6, 2, 0, 0, 0, 0, 64, -1, 2 }
    };
    ddlStructDef_t structs[] = { { "root", 7936, 2, rootMembers }, { "itemstat_s", 5, 3, item },
                                { "weaponstat_s", 104, 2, weapon } };
    ddlDef_t ddl = { 100, 7936, structs, 3, nullptr, 0, nullptr };
    ddlState_t root, state, other;
    DDL_Reset(&root, &ddl);
    char buffer[1100] = {};
    assert(DDL_AssociateBuffer(buffer, sizeof(buffer), &ddl));

    const char *purchased[] = { "ItemStats", "152", "purchased" };
    assert(DDL_MoveToPath(&root, &state, 3, purchased));
    assert(DDL_MoveTo(&root, &other, 3, "ItemStats", "152", "purchased"));
    assert(state.absoluteOffset == other.absoluteOffset && state.member == other.member);
    assert(DDL_SetInt(&state, 1, buffer));
    assert(DDL_GetInt(&other, buffer) == 1);
    assert(DDL_GetUserFlag(buffer, 0));

    // Clearing the menu badge must never clear ownership, nor set it on another item.
    assert(DDL_MoveTo(&root, &other, 3, "ItemStats", "152", "new"));
    assert(DDL_SetInt(&other, 1, buffer));
    assert(DDL_SetInt(&other, 0, buffer));
    assert(DDL_GetInt(&state, buffer) == 1);
    const char *neighbor[] = { "itemstats", "153", "purchased" };
    assert(DDL_MoveToPath(&root, &other, 3, neighbor));
    assert(DDL_GetInt(&other, buffer) == 0);

    // Two nested numeric arrays: the selected item remains a struct until its leaf.
    const char *attachment[] = { "WeaponStats", "29", "purchasedAttachments", "4" };
    const char *option[] = { "WeaponStats", "29", "purchasedOptions", "63" };
    assert(DDL_MoveToPath(&root, &state, 4, attachment));
    assert(DDL_SetInt(&state, 1, buffer));
    assert(DDL_MoveToPath(&root, &other, 4, option));
    assert(DDL_GetInt(&other, buffer) == 0);
    assert(DDL_SetInt(&other, 1, buffer));
    assert(DDL_GetInt(&state, buffer) == 1);
    const char *challenge[] = { "ItemStats", "152", "isProVersionUnlocked", "2" };
    assert(DDL_MoveToPath(&root, &state, 4, challenge));
    assert(DDL_SetInt(&state, 1, buffer));
    assert(DDL_GetInt(&state, buffer) == 1);
    assert(!DDL_MoveToPath(nullptr, &state, 3, purchased));
    assert(!DDL_MoveToPath(&root, &state, 0, purchased));
    const char *missing[] = { "ItemStats", "152", "missing" };
    assert(!DDL_MoveToPath(&root, &state, 3, missing));
    puts("PASS native CAC DDL: ownership/badges, attachments, options, pro challenges, dirty flag");
}
