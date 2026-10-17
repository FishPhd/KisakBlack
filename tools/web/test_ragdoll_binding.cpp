#include <ragdoll/ragdoll_binding.h>
#include <cassert>
#include <cstdio>
int main() {
    assert(Ragdoll_ValidAnimBone(0,111));
    assert(Ragdoll_ValidAnimBone(110,111));
    assert(!Ragdoll_ValidAnimBone(111,111));
    assert(!Ragdoll_ValidAnimBone(255,256));
    assert(!Ragdoll_ValidAnimBone(0,0));
    assert(Ragdoll_ValidParentBone(0,16));
    assert(!Ragdoll_ValidParentBone(-1,16));
    assert(!Ragdoll_ValidParentBone(16,16));
    puts("Ragdoll missing/invalid binding checks PASS");
}
