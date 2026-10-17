# Zombies interaction verification ledger

This ledger records acceptance work, not a declaration of complete support. **failed — unverified** means no gameplay evidence has been captured for that item; it does not assert that a particular action was observed failing. Replace that status only with a run ID, build identity, map/round, action, and engine/screenshot evidence. Asset tracing or a successful map boot cannot pass an interaction.

Sources were inspected from the configured `build/zone/Common` installation on 2026-10-13. Compressed GSC/CSC rawfiles were extracted into ignored `build-web/zombies-source-audit/<zone>/<script path>`. Base and patch sources are retained separately; do not confuse the synthetic `build/vmprobe` scripts with shipped scripts. Source names below are relative to that extraction. Patch source availability does not prove the engine loaded that patch.

## Assets and trace evidence

- **Installed; gameplay unverified:** `zombie_theater` (Kino), `zombie_pentagon` (Five), `zombietron` (Dead Ops).
- **blocked by missing assets:** Ascension `zombie_cosmodrome.ff`, Call of the Dead `zombie_coast.ff`, Shangri-La `zombie_temple.ff`, Moon `zombie_moon.ff`.
- **blocked by missing assets:** Nacht `zombie_cod5_prototype.ff`, Verrückt `zombie_cod5_asylum.ff`, Shi No Numa `zombie_cod5_sumpf.ff`, Der Riese `zombie_cod5_factory.ff`, and their required shared `waw_zombie.ff`.
- Localized `en_<map>.ff` and `<map>_patch.ff` files are installed for the missing maps. They do not replace the required base fastfile.
- `gen_manifest.py --client-zombies` now traces each complete installed map, includes fastfiles discovered in that trace, and produces a separate map IWD and loose-file set. `zombies-assets.json` reports required missing zones, trace state, duration, exit code, and log path. Raw logs are retained under `assets-gen/traces/<invocation>/`.
- These startup traces cover assets opened during startup only. Models, weapons, sounds, and effects opened during later interactions still require gameplay traces; do not infer their completeness from this manifest.

## Shared survival checklist — apply separately to every available map

All entries are **failed — unverified** for Kino and Five unless a later run is cited. DLC entries are **blocked by missing assets**. Sources are `common_zombie/maps` and `common_zombie_patch/maps`.

- Actor lifecycle: visible attached head and body, movement, attack, headshot damage, dismemberment, death, corpse cleanup, and entity reuse; character/aitype scripts and `_zombiemode_spawner.gsc`.
- Points/rounds: correct kill and hit awards, escalating waves, special rounds, maximum active actors, end-of-round rewards, death, and fresh restart; `_zombiemode.gsc`, `_zombiemode_score.gsc`, `_zombiemode_ai_dogs.gsc`.
- Room purchases: correct hint and price, insufficient funds, one charge per purchase, blocker movement/collision, adjoining zone activation, spawn changes, and repeated use; `_zombiemode_blockers.gsc`, `_zombiemode_zone_manager.gsc` plus each map's `add_adjacent_zone` declarations.
- Barricades: hold-use repairs, board animations/collision, points cap, interruption, and AI entry; `_zombiemode_blockers.gsc`.
- Power and perks: prerequisite gating, price, perk effect, solo Quick Revive limits, downed/death cleanup, and reset; `_zombiemode_perks.gsc`, `_laststand.gsc` plus map power flags.
- Wall weapons and ammunition: weapon identity, price, ammo refill, insufficient funds, inventory limits, upgraded ammo pricing; `_zombiemode_weapons.gsc`.
- Mystery box: purchase, randomized selection, take/timeout, limited weapon counts, teddy relocation, fire sale, and repeated/interrupted use; `_zombiemode_weapons.gsc` and map `*_magic_box.gsc`.
- Pack-a-Punch: actual stock cost, remove the deposited weapon, return the correct upgraded weapon/ammo, pickup timeout, interruptions/death, and simultaneous/repeated use; `_zombiemode_weapons.gsc::init_weapon_upgrade` and its upgrade transaction functions.
- Powerups: nuke, insta-kill, double points, max ammo, carpenter, fire sale, death machine where enabled, HUD/audio, stacking, duration, and restart; `_zombiemode_powerups.gsc` plus map `include_powerups`.
- Traps/turrets: power and price, damage to the intended actors/players, duration, cooldown, repeated activation, and death interruption; `_zombiemode_traps.gsc`, `_zombiemode_auto_turret.gsc` plus map trap declarations.
- Equipment/wonder weapons: claymore, Bowie knife, monkey bomb, crossbow, ballistic knife, Ray Gun, and the map's configured special weapon; `_zombiemode_equipment.gsc`, `_zombiemode_bowie.gsc`, `_zombiemode_weap_*` and map weapon inclusions.
- Client feedback: use prompts, weapon/perk/collectible HUD, sounds, FX, shellshock, vision transitions, and restoring camera/input after each interaction; corresponding shipped CSCs.
- Stability gate: three fresh starts plus one 30-minute run reaching round 10; record all crashes with action, Wasm stack and raw logs. No qualifying runs recorded in this ledger yet.
- **awaiting co-op validation:** revive another player, joint transport, overlapping purchases, reconnect/leave during transactions, and all script paths requiring multiple players. Preserve stock player-count requirements.

