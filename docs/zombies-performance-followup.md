# Zombies performance follow-up

Updated: 2026-10-14 UTC. Owner: second agent responsible for performance follow-up.

This report preserves findings from existing game logs while Zombies correctness work proceeds. Historical sessions use different builds and cache conditions. A warning or a slow load is evidence of that event; it does not establish the cause of Zombies crashes or sustained frame slowdown.

## Findings

### PERF-001 — Long Zombies asset loading phases

Status: observed loading cost; regression and cause unconfirmed. Priority: investigate after gameplay correctness.

The October 12 runtime log contains these measured asset loading durations:

- `common_zombie`: 9,426.7 ms; inflate 536.9 ms; total logged load 9,429 ms. Evidence: `tmp_zombie_runtime_temporaryclient.log:584-585`.
- `zombie_theater`: 15,579.2 ms; inflate 633.6 ms; total logged load 15,582 ms. Evidence: `tmp_zombie_runtime_temporaryclient.log:7521-7522`.
- `code_post_gfx_mp`: 3,687.9 ms; total logged load 3,690 ms. Evidence: `tmp_zombie_runtime_temporaryclient.log:270-271`.

An earlier run logged `common_zombie` assets at 406.7 ms and `zombie_theater` assets at 620.5 ms (`tmp_zombie_runtime_report.log:526` and `:7449`). These runs are not a controlled benchmark: build, instrumentation, cache and browser conditions have not been matched. Do not report their ratio as a regression.

The timing producer is `src/database/db_file_load.cpp:511`. Its asset phase covers asset loading and is separate from its inflate and delayed image phases; do not add those timings to infer an independent wall-clock total. Follow-up: reproduce loading on the same build with recorded cache state, capture worker CPU samples during the expensive phase, and identify asset types or archive reads dominating the load. Record cold and warm runs separately.

### PERF-002 — Shared physics pool exhaustion with fatal trap

Status: confirmed historical resource failure; Zombies reproduction unconfirmed. Priority: high for shared-engine follow-up.

At `2026-10-14T04:10:25.359Z`, `build-web/agent-session/browser.jsonl:790710-790712` records an assertion in physics memory allocation, `"no_error" - phys_free_list error: out of memory.`, then a Wasm unreachable trap. The stack includes `Phys_CreateBodyFromState`, `Phys_ObjCreate`, `DynEntCl_CreatePhysObj`, dynamic entity activation and the physics worker.

Target: `021C96019A5506EA4F6E4724A0AD3912`. Its saved state (`build-web/agent-session/last-state-021C96019A5506EA4F6E4724A0AD3912.json`) identifies map `mp_havoc`, gametype `gun`, 18 bots and one player. This was multiplayer; it must not be presented as a confirmed Zombies failure.

Follow-up: inspect dynamic entity body activation and pool release across map transitions, measure allocated/free pool counts and their high-water marks, and reproduce repeated map loads plus sustained combat. Compare Zombies actor/ragdoll-heavy rounds using the same counters. Diagnose lifetime or capacity before changing pool limits.

### PERF-003 — Decal triangle capacity warnings

Status: confirmed capacity warnings; frame impact and Zombies attribution unconfirmed. Priority: investigate if reproduced with visible loss or frame spikes.

The shared browser archive contained 257 `R_MarkFragments: Too many triangles to mark, max 255` events at audit time. Example: `build-web/agent-session/browser.jsonl:841535` at `2026-10-14T06:09:42.511Z`, radius 100 at `(-863, 1691, 189)`. The corresponding target `87665444F1C0FD06DF890992B7B303C9` has saved map `mp_crisis`, gametype `dm`. Other saved targets identify multiplayer maps or lack map state. No event has yet been positively attributed to Zombies.

The warning producer is `src/gfx_d3d/r_marks.cpp:899`; it reports the mark operation exceeding triangle capacity. It does not measure CPU/GPU duration. Follow-up: deduplicate by map, origin and radius; correlate with action and frame timing; inspect expensive or truncated marks only when impact is reproduced. Keep the capacity warning and any measured frame problem as separate findings.

## Excluded noise

- Missing `code_warning_soundcpu`, `code_warning_serverfps`, `code_warning_collision` and similar materials are asset load errors, not proof that the named performance limit was exceeded.
- Redundant assets and localization names containing `OVERFLOW`, `WARNING` or `SLOW` are not actual overload events.
- Chrome update-key and Google Cloud Messaging quota errors concern browser services and are not established game performance issues.

## Capture and handoff

For each fresh Zombies run, preserve engine and browser logs, build hashes, map, round, player/actor counts where available, resolution/backend, cache state, actions and timestamps. Keep raw evidence under a unique run directory so later sessions cannot overwrite it. Extract only events from that run and target; this shared archive spans multiple modes.

