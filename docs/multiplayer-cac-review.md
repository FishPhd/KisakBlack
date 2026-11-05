# Draft: Restore multiplayer class ownership and validation

Purchasing or validating class equipment could pass an incomplete path to DDL:
the declared path depth was three or four while only two path operands were
supplied. The resulting varargs reads could fail lookup, read unrelated stack
values, or fault. This change restores the original item, attachment, attachment
point, option, new-item and pro-challenge paths across the connected client and
server ownership chain.

The branch is ready for **draft code review**, with gameplay qualification still
blocked. It does not complete the requested persistent class/spawn/progression
path. No MR was opened, pushed or merged.

Review against local `weapon-bytecode` at **a16c447**, using
`git diff a16c447..mp-restoration-nov01`. The remote `origin/weapon-bytecode`
was at `8aea211`, 24 commits behind that base. Those earlier weapon, movement,
bot and layout dependencies are inherited, not new changes in this review.
The implementation and reusable checks are committed in `62597dd`.

The connected repair includes:

- Twenty missing DDL paths in `bg_unlockable_items.cpp`, plus the pro-challenge
  lookup used by both weapon-clear routines in `live_stats.cpp`.
- Both original 72-option clearing loops, preserving the separate 24-attachment
  bound and three pro-challenge flags.
- The server option check's sixth `checkWeapon` argument, its rank-driven flag
  clearing before the free-option check, and the original caller flags.
- The original old/new CAC accounting sequence: an old option without its base
  weapon clears the new option instead of charging for it.

All three requested areas were investigated before selecting this scope.
Existing bot weapon-selection, rechamber, movement-freeze, reaction and vehicle
fixes remain intact. No new bot defect was demonstrated in the reviewed owners.
The full selector's surprising fallback—retaining an invalid nonzero command
weapon when no alternative qualifies—is preserved.

The real player-class route is the stock menu's `equipclasscurrentitem` command,
purchase/ownership checks, `BG_UnlockablesEquipClassToSlot`, `cacLoadouts` DDL,
then stock `_class.gsc` `getdstat` reads and `giveLoadout`. The stock script builds
weapon/attachment names and grants weapons, grenades, equipment and perks; its
cached custom class is reused on respawn. `ui_useCustomClassInfo` selects the
separate custom-game settings route and is not the player-profile path tested
here. The non-PC `GetLoadoutItemFromProfile` stub was not established as a pinned
PC defect and was not changed.

Original identities, exact region hashes, ABI/caller evidence, asset hashes and
the extracted v100 schema projection are in
[`cac_ownership_reference.json`](../tests/cac_ownership_reference.json).
Representative original RVAs are item purchase `0x3b94b0`, attachment purchase
`0x3b9670`, option purchase `0x3b97a0`, option validation `0x2449d0`, client/server
weapon clearing `0xae740` / `0x237c50`, and pro-challenge lookup `0x2b8070`.
The standalone source server item setter is only an inline region inside the
original clear owner; it is not counted as another original function.

Validation results and binary/build identities are recorded compactly in
[`mp_restoration_validation.json`](../tests/mp_restoration_validation.json):

- Native game build passes. Base and final candidate use the same recorded
  MSVC 14.37 x86 Release profile, SDK, libraries, definitions, optimization and
  floating-point settings. EXE/PDB, compiler/linker, library and source identities
  are recorded. The base build reproduced the archived a16c447 binary identities.
- Production integration passes with actual DDL lookup and bit serialization
  against metadata decoded and checked from the pinned stock fastfile. It covers
  ownership reads/writes, indexed boundaries, all 72 resets, pro flags, rank
  validation, class slot writes/readback and weapon-change attachment resets.
  Its isolated buffer round trip is not a service or disk persistence test.
- Full `Bot_UpdateWeapon` replay: **1,886 cases**, zero native-original/Unicorn/
  selected-production differences. The complete dependency closure declares
  **939 original bytes**; **925** were executed in the corpus. Weapon zero,
  ammo/reserve lookup, held-slot ordering and preservation guards are included.
- DDL integer IO replay: **140 transitions / 35 stock ownership paths**, zero
  native-original/Unicorn/production differences, with every 40,960-byte buffer
  compared. It declares **716 original bytes**, of which **519** were executed.
  Original `MoveTo` and complete CAC callers remain unqualified: the lookup's
  CRT/locale dependency closure is unsupported, not supplied with fake results.
