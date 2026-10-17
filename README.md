# KisakBlack

## About the project
An open source fully-buildable reimplementation of Call of Duty Black ops's Multi-Player .exe

Aimed towards mod developers and BLOPS enthusiasts.

### Development Blog
Learn about the Development of KisakBlack here: [https://lwss.github.io/Kisak-Black/](https://lwss.github.io/Kisak-Black/)

## Current Requirements
- Windows OS
- Visual Studio 2022
- CMake >= 3.16
- [DirectX SDK 2010](https://www.microsoft.com/en-us/download/details.aspx?id=6812)
- Call of Duty: Black Ops multiplayer game files on disk (`main` and `zone`)


## How to build
1) Install the above requirements and Clone repo
2) create a `/build` folder
3) `CMAKE .. -G"Visual Studio 17 2022" -DCMAKE_GENERATOR_PLATFORM=WIN32`
4) Open the generated .sln file
5) Build Debug or Release (Visual Studio dropdown)
6) Copy BLOPS Game files to `build/(BUILD_TYPE)/*` (Don't try to cherry-pick them, small files like localization.txt are needed)
8) Copy `binkw32.dll` and `vpx.dll` into the folder as well (in-tree under `src/binklib` and `src/libs/libvpx-1.5.0`)
11) Run the game via Visual Studio play button or just the .exe


```
Keep in Mind: This is a ~20 year old game with some known exploits. We will try to fix these as we become aware of them.
However, there is a non-zero chance of some type of binary exploitation when playing online. Use a sandbox (Sandboxie?) for peace of mind. 
```

## WebAssembly build (play in the browser)
`KISAK_PLATFORM` selects the target: `Desktop` (default, MSVC x86 + Direct3D 9) or `Web` (Emscripten, wasm32).
The Web build compiles the multiplayer engine. **Play** boots the real multiplayer menus (Create Game, the Private Match lobby, the BOTS row). Direct3D 9 calls are translated to WebGL2. Guests join the host using the room link on the page, with WebRTC datagrams and a WebSocket relay fallback.

The web build also includes a Dawn renderer through the SDK-pinned `emdawnwebgpu` port. Play defaults to `renderer=auto`, which uses WebGPU when the adapter supports BC texture compression and WebGL2 otherwise. Pass `&renderer=webgpu` to request Dawn explicitly, or `&renderer=webgl2` to force WebGL2. Float32 filtering and blending are enabled only when supported. A scene that actually samples a float32 texture or blends into a float32 target still needs the corresponding feature and falls back to WebGL2 when it is absent; texture precision is preserved. Adapter-request exceptions also fall back. Play skips Dawn validation unless `gpuValidation=1`. A Dawn initialization, validation, or device-loss failure reloads once into WebGL2. To retry Dawn after that fallback, clear `sessionStorage['kisak-renderer']` and reload. Configure with `-DKISAK_WEB_WEBGPU=OFF` for a build without the Dawn dependency.

Dawn uses a dedicated C++ pthread with an explicitly transferred canvas and asynchronous adapter/device/pipeline callbacks. The engine records owned resource uploads and draw-state snapshots; the executor renders directly to the canvas. SM3 bytecode is translated to WGSL using the existing decoder. Pipeline, shader-module, texture-bind, and render-state caches avoid repeated work. Immediate draws and dynamic buffer updates that the open pass has not bound are packed into one bump allocation and writeBuffer'd before use; a buffer the pass already uses still updates with an ordered copy. Shader constants use compact register banks and reuse unchanged blocks; relative addressing retains all 256 float registers. Partial clears and temporary depth targets preserve the D3D rendering path. Up to four upcoming pipeline variants compile asynchronously; persistent pipeline seeds, draw merging, and complete render-target readback coverage remain unfinished. Mismatched targets with stencil testing currently trigger the fallback. Graphics presets are shared with WebGL2.

Short path, from the repo root, after `tools\web\setup_emsdk.ps1` and `$env:EMSDK = 'D:\emsdk'; . "$env:EMSDK\emsdk_env.ps1"`:

```
cmake --preset web
cmake --build --preset web
python tools/web/gen_manifest.py --game D:\GitHub\KisakBlack\build --build build-web --out build-web/assets-gen --client --client-zombies
tools\web\play.ps1
```

