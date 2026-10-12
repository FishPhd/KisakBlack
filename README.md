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

Short path, from the repo root, after `tools\web\setup_emsdk.ps1` and `$env:EMSDK = 'D:\emsdk'; . "$env:EMSDK\emsdk_env.ps1"`:

```
cmake --preset web
cmake --build --preset web
python tools/web/gen_manifest.py --game D:\GitHub\KisakBlack\build --build build-web --out build-web/assets-gen --client
tools\web\play.ps1
```

`play.ps1` starts the server and prints `http://localhost:8080/?assets=/assets/&gen=/gen/`. If your game files are not in `D:\GitHub\KisakBlack\build`, pass `-Assets D:\path\to\game`. `serve.mjs` uses that `build` folder when `--assets` is omitted and `build\main` exists.

Press **Play**. The first start imports the frontend set into the browser (once) and compiles shaders as they are first used. Create a Private Match from the game menus. Set **BOTS** on the lobby row (left/right, same as Change Map). Bots join both teams about 10 seconds after the match starts.

Keys: click the canvas for the mouse, **Esc** opens the menu, **F11** toggles fullscreen, **F2** toggles the log. A second browser tab joins with `&join=ROOM` (the room id is on the host page). Headless server only: **Start** instead of **Play**.

Both Play and browser Start server use the packet bridge. Packet arrivals and engine notifications drive the bridge, with a startup/recovery timer rather than an rAF network pump. The existing server also handles WebRTC signaling; no separate matchmaking or signaling service is required. The game keeps its own packet sequencing, fragmentation, reliable commands, snapshots, and prediction. Multiple guests have distinct virtual addresses. `&net=relay` forces the fallback for diagnosis; `&ice=` accepts a URL-encoded JSON array of RTC ICE servers when STUN/TURN is needed across networks. With the default empty ICE list, direct connections depend on local connectivity; otherwise packets use the relay. Remote access needs the same site available through HTTPS and its WebSocket endpoint.

Restart `tools/web/play.ps1` after changing `serve.mjs`, then reload both players. The page checks the relay protocol and reports an old server instead of silently dropping packets. A room link connects to a running match; the Private Match menu currently has no networked party lobby. For testing connections outside a match, `/transport-test.html` exercises WebRTC and relay with two guests without loading the engine. The development server saves each page's last 300 log lines every five seconds in `build-web/diagnostics/<session>.json`, including host/guest role and room, and saves immediately on a reported Wasm trap. A hard renderer crash leaves the last periodic snapshot.

Joining tabs have separate persistent profiles under `profile/guests/`. The host saves validated first-visit classes and progression under `players/by_xuid/<xuid>/`, then uses that record on return visits. Saves run on disconnect, match end, map change, restart, and shutdown. Settings remain in each player's own profile. XUIDs remain self-asserted private-room identities. Node can persist profiles by passing `--profiledir <existing host directory>` to `run_node.mjs`; without it, the Node profile is temporary.

For a returning test player, add `&player=client2` to the guest URL. A named player retains its guest identity across tab closes in the same browser profile and origin; use a different name for each simultaneous guest. Without `player`, identity lasts for that tab's session. The browser host's authoritative saves are in OPFS at `profile/players/kisak_mpstats.dat`, `profile/players/kisak_btstats.dat`, and `profile/players/by_xuid/<xuid>/`. These are local files managed by the host browser, not files inside the Node HTTP server's `build/` folder. Keep the same browser profile, hostname and port, and do not reset storage to retain them.

Play > Wager Match opens the existing local lobby. One in the Chamber, Sticks and Stones, Gun Game, and Sharpshooter are available in its gametype picker. Selecting one starts its shipped wager script with the BOTS setting. Missing `playlists_mp.info` falls back to local wager metadata; an installed playlist file takes precedence. Developer testing: `kisak_wager gun mp_nuked` (enable `con_restricted_access 0` for developer commands). The PC assets contain a wager server browser rather than a separate wager lobby. The source and installed assets include Zombies-related components, but the current game bootstrap still uses multiplayer game scripts. Main Menu opens the same server's `/zombies` route, which shows availability and links back to multiplayer. Zombies gameplay remains under integration; this route does not claim it is playable.

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

