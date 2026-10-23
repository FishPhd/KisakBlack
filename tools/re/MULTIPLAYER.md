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
- Original snapshot stride `0x20300`, light stride `0x170` with aligned matrices,
  and view-origin/axis boundaries. These restore the live client-info base
  `cg+0x5f238` without inserting speculative filler.
- Original vision-settings records of `0x2a0` bytes, eight cache slots and six
  channels. Names occupy the separate cache-name table; the reconstructed
  embedded `name[64]` incorrectly enlarged every copy and channel record.
- Server volumetric fog occupies 17 floats at `cg+0x718ec..0x7192c`, in renderer
  argument order. Parsing and script restoration now use that per-client state
  instead of a separate global with the renderer's different `GfxFog` layout.
  With these corrections, `sizeof(cg_s)` matches the original `0x71980` allocation.

Run the small checks without launching a game:

```powershell
build-re/venv/Scripts/python.exe tools/re/test_damage_dispatch.py --check-original-layout
build-re/venv/Scripts/python.exe tools/re/test_damage_dispatch.py
build-re/venv/Scripts/python.exe tools/re/test_damage_dispatch.py --check-original-cgame-layout
build-re/venv/Scripts/python.exe tools/re/test_damage_dispatch.py --check-original-cgame-extent
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

The `--check-original-cgame-layout` offset guards and the separate
`--check-original-cgame-extent` size guard now **pass on native x86 and Wasm**.
The former `0x5e178` client-info failure and intermediate `0x71f80` size failure
remain historical observations in the evidence inventory and local logs.
Passing these boundaries does not qualify every interior member or independently
execute the affected game functions. Optional vision debug labels now use cache
lookup and show `<blended>` for unnamed settings; that diagnostic difference is
recorded separately and remains pending review.

## Full native comparison with the default branch (2026-10-23)

The full scans against the same pinned original measured **12.527808%**
demonstrated structural similarity for default branch `master` at `306bb14`,
and **14.242513%** for `wasm-decomp` at `6213c96`: **+1.714705 percentage
points**. Accepted original-function byte coverage increased from **17.948045%**
to **20.439439%**. Both used confidence threshold `0.7` and the same
5,506,154-byte original-function denominator.

These are whole-branch Release observations, including earlier platform/source
changes, compiler effects and heuristic matches. They do not isolate this commit
or establish exact machine-byte agreement. Both builds used MSVC x86 toolset
14.37.32822, `/O2` and disabled stack-cookie instrumentation. The archived master
source needed its missing `setjmp.h` declaration and `jmp_buf` argument cast
supplied through a build-only forced include; its source files were unchanged.
The branches retain their different native dependency wrappers.

Full local reports are retained at
`build-re/binary-diff/reports/run-1792773398033490000/` (master) and
`build-re/binary-diff/reports/run-1792773372152870500/` (decomp).
Binary/report hashes and numeric results are recorded in
`last_full_branch_comparison` in the evidence inventory. All eight existing
behavioral pilots also passed 6,677 original-capture cases per backend on native
x86 and Wasm; their original contracts remain narrower than this whole-EXE scan.

## Wasm Combat Training check (2026-10-23)

The menu route PLAY → COMBAT TRAINING → START MATCH loaded Summit
(`mp_mountain`) and reached class selection. The first run then failed with
corrupt user-command timestamps and `EXE_ERR_NOT_RECEIVED`. Its local browser
capture is retained under `build-web/combat-training-check/`.

Client, cgame and server allocations still used literal sizes from before the
layout restoration, allowing adjacent buffers to overlap. Allocation, clearing
and reservation now use matching compiled structure sizes for the affected
buffers. Their relationship to original PC storage and remaining layout/capacity
differences is recorded in scoped `exceptions` in the evidence inventory; this
memory-safety fix does not establish original allocation equivalence.

After rebuilding, a fresh menu traversal selected SMG and spawned one human
with 11 bots. Camera and movement responded (roughly 185 units of displacement),
ADS reached 1.0, and attack input was issued. The connection remained active
through another 14.8 seconds of server time. The complete retest browser log
contained no invalid-command-time, missing-reliable-command or corrupt-network
errors. Result/state JSON, build hashes and screenshots remain local under
`build-web/combat-training-retest/`; the test used the normal training menu and
retained its stock first-use introduction/difficulty dialogs.

Remaining qualification work includes independent execution of the restored
routines and their dependencies, an evidence-backed `CM_Trace` identity, legacy
demo format coverage, unchecked interior `cg_s` members, and extended combat/death/respawn
coverage. This entry/control smoke test and successful compilation do not
establish byte-for-byte gameplay equivalence.
