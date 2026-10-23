// Independent whole-record extent boundary. The prefix can match while this
// still fails; preserve the original allocator's value rather than adding filler.
#include <cgame_mp/cg_local_mp.h>
static_assert(sizeof(cg_s) == 0x71980,
              "Original CG_AllocateClientMemory extent at 0x5c7b00");
