#pragma once

struct gentity_s;
struct XAnim_s;

// Scripted root motion is owned by the server and replicated by normal snapshots.
bool G_UpdateScriptedAnimation(gentity_s *ent);
void G_ClearScriptedAnimation(gentity_s *ent);
void G_ClearScriptedAnimations();
XAnim_s *G_GetActorAnimationTree(const gentity_s *ent);
