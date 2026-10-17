#pragma once

struct XAnimTree_s;
struct DObjModel_s;

// Local co-op runs the authoritative script animator in the same engine as the
// renderer. Publish completed frames rather than reading a live server DObj.
void G_PublishLocalActorAnimations();
void G_ClearLocalActorAnimations();
bool G_CopyLocalActorAnimation(int entityNum, int useCount, XAnimTree_s *target);
// Completed server model composition, including attached heads and destructibles.
// Returns zero when no matching local authoritative frame is available.
int G_CopyLocalActorModels(int entityNum, int useCount, DObjModel_s *models,
    int capacity, unsigned int *hidePartBits);