- Existing native catalog: **56 units / 224,870 cases** pass. All 111 contract
  tests pass, the deliberate mutation is detected, and all 1,886 prior capture
  artifacts remain unchanged. Case totals are not whole-game coverage measures.
- Whole-EXE structural score: **14.133471% → 14.182762%**, **+0.049291 percentage
  points**, at unchanged confidence `0.7` and denominator 5,506,261 original
  decoded function bytes. The differently configured saved-main build is unused.
- Independent instruction/byte comparisons cover **23 function pairs / 7,428
  original bytes**. Per-pair instruction counts, raw positional byte differences
  and mnemonic edit diagnostics are recorded. **Zero complete pairs are
  byte-identical.** PE HIGHLOW normalization finds no operands here and therefore
  changes none of the raw comparisons; relative calls and arbitrary addresses
  are not normalized. Mnemonic edits ignore operands and do not prove semantics.
- The large CAC caller is excluded because Ghidra splits its native body after
  an assertion. Automatic false pairings are reported separately, including the
  high-confidence client-clear/server-clear confusion. They do not alter the
  existing whole-EXE metric or acceptance threshold.

The final native smoke used `build-re/multiplayer/nov01/profile`, entered Summit
TDM through Combat Training, selected SMG, and displayed the stock MP5K/ASP
loadout while bots moved, fought and scored. Human movement/fire was issued.
Create-a-Class remained locked on the fresh profile. The process then failed
with Windows event 1000: **0xc0000005**, address **0xc1f80000**, unknown module,
PID 12520 at 2026-11-05 09:40:06 Pacific. Its cause and relationship to this
change are not established. Custom-class edit/save/spawn, human death/respawn,
human XP/rank display and profile-reload persistence are **not qualified**.
The log remains local under the isolated profile; no extra boots were used to
conceal the failure.

Progression remains incomplete. Stock `_globallogic.gsc`, `_rank.gsc` and
`_persistence.gsc` own mode eligibility, award amounts, XP caps, rank/unlock
updates and `setdstat` mutations. Combat Training enables ranked progression;
ordinary private/offline/wager gates must remain in place. No replacement C++
award rules were added. The native integer stat getter/setter at `0x1d8440` and
`0x18dcd0` still omit original permission-2 global-bank routing, and the setter
also omits the original mod-directory guard. In this non-`KISAK_LIVE` build,
service gates also block typed stat operations, incoming stat updates and
Demonware storage. Training storage's original `mpstatsBasicTraining` user file
has no functioning local-disk substitute here. Rank-table construction was not
shown defective and was left intact. Unlock-all, fabricated XP and successful
service stand-ins were not introduced.

The secondary Wasm replay build is blocked by the absent
`src/web/compat/kisak_web_compat.h` on this branch. No Wasm pass is claimed.

The new standalone checks require the repository, the pinned original game
assets, VS2022 x86 toolset 14.37 and the packages in
[`fidelity-requirements.txt`](../tests/fidelity-requirements.txt). From the repo
root, run:

```powershell
python tests/test_cac_ownership.py
python tests/replay_cac_ddl.py
python tests/replay_bot_weapon_selection.py
```

They extract current production definitions, preserve content-addressed original
outputs under ignored `build-re/`, and terminate on unsupported dependencies.
The schema fixture is checked against the original serialized asset, including
the retained member records. EXEs, PDBs, raw fastfiles/scripts, capture archives
and full analysis reports are excluded from the branch.

For scoped static reproduction, analyze each exact EXE with its adjacent PDB
using the existing local original-code workflow. Run the included
`tests/ExportMultiplayerFunctions.java` on each saved Ghidra project with output
`scoped.json` and the complete-owner names from the reference JSON as subsequent
arguments. Then run:

```powershell
python tests/compare_mp_functions.py --base <base-dir>/KisakBlack.exe --candidate <candidate-dir>/KisakBlack.exe --out <local-report.json>
```

The existing 56-unit catalog and whole-EXE scan additionally require the local
`tools/re/` workflow documented at the start of this task. The newly committed
standalone checks do not depend on that ignored harness.
