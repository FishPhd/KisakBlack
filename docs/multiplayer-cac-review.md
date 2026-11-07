# Draft: Restore multiplayer ownership, weapon updates and stat completion

Class equipment ownership calls supplied fewer DDL operands than their declared
path depth, so lookup could read unrelated stack values or fail. The weapon frame
also omitted the original quick-scope updater, and integer stat access ignored
the original GLOBAL-bank routing rule. Server stat completion was also compiled
out, and its personal-best helper omitted result rows. This branch restores
those connected native paths using the pinned original executable and assets.

The branch is ready for draft code review. The persistent custom-class/spawn/XP
path remains incomplete, and the final gameplay smoke failed. No MR has been
opened or merged. Review `git diff a16c447..mp-restoration`; the remote
`origin/weapon-bytecode` was 24 commits behind that intended base. Earlier weapon,
movement, bot and layout repairs are inherited dependencies.

The implementation commits are `62597dd` (class ownership), `c779e67`
(quick-scope chain), `5c53093` (integer stat routing), and `13416d8` (server
stat completion/personal-best chain). All non-Wasm local branch
tips were already ancestors of the restoration history; redundant local names
were removed after verification. Their histories are retained, remote branches
were not deleted, and an ignored recovery bundle is at
`build-re/consolidation/nov05/branches-before.bundle`.
`wasm`, `wasm-decomp` and `wasm-zombies` remain separate at their existing tips.

## Resulting native behavior

- Ownership and validation pass all original item, attachment, attachment-point,
  option, new-item and pro-challenge operands to DDL. Both weapon-clear routines
  reset 72 options, preserving the separate 24-attachment bound and three pro
  flags. The server option check restores its sixth `checkWeapon` argument,
  rank-driven clearing before free-option checks, and exact caller flags.
  Old options without their base weapon are cleared rather than charged again.
- Each weapon update snapshots ADS fraction before ADS interpolation, then calls
  the original quick-scope update after hold-breath. The complete updater handles
  scope/animation eligibility, ADS entry/release, timer decay and spread scale,
  including original integer wrapping and floating-point operation order.
  `player_aqs_scale` has its original default 8, range 0..50 and flags `0x80`.
  The existing `bg_aqs` gate and its original default remain unchanged.
- Integer stat reads/writes route permission-2 members to GLOBAL unless the
  training dvar, supplied-buffer training flag or dedicated mode excludes that
  routing. The setter rejects a nonempty mod directory before mutation. Explicit
  forced-buffer readers and direct-buffer writers retain their own semantics.
- Validated server stat completion again updates item/pro NEW flags, item and
  global/mode personal bests, and highest accuracy/KD. Personal-best rows retain
  the original prefix, unsigned deltas and 1,000-row bound. Accuracy widens
  before multiplication. This owner uses GLOBAL/GLOBALSTABLE even in Combat
  Training; stock script XP/mode rules remain unchanged.

All three requested areas were investigated. Prior bot weapon-selection,
rechamber, reaction, movement-freeze and vehicle-classification fixes are
preserved. No new bot AI change is claimed. The complete bot selector retains
the original fallback, including a nonzero ineligible command weapon when no
alternative qualifies. All 47 weapon switch routes already match the original
compressed jump table; the historical switch-misrouting warning was stale.

## Authority and path ownership

Reference EXE SHA-256:
`17304fa6bf0eac57ac8da34c3d30b9106e6cc0d0263b0ce189b9bcd8c34675cc`.

Original identities use instructions, callers, ABI, offsets, registration data
and strings rather than automatic function pairing:

- [Class ownership evidence](../tests/cac_ownership_reference.json) pins owner
  bytes, stock asset/schema hashes and path projections. Representative RVAs are
  item purchase `0x3b94b0`, option validation `0x2449d0`, client/server clearing
  `0xae740` / `0x237c50`, and pro lookup `0x2b8070`.
