#pragma once
// 255 is DObj's missing-bone sentinel, never an index into a pose array.
constexpr bool Ragdoll_ValidAnimBone(unsigned index, unsigned count) {
    return index != 255 && index < count;
}
constexpr bool Ragdoll_ValidParentBone(int index, int count) {
    return index >= 0 && index < count;
}