`play.ps1` starts the server and prints `http://localhost:8080/?assets=/assets/&gen=/gen/`. If your game files are not in `D:\GitHub\KisakBlack\build`, pass `-Assets D:\path\to\game`. `serve.mjs` uses that `build` folder when `--assets` is omitted and `build\main` exists.

Press **Play**. The first start imports the frontend set into the browser (once) and compiles shaders as they are first used. Generated client assets are split into a small frontend IWD and one IWD per selected map. The map picker starts a bounded prefetch; cached files remain in OPFS for later visits. Create a Private Match from the game menus. Set **BOTS** on the lobby row (left/right, same as Change Map). Bots join both teams about 10 seconds after the match starts.

Fresh browser profiles choose a resolution that fits the window's physical pixels, up to 1920×1080, with Normal texture detail and 4× minimum anisotropic filtering. Saved graphics choices take precedence on later boots; the launcher can explicitly override resolution. The in-game resolution applies in both windowed and fullscreen modes. Fullscreen follows the browser's actual state, including exits and rejected requests. Dynamic lights remain reduced and distortion, motion blur, depth of field and shadows start disabled; their existing controls remain authoritative. Browser MSAA is unavailable and its menu offers only Off. Both shader-warming controls use the browser warming setting. Check player and sight visibility after changing graphics options or adding a map. Run `node --test tools/web/test_graphics_settings.mjs` for startup/persistence and fullscreen synchronization regressions.

The browser already uses pthreads for the game, renderer, database and job queue, plus a yielding WebGPU executor. Visibility, effects, skeletal animation/skinning and physics use the existing job queue. Automatic job-worker counts now use the browser's reported cores: one on 1–3 cores, two on four, three on five, and four on six or more. This bounds contention with game/render work without pretending all Apple silicon cores have equal throughput; the browser controls core placement. `+set r_smp_worker_threads 1` or `2` in `?args=` provides a lower-budget comparison. The pre-spawned Emscripten pool is separate from the number of active job workers. WebGPU pipeline and queue completion callbacks wake the executor on the next event-loop turn instead of waiting for another display tick. Depth-write state changes retain the current render pass; read-only depth attachments are used when the shader samples that same depth texture.

Apple silicon and low-end validation still require real hardware: test Chrome and Safari with the same map, bot count, camera and resolution and record median/p95/p99 frame intervals. Missing BC compression uses WebGL2; optional float32 support depends on the adapter/browser. The fixed 3 GB Wasm heap remains a substantial memory requirement (a saved Zombies run reached about 2.5 GB of program-break high-water). Reducing duplicate CPU/GPU asset storage and auditing resource lifetimes should precede shrinking that heap. Further frame optimizations should follow CPU/GPU profiling: batch draws/uploads for CPU overhead, reduce internal resolution for fill-rate pressure, and cap AI/physics load for heavy local matches. More threads alone cannot fix a GPU or memory bottleneck.

`node tools/web/test_portability.mjs` uses the owned browser session to check two-core/one-worker startup, eight-core/four-worker startup without optional float32 GPU features, and four-core/two-worker WebGL2 fallback after an adapter-request rejection. It reloads the game between cases and restores normal capabilities afterward. This simulates capability reporting, not the speed or memory pressure of those devices. `tools/web/test_thread_policy.cpp` checks the worker budget across core counts; compile it with Emscripten and run its output under Node.

Keys: click the canvas for the mouse, **Esc** opens the menu, **F11** toggles fullscreen, **F2** toggles the log. A second browser tab joins with `&join=ROOM` (the room id is on the host page). Headless server only: **Start** instead of **Play**.

Both Play and browser Start server use the packet bridge. Packet arrivals and engine notifications drive the bridge, with a startup/recovery timer rather than an rAF network pump. The existing server also handles WebRTC signaling; no separate matchmaking or signaling service is required. The game keeps its own packet sequencing, fragmentation, reliable commands, snapshots, and prediction. Multiple guests have distinct virtual addresses. `&net=relay` forces the fallback for diagnosis; `&ice=` accepts a URL-encoded JSON array of RTC ICE servers when STUN/TURN is needed across networks. With the default empty ICE list, direct connections depend on local connectivity; otherwise packets use the relay. Remote access needs the same site available through HTTPS and its WebSocket endpoint.

