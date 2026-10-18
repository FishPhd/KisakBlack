// Isolate the production binding and cached DObj lookup, without linking the
// renderer/physics engine. Run emitted C++ with any C++17 compiler.
import fs from 'node:fs';
const update = fs.readFileSync('src/ragdoll/ragdoll_update.cpp', 'utf8');
const lookup = fs.readFileSync('src/xanim/dobj_utils.cpp', 'utf8');
const rebind = update.slice(update.indexOf('char __cdecl Ragdoll_RebindBody('), update.indexOf('char __cdecl Ragdoll_ValidateBodyObj('));
const getBone = lookup.slice(lookup.indexOf('int __cdecl DObjGetBoneIndex('), lookup.indexOf('int __cdecl DObjGetChildBones('));
const source = `
#include <cassert>
#include <cstdio>
#include <ragdoll/ragdoll_binding.h>
#define __cdecl
#define __int8 char
#define iassert assert
#define bcassert(index, count) assert((index) < (count))
#define Assert_MyHandler(...) false
#define __debugbreak() assert(false)
struct XModel { unsigned numBones; unsigned *localBoneNames; };
struct DObj { int numBones, numModels; XModel **localModels; };
struct Bone { int parentBone; unsigned char animBones[2]; };
struct BoneDef { int parentBone; unsigned animBoneNames[2]; };
struct LerpBone { unsigned char animBone; int parentBone; };
struct LerpDef { unsigned animBoneName; int parentBoneIndex; };
struct RagdollBody { unsigned state; int numBones,numLerpBones; Bone *bones; LerpBone *lerpBones; };
struct RagdollDef { BoneDef *boneDefs; LerpDef *baseLerpBoneDefs; };
constexpr int BS_DOBJ_WAIT=1;
static RagdollBody body; static RagdollDef def; static DObj obj;
static RagdollBody *Ragdoll_HandleBody(int) { return &body; }
static bool Ragdoll_ValidateBodyObj(RagdollBody *) { return true; }
static RagdollDef *Ragdoll_BodyDef(RagdollBody *) { return &def; }
static DObj *Ragdoll_BodyDObj(RagdollBody *) { return &obj; }
static char Ragdoll_SnapshotBaseLerpOffsets(RagdollBody *) { return 1; }
static int XModelGetBoneIndex(XModel *model,unsigned name,unsigned start,unsigned char *index) {
    for(unsigned i=0;i<model->numBones;++i) if(model->localBoneNames[i]==name){*index=start+i;return 1;} return 0;
}
${getBone}
${rebind}
int main() {
    unsigned names[]={10,20,30,40}; XModel model{4,names}; XModel *models[]={&model}; obj={4,1,models};
    Bone bones[2]{}; LerpBone lerps[1]{};
    BoneDef definitions[2]={{-1,{20,30}},{0,{40,0}}}; LerpDef lerpDefs[]={{10,0}};
    body={2,2,1,bones,lerps}; def={definitions,lerpDefs};
    unsigned char missing=255; assert(!DObjGetBoneIndex(&obj,20,&missing,-1));
    assert(Ragdoll_RebindBody(1));
    assert(bones[0].animBones[0]==1); assert(bones[0].animBones[1]==2);
    assert(bones[1].animBones[0]==3); assert(lerps[0].animBone==0);
    definitions[1].animBoneNames[1]=99; // optional capsule endpoint absent
    assert(Ragdoll_RebindBody(1)); assert(bones[1].animBones[0]==255);
    puts("PASS production ragdoll rebind and cached bone lookup");
}
`;
fs.mkdirSync('build-web', {recursive:true});
fs.writeFileSync('build-web/test_ragdoll_lookup.cpp', source);
