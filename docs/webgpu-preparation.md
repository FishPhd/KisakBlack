# WebGPU preparation and qualification

Full preparation is the multiplayer WebGPU default. Preparation completion is a workload result, not a guarantee of a particular FPS or of coverage for every possible gameplay interaction. Zombies retains normal preparation; normal WebGL keeps warming disabled while full WebGL preparation uses the existing program warmer.

## Startup and map loading

The game synchronizes frontend asset loading, snapshots owned texture references outside the database lock, uploads authored texture levels, and submits resource-only batches. GPU callbacks fence those batches independently of presentation. Runtime preparation imports exact portable and learned recipes using the same normalized descriptors as actual draws. Candidate enumeration is available only with `gpuCapture=1&gpuEnumerate=1`. Utility blit, depth-copy and partial-clear pipelines compile asynchronously before readiness is released. Graphics-setting changes trigger preparation again.

Full preparation also snapshots the world's vertex/layer buffers and every loaded model's static vertex/index buffers. It uploads them through bounded resource batches before the final queue fence. Dynamic geometry keeps its existing per-frame uploads. Telemetry reports prepared buffer counts and `firstStaticBufferUploads` separately, so first-use mesh work cannot hide inside ordinary frame upload totals.

Engine-owned images and geometry are not all database assets. The same gate also visits built-in images, the static-model cache buffer, and particle-cloud vertex/index buffers. Independent playback exposed `$model_lighting` and the two particle-cloud buffers as first-use uploads at spawn; preparing these removed those uploads on the tested Nuketown route.

Resource preparation coalesces work until 4 MiB or 512 commands, then flushes at each collection boundary. Empty batches do not schedule queue work unless they carry a completion fence. The final readiness fence remains ordered after all images, buffers and utility requests. `resourceBatches`, `resourceCommands`, and `resourceBatchHighWater` are cumulative device counters in warmup telemetry. One oversized resource drains alone; queued work remains bounded instead of admitting more uploads on top of it.

Full mode loads the high mip set allowed by the existing picmip settings and keeps loaded static images resident for the current map. Multiplayer streaming must not replace a prepared low-mip texture after spawning. Normal/off modes retain their streaming policy; WebGL2 fallback and Zombies retain their prior behavior. This can use more texture memory than a view-dependent streaming working set. Existing map lifetimes still release textures on transitions.

The preparation device uses `R_SetInitialContextState` before initializing its command buffer, matching gameplay's separate-alpha blending and two-sided stencil settings. Bare D3D device defaults generated different keys for blended and stencil-tested passes, so enumerating every loaded material still missed their actual first draws. Particle-cloud (8-byte stream) and water (two 16-byte streams) declarations are included when routing supports them. Polygon-offset dvars are part of the preparation profile, including their exact float identities with signed zero normalized.

The catalog is generated for the exact renderer/translator, asset-manifest generation, graphics options and supported float32 capabilities. It contains portable descriptors and source, and compilation still occurs on the current GPU device. The generation tool publishes its manifest reference only when it has captured the frontend and every multiplayer client map. Interrupted capture keeps evidence but does not advertise complete coverage.

Graphics-profile keys read boolean dvars as their logical 0/1 value. Reading the entire integer member of their storage union included unrelated bytes and could reject a compatible catalog despite identical settings.

Catalog manifest version 3 partitions exact observed draw recipes into frontend, shared map, weapon/effect, and individual map groups. Frontend preparation does not compile map or weapon groups. Each map gate imports its groups and the exact local learning set together. Version 2 catalogs remain readable when their renderer/profile matches; they cannot provide scoped loading. Renderer/profile version 4 invalidates older layouts and settings identities.

Each map load starts a preparation generation before `CG_Init`; duplicate calls for that generation/profile/map are ignored. Asset registration queues names instead of invalidating the whole gate. The game thread resolves at most 16 pending dependencies per tick and uploads only their textures/static meshes. Late class assets never cause a second full map scan. Preparation owns the hidden, non-archived shader-warming dvar on both backends; legacy saved toggle values do not override `preparation=off|normal|full`.