## Kino der Toten — `zombie_theater`

- **failed — unverified:** power switch, stage/foyer adjacency and curtains; `zombie_theater_patch/maps/zombie_theater.gsc::wait_for_power`, `zombie_theater/maps/zombie_theater_movie_screen.gsc::setupCurtains`.
- **failed — unverified:** teleporter core/base-pad linking, free stock teleport, projector room, Pack-a-Punch access, timed return through extra rooms, occupied landing spots, cooldown/relinking, zombie handling, and restoring movement/camera/FOV; `zombie_theater_patch/maps/zombie_theater_teleporter.gsc`. Inspect `teleport_link_think`, `player_teleporting`, `teleport_players`, `start_wall_clock`, `movie_graveyard_return`; preserve source timers rather than replacing them with new constants.
- **failed — unverified:** film reel pickup, collectible HUD, projector reel change, projected images, radio audio, and death cleanup; `zombie_theater_patch/maps/zombie_theater_movie_screen.gsc::movie_reels`, `movie_projector_reel_change`.
- **failed — unverified:** three meteor activations and music override/restoration, radio triggers/dialogue; `zombie_theater/maps/zombie_theater_amb.gsc::meteor_egg`, `play_music_easter_egg`, `radio_egg_trigger`.
- **failed — unverified:** Hellhound rounds, power-enabled Nova crawlers, Thundergun, stage traps and automatic turret; `_zombiemode_ai_dogs.gsc`, `zombie_theater_quad.gsc`, `_zombiemode_weap_thundergun.gsc`, map trap entities.

## Five — `zombie_pentagon`

- **failed — unverified:** both elevators, rider attachment, solid doors, floor/spawn-zone transitions, last-stand handling and restored controls; `zombie_pentagon/maps/zombie_pentagon_elevators.gsc`, `zombie_pentagon.gsc::enable_zone_elevators_init`.
- **failed — unverified:** power, random portals, valid destination/occupancy, player cooldown, zombie portal traversal, DEFCON switch progression, Pack room routing/door collision, countdown/reset, and Bonfire Sale override; `zombie_pentagon_patch/maps/zombie_pentagon_teleporter.gsc`, `zombie_pentagon/maps/zombie_pentagon.gsc::pentagon_bonfire_init`.
- **failed — unverified:** trap battery pickup, inventory/HUD, installation, cover movement, powered trap use, and death cleanup; `zombie_pentagon/maps/zombie_pentagon_traps.gsc::pickup_trap_piece`, `pentagon_fix_electric_trap`.
- **failed — unverified:** Pentagon Thief round, targeting, weapon theft/recovery, escape/kill outcomes and rewards; `zombie_pentagon_patch/maps/_zombiemode_ai_thief.gsc`.
- **failed — unverified:** Nova crawlers, Winter's Howl and upgraded effect, box relocation and map ambient/music triggers; map quad/magic-box/amb scripts and `_zombiemode_weap_freezegun.gsc`.

## Dead Ops Arcade — `zombietron`

Verify the arcade flow independently; the survival Pack-a-Punch/door checklist does not describe its progression.

