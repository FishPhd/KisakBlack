# Multiplayer restoration status

The current work targets the pinned original multiplayer executable from
`catalog.json`. [multiplayer-restoration.json](multiplayer-restoration.json)
records instruction hashes, data-table locations, corrected historical BinDiff
candidates, dependency notes, and the precise scope of the restorations.
It is an evidence inventory, not a replacement execution catalog. New routines
remain unverified and pending mapping review until their original execution
adapters and comparisons exist. The eight existing pilots retain their narrower
contracts in `catalog.json`; their results do not qualify all multiplayer logic.

Restored gaps:

- Original `playerState_s` size `0x26e8`, held weapon stride `0x1c`, per-weapon
  zoom selection, quick-scope state, score wager winnings, and client stride
  `0x2a28`. Server registration and snapshot copies use those restored sizes.
  Corpse rendering uses the original corpse array and restored client-info stride.
- The original 181 player-state and 51 client-state network descriptors, plus
  version-3 held-weapon zoom encoding on both reader and writer paths.
- All 42 named script-visible client descriptors, including `wagerwinnings`.
  Their offsets now address the restored layout. Custom accessor behavior is
  separately unqualified; the original PS-offset getter reads archive time and
  that behavior is preserved.
- Last Stand damage protection before hit-location scaling. Non-melee damage is
  blocked while `lastStandStartTime > level.time`; equality permits damage.
- Quick-scope timer/scale updates and spread/view consumers, using original dvar
  defaults, ranges, and arithmetic order. Existing `sinf` use in idle motion
  remains unqualified against the original x87 sine instruction.

Run the small checks without launching a game:

```powershell
build-re/venv/Scripts/python.exe tools/re/test_damage_dispatch.py --check-original-layout
build-re/venv/Scripts/python.exe tools/re/test_damage_dispatch.py
# With the repository Emscripten environment loaded:
build-re/venv/Scripts/python.exe tools/re/test_damage_dispatch.py --check-original-layout --wasm
build-re/venv/Scripts/python.exe tools/re/test_damage_dispatch.py --wasm
```

The layout check reads descriptors from the hash-pinned PE, compares names,
order and numeric metadata, and compiles member-offset/extent assertions using
production headers. Character name fields are encoded in four-byte chunks.
The damage regression extracts the production function and uses explicit test
doubles for dependencies; it is not an original-execution equivalence result.
Keep existing native expected captures and failures intact. Header and RE-tool
changes also require rebuilding and rerunning all existing pilots on native and
Wasm, as specified in [README.md](README.md).

The broader `--check-original-cgame-layout` guard currently **fails on native
and Wasm**: the reconstructed live client-info base in `cg_s` is `0x5e178`,
while the original instruction at `0x7ceae7` uses `0x5f238`. This guard is kept
in `damage-dispatch/original_cgame_layout.cpp`; its failure is not included in
the passing network/server-client contract. The enclosing `cg_s` members need
mapping before that difference can be resolved. No padding has been invented.

Remaining qualification work includes independent execution of the restored
routines and their dependencies, an evidence-backed `CM_Trace` identity, legacy
demo format coverage, and a final Wasm Combat Training pass covering entry,
spawn, movement, firing, bot combat, death and respawn. No browser smoke test or
successful compilation establishes byte-for-byte gameplay equivalence.