`gpuCapture=1` now records exact descriptors from actual rendering without speculative enumeration. The capture route enables firing effects, exercises traversal/ADS/reloads/equipment, includes zero/eleven bots on the primary fixtures, and enumerates every registered weapon variant through a local agent-and-cheats-only fixture. Every usable attachment combination is drawn and aimed; distinct effect families are fired/reloaded, including projectile effects. Fallback placeholders and internal left-hand components are explicitly listed as exclusions. Fast fixture selection skips repeated equip animations; the normal class-to-spawn path and traversal still exercise ordinary selection. The list of exercised variants is retained. Unregistered or scripted-only effects are not implied covered. `gpuEnumerate=1` is an offline diagnostic, never the normal loading path. Resume requires the identical build, asset identity, and route version. Captured frame times do not qualify warmed performance.

The offline packer merges recipe identities across maps, groups variants sharing shader sources, and emits bounded v2 codec chunks plus Brotli siblings for HTTP transport. The manifest hashes describe decoded bytes; HTTP content encoding does not change their identity. Existing published captures can be repacked with `node tools/web/compact_gpu_catalog.mjs build-web/assets-gen` without running the game again. Keep the `.bin` and `.bin.br` files together on a host that negotiates Brotli content encoding.

Runtime learning snapshots references on the executor and encodes them on a separate CPU worker. Each bounded chunk waits for a page acknowledgement before another is sent. The writer never owns GPU handles. Persistent application recipes have a 64 MB budget; exceeding it leaves the previous committed set intact and reports a failed cache write. Catalog persistence uses its separate content-addressed store. Capture mode intentionally buffers the current map's export for the offline tool.

Recipe eviction operates on entire indexed sets, including all their chunks. Successful indexed reads refresh the set's access time. Interrupted initial writes publish no partial index, and failed replacements preserve the previous committed set. Canonical asset identities exclude the optional installation inventory and catalog publication metadata, so adding either does not invalidate learned recipes for unchanged assets.

Full mode persists recipes encountered by actual rendering. Speculative enumeration is exported only by explicit offline enumeration (`gpuCapture=1&gpuEnumerate=1`); repeatedly serializing every speculative variant during gameplay would consume the application-cache budget without adding route coverage. Identical vertex and pixel WGSL strings are interned independently across shader combinations and restored chunks. Repeated map preparation reuses existing recipe objects.

Pipeline reflection stores constants and sampler metadata only. GLSL text, diagnostics and linkage vectors are not copied into each WebGPU state variant. Catalog downloads report persistence separately from successful decoding: unavailable storage or failed writes remain explicit even when this session can prepare from downloaded bytes.

`preparation=full|normal|off` controls the experiment. Full preparation has no time-based successful cutoff. Memory-budget exhaustion, cache failure, compilation failure and stalled progress are reported as incomplete. The user may Skip. Submitted jobs can finish after cancellation. A stalled resource queue invokes the existing renderer fallback rather than leaving readiness blocked forever.

## Full disk installation and hitch diagnosis

The Load control offers `maps on demand` (`mode=lazy`), `all map packs` (`mode=bulk`), and `download everything (full install)` (`mode=all`). The full installation includes original IWDs, installed fastfiles and loose web assets across multiplayer and Zombies, plus published pipeline catalogs. Videos, executables, logs and player profiles are excluded. Missing stock maps are not manufactured. Generated trace archives are omitted only when original archives cover their members (CRC and decoded size); noncovered packs are retained. Full installation therefore does not download duplicate copies of every map's shared images.

Generate or refresh the inventory independently of expensive capture:

```powershell
python tools/web/gen_manifest.py --game build --inventory-only
```

Normal manifest generation also writes it. `installation.json` contains SHA-256 checksums of 4 MiB blocks, and its reference in `manifest.json` authenticates the inventory itself. Files stream into OPFS with bounded buffers and commit only after size/checksum verification. Existing files are rechecked when verification receipts no longer match their content identity, size or modification time. `assetVerify=1` forces a content scan. Completed files survive interruptions; subsequent launches transfer only missing/invalid files. Storage quota failures are explicit. Once completed, a later lazy/bulk launch reuses the verified full installation and does not redownload superseded packs.