- [Quick-scope evidence](../tests/quickscope_update_reference.json) pins the
  complete updater at `0x3bf320` (350 bytes), caller at `0x1cf0e0`, ADS snapshot
  and ordered calls, original switch tables and dvar registration instructions.
- [Stat-routing review](multiplayer-stat-routing-review.md) establishes complete
  getter/setter identities at `0x1d8440` / `0x18dcd0` (180 / 192 bytes), field
  permissions, training/dedicated/storage callees and the mod-directory guard.
- [Personal-best review](multiplayer-personal-bests-review.md) establishes the
  server owner `0x222960` (1,036 bytes), result helper `0x509580` (256 bytes),
  accuracy owner `0x50b010` (242 bytes), caller ABI, bank offsets and stock paths.

Player Create-a-Class follows the stock menu's `equipclasscurrentitem`, native
ownership/purchase checks, `BG_UnlockablesEquipClassToSlot`, `cacLoadouts` DDL,
then stock `_class.gsc` `getdstat` and `giveLoadout`. Scripts construct weapon
and attachment names, grant equipment/perks and reuse the cached class on
respawn. `ui_useCustomClassInfo` selects separate custom-game settings.

Stock `_globallogic.gsc`, `_rank.gsc` and `_persistence.gsc` own award events,
mode eligibility, XP caps, rank/unlock updates and `setdstat` mutation. Their
rules and rank tables were not replaced. Combat Training and private/wager
mode gates remain intact; no fabricated XP or unlock-all was added.

## Verification

The current [validation record](../tests/mp_personal_bests_validation.json)
contains source/EXE/PDB/profile identities, per-function instruction/byte
differences, false pairings and smoke limitations. The
[previous record](../tests/mp_restoration_followup_validation.json) retains the
previous candidate; the [initial record](../tests/mp_restoration_validation.json)
contains detailed pinned compiler/linker/library identities.

- Final native x86 Release build passes. The intended base and candidate use
  identical MSVC 14.37, SDK 10.0.22000, libraries, definitions, optimization and
  floating-point settings; profile SHA-256 is unchanged. No saved-main build
  with different settings is used.
- Complete quick-scope update and spread-consumer replay: **3,536 cases**, zero
  original-native/Unicorn/selected-production differences. The dependency closure
  declares **715 original code bytes**, **699 executed**. Adjacent ADS/duration
  boundaries, viewmodel selection, signed timer extremes and repeated frames
  are covered. All other player-state, asset and setting bytes are guarded.
  Full `PM_Weapon` and ADS interpolation are not replay-qualified; caller order
  and registration are separate instruction audits.
- Stat-routing regression with actual stock DDL and production lookup/IO:
  **33 failed checks before, zero after, out of 585**. This uses explicit fixture
  storage/service context. Its **372 reviewed original owner bytes** are not
  qualified original-native owner replay; unsupported dependencies block it.
- Personal-best helper: **224 cases**, zero original-native/Unicorn/production
  differences, **972 declared / 713 executed original bytes** with real integer
  DDL dependencies. Accuracy arithmetic inline: **201 cases**, zero differences,
  **90 declared/executed bytes**. Full server/accuracy owner replay remains
  unqualified. Server production integration with real stock DDL improves from
  **7 failures / 15 checks to 0 / 18**; catalogue context is an explicit fixture.
- Production class ownership/serialization tests pass. Full bot selector:
  **1,886 cases**, zero differences, **939 declared / 925 executed original
  bytes**. DDL integer IO: **140 transitions / 35 paths**, zero differences,
  **716 declared / 519 executed original bytes**, with whole buffers compared.
  Original lookup/CAC callers and service persistence remain unqualified.
- Existing native catalog: **56 units / 224,870 cases** pass. All **111** contract
  checks pass, and the deliberate mutation is detected. Original expected
  outputs remain immutable. Case totals are not whole-game coverage.
- Whole-EXE structural score: **14.133471% → 14.189507%**, **+0.056036 percentage
  points**, at unchanged confidence `0.7` and denominator 5,506,261 original
  decoded function bytes. The previous candidate scored 14.249257%; this chunk
  regresses that structural score by 0.059750 points. The metric is unchanged.