Restart `tools/web/play.ps1` after changing `serve.mjs`, then reload both players. The page checks the relay protocol and reports an old server instead of silently dropping packets. A room link joins the host's private-match party: guests wait in that lobby until the host starts, then connect, and return to the same lobby when the match ends. A guest who opens the link while a match is already running connects immediately. For testing connections outside a match, `/transport-test.html` exercises WebRTC and relay with two guests without loading the engine. The development server saves each page's last 300 log lines every five seconds in `build-web/diagnostics/<session>.json`, including host/guest role and room, and saves immediately on a reported Wasm trap. A hard renderer crash leaves the last periodic snapshot.

Joining tabs have separate persistent profiles under `profile/guests/`. The host saves validated first-visit classes and progression under `players/by_xuid/<xuid>/`, then uses that record on return visits. Saves run on disconnect, match end, map change, restart, and shutdown. Settings remain in each player's own profile. XUIDs remain self-asserted private-room identities. Node can persist profiles by passing `--profiledir <existing host directory>` to `run_node.mjs`; without it, the Node profile is temporary.

For a returning test player, add `&player=client2` to the guest URL. A named player retains its guest identity across tab closes in the same browser profile and origin; use a different name for each simultaneous guest. Without `player`, identity lasts for that tab's session. The browser host's authoritative saves are in OPFS at `profile/players/kisak_mpstats.dat`, `profile/players/kisak_btstats.dat`, and `profile/players/by_xuid/<xuid>/`. These are local files managed by the host browser, not files inside the Node HTTP server's `build/` folder. Keep the same browser profile, hostname and port, and do not reset storage to retain them.

Play > Wager Match opens the existing local lobby. One in the Chamber, Sticks and Stones, Gun Game, and Sharpshooter are available in its gametype picker. Selecting one starts its shipped wager script with the BOTS setting. Missing `playlists_mp.info` falls back to local wager metadata; an installed playlist file takes precedence. Developer testing: `kisak_wager gun mp_nuked` (enable `con_restricted_access 0` for developer commands). The PC assets contain a wager server browser rather than a separate wager lobby.

The experimental `--zombies` boot path loads the installed Zombies assets and stock map scripts inside the shared engine, with an isolated player profile. `/zombies` and `/zombies/` automatically open the stock Zombies frontend; SOLO selects Kino der Toten and launches its map. Generate frontend assets and Kino's client pack with `--client-zombies`. Menu navigation, map loading, player movement, and camera input have been checked in visible Chrome. Native actor and client integration remains incomplete: this checkout builds the multiplayer runtime, whose actor species and animation bindings only support dogs. Zombie spawning, kills, and round progression are not yet verified as a playable solo path.

Web pacing uses a fractional deadline and bitmap presentation waits for the page to consume the previous frame. F1 shows copied/shown counters. Shader constants use shared stage uniform buffers, relative address components are preserved, and position W defaults to one. Loaded PCM16, MS ADPCM, and WMAv2 sounds use the same PCM voice graph, with WMAv2 decoded once per loaded sound. Bad loaded buffers/packets are refused. Codec source and license are in `src/web/audio/wma/`.

Regression checks: `tools/web/test_audio.ps1` needs Emscripten and FFmpeg and checks decoded samples against a generated reference plus corrupt-packet handling. Compile `tools/web/test_sm3.cpp` with `src/web/gl/web_sm3.cpp` for translator checks. Start the local server and run `node tools/web/test_relay.mjs` for relay routing, or open `/transport-test.html` for real browser WebRTC and relay tests with two guests. In a match, verify a lit prop, an occluded lamp, weapon/effect placement, first-click audio, and a guest disconnect/rejoin; those visual and gameplay checks complement the isolated regressions.

### Browser agent testing

Build the web client and generate its client manifest first. Node 22+ and Chrome or Edge are needed. The driver launches a localhost server and an isolated browser profile, and records page, worker, and engine logs continuously while the session is open:

```powershell
node tools/web/cdp.mjs launch --http-port 8097
node tools/web/cdp.mjs wait menu
node tools/web/cdp.mjs status
node tools/web/cdp.mjs click-item "PLAY"
node tools/web/cdp.mjs shot
node tools/web/cdp.mjs logs 10
node tools/web/cdp.mjs smoke
```