Disk installation does not keep all game assets in RAM, decode all sounds, or serialize device-specific GPU pipelines. Portable descriptors/WGSL still compile on the current GPU device; browser/driver binary caching is outside application control.

Use `assetNetwork=block` to reject and record any asset or catalog fetch requested after the engine reaches an active map, except while the preparation gate is active. Multiplayer relay/WebRTC traffic remains available. This guard covers both manifest fetch hooks and the loose `/pack/` fallback. A successful full-install route should show neither actual transfers nor blocked unexpected requests.

`Save hitch report` exports frames above 50 ms and overlapping file waits, archive reads/inflation, image loading/decoding, audio startup/decoding, shader compilation and first GPU uploads. Producer events share an epoch-millisecond clock. Fastfile log-derived spans are labelled `clockQuality: log-received`; overlapping work is evidence for investigation, not proof of causation. Runtime WebGPU misses and long frames are retained without enabling GPU timestamps; use `gpuProfile=1` for every-frame detail and timing queries. WebGL compilation and frame intervals also participate in diagnosis. Unexplained hitches stay explicit.

The diagnostic timeline is bounded. Agent GPU history retains 12,000 frames and 24,000 events; `agent.metrics(sinceFrameId)` returns incremental samples and reports an expired cursor. Capture uses these cursors to avoid repeatedly serializing an entire multi-map session.

```powershell
node tools/web/cdp.mjs launch --http-port 8097 --headed --renderer webgpu --gpu-profile --asset-mode all --block-assets --profile build-web/first-play-profile
node tools/web/benchmark_gpu.mjs --asset-modes all --block-assets --preparation full --cache both --maps all --combat --traversal --repetitions 3
```

Asset modes do not clear previously installed assets. Use separately named, initially empty browser profiles for true cold-asset comparisons; retain the profile path and browser identity with results. A fresh profile also changes browser compiler-cache state, while system/driver caches remain uncontrolled. Qualification samples must use the same cap, routes, bots and graphics settings and must not overlap builds or profilers.

Run `node tools/web/test_asset_install.mjs`, `python tools/web/test_gen_manifest.py`, and the existing cache/preparation/codec tests for integrity, truncation, quota/write failure, resume identity, grouped eviction and guard behavior. Compile/run `test_archive_cache.cpp` with Emscripten for archive-block reads/seeks/EOF regression coverage.

For an already active, fully prepared match, `node tools/web/probe_first_play.mjs` records the first and repeated traversal/combat routes on that same device without priming. It is a diagnostic pair, not three independent cold-device repetitions. `node tools/web/inspect_gpu_misses.mjs --route <route.json> --out <misses.json>` resolves runtime recipe hashes against the learned application cache and reports exact shader identities, render states, declarations and target formats. Unresolved identities remain explicit.

## Reproduce qualification

1. Build the release WebAssembly target and launch `node tools/web/cdp.mjs launch --headed --http-port 8097 --renderer webgpu --gpu-profile`.
2. Run `node tools/web/generate_gpu_catalog.mjs`. Preserve `assets-gen/gpu-catalog/capture.json` and any `failure.json`. Reload after successful publication to test catalog startup.
3. Run `node tools/web/benchmark_gpu.mjs --preparation normal,full --repetitions 3`. Defaults cover Nuketown and Summit, zero and eleven bots, application-recipe misses and cached reloads, and first/repeated camera visits at 960×540. Browser/driver caches are not cleared by the application-cache comparison. Validation is off for release measurements; add `--validation` for a separate correctness run.
4. Compare workers with `--preparation full --workers 1,2,4`; compare loading concurrency with `--preparation full --preparation-jobs 4,8,16`. Worker counts must fit the reported core budget. Preserve identical maps, bots, cameras and repetitions.
5. Use `node tools/web/profile_renderer.mjs 10` in a fully warmed active match. Sampling during a benchmark changes the workload; keep profiling and qualification samples separate.