- Independent comparisons cover **29 pairs / 10,794 original bytes**. **Zero
  pairs are byte-identical**. PE HIGHLOW normalization finds no operands here,
  so it changes none of the raw comparisons. Relative calls, registers and other
  address operands are not normalized; mnemonic edits are diagnostics only.
- Automatic false whole-function pairings are separate from the unchanged
  metric. Two accepted examples confuse client/server clear owners and the
  integer-stat owner with a native wrapper containing inlined logic. The large
  CAC caller remains excluded because Ghidra splits its native body at an
  assertion. The quick-scope inline candidate region is not counted as another
  complete native function pair.

## Gameplay and remaining gaps

The corrected stock Combat Training setup sets `onlinegame 1` in addition to
the training flag and stock menu/config setup. This passes the earlier
initialization blocker; the old Nov06 startup omitted that prerequisite.
The final candidate reached the CT lobby with CREATE A CLASS correctly locked,
started via the stock START MATCH action, selected stock SMG through the UI and
spawned with MP5K/ASP, grenades and the displayed stock perks. Bots fought and
ran script stat updates. Ordinary movement/firing commands were enqueued.

At 18:19:35 Pacific the final process crashed with an access violation at
native RVA **0x55ffd8**, reading **0x14** in `gjk_query`. Its captured stack
reaches `gjk_query_cached`, `gjk_trace`, `PM_gjk_ground_trace`,
`AIPhys_GroundTrace` and `Actor_Physics_1`. The earlier diagnostic captured an
invalid cluster-link read in `process_cluster_environment_collision` (RVA
`0x53e300`, EDI `0xffffffff`, read `0x2f`). Lifetime/shape root causes are not
proven. No collision bypass or assertion suppression was added.

The final launch did not honor the requested isolated home directory; it wrote
logs to `build-release/main`. Destructive persistence checks were therefore
not performed. Custom-class editing/saving, sustained human combat,
death/respawn, human XP/rank UI updates and reload persistence remain
unqualified. Initial CT setup used the native Cbuf API, so the complete stock
frontend navigation is also unqualified. No clean combined gameplay pass is
claimed. Earlier failed captures remain in their historical validation records.

Sign-in, fetched-stat and DDL-validation requirements remain explicit.
`LiveStats_SpendCurrency` is still gated without `KISAK_LIVE`; counter/record
dependencies require further work before a complete purchase path can be
qualified. Authenticated fetch/upload and original `mpstatsBasicTraining`
storage lack a functioning local backend here. Persistent class/XP behavior and
the native crash remain open. No external-service success stubs were introduced.
Secondary Wasm replay is blocked by the missing
`src/web/compat/kisak_web_compat.h`; no Wasm pass is claimed.

## Reproduce the scoped checks

Use the pinned original assets, VS2022 x86 toolset 14.37 and the packages in
[fidelity-requirements.txt](../tests/fidelity-requirements.txt). From repo root:

```powershell
python tests/test_cac_ownership.py
python tests/replay_cac_ddl.py
python tests/replay_bot_weapon_selection.py
python tests/test_stats_bank_routing.py
python tests/replay_quickscope_update.py
python tests/replay_personal_bests.py
python tests/replay_match_accuracy.py
python tests/test_server_personal_bests.py
```

For each exact saved native EXE/PDB, export named bodies with
`tests/ExportMultiplayerFunctions.java` from its Ghidra project, then run
`tests/compare_mp_functions.py` with `--mapping` pointing to
`tests/cac_ownership_reference.json`, `tests/mp_followup_reference.json` and
`tests/personal_bests_reference.json`
separately. Undecodable or missing boundaries block comparison.
The existing local `tools/re/` workflow reproduces the catalog and whole-EXE
scan. Generated EXEs/PDBs, profiles, captures, fastfiles and bulky reports remain
outside the MR. Behavioral agreement, instruction similarity and proven byte
identity are separate; this branch makes no whole-game fidelity claim.