`smoke` reloads the host, clicks Play and Private Match, selects Nuketown through the map picker, starts the match, selects the required team/class menus, checks movement and camera response, joins a guest through the room link, captures both screens, and closes the owned browser/server. `--map MAP` changes the test map. Use `--keep-open` to retain the host for debugging; run `close` when finished. `join ROOM` opens a guest interactively; `--role guest` directs subsequent commands to it. `key KeyW 750`, `key ArrowRight 300`, and `look 25 -10` control gameplay. Mouse clicks use page coordinates; `click-item` maps the engine's visible menu rectangles through canvas scaling and letterboxing. Arrow keys control camera look only during gameplay in agent mode and retain menu navigation behavior. `eval`, `reload`, and `stacks` remain available for diagnostics. See `node tools/web/cdp.mjs help` for options.

The default session metadata, screenshots, profiles, and logs live in the ignored `build-web/agent-session/` directory. `browser.jsonl` contains continuous console and engine records, including worker exceptions. Failed waits save `failure.json` with the last state and log tail, plus `failure.png`. `smoke.json` records successful steps. Use `--session PATH` to keep separate runs; use the same session/profile and HTTP port to retain cached assets. The profile is retained on close. Browser runs are headless by default and use hardware acceleration; `--headed` opens the test browser. The driver waits up to 180 seconds for game states; `--timeout MS` changes that limit.

`launch` snapshots the engine and page files and records their hashes. Rebuilds cannot replace files inside an active test. After rebuilding or editing the page, close and launch again to refresh the snapshot. Profile saves use a separate OPFS worker so archive reads do not block progression writes; this mounts the existing browser profile data and preserves the normal rename path.

Agent controls require both the development server's `--agent-test` switch and `?agent=1`. `tools/web/play.ps1 -AgentTest` prints the test URL. The switch refuses a public server bind, and ordinary servers and static deployments do not enable the agent API. There is no agent toggle in the game UI.

The menu becomes ready after the engine reports an interactive multiplayer frontend and the page presents a subsequent frame. Timings distinguish manifest validation, asset import, Wasm startup, engine initialization, UI loading, and the usable menu. Generated boot archives use a read-only memory cache capped at 512 MB (the current client archive adds about 415 MB); map fastfiles and profiles keep their existing storage. A failed or oversized cache allocation falls back to normal reads. This avoids hundreds of thousands of small OPFS archive reads during startup. Console DOM updates are batched, while diagnostic records are captured immediately.

Game snapshots include `inputReady`, which stays false while the game's countdown or weapon state blocks camera input. The smoke test waits for this state after selecting a class. Room-link guests enter private online mode before connecting so their class-selection scripts use the correct game mode.

For a comparable startup check, complete the build before running the following commands. Both paths use the same binary, origin, cached assets, and browser profile. Background map prefetch is excluded from the boot-import check:

```powershell
node tools/web/cdp.mjs launch --http-port 8097
node tools/web/cdp.mjs bench 5 --read-cache off --out build-web/agent-session/baseline.json
node tools/web/cdp.mjs bench 5 --read-cache on --out build-web/agent-session/optimized.json
node tools/web/cdp.mjs compare build-web/agent-session/baseline.json build-web/agent-session/optimized.json
node tools/web/cdp.mjs smoke
node --test tools/web/test_agent.mjs
```

Each benchmark fills the asset cache before its five measured reloads and reports the median time from navigation to usable menu. `--trace` also saves a page CPU profile. To verify live error capture and timeout artifacts against an open test session, set `$env:KISAK_AGENT_SESSION = (Resolve-Path build-web/agent-session/session.json).Path` and run `node --test tools/web/test_agent.mjs`; this test injects page/worker errors and reloads the game afterward. Agents can build a change, run `launch` and `smoke`, inspect the artifacts, and repeat that loop.

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
