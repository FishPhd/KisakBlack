#pragma once
// DObjGetBoneIndex caches 255 as a failed lookup and immediately returns.
// 254 asks it to search the model, just like the other client bone bindings.
constexpr unsigned char RAGDOLL_BONE_UNRESOLVED = 254;
// 255 is DObj's missing-bone sentinel, never an index into a pose array.
constexpr bool Ragdoll_ValidAnimBone(unsigned index, unsigned count) {
    return index != 255 && index < count;
}
constexpr bool Ragdoll_ValidParentBone(int index, int count) {
    return index >= 0 && index < count;
}