`launch` defaults to `--renderer auto`, so a visible window uses Dawn when BC compression is available. Pass `--renderer webgl2` to force WebGL2, or `--renderer webgpu` to request Dawn explicitly. Use `launch --http-port 8097 --headed` for a visible test. In agent mode, `&gpuFallback=0` retains a Dawn failure for diagnosis instead of reloading. `node tools/web/test_webgpu.mjs` validates the WGSL fixtures saved from `test_sm3` in `build-web/wgsl-fixtures.txt` against the running browser's compiler.

`smoke --host-only --keep-open` enters gameplay without starting a guest for rendering measurements. `node tools/web/profile_renderer.mjs 10` saves CPU profiles of the owned page and engine/executor workers; workers that cannot answer the inspector are recorded as skipped. Frame-performance results include the starting camera and bot count. Compare steady matches with the same settings and inspect the screenshot's engine FPS counter as well as frame intervals.

WebGPU defaults to `preparation=full`: compile a compatible generated all-map catalog before exposing the multiplayer menu, prepare loaded frontend images, and upload each map's loaded static textures before gameplay readiness. `preparation=normal` retains the previous 4,096-recipe/32 MB match warmup; `preparation=off` disables preparation for comparisons. Full preparation processes the known workload to completion, with a 256 MB live-enumeration safety budget and explicit incomplete status if that budget is reached. Progress distinguishes complete, skipped and incomplete preparation. Skip stops speculative scheduling and releases readiness; submitted jobs can finish. Resource-only batches and GPU completion fences let loading uploads progress without presenting gameplay frames. Loading uses eight pipeline jobs by default; `preparationJobs=4|8|16` supports controlled comparisons. Runtime lookahead remains limited to two jobs and 1 ms of scheduling per executor tick.

Learned recipes and deduplicated WGSL are stored as bounded chunks in OPFS (`gpu-recipes-v2`), with 16 MB per chunk and 64 MB total. Chunk sets publish an index after writing all chunks, and incomplete sets are recoverable misses. GPU handles and compiled binaries are never persisted. Renderer/translator source hashes, manifest identity, shader hashes, graphics settings and capability choices invalidate stale recipes. Full preparation reports missing/stale catalogs and continues with live map preparation. Static textures follow map lifetimes; all maps' textures are not retained simultaneously. Safe dynamic-buffer append ranges are coalesced before draws; overlapping updates and index-alignment hazards retain ordered copies. DISCARD allocations have separate identities and buffer reuse waits for queue completion.

Generate a portable catalog with an owned visible WebGPU session: `node tools/web/generate_gpu_catalog.mjs --session build-web/agent-session/session.json`. It loads the frontend and every multiplayer client map in the asset manifest, captures material enumeration plus camera/firing/grenade activity, writes content-hashed chunks under `assets-gen/gpu-catalog/`, and attaches the catalog only after all maps succeed. `gen_manifest.py --gpu-catalog-session SESSION` runs that capture after asset packing. Rebuild/relaunch the session before capture after renderer changes. Catalogs match quality settings and capability choices; viewport size does not change pipeline compatibility. See [preparation validation](docs/webgpu-preparation.md).

Use `launch --headed --http-port 8097 --renderer webgpu --gpu-profile --gpu-validation` for controlled diagnostics. Profiling is off by default; enabled frame records and pipeline events are batched every 250 ms. WebGPU frame intervals measure **completed CPU submission**, rather than display presentation or GPU completion. Optional asynchronous timestamp queries report `gpuSpanMs`, the elapsed GPU timestamp span from the first pass to the final canvas pass; multiple submissions can include intervening queue gaps. `queueCompletionMs` measures asynchronous callback latency, not pure GPU execution. Diagnostics also retain executor CPU time, producer queue waits, WGSL translation/compile time, pipeline misses, upload/hoisted bytes and render-pass endings (target, buffer hazard, flush, copy, clear, uniform arena).

`node tools/web/benchmark_gpu.mjs` runs three matched fresh-cache/cached-reload repetitions on `mp_nuked` and `mp_mountain` with zero and eleven bots at 960×540, retaining first/repeated camera sweeps separately. JSON records include build hashes, exact cameras, bot counts, preparation progress, raw frame profiles and screenshots. Add `--ab` to compare four/two workers, callback/display-tick wakeups, merged/previous depth policy, and coalesced/ordered uploads independently. `--maps`, `--bots`, `--repetitions`, `--dwell` and `--out` allow smaller diagnosis runs. Run `test_gpu_policy.cpp` through Emscripten/Node and `node tools/web/test_gpu_cache.mjs` for state normalization, conservative upload ordering and cache validation; `test_portability.mjs` retains capability/fallback coverage. Actual Apple silicon performance remains a separate qualification task.

