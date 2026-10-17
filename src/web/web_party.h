#pragma once

struct UiContext;
struct menuDef_t;
void Web_EnsurePartyMenu(UiContext *dc, menuDef_t *menu);
int Web_PartyMemberCount();
const char *Web_PartyMemberName(int index);
int Web_PartyMemberRank(int index);
int Web_PartyMemberPrestige(int index);
bool Web_PartyMemberIsHost(int index);
