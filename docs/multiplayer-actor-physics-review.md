# Restore the actor collision visitor after physics reset

`Actor_InitMove` cleared the entire physics object and then assigned a collision
visitor. C++ assignment copies fields without replacing the destination's
virtual table pointer. The clear therefore left that pointer zero. A later
virtual collision update could read address `0x14`, matching the earlier
captured `gjk_query` fault at native RVA `0x55ffd8` in the previous executable.

The original initializer explicitly restores the derived visitor table after
the clear. Reconstruct the C++ visitor in place from the existing static
template, then retain the original reset and remaining initialization. This
repairs object construction rather than skipping collision or accepting an
invalid visitor. No bot decision rules, XP awards, assertions or mode gates are
changed. Actors include entities such as dogs; this is distinct from soldier
bot weapon selection.

## Independent original identity

[actor_init_move_reference.json](../tests/actor_init_move_reference.json) pins
the executable hash, complete 543-byte owner at original RVA `0x1dd8c0`, and
immutable replay hashes. Identity evidence includes:

- ECX carries `self`; the caller is in the original owner at `0x69233`.
- Clear `self+0x234`, size `0x7d0`, through original `_memset` at `0x5ca590`.
- One-time static guard at RVA `0x39e3100`, dummy visitor at `0x39e2a48`, and
  derived table VA `0xac2c00`. At `0x1dd9f9`, read that table pointer and store
  it at `self+0x34c`. This instruction establishes the missing behavior.
- Reset primitive count/overflow and min/max bounds; initialize entity number,
  alive state, 50ms frame, mask `0x2820011`, hit entity 1023, ground entity 1022,
  and bounds from the entity at offsets `0xf0`/`0xfc`.

The native crash instruction was `call [eax+0x14]` with EAX zero, after loading
EAX from the supplied proximity visitor. The initializer defect is independently
demonstrated by replay. This does not prove that every earlier collision crash
has the same cause or exclude later corruption.

## Validation and limits

The complete initializer, with an already-initialized static visitor, agrees
across original-native, Unicorn and selected production code for **112 cases**.
The previous implementation differs in **all 112**, at the missing table
pointer. Original outputs are immutable. Both original scalar and SSE memset
paths execute; no substitute memset result is supplied. The closure declares
**895 original code bytes**, of which **607 execute** in this corpus.

Fixtures vary entity numbers, bounds, signed zeros, input poison, actor
alignment and memset dispatch. Actor data outside the physics object and all
entity data are guarded. Separate production checks call the actual virtual
tree visitor, reinitialize the actor, and call it again.

The current headers align vectors differently from the original: physics is at
actor offset `0x240` instead of `0x234`, and the visitor is at physics offset
`0x120` instead of `0x118`. The test projects all physics fields into original
offsets and normalizes only the known visitor type pointer. This establishes
field agreement, **not raw structure/ABI identity**. Original first-use static
initialization/CRT atexit registration, diagnostics and full GJK/world traversal
remain unsupported. Unexpected world traversal or dependencies terminate.

The pinned native Release build passes. The unchanged native catalog also
passes **56 units / 224,870 cases**, with **111** contract checks and deliberate
mutation detection. The [validation record](../tests/mp_actor_physics_validation.json)
contains source, EXE/PDB, original-binary and unchanged build-profile identities.

Independent native comparisons now cover **30 pairs / 11,337 original bytes**.
No pair is raw-byte or PE-HIGHLOW-normalized identical. For this owner, original
code has 91 instructions; previous/candidate native bodies have 144/142. Raw
positional differences are 764/766 bytes; mnemonic edits affecting original
instructions remain 60. These diagnostics do not establish semantic behavior.

The unchanged whole-EXE structural metric is **14.110600%**, versus the previous
candidate's **14.189507%** and MR base **14.133471%**. Confidence remains `0.7`
and the denominator remains 5,506,261 original decoded function bytes. The
automatic matcher pairs this original initializer with `CG_SetUnionType`, below
threshold, and gives its repair zero credit before and after. Known false
pairings are separately recorded; the metric is not adjusted.

## Final native smoke

The Nov07 launch uses the actual registered `fs_h` setting. Startup search paths
and logs confirm isolation under
`build-re/multiplayer/actor-followup/nov07-profile`. Stock CT setup reached the
lobby; START MATCH and stock SMG selection used the UI. The player spawned with
the MP5K and displayed stock perks while bots fought and scored. Fresh-profile
Create-a-Class remained locked; no unlock or XP grant was fabricated.

The process later failed executing address `0xc2740000`, outside the game
module. The observer recorded the exception without suppressing it. Its
accurate stack is unusable; fuzzy stack entries are diagnostics only, and the
root cause is unproven. No actor-initialization or associated actor-world-query
events occurred before this failure. Consequently the repair is **not
live-gameplay qualified**, and the combined smoke fails.

Sustained human combat/death/respawn, custom-class save/spawn, appropriate human
XP/rank UI changes and profile-reload persistence remain incomplete. The earlier
invalid cluster-link crash also remains unresolved. Initial CT mode setup used
the engine command API, so complete frontend navigation is unqualified.

Reproduce the scoped comparison with `python tests/replay_actor_init_move.py`.
Use its `--source` option with the parent's `actor_mp.cpp` to reproduce the 112
failures. Generated captures, profiles, binaries and large reports stay ignored.