For first-play stutters, select **Load → download everything (full install)** or use `?mode=all`. This verifies the complete installed web asset inventory, including original archives that cover assets missed by trace packs. Generate/update it with `python tools/web/gen_manifest.py --game build --inventory-only`. Add `assetNetwork=block` to reject unexpected asset downloads during matches and use **Save hitch report** to inspect long frames. Full preparation supplements catalogs with loaded dependencies; disk downloads and GPU preparation are separate stages. See [preparation and qualification](docs/webgpu-preparation.md) for storage, cache and benchmark details. The benchmark now accepts `--maps all --traversal --combat --asset-modes all --block-assets`.

`smoke` reloads the host, clicks Play and Private Match, checks that the page shows the join code, lets a guest wait in that lobby, selects Nuketown through the map picker, checks that the guest lobby follows, starts the match, selects the required team/class menus, checks movement and camera response, waits for the guest to join the live match, returns both players to the lobby, and closes the owned browser/server. `--map MAP` changes the test map. Use `--keep-open` to retain the host for debugging; run `close` when finished. `join ROOM` opens a guest interactively; `--role guest` directs subsequent commands to it. `key KeyW 750`, `key ArrowRight 300`, and `look 25 -10` control gameplay. Mouse clicks use page coordinates; `click-item` maps the engine's visible menu rectangles through canvas scaling and letterboxing. Arrow keys control camera look only during gameplay in agent mode and retain menu navigation behavior. `eval`, `reload`, and `stacks` remain available for diagnostics. See `node tools/web/cdp.mjs help` for options.

The default session metadata, screenshots, profiles, and logs live in the ignored `build-web/agent-session/` directory. `browser.jsonl` contains continuous console and engine records, including worker exceptions. Failed waits save `failure.json` with the last state and log tail, plus `failure.png`. `smoke.json` records successful steps. Use `--session PATH` to keep separate runs; use the same session/profile and HTTP port to retain cached assets. The profile is retained on close. Browser runs are headless by default and use hardware acceleration; `--headed` opens the test browser. The driver waits up to 180 seconds for game states; `--timeout MS` changes that limit.

`launch` snapshots the engine and page files and records their hashes. Rebuilds cannot replace files inside an active test. After rebuilding or editing the page, close and launch again to refresh the snapshot. Profile saves use a separate OPFS worker so archive reads do not block progression writes; this mounts the existing browser profile data and preserves the normal rename path.

Agent controls require both the development server's `--agent-test` switch and `?agent=1`. `tools/web/play.ps1 -AgentTest` prints the test URL. The switch refuses a public server bind, and ordinary servers and static deployments do not enable the agent API. There is no agent toggle in the game UI.

The menu becomes ready after the engine reports an interactive multiplayer frontend and a subsequent frame reaches the presentation path. Timings distinguish manifest validation, asset import, Wasm startup, engine initialization, UI loading, and the usable menu. Generated frontend archives use a read-only memory cache capped at 512 MB. Other immutable IWDs use two 64 KB read blocks per open archive, including older archives retained in OPFS, which avoids repeated small storage reads without caching entire map archives. Map fastfiles and profiles keep their existing storage. `--read-cache off` disables both archive caches. The generated frontend IWD is about 37 MB for the current assets; each of the two generated map IWDs is roughly 390 MB. Exact sizes depend on the source asset installation. Console DOM updates are batched, while diagnostic records are captured immediately.

Game snapshots include `inputReady`, which stays false while the game's countdown or weapon state blocks camera input. The smoke test waits for this state after selecting a class. Room-link guests enter private online mode when they connect, once the host starts the match or if they join a match already in progress, so their class-selection scripts use the correct game mode. A disconnect while the room is still in the lobby leaves them in that party.

For a comparable startup check, complete the build before running the following commands. Both paths use the same binary, origin, cached assets, and browser profile. Background map prefetch is excluded from the boot-import check:

