#include <universal/com_math.h>
#include <cstring>
#include <cstdlib>

void bounds_call(bool original, void* entry, const unsigned* input, unsigned* output) {
    struct Guarded { unsigned left[4]; float bounds[6]; unsigned right[4]; } guard{};
    for (unsigned i = 0; i != 4; ++i) guard.left[i] = guard.right[i] = 0xa55a55aa;
    std::memcpy(guard.bounds, input, 24);
    using Expand = void (__cdecl *)(float*, float*);
    auto expand = original ? reinterpret_cast<Expand>(entry) : ExpandBoundsToWidth;
    expand(guard.bounds, guard.bounds + 3);
    for (unsigned i = 0; i != 4; ++i)
        if (guard.left[i] != 0xa55a55aa || guard.right[i] != 0xa55a55aa) std::abort();
    if (std::memcmp(guard.bounds, input, 8) || std::memcmp(guard.bounds + 3, input + 3, 8)) std::abort();
    std::memcpy(output, guard.bounds + 2, 4);
    std::memcpy(output + 1, guard.bounds + 5, 4);
}
