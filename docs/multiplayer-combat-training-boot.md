# Native Combat Training boot flow

The native client now defaults to the stock Combat Training frontend when no
explicit startup flow was requested. This is a requested launch-policy change,
not a claim that the original executable booted directly into Combat Training.
The first frontend update queues the stock entry after startup commands and
`readStats` have run; it does not start a match automatically.

The original menu action supplies `onlinegame 1`, the training flag, clearing of
other match-mode flags, `default_xboxlive.cfg`, `dvar_defaults.cfg`, Summit and
TDM. Its popup and difficulty selector remain original assets. First-use users
receive the stock message, whose acceptance opens the selector. Previously
acknowledged profiles open the same selector directly on default boot. No
difficulty value is forced: Recruit/Regular/Hardened/Veteran retain the original
`easy`/`normal`/`hard`/`fu` mappings. The lobby's original START MATCH action
continues to own match launch.

Explicit map/connect/demo/exec/menu commands and explicit match-mode dvars take
precedence. Startup buffers with unquoted semicolons or line breaks also retain
control, including explicit commands following an otherwise harmless setting.
Dedicated, active-server, connecting, demo, error and mod contexts
are not redirected. Missing stock menus defer the launch rather than receiving
fabricated results. The policy is one-shot and excluded from Wasm/dedicated
builds; returning to the main menu later does not reopen training automatically.
Logo and intro-video states defer the pending launch until the frontend is ready.

[combat_training_frontend_reference.json](../tests/combat_training_frontend_reference.json)
pins both stock `ui_mp.ff` and overriding `patch_ui_mp.ff`, their inflated bytes,
the base entry action at inflated offset `0xe12c`, all four difficulty actions
in both assets, the lobby's first-use condition and popup acceptance. The test validates these bytes before
running selected production startup helpers with explicit command/frontend
bindings. All **51** policy checks pass, covering precedence, first-use versus
acknowledged selection, asset readiness and one-shot behavior.

The Nov08 pinned native builds automatically displayed the original four-choice
selector. The reviewed build also displayed the original first-use message;
accepting Continue opened the selector. Selecting Regular saved
`bot_difficulty "normal"`; the stock START MATCH action loaded Summit and stock
SMG selection spawned the player with MP5K/ASP, grenades and stock perks.
Isolated `fs_h` profiles kept these checks separate from the normal profile.

Earlier Nov08 runs observed bot gunfire, damage, human deaths, killcam and
respawning with replenished stock equipment, but crashed before the round ended.
The invalid destination `0xbf900000` has not yet been tied to a faulty original
function. Match completion, class editing, XP/rank UI and persistence remain
unqualified. No assertion or collision bypass was added.

Run `python tests/test_combat_training_boot.py`. These checks do not execute the
original menus or establish original-function equivalence, successful gameplay,
XP or storage behavior. The final native smoke must qualify automatic boot,
actual selector interaction, START MATCH and match completion separately.