```powershell
node tools/web/cdp.mjs launch --http-port 8097
node tools/web/cdp.mjs bench 5 --read-cache off --out build-web/agent-session/baseline.json
node tools/web/cdp.mjs bench 5 --read-cache on --out build-web/agent-session/optimized.json
node tools/web/cdp.mjs compare build-web/agent-session/baseline.json build-web/agent-session/optimized.json
node tools/web/cdp.mjs smoke
node tools/web/cdp.mjs perf 300 --out build-web/agent-session/frame-performance.json
node --test tools/web/test_agent.mjs
```

Each benchmark fills the asset cache before its five measured reloads and reports the median time from navigation to usable menu. `--trace` also saves a page CPU profile. `perf` samples a running match for the requested number of seconds and reports median, p95, and p99 render and presented-frame intervals, bitmap-copy time, and frame budget misses. It rejects a sample if the match ends or changes maps; extend the match time limit before a five-minute run. `smoke-performance.json` retains the host's load and presentation measurements. Compare runs on the same browser, hardware, map, bot count, and camera route. To verify live error capture and timeout artifacts against an open test session, set `$env:KISAK_AGENT_SESSION = (Resolve-Path build-web/agent-session/session.json).Path` and run `node --test tools/web/test_agent.mjs`; this test injects page/worker errors and reloads the game afterward.

The Node runner can also host or join using the same transport: add `--relay ws://127.0.0.1:8080/relay`, and `--join ROOM` for a guest. Node uses the WebSocket fallback. Guest engine arguments need `+set dedicated 0`. Supply `--profiledir ABSOLUTE_DIRECTORY` after the runner's `--` separator to retain host/guest records across restarts; that directory must already exist. The relay regression accepts an optional WebSocket URL for a different port.

Hardware acceleration (do this if the page banner says software WebGL, or if the frame rate stays very low):

1. Chrome or Edge: Settings, System, turn on **Use hardware acceleration when available**, then relaunch.
2. Open `chrome://gpu`. **WebGL2** must say **Hardware accelerated**. The renderer should be ANGLE plus your GPU (Radeon, NVIDIA, Intel), not SwiftShader or Microsoft Basic Render Driver.
3. On a laptop with two GPUs: Windows Settings, System, Display, Graphics, set Chrome or Edge to **High performance**.

Optional: `tools\web\compress_gen.ps1` writes `.br` next to the generated files (`pip install brotli`) so the server can send them compressed.

Limitations: no video playback or MSAA, the default wasm32 client reserves 3 GB, and rooms provide private hosting rather than public matchmaking. Browser background throttling can affect a hosting tab.

Memory64 (up to 16 GB) is a separate build, `cmake --preset web-m64` then `cmake --build --preset web-m64` (output in `build-web-m64/`, the playable `build-web` client is left alone). It is off by default because 64-bit pointers do not match the 32-bit fastfile layout. The layout check in `src/database/db_layout_asserts.cpp` stops that build until a 64-bit asset port exists. PCM/ADPCM streams use queued windows; streamed WMA is refused. Debug: `-DKISAK_WEB_TRACE_PHASES=ON` with `-DKISAK_WEB_PROFILING_FUNCS=ON` publishes `PROF_SCOPED` regions through `Module._Kisak_FutexWaits()`.
## Known Issues
(Use the **[issues](https://github.com/SwagSoftware/KisakBlack/issues)** section)

## Troubleshooting
- ***Can't Connect to Dedicated Server*** :
  -  Check `net_ip` and `net_port`, the server will increment the port if the preferred one isn't available but the client won't sweep upwards.
 - ***DLL Error upon launch*** :
   - You didn't copy over the necessary runtime DLL's

## FAQ
- Can we use AI in this project?
  - Yes you can, but you're still responsible for whatever you commit. In general, you should have the AI be assisting you, and not carrying you. We have started using AI to help de-bug, and it's been extremely helpful.

## Credits and Special Thanks
- ***All Original BLOPS Developers (for creating one of the best games of all time)***
- https://github.com/SwagSoftware/KisakCOD (Some code re-used)
- https://github.com/PJayB/jk3src (Jedi Academy fork with .sln)
- https://github.com/voron00/CoD2rev_Server - Useful yacc code for the gsc scripting here
- https://github.com/shiversoftdev/BO3Enhanced - Viewed as reference code for some of the Steam API Auth
- [RAD Game Tools](https://www.radgametools.com/) for their Bink library.


## Discord
[Join the KisakCOD Discord](https://discord.gg/9uqntRWMA3)