The comparison tool keeps the client's 60 FPS cap unless `--maxfps` is specified. Use `--maxfps 0` for a separate uncapped ceiling run; the cap is recorded in each result and filename. BO1Z's public boot arguments use `com_maxfps 0`, so its displayed FPS is not directly comparable to this client's default capped runs. Keep the same cap across matched samples.

Add `--combat` to fire and reload at each camera view. That route waits for the initial weapon raise and reload animations instead of accidentally testing only camera turns. Camera and combat outputs have separate names. A single validation-enabled run can check preparation misses; it does not qualify frame-time improvements.

For the texture bind-group regression comparison, use `--texture-bindings legacy,stable --cache cached --preparation normal --repetitions 3`. Stable bindings identify the texture resource and view independently of pixel uploads. The diagnostic URL `gpuTextureBindings=0` restores the prior content-generation key. Record stored bind-group counts and heap high-water over time as well as frame percentiles. An increase in content generation must not retain another bind group for an unchanged view.

The producer-notification experiment is opt-in (`gpuSubmitWake=1`). It coalesces asynchronous notifications to the existing WebGPU owner after publishing a frame or resource batch. `ownerQueueWaitMs` measures publication to first executor adoption separately from producer backpressure, pipeline waits and GPU completion. Compare it with `--submit-wake display,notify`; the benchmark alternates policies within each repetition. Do not run builds or sampling profilers during matched timing measurements. Discard any overlapping samples.

`node tools/web/benchmark_warmed.mjs` compares notifications on one already active 960×540 GPU device. It requires two consecutive priming routes without pipeline compilations or first texture uploads, then runs three counterbalanced repetitions and rejects any timed route with a miss. This isolates warmed scheduling from reloads and browser compiler caches. Its sampling profiler counterpart records inclusive CPU skinning time and rendered frame counts; sampling overhead invalidates direct frame-time qualification.

JSON samples retain required pipeline compilations, first texture uploads, raw frame/compile/GPU events, submission intervals and screenshots. GPU timestamp spans measure the interval between pass timestamps; submission intervals and queue callback latency are separate measurements. Program-break high-water logs do not measure live allocations or GPU memory.

## Acceptance

- Frontend and every manifest multiplayer map must prepare and enter gameplay without a trap, device fallback or continuing heap growth across repeated loads.
- Covered first routes must have zero required pipeline compilations and zero first uploads of covered static textures. Investigate remaining events instead of treating catalog publication as proof of coverage.
- Across three matched repetitions, target first-route p95/p99 within 10% of the repeated route. Keep raw samples and report both stutter reduction and sustained frame time.
- Test Skip, unavailable/corrupt/missing catalog data, interrupted chunk sets, graphics changes, device loss, and host/guest loading independently.
- A worker/renderer optimization becomes default only after a 5% median or p95 improvement with no p99 regression above 5% and no correctness or visual regression. GPU skinning requires CPU skinning at least 10% of frame budget and at least 20% measured GPU headroom before prototyping.

Run `node tools/web/test_gpu_preparation.mjs`, `node tools/web/test_gpu_cache.mjs` and `node tools/web/test_gpu_catalog_codec.mjs` for catalog identity, modes, complete map coverage, streaming, corruption, storage failures and cache limits. The `test_gpu_policy.cpp` executable also accepts catalog file paths to validate offline packed chunks through the actual engine decoder. Existing GPU policy, shader and portability tests remain applicable. All-map capture and measured qualification are required before declaring the performance work complete.

## BO1Z implementation reference

Inspection on October 15, 2026 of BO1Z's public [startup module](https://vel.gg/bo1z/engine.js), [canvas policy](https://vel.gg/bo1z/canvas.js) and [JavaScript runtime](https://vel.gg/bo1z/artifacts/KisakBlack-web.mjs) found a worker-owned WebGPU executor, a shared-memory command ring, asynchronous per-map pipeline seeds, and pooled uniform/upload arenas. Its start prompt waits for seeded compilation and initial paused-frame compiles, with a 45-second timeout. The canvas policy bounds pixel density using 1920/width and 1440/height, then enforces minimum dimensions; it is not an equal-resolution comparison with this client's 960x540 default cap.

