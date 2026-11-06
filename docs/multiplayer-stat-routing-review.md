# Native multiplayer integer-stat routing

The reconstructed integer-stat getter and setter ignored the result of the
training check and always accessed their supplied buffer. A normal client could
therefore read stale NORMAL-bank rank/currency values and write the wrong bank.
The setter also allowed writes while a mod directory was active.

The restored owners route a member with permission `2` to GLOBAL, controller `0`,
with buffer verification enabled, unless the training dvar, the supplied buffer's
training flag, or dedicated-server mode excludes that routing. The setter rejects
a nonempty mod directory before lookup or mutation, with the original null-dvar
allowance. Existing assertions, DDL lookup, integer IO and error behavior remain.

## Original identity and instruction evidence

Reference: `build/BlackOpsMP.exe`, x86 image base `0x400000`, SHA-256
`17304fa6bf0eac57ac8da34c3d30b9106e6cc0d0263b0ce189b9bcd8c34675cc`.
The test verifies these full owner ranges against immutable hashes:

- `LiveStats_GetIntPlayerStatInternal`: RVA `0x1d8440`, 180 bytes,
  SHA-256 `366407c1193eef3261eae2ce9104b8e22b5ae9b30b9ee2fbb34a6ad7c39509f2`.
- `LiveStats_SetIntPlayerStatInternal`: RVA `0x18dcd0`, 192 bytes,
  SHA-256 `87cf131a586482460ab5c3a83b7c979f84ff4c53441380c49f17213f078a0035`.

These identities use instructions, arguments, callers and strings rather than
automatic source/function pairing:

- Both owners build a 16-byte DDL state through RVA `0xe4030`
  (`DDL_MoveToPath`) from root VA `0x4316588`, then invoke the training helper at
  RVA `0x224190` and the dedicated helper at RVA `0x1b1310` in that order.
  The training helper checks dvar VA `0x441274c`, current byte at `+0x18`, then
  tail-calls the buffer's training-flag reader at RVA `0x135c90`. The dedicated
  helper checks dvar VA `0x355e968`, current DWORD at `+0x18`.
- Getter RVA `0x1d849e` and setter RVA `0x18dd3c` load the resolved member, then
  compare its DWORD at `+0x2c` to `2`, matching the production permission field.
  Getter RVA `0x1d84a8` bytes
  `6a 01 6a 06 6a 00 e8 8d 55 e7 ff` push verify=true, bank=6, controller=0,
  and call the storage wrapper at RVA `0x4da40`. The setter has the corresponding
  sequence at RVA `0x18dd46`. Each replaces the buffer before actual DDL IO.
- Setter RVA `0x18dcd0` begins
  `a1 88 a0 83 03 83 ec 10 85 c0 74 0e 8b 40 18 80 38 00 74 06 32 c0`:
  load optional fs-game dvar, dereference its string, reject a nonzero first byte.
- Getter caller RVA `0xd2520` reads the key table at VA `0xc08808`, constructs
  `PlayerStatsList` from string VA `0xa26778`, passes depth `2` and a caller buffer,
  and calls RVA `0x1d8440`. Setter caller RVA `0x73460` constructs the same path
  after fetching NORMAL and calls RVA `0x18dcd0`. Caller RVA `0xb6a50` constructs
  the depth-3 `PlayerStatsByGameMode` path before calling that same setter.
- Their final IO calls are RVA `0x85570` (`DDL_GetValue`) and RVA `0x13c590`
  (`DDL_SetInt`). Both failure paths call RVA `0x227b50` with the matching
  `DDL:Error getting/setting player stat value for %s` diagnostics.

The 372 bytes above are instruction-reviewed original owner bytes, not a count
of byte-identical candidate machine code or original-native replay coverage.

## Regression results and reproduction

Run `build-re/venv/Scripts/python.exe tests/test_stats_bank_routing.py`.
The standalone runner extracts the production owners, their wrappers, real DDL
lookup/integer IO, and the unchanged native operation gate. It decodes a stat
projection from hash-pinned stock `ddl_mp/stats.ddl` in
`build/zone/Common/code_post_gfx_mp.ff`, preserving member offsets and permissions.
It builds with pinned VS x86 compiler 14.37, `/O2 /fp:precise /MT`, assertions on,
and records compiler/linker hashes and source/runner identities under ignored
`build-re/stats-bank-routing/`.

The same test and profile against the pre-change production source reported
33 failing checks out of 585; the candidate reports zero. These are regression
checks, not 585 independent original captures or whole-game coverage. Covered:

- Divergent NORMAL/GLOBAL RANKXP, CODPOINTS, CURRENCYSPENT, PLEVEL and STATS_VERSION
  across all training-dvar, buffer-training-bit and dedicated-mode combinations.
- Zero writes, full persistent-record guards, and stock permission-3 local IO.
- Forced-base snapshot reads and direct-buffer writes that retain their buffer.
- Active-mod rejection preserving both buffers; missing mod dvar remains allowed.
- Existing sign-in, Demonware, fetched-stat and DDL-validation gates stay closed
  when the corresponding controlled fixture prerequisite is absent.

The storage selector and service predicates are explicit fixture bindings; they
provide context inputs and do not execute an original service session. Complete
original-native/Unicorn owner replay is unqualified because the full original
lookup/storage dependency closure has no adapter. No substitute dependency
results are counted as original expected outputs. The existing qualified DDL
integer IO replay is separate from this connected production regression.

## Remaining work

Forced-base helpers and direct-buffer writers intentionally keep their existing
semantics. No `currencyOnSpent` hook exists on this branch; that hook belonged to
the excluded `wasm-zombies` changes. No service-readiness behavior was imported.

Stock award scripts, mode-specific XP eligibility, UI rank refresh, authenticated
fetch/upload and disk persistence are outside this fix. `LiveStats_SpendCurrency`
still returns false without `KISAK_LIVE`; unguarding it would expose additional
counter/record dependencies and does not form part of the qualified change.
This work does not establish a complete XP award or purchase/save path, live
gameplay agreement, instruction similarity, or proven candidate byte identity.
