# Restore server stat completion and personal-best results

Native builds compiled `SV_UpdatePersonalBestsForClient` to a return. After
`SV_AddModifiedStats` finishes a client's dirty-stat transmission, that call
therefore skipped rank/pro new-item flags, item records, global/mode personal
bests and highest accuracy/KD processing. Its original local algorithm has no
external-service calls. Restore the owner, its missing `ItemStats/<id>/stats`
operand, and the connected personal-best helper. Existing validation and
demo/test-client gates remain in place.

The helper also lost its sixth prefix argument and result-row construction.
Nonzero match deltas now append the original name, mode prefix, value and delta;
rating is left untouched. Zero deltas do not append. Unsigned subtraction and
best comparisons preserve wrapped/large counters. The original 1,000-row limit
and diagnostic are retained. Both the server owner and client comparison caller
pass the original empty/global or game-mode prefix.

Accuracy previously multiplied in 32 bits before dividing. For 450,000 hits out
of 500,000 shots it produced 410 instead of 9,000. Widen before multiplying by
10,000, divide with the original double precision, truncate to signed 64 bits,
then retain the low DWORD. The original KD owner intentionally uses wrapping
32-bit multiplication; it is unchanged.

## Original identities

[personal_bests_reference.json](../tests/personal_bests_reference.json) pins the
original executable, owner byte hashes and immutable replay output hashes.
Identities were established from instructions and callers, independently of
BinDiff names:

- Server owner `0x222960`, 1,036 bytes: `client_t` stride `0x7c2e8`, validated
  flag at `+0x7c280`, GLOBAL/GLOBALSTABLE at `+0x66e69`/`+0x70e69`, RANKXP/PLEVEL
  reads and rank calls, 256-item/clan loops, pro-version transition, and the
  complete global/mode iteration. The DDL call pushes three path operands,
  including the literal `stats`. The count is cleared at the original point
  after item/clan processing.
- Personal-best helper `0x509580`, 256 bytes: best state in ESI and five stack
  operands (controller, live, stable, stat state, prefix), count at RVA
  `0x3f165ac`, 20-byte rows at `0x3f02090`. Enum-name lookup, row stores, unsigned
  comparisons and DDL calls establish its identity. Client call sites at
  `0x1ae182`/`0x1ae381` supply the empty/mode prefixes.
- Accuracy owner `0x50b010`, 242 bytes: TOTAL_SHOTS/HITS/HighestStats lookup
  sequence and x87 arithmetic. The unsigned conversion/multiply/divide/FISTP
  inline at `0x50b07d..0x50b0d7` is 90 bytes. Constants are the original 2^32
  correction and 10,000 multiplier.

This server owner explicitly uses GLOBAL even during Combat Training. It must
not be changed to the training bank based on a plausible name. Stock scripts
continue to own XP awards, eligibility, caps, ranks and `setdstat` events. This
change does not grant XP, replace rank tables, authenticate storage or complete
Combat Training persistence.

## Qualification

- Original-native / Unicorn / selected production helper replay: **224 cases**,
  zero differences. **972 declared / 713 executed original bytes**, including
  the real original integer DDL closure. Compare all 1,000 result rows and both
  40,960-byte buffers; normalize only fixture pointer identities. Valid states,
  zero/nonzero deltas, signed extremes, unsigned comparisons, prefixes and row
  999 are covered. Diagnostics and invalid/cap-exceeded inputs are excluded.
- Accuracy inline replay: **201 cases**, zero differences across all three
  backends, **90 declared/executed bytes**. A native harness RET is placed at the
  following-call boundary outside those bytes; Unicorn stops at that boundary.
  Production bindings supply operands. This does not qualify original lookup
  dependencies or the complete accuracy owner.
- Server production integration uses the hash-pinned stock DDL projection and
  real lookup/IO, item/pro ownership, rank algorithm and record helpers. It
  checks validation, GLOBAL selection, global/mode rows, item/pro NEW flags,
  default-item exclusion, accuracy and unchanged stable/normal banks. Before:
  **7 failures / 15 checks**; after: **0 / 18**. The extra checks inspect rows
  only after the restored owner creates them. Four DDL range warnings are
  expected from real short-field rejection; they are not turned into success.
  Rank thresholds/item metadata are controlled fixture inputs. Missing clan
  metadata is unsupported and accidental use aborts. Full original-native
  server-owner replay remains blocked by its original lookup/catalogue closure.

Run `tests/replay_personal_bests.py`, `tests/replay_match_accuracy.py` and
`tests/test_server_personal_bests.py` with the pinned x86 toolchain and original
assets. The first two reject changed original output hashes. Generated captures
are content addressed and never substituted from production outputs.

Coverage overlaps: the helper's 972-byte closure includes the existing
716-byte DDL closure, and the 90-byte accuracy inline is inside its 242-byte
owner. Do not add these figures as independent whole-game coverage.

The [current validation record](../tests/mp_personal_bests_validation.json)
records source/build identities, the unchanged whole-EXE metric, independent
instruction/byte differences, false pairings and failed gameplay qualification.
No byte-identical function or complete custom-class/XP persistence flow is
claimed.