The inspected runtime also enables a `nostall` path by default: while an exact pipeline compiles it can use a nearby cached pipeline or skip the affected draw/group. Those policies can smooth compilation stalls, but do not establish equivalent rendering. This client keeps exact required pipelines and waits for completion. Arena reuse and command transport are relevant candidates for later warmed CPU optimization; no BO1Z timing, Mac-versus-desktop speed ratio, or GPU-compute advantage was established by source inspection. No external engine code was imported.

## Recovery and best-case qualification

The **Best-case preparation test** button selects `mode=all&preparation=full&bestCase=1&assetNetwork=block&renderer=webgpu`. It validates the full installation, downloads published catalogs, compiles the selected profile's complete catalog on this device, and prepares the selected map's static resources. `downloaded`, `validated`, `compiledOnDevice`, and `coverageVerified` are distinct: the first three never imply the fourth. Missing/stale/quarantined catalogs and memory-budget failures remain incomplete.

`node tools/web/benchmark_gpu.mjs --best-case --maps all --repetitions 3 --validation --out build-web/preparation-qualification` runs outside capture mode with effects enabled. It includes class-to-spawn work and first/repeated traversal in its zero-miss/upload checks. `--best-case` does not clear browser/driver binary caches. Use isolated browser profiles to compare asset-cold and installed sessions. Run release timing separately without `--validation`; the benchmark monitors competing Windows build processes and marks contaminated measurements unqualified. The standard matrix defaults to `off,normal,full`; use `--asset-modes lazy,all` to compare installation modes.

WebGPU fallback uses a one-use recovery token only for the immediate recovery reload. Manual/explicit WebGPU launches retry the adapter. The previous sticky `kisak-renderer` value is removed; Retry WebGPU clears recovery state and shows the recorded failure in its tooltip. A repeated failure within one minute stops the reload loop. Native codec rejection quarantines only the corresponding optional recipe set/chunk, preserving installed game files.

Sparse D3D vertex streams are packed into consecutive WebGPU binding slots. Binding reuse compares engine identity plus the resolved GPU buffer, offset, and size. In-flight compilation polling is bounded by outstanding jobs. Compatible completed map gates retire obsolete pipelines and recipe/source data while retaining frontend/shared requirements; best-case mode retains the complete profile. Imports are bounded to 128 MiB of recipe/source data and 65,536 recipes, independently of chunk size.

`node tools/web/qualify_gpu_catalog.mjs --session build-web/agent-session/session.json` performs independent playback, requires all defined map/repetition/bot scenarios, and writes a qualification receipt only after zero pipeline misses, zero first static uploads, zero blocked requests, and uncontaminated measurements. Generation alone never writes that receipt. Each benchmark also asserts that class/spawn activity did not restart the complete map gate; `--check-loading` additionally reselects the same class and attempts a legacy warming toggle.

Use `--map-only` for an initial diagnostic without the long registered-weapon fixture. Its report explicitly covers only the map route; it does not replace complete catalog qualification. Loading reports separate frontend, map-to-class and class-to-input durations. Memory reports retain the WebAssembly program-break and heap high-water values plus sampled resource counts; GPU resident bytes remain unavailable rather than being inferred from those values.

Loading diagnostics also separate class-menu selection from the wait for engine input after the menu closes. The latter can include the game's spawn freeze/countdown and must not automatically be attributed to shader compilation. Coverage receipts retain the tested map, route scope, profile and catalog identity; changing maps or profiles invalidates that device's previous coverage indication. Hitch reports preserve cached recipe-hit context, but cached hits alone never count as evidence explaining a long frame.

Inventory generation now records `contentIdentity` from canonical paths, sizes, and content hashes. Inventory metadata does not invalidate recipes, but same-size asset byte replacements do. Regenerate inventory and catalog together when changing the installed asset universe.

The bot stress route also exposed a non-shader problem: optional ragdoll parents could retain the missing-bone sentinel 255, then enter base-pose lookup. Ragdoll binding now propagates failure before reading skeleton arrays, retaining the existing ordinary corpse path. This avoids assertion storms and invalid transforms contaminating shader measurements.

