// Broader qualification boundary, kept separate from the restored network and
// server-client contracts. This check currently FAILS on native and Wasm.
// Preserve it until the enclosing cg_s members are individually reconstructed;
// neither guessed padding nor a browser smoke test can establish equivalence.
#include <cgame_mp/cg_local_mp.h>
#include <cstddef>
static_assert(offsetof(cg_s, bgs) + offsetof(bgs_t, clientinfo) == 0x5f238,
              "Original live client-info base at 0x7ceae7");