Use the existing `tools/web/cdp.mjs perf` command when symptoms occur in an active visible match. It captures median/p95/p99 presented-frame intervals, intervals over 50 ms, render/presentation/copy timing and fastfile metrics. `tools/web/profile_renderer.mjs` captures CPU samples across engine workers. Preserve its profiles and summary with the run. Do not background the tab during frame measurements.

Append deduplicated findings with: stable ID, status, priority, build/run, map/round/interaction, first/last timestamps, count, exact message, source file and line, observed metric, attribution confidence and next investigation. Mark a fresh clean run explicitly as having no new performance findings. Performance fixes are separate follow-up work unless a resource failure directly blocks Zombies correctness.

## Fresh Zombies validation

### KINO-BASELINE-20261014 — Fresh baseline

Session started `2026-10-14T06:21:57.369Z`; Kino gameplay was observed from approximately `06:27Z`. Target: `99A534995DDE5BC57F94900D57E0BF15`; document: `628e1646486d4e07bd28a1e24bd65ea9`. Raw evidence remains in `build-web/agent-session/browser.jsonl`; saved state is `build-web/agent-session/last-state-99A534995DDE5BC57F94900D57E0BF15.json`. Snapshot at `06:28:46.194Z` identifies `zombie_theater`, `zclassic`, one player, zero bots, active connection 10 and an empty crash field. Health was zero, so this is evidence of an active engine, not a successful survival or interaction acceptance test. Round and interaction actions are not captured in that snapshot.

Session metadata records visible Chrome, WebGL2 and these build hashes: Wasm `f3714d6d6ea49230cc81261fe646a62a7611b45142b5a4d8a515642392676dfd`; loader `4560319d3788906aa9525f51dc32ca171a6fbf6152aa82420c0bc0cd133936a5`; shell `c35212e5df1bf805dc64d4e357d3f0aad2b936407e103cb0bcfedd9573f4a25b`. Cache state and frame percentiles were not established for this baseline.

### PERF-004 — Moving hidden parents with children

Status: confirmed Zombies optimization diagnostic; frame cost unmeasured. Priority: performance follow-up after interaction correctness.

`browser.jsonl:1072288` at `06:27:59.185Z` reports `Ent #308 of type 6 is SVF_NOCLIENT, but has children and seems to be moving. There is a potential optimization here.` The fresh target contains 140 copies of this diagnostic through the audit ending around `06:28:48Z`, across entities 301, 302, 308, 314, 315, 316, 317, 318 and 319; entity 308 accounts for 15. Counts describe this bounded capture, not the eventual complete session.

Producer: `src/game_mp/g_main_mp.cpp:2910`. Follow-up: identify those parents and their linked children, measure transform/update cost and logging volume, then preserve attachment motion and map interactions while removing demonstrated unnecessary work. The diagnostic alone does not prove a frame-rate problem; do not clear visibility or unlink children merely to silence it.

### PERF-005 — Zombies heap high-water observation

Status: measured program-break/heap-size observation; exhaustion and leak unproven. Priority: capacity monitoring during extended validation.

The fresh run logs `heap-high-water=2497MB of 3072MB` at `browser.jsonl:1072220` (`06:27:28.033Z`) and again at `:1072488` (`06:28:48.239Z`). This is approximately 81.3% of the reported heap size with a 575 MB difference. The producer at `src/web/web_main.cpp:1065-1067` derives the first value from `sbrk(0)` and the second from `emscripten_get_heap_size()`. These values are not equivalent to live asset memory, browser process working set or remaining allocatable memory, and the second value alone does not establish a configured maximum.

Follow-up: capture this value over repeated restart/map-transition cycles and longer rounds alongside live pool usage. Determine whether growth stabilizes before labeling it a leak; correlate any allocation failure with its allocator and pool. This observation is separate from PERF-002's historical multiplayer physics pool failure.

### Correctness evidence retained for the primary agent

The fresh target contains 20 obstacle assertions (`actor_mp.cpp:2598`, first at `browser.jsonl:1072240`) and nine path-length assertions (`actor_mp.cpp:1379`, first at `:1072241`) through the same audit window. The messages concern the `ENTITYNUM_NONE` obstacle flag and current path length relative to the preceding segment. These are confirmed Zombies AI correctness issues; CPU cost, retry loops and frame impact have not been measured. The primary correctness agent owns diagnosis and repairs. Performance follow-up should profile only if repeated failures or excessive retries remain after repair.

Two archived Zombies `VEH_TouchEntities` memory-access traps are retained at `browser.jsonl:808864` (`05:56:26.395Z`) and `:1061760` (`06:19:14.066Z`), both on target `87665444F1C0FD06DF890992B7B303C9`. That target spans mode changes, so its final multiplayer snapshot does not classify these earlier events. The primary agent identified these as Zombies crashes and is repairing vector bounds in vehicle entity touching. Their root cause and validation belong to correctness work; neither trap establishes a performance slowdown.