Full WebGL preparation now runs its program-link queue while the loading screen is active, before class selection. It polls only unfinished links and reports rejected programs or a stalled gate as incomplete. A completed generation does not restart in the class menu. Normal WebGL and Zombies still leave this queue disabled by default.

Recipe quarantine requires a concrete codec or shader-validation rejection. Device loss, cancellation, memory pressure, and unspecified compilation failure remain retryable and do not blacklist the downloaded catalog. Native rejection still marks the current preparation incomplete.

Catalog downloads decode into a buffer bounded by the advertised chunk size and cancel oversized responses. Both catalog formats deduplicate identical chunks; conflicting descriptors, malformed groups and selected sets over 128 MiB are rejected before fetching. A renderer delivery failure remains incomplete until delivery succeeds, even if every chunk has already downloaded and passed its checksum. Partial downloads and scoped map loads do not prune the preceding catalog generation.

Catalog identity hashes the profile's exact recipe/chunk description as well as the asset identity, so replacing recipes for unchanged assets invalidates old coverage receipts. The prepared scope must match the current map, and a failed learned-cache supplement prevents a complete catalog result. Temporary OPFS read failures preserve committed learned files for retry; missing chunks, malformed indexes and invalid sizes withdraw the broken optional set. Indexed sets are size-checked together before importing any chunk. Cache eviction stops if it cannot invalidate the old index, preserving the chunks that index still references.

Blocked asset requests have cumulative counters separate from the rolling trace. Long weapon qualification cannot lose an early blocked request when detailed events are trimmed. Benchmarks without those counters cannot claim verified best-case coverage.

`node tools/web/test_gpu_recovery_live.mjs` destroys only the owned game worker's `GPUDevice`, verifies automatic WebGL recovery, checks that valid installed assets and optional catalogs survive, and exercises Retry WebGPU. It also checks that warming controls are hidden and legacy toggle changes cannot disable full preparation on either backend. Run it separately from capture and timing runs.

Weapon capture distinguishes selectable player variants from turret and non-player definitions rejected by `G_GivePlayerWeapon`. Those definitions require separate world/vehicle scenarios and are recorded as uncovered, rather than silently counted as successful weapon coverage. Qualification cannot issue a complete coverage receipt while these entries remain unresolved.

## Validation status, October 17, 2026

The previous frozen renderer (`db592658…`) produced 922 exact recipes across all 14 installed multiplayer maps and 17 scoped chunks. The capture exercised 1,677 player weapon variants, excluded 11 internal/fallback components, and explicitly left 17 world/vehicle definitions uncovered. These are capture results, not complete independent coverage qualification.

Validation-enabled independent playback in `build-web/preparation-builtin-playback/` passed the defined Nuketown map route with capture disabled, zero required pipeline compilations, zero first texture/static-buffer uploads, zero blocked asset requests, and exactly one completed map gate. This diagnostic skipped the full registered-weapon fixture. Live device-loss recovery also passed, retaining the verified 183-file installation and returning to WebGPU through both ordinary relaunch and Retry without asset transfer.

The subsequent upload batching change builds successfully and changes renderer identity to `84fc766c…`. The prior catalog is therefore intentionally incompatible with that build and must be regenerated, followed by separate playback. JavaScript cache/recovery/installation/preparation tests and the six manifest tests pass. New GPU playback and the native policy-test rebuild remain pending: Chrome GPU subprocesses exited with access-denied errors in the restricted launch, and automatic approval review could not authorize the separate native compile because workspace credits were exhausted. No timing improvement or complete-plan acceptance is claimed for this latest change.

A bounded CPU shader-translation cache was tested and removed: 4,715 registrations required only about 487 ms of measured translation, while the comparison did not improve loading. Remaining work includes new-build catalog capture, full weapon/world fixture coverage, independent all-map playback, WebGL/Zombies map regression checks, and isolated three-repeat off/normal/full/best-case performance measurements. Earlier measurements and receipts must not be reused as proof for a different frozen build.