- **failed — unverified:** intro/tutorial skip, top-down camera, movement/aim/fire, control restoration and restart; `zombietron/maps/_zombietron_scenes.gsc`, `_zombietron_main.gsc`.
- **failed — unverified:** arena rounds, spawn/death accounting, all exit choices, transitions and bonus rooms; `_zombietron_main.gsc`, `_zombietron_spawner.gsc`, `zombietron.gsc`.
- **failed — unverified:** weapon pickups, timers, vehicles where spawned, speed boost, bombs, lives, treasures/multipliers and scoring; `_zombietron_weapon.gsc`, `_zombietron_pickups.gsc`, `_zombietron_score.gsc`.
- **failed — unverified:** Room of Fate choices and distinct outcomes; `_zombietron_fate.gsc::choose_fate_round`, fate callbacks, and exit handling.
- **failed — unverified:** dogs, crawlers, engineer, buffalo, Cosmic Silverback and end-game scenes; `_zombietron_ai_*`, `_zombietron_scenes.gsc`.
- **awaiting co-op validation:** multiple players, shared transitions, lives/revival and player-count-dependent fate/progression behavior.

## DLC and legacy maps — partial source inventory

All listed solo checks below are **blocked by missing assets**. These are source-backed starting inventories; missing base zones also prevent a complete entity, script, model, weapon, sound and FX inventory. Re-extract and extend the checklist when complete assets are provided. Do not certify all unique interactions from patch sources alone.

- Ascension: lander calls/rides and linked riders, rocket/Pack-a-Punch progression, power, trap functions, perk-stealing monkeys, Gersch device and sidequest progression. Evidence: `zombie_cosmodrome_patch/maps/zombie_cosmodrome.gsc`, `zombie_cosmodrome_lander.gsc`, `zombie_cosmodrome_ai_monkey.gsc`, `_zombiemode_weap_black_hole_bomb.gsc`. **awaiting co-op validation:** script-gated joint sidequest stages.
- Call of the Dead: zipline, flinger, director AI/water interaction, map weapon effects, collectible/sidequest and rewards. Evidence: `zombie_coast_patch/maps/_zombiemode_player_zipline.gsc`, `zombie_coast_flinger.gsc`, `zombie_coast_ai_director.gsc`, `zombie_coast_eggs.gsc`, `zombie_coast_achievement.gsc`. **awaiting co-op validation:** cooperative sidequest branch.
- Shangri-La: player-count-gated Pack-a-Punch, elevators, monkey powerup handling, sidequest stage transitions/push-to-talk/Brock interactions. Evidence: `zombie_temple_patch/maps/zombie_temple_pack_a_punch.gsc`, `zombie_temple_elevators.gsc`, `zombie_temple_ai_monkey.gsc`, `zombie_temple_sq*.gsc`. **awaiting co-op validation:** synchronized switches and cooperative sidequest stages.
- Moon: Earth/wasteland teleporter, player-count routing, gravity/air and gasmask, jump pads, excavators, astronaut grab/transport, falling enemies, black-hole weapon and achievement/sidequest dependencies. Evidence: `zombie_moon_patch/maps/zombie_moon_teleporter.gsc`, `zombie_moon_wasteland.gsc`, `zombie_moon_gravity.gsc`, `zombie_moon_digger.gsc`, `_zombiemode_equip_gasmask.gsc`, `_zombiemode_jump_pad.gsc`, `_zombiemode_ai_astro.gsc`, `zombie_moon_achievement.gsc`. **awaiting co-op validation:** synchronized transport and player-count-gated quest stages.
- Nacht: stock map initialization, mystery box and weapon inclusions; `zombie_cod5_prototype_patch/maps/zombie_cod5_prototype.gsc`. Reconstruct base entity interactions before deciding applicability of power/perks/Pack-a-Punch.
- Verrückt: split starting areas, power/door and spawn-zone connectivity; `zombie_cod5_asylum_patch/maps/zombie_cod5_asylum.gsc`. **awaiting co-op validation:** split-start routing and shared purchases.
- Shi No Numa: zipline, hut purchases/perk allocation, mystery box and zone transitions; `zombie_cod5_sumpf_patch/maps/zombie_cod5_sumpf_zipline.gsc`, `zombie_cod5_sumpf_perks.gsc`, `zombie_cod5_sumpf_magic_box.gsc`, `zombie_cod5_sumpf.gsc`.
- Der Riese: teleporter linking/activation, Pack-a-Punch access, return/cooldown and entity occupancy; `zombie_cod5_factory_patch/maps/zombie_cod5_factory_teleporter.gsc`. Remaining base-only interactions await extraction.

## Recording a result

For each interaction capture before/after points, inventory, player origin/angles/control state, relevant flags, actor/round counts, duration, screenshot, and raw console logs. Exercise insufficient funds, prerequisites, repeat activation, interruption/death and restart. A **passed** result requires the intended outcome plus those applicable negative cases; annotate exceptions explicitly.
