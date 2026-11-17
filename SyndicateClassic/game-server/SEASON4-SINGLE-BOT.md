# Season 4.00 bot validation

The current bot target defaults to 50 and is capped at 50. TILTED_BOT_COUNT can select a smaller count; zero disables bots. Earlier single-bot validation entries below are historical.

Season 4 uses TickFlush__RepGraph. This now invokes SpectraBots::Tick for the world's main net driver, just as the legacy path does. Previously the bot tick only ran in the legacy replication path.

The Athena match check now reads Object->Class->Name directly. UObject class names are native metadata; the previous reflected Name property lookup returned no name and prevented match detection.

The single bot spawns near the first connected human during warmup. Its Season 4 pawn receives head, body and backpack through ServerChoosePart, in addition to the existing player-state customization. AI-controlled walking and the existing always-relevant replication settings remain enabled.

Validated on Fortnite 4.00 / UE 4.20: Release/x64 build passed, the hidden server reached ready, logs reported bots=1/1, ai=1, bad=0 and two alive players, and the client showed a fully rendered bot walking beside the human on spawn island. Combat and bus/landing behavior were not validated in this pass.

The Syndicate Launcher shortcut points to the rebuilt WPF launcher. Its Season 4 layout uses the supplied reference artwork, a single Fortnite navigation tab and a green Launch button. Account and settings controls remain available.

## Sprint and terrain avoidance (October 8, 2026)

Season 4 fake-player movement attributes were uninitialized: the engine reported GetMaxSpeed=0 despite MaxWalkSpeed=800. The bot now copies seven numeric movement attributes from the loaded human's MovementSet using matching reflected property types and sizes, then sets the native replicated CurrentMovementStyle to Sprinting. Live validation reported sprint=1, maxSpeed=550 and horizontal velocity approximately 550 units/second, without overwriting velocity or forcing jumps.

Steering uses reflected SphereTraceSingle calls with build-correct FVector and HitResult sizes. A chest sweep checks obstacles, and three ground probes reject steep normals, excessive steps and drop-offs. Missing trace support stops movement. Safe alternative headings are sampled every 0.1 seconds. The old forced velocity, stuck jumps and competing MoveToLocation request were removed.

In a live 4.00 warmup test (server PID 11576), the single bot traveled from approximately (-143395,-112457) through (-137499,-117569) to (-138691,-120037), with sampled MovementMode=Walking, sprint=1 and bad=0. At the latter sample, checks had rejected 172 obstructed headings, 31 steep/step headings and 154 drop-off headings, with zero trace errors. These counters count rejected candidate headings, not distinct physical obstacles. This is local obstacle avoidance, not a guarantee of a complete route through every enclosed space.

Random outfits now apply ServerChoosePart after profile-dependent visualization, and do not require an optional backpack to pass validation. The user confirmed a changed visible outfit. The last run selected Black Knight (CID_035).

## Match behavior (October 8, 2026)

The single bot gets a fresh generated display name per match. Warmup movement, random outfits, and always-relevant replication remain enabled. The managed server starts its aircraft phase after 90 seconds with a connected player; the local validation harness can shorten this using TILTED_BOT_TEST_WARMUP_SECONDS (15–90 seconds).

The bot selects a nearby cluster of main-island chests and a collision-checked landing patch. It requests the native aircraft jump and falls back to authoritative RestartPlayer when a connectionless controller's jump is declined. The new pawn keeps its outfit, health, replication and cached movement attributes. Native skydiving/glider physics handles descent; horizontal input steers toward the patch. No position teleport to the POI or forced vertical velocity is used.

Ground behavior searches actual ranged-weapon pickups and chests. Pickups are removed only after successful weapon equip. The bot stores the weapon identity/ammo without creating a client-only world inventory. Chest opening uses the server's existing chest loot hook. Unreachable loot targets are skipped for the remainder of the match. Nearby doors use native BlueprintOnInteract, with distance measured from the door component. Visible opposing players within 3000 units trigger native-validated wooden wall placement, limited to ten walls per match and without replacing map objects.

Live server PID 20660 verified a bus jump from (-2868,-65472,81536), landing at (-529,-57294,1613), 61 units from the selected patch. It resumed sprinting, picked up and equipped a world gun, and built wooden walls after detecting the connected player. The client showed the outfitted bot at Shifty Shafts. Release/x64 build passed. Chest and door interaction verification is still in progress; local steering does not guarantee routes through every enclosed area. Weapon firing is not implemented by this change.

Chest verification update: server PID 26428 landed at (9534,-122038,1997), 50 units from its landing patch, opened the selected chest using SpawnLootHook, then picked up/equipped its dropped weapon. Loot sight now falls back to a reflected sphere trace at chest height and only accepts an unobstructed path or a hit on the target actor itself. Chests use a 400-unit interaction range to avoid conflict with the 280-unit obstacle lookahead; guns remain at 200. Failed loot attempts expire after 40 seconds and stay skipped for that match. Closed-door approach after weapon pickup is being verified separately.

## Current validation (October 9, 2026)

Release/x64 build passed with temporary test teleports, free-build, damage and command hooks removed. The hidden server (PID 13364) spawned 50 bots with 50 unique names and 14 outfit variants. All 50 jumped from the bus and landed. The captured run logged 3 door openings, 5 chest openings, 10 weapon equips and 197 collision-checked jump requests. Jump-request counts do not prove every jump completed safely. The client displayed 51 players and 120 FPS during observation. Normal launcher warmup remains 90 seconds; this validation host used the harness-only 30-second override.

Bots become damageable on landing, with minimum health zero; a separate single-bot test verified a pickaxe hit reduced health from 100 to 90. Warmup and airborne protection remain intentional. Native downward skydive input and a downward control pitch accelerate the high-altitude descent while preserving native glider behavior.

Wall placement now uses the measured native half-cell wall grid, quarter-turn orientation and floor-height snapping. The old world-space application of the mesh's 10-unit offset was removed. Native placement validation still rejects invalid placements. Steering retains headings and detour direction, abandons inaccessible elevated loot, and chooses new nearby destinations from the bot's current location. Ground and clearance probes permit checked drops up to three 384-unit floors and request native jumps when a suitable landing surface is found. This remains local steering, not complete navigation through every enclosed space.

Door interaction now invokes the original native ServerAttemptInteract with ITSimple (1) and the bot's pawn/controller context. This was verified with an isolated door and again in the 50-bot run. Actor searches are shared through short-lived weak-reference caches; spawn positions and bus exits are staggered. Each bot preserves its own randomly selected outfit across the bus pawn replacement. Weapon firing is not implemented by these changes.
## Combat and elimination repair (October 9, 2026)

The current build adds basic server-controlled combat after landing and acquiring a gun. It checks opposing teams and line of sight within 3500 units, aims toward the target, consumes loaded ammunition, and resolves a collision trace with spread. A hit applies 18 damage, consuming shield first; walls/terrain block the trace. Lethal hits invoke the native ForceKill path so connected players enter the normal elimination/spectating flow. This is a custom paced hitscan implementation, not the full native weapon ability/ballistics system; advanced reloads and weapon-specific firing patterns are not implemented.

Weapon pickup now gives every equipped gun a unique GUID and verifies the resulting WeaponData. Previously the repeated zero GUID could return the existing pickaxe even though the loot log claimed a gun was equipped.

Bot deaths now have an idempotent native OnDeathServer bridge. It extracts the actual instigator using reference-aware script-stack parsing, credits the opposing killer, removes the bot from AlivePlayers and updates PlayersLeft exactly once. The connected-player death hook accepts the active bot attacker when the older ForceKill call loses that identity. Connectionless bot controllers are excluded from player quest calls.

Live single-bot validation: killing the bot with the client's pickaxe displayed 1 kill and reduced the HUD from 2 players to 1. The attack test showed client health decreasing, with server ammunition decreasing for each shot. The corrected lethal hit displayed 'You placed #2', spectated the named bot and reduced the HUD to 1 remaining. Shield handling is implemented but the isolated test's shield setup stayed at zero, so shield absorption was not confirmed in that run.

Release/x64 rebuilt successfully after removing temporary combat commands, staging teleports and forced test health. Normal population remains 50 with the existing 90-second warmup; warmup/airborne protection remains enabled. Existing movement, random names/outfits, loot and building logic are preserved.

Normal release validation: hidden server PID 4680 loaded 50 bots and the client HUD showed 51 players. After the bus transition, bots acquired guns and fought each other. Five native lethal-hit callbacks each logged credited=1; AlivePlayers and PlayersLeft both fell from 51 to 46, also visible on the client HUD. The server remained responsive after these eliminations. The launcher resolves this rebuilt Release/x64 DLL directly.

## Names, kill feed, house routes and storm rotation (October 9, 2026)

Name assignment now updates the bot's actual replicated PlayerState fields and gives each bot a distinct PlayerId. Fortnite 4.x's encoded stored name is written using the inverse of its native GetPlayerName transform. The live eight-bot test read back all intended names exactly; the subsequent normal 50-bot startup read back 50 unique names.

Bot elimination now fills replicated DeathInfo (killer/finisher, cause, distance and initialized flag) and calls OnRep_DeathInfo. Kill-score/player-count handling remains idempotent. Visual kill-feed validation is pending at this point in the test.

Blocked loot targets now route through nearby same-floor door components and across an opened doorway before resuming the loot target. Door interaction runs independently of loot/combat decisions, including during safe-zone rotation. In the live tests, bots opened doors and crossed entrances; an earlier run opened a chest and equipped its dropped gun. This is local obstacle steering and entrance routing, not complete multi-floor house pathfinding; unreachable loot still times out and is skipped.

Safe-zone movement uses the server indicator's next center/radius, takes priority over loot and combat, and continues until a safe interior margin is reached. Live logs showed LuckyScout27 reducing center distance from 70,567 to 63,871 to 57,635 and reaching the interior; ChillScout22 also reached it. Nearby door interaction remains available while rotating. Landing candidate search expands after 25 seconds to avoid waiting indefinitely for a very narrow cluster beneath the bus.

Release/x64 rebuilt successfully. Normal 50 bots and 90-second warmup are restored; no test population override remains in the running server.

Final observation of this pass: the live 50-bot server (PID 30512) successfully displayed kill-feed entries and credited the human's eliminations, with the remaining-player counter decreasing. However, client identities still alias: a captured feed showed LuckyRaven52 eliminating LuckyRaven52, followed by dashy55 eliminating LuckyRaven52. The attempted unique ReplicationBytes payload did not resolve the client name cache. Distinct server-side name readbacks are NOT proof of distinct displayed names. Name replication remains unresolved; investigate native UniqueNetId validity/serialization and client cache registration before calling it fixed. Current door/loot and safe-zone behavior are verified only to the extent described above; enclosed and multi-floor navigation still has unreachable targets. No further server restart was performed during the user's active match.

## Native identities, spread and load reduction (October 9, 2026)

Client name aliasing is resolved in the tested 4.00 build. Each bot now receives a real native UniqueNetId from the installed game's OnlineEngineInterface identity factory before roster registration. The factory has an explicit 4.00/RVA/layout guard; the account identifier is newly generated, never copied from the connected player. The roster's raw SDK struct assignment increments the shared reference it owns. Live server PID 18924 constructed 50 native IDs. Client screenshots showed dashy55 eliminating CosmicRanger28 and then ChillBandit44, with distinct kill-feed and center-screen names and consistent kill/remaining-player counters.

Landing selection prefers separation from reserved destinations across a wider reachable area, and bus departures are staggered over roughly 26 seconds. The 50-bot test recorded 50 jumps and 50 landings, with selected destinations occupying 39 distinct 10,000-unit XY cells. This is measured spread, not a promise of perfectly uniform distribution across the whole island.

Movement now commits to detour headings longer and detects net lack of progress over six seconds, abandoning the blocked loot route for a committed escape. It retains collision, slope and drop checks. Door searches now respect a cooldown even when no nearby door is found, fixing a full-world wall scan on every tick per bot. Terrain probes and sensing are staggered; trace metadata is cached; redundant per-frame combat visibility probes and DBNO writes were removed. Bus replacement pawns receive the same 15-Hz network update frequency as the warmup pawns. Always-relevant visibility is preserved. The live 50-bot server generally sustained its 30-Hz cadence; the client showed approximately 103-120 FPS in sampled gameplay. There is no controlled old-build performance baseline.

Pickaxe wall-damage validation remains in progress: blocking destructible walls are detected and the bot equips a pickaxe, but the first native generic-damage/SetHealth test left wall health unchanged. A ForceBuildingHealth fallback and API diagnostics are being tested; do not infer success from the swing log alone.

Wall damage update: live server PID 18628 resolved ForceBuildingHealth (SetHealth is absent in 4.00). Repeated guarded pickaxe hits reduced destructible wall health in 50-point steps, including 50 -> 0, while the server continued opening chests, equipping pickups and processing bot combat. The final build also updates replicated weapon fire feedback for swings, restores the previously equipped gun afterward, and traces the destroyed wall's collision before logging clearance. Only nearby, damageable BuildingWall actors with meaningful health are eligible; terrain and door-bearing walls are excluded. API-address diagnostics were removed. Final Release/x64 build passed; normal 90-second warmup restored. Clearance feedback is undergoing a final live check.

Final wall-clearance verification: Release/x64 server PID 10296 ran with the normal 90-second warmup, spawned 50 bots with 50 unique server name readbacks, and recorded all 50 landings. Native SilentDie completes destruction when the lethal notification leaves an undestroyed zero-health wall; its parameters are populated from this build's reflected metadata. Immediate and one-second follow-up traces recorded 74 clearance confirmations and zero delayed-still-blocked reports at the validation snapshot. Recent performance samples remained 30.0 ticks/second, with worst frames 33.4-35.2 ms in those windows. The final source and launcher-loaded Release DLL match this implementation; the hidden server and client were left running. Complex enclosed/multi-floor routing remains local steering and is not guaranteed to solve every house; the implemented anti-oscillation changes are committed detours, net-progress detection, blocked-target abandonment and destructible-wall harvesting.


## 2026-10-09: loot, pursuit, door traversal and 99 bots

The default is now 99 bots with the normal 90-second warmup. Bots prioritize usable guns/chests after landing, collect real world pickups and ammunition, reload using collected reserves, and pursue living enemy bots or humans. Throwable definitions are excluded from gun selection. Loot target commitment and failed-target cooldowns reduce switching. Opened doors have a staged crossing destination and a completed-door cooldown to reduce immediate reversals. Nearby loot remains available during safe-zone travel.

Release/x64 compiled successfully. Staged 8-, 24- and 99-bot runs were performed. The final 99-bot run recorded 99 bus departures, 99 landings, 67 gun pickups, 22 chest openings, 271 door crossings, 39 credited eliminations and 32 reloads at the saved snapshot. Distinct bot names and kill-feed entries were observed in the client; a bot also eliminated the human test player in an earlier full-population run. Door/property scans were distance-filtered and sensing staggered. Final full-population samples were roughly 29.1-29.9 server ticks/second and 110-120 client FPS; isolated longer frames still occurred. These are observations, not a guarantee that every bot finds a weapon or every house route succeeds. Complex multi-floor navigation still uses local steering.

Season 4 Hop Rock spawning was restored from the map's native consumable spawners. Native replication registration makes the rocks visible; ground traces place them on terrain rather than at floating spawner markers. Client visibility and ground placement were checked. The consume/low-gravity effect was NOT verified; an experimental interaction override was removed rather than replacing the normal handler with an unproven path. Temporary player teleport/rock-test controls and test environment overrides were removed from the final setup.

## 2026-10-09: follow-up storm, combat priority and missing rocks
User reported late-game surviving bots outside the circle, passive nearby bots, and missing visible Hop Rocks. Existing log showed living bots far outside a 625-unit next circle, with no explicit AI storm damage loop. Armed combat was gated behind all loot/harvest activity, and rotation disabled combat entirely.

Added one-second AI storm health checks using the interpolated current circle and configured percentage/absolute storm damage. Observed health loss is subtracted to avoid adding the same native damage twice. Storm lethals use the existing idempotent roster elimination path and destroy the AI pawn; zero-health/missing landed pawns are reconciled. PlayersLeft is reconciled to the remaining AlivePlayers roster at elimination. No human kill credit is assigned for storm deaths.

Combat now runs ahead of loot and harvest; armed bots stop seeking replacement guns. Nearby visible enemies outrank nearer occluded enemies. Rotating bots may fight visible enemies in range but do not chase distant opponents away from the circle. Unarmed gun/chest searches cover 24,000 units. Existing collision and door checks remain.

Hop Rock spawners now produce all eight native drops with separated positions instead of only one. Ground placement has a 35-unit clearance. Actor hiding and mesh distance culling are explicitly cleared. The first new server startup logged 24 spawners with eight rocks each (192). This is server spawning evidence, not client visibility verification.

Final Release/x64 build succeeded. Default 99 bots and 90-second warmup retained. In-match validation is pending: the launched client was at sign-in and the user was asked to sign in. Do not treat these follow-up changes as gameplay-verified yet. No temporary teleport or test command hooks were added.


## 2026-10-09: follow-up live verification completed

The full 99-bot run recorded 99 landings, 59 gun pickups, 44 credited combat eliminations, eight explicit storm deaths and 39 reloads at the saved snapshot. Observed elimination counts matched the remaining roster; the client HUD decreased through 100, 85, 56 and 45. Storm deaths removed the pawn and roster entry without awarding a killer. Recent full-population performance samples remained about 29.3-30.0 ticks/second. This verifies the storm death/count and combat-priority changes; local steering still cannot guarantee every enclosed house route.

The missing Hop Rocks were reproduced in the client. Dynamic native consumables remained dormant (NetDormancy=2) with no actor channel. Setting them awake through SetNetDormancy(1) and flushing dormancy after replication setup restored client visibility. The follow-up client screenshot showed multiple blue glowing rocks around the crater beside the racetrack. All 24 native spawners produced eight drops each. Consumption/low-gravity remains unverified and the normal interaction handler is retained.

Temporary teleport/file-command and network diagnostics were removed after the test. The final configuration restores 99 bots and the normal 90-second warmup.

## 2026-10-09: rubber-banding performance fix

The reported match showed server rates of 4.8-26.9 ticks/second and worst frames up to 505 ms. Bot wall/loot queries copied full-island actor lists repeatedly; SDK TArray::Add reallocates for each addition unless reserved. Queries now cache actor positions, filter nearby candidates before resolving them, and reserve the output array once. Destroyed/picked-up objects remain checked, and dynamic caches refresh every two seconds.

A rotating fair batch processes up to 24 bot decisions per tick. Native movement input continues between decisions only when the previous decision explicitly authorized movement; combat/harvesting stops remain respected. Bus landing and storm checks continue each frame. Timers receive accumulated elapsed time, and combat sensing intervals are staggered. Default population remains 99.

Release/x64 built and deployed. Live server PID 2436 recorded all 99 landings, 49 gun pickups and 21 credited eliminations at the saved snapshot. Recent loaded combat windows measured 29.9-30.0 ticks/second, with worst frames 33.4-58.9 ms, compared with the earlier half-second stalls. This is an observed improvement, not a guarantee against every possible hitch. User-side rubber-banding feedback is pending. The running match was left undisturbed after deployment.

## 2026-10-09: shot feedback, victory, death loot and zone fallbacks

Implemented a reflected 4.00 NetMulticast_Athena_BatchedDamageCues call for bot shot/impact feedback. Startup inspection confirmed that 4.00 takes one 88-byte AthenaBatchedDamageGameplayCues BatchData, rather than the later split Shared/NonShared structs. Field offsets, sizes and boolean masks are resolved at runtime. Weapon shot-state replication is enabled; native damage callbacks receive momentum and a live effect context. Temporary startup API inspection was removed. Audio, tracer and directional feedback still require client verification.

Bot elimination now invokes the same native RemoveFromAlivePlayers path used for human deaths, preserves idempotent cleanup/count reconciliation, and notifies the sole survivor, updates winning state, sets EndGame when available, and calls EndMatch. Before cleanup, each bot drops a gun/ammo when the definition is available plus wood using native pickups. Last-bot Victory Royale and pickup usability are not yet live verified.

Bots still unarmed at SafeZonePhase >= 2 receive a common assault rifle and 120 reserve rounds, once per bot after a successful equip. Loot/combat priority otherwise remains. Stuck zone rotation may recover after 15 seconds without 500 units of progress: both current and destination positions must be at least 5000 units from, and occluded from, every connected view target. Destinations are traced for walkable ground inside the next circle and use native collision-checked teleport. Attempts run at most every three seconds, with three candidates; successful recovery has a 45-second cooldown. If no hidden safe candidate is found, normal movement and storm damage continue.

Release/x64 compilation succeeded. The default remains 99 bots with the previous performance optimization. In-game validation is pending: Fortnite was closed, Syndicate Launcher was signed out, and the user was asked to sign in and launch it. No claim of gameplay verification is made for this entry.

## 2026-10-10 combat / loot revision

Implemented in SpectraBotsIntegration.h:
- Aircraft DropStartTime/DropEndTime gates plus staggered departures within that window.
- Collision-checked strafing during combat, continuous input between decision slices.
- Reflected 4.00 weapon rate, reload, pellet count, range/body and environmental damage; bounded shot catch-up. Burst rifle timing uses BurstFiringRate.
- Enemy player-build suppression; forty-wall material budget and combat-triggered placement.
- Thirty seconds without combat/damage restores 100 health and random 10-100 shield. Storm and reload/combat prevent recovery.
- Random guns from loaded Athena loot definitions, matching ammo, healing item and wood on death. Gun count grows from 1 to 4 with SafeZonePhase.
- Weapon category death causes for bot victims and synchronous bot shots killing a human. Legacy SMG asset names categorized separately from pistols.
- Shot, hit, recovery, structure-hit and moving-fighter counters in existing periodic report.

Validation:
- Release compile passed (installed first revision and final candidate).
- First revision live 99-bot match: all 99 jump requests inside the aircraft window; all 99 landed; 61 eliminations / 61 loot drops; 122 cover placements at evidence capture.
- 31 weapon profiles resolved from this build, including AR 30, bolt-action sniper 105, pump 9 per pellet x10, tactical 7 per pellet x10.
- Recent server measurements: 30 ticks/sec, worst-frame 33.5-33.7 ms. Evidence: workspace work/tilted/eight-fixes-first-match.log.
- Final candidate additionally fixes burst spacing, SMG classification, independent pellet structure hits, and validation counters. Built at workspace work/tilted/candidate/Erbium.dll; not yet copied into the active release location. User restart timing question pending to avoid interrupting their active match.
- No claim of exhaustive weapon validation: native projectile travel/explosion and critical-hit behavior have not been validated; the connectionless bot firing path remains a collision-traced damage implementation.
- Recovery timing, progressive late-circle gun counts, and final candidate counters still require live validation after restart.

## 2026-10-10 inventory, firing and landing follow-up

Deployed Release/x64, including the previously pending combat candidate.
- Guns, loaded rounds and ammo reserves tracked per bot (up to five gun definitions). Death drops only tracked inventory and remaining materials; no generated gun fallback. Secondary inventory guns can be equipped when the active gun runs dry.
- Legendary-or-higher pickups/emergency grants restricted until SafeZonePhase 4.
- Semi-auto pistols capped at a minimum .42-second trigger interval. Burst rifles use three .16-second shots with at least .85 seconds between bursts; no multi-shot decision catch-up for these categories, and firing effects release between triggers.
- Cover tries front/left/right grid placements, with 1.5-second successful placement cooldown and existing native validity/material limits.
- Assigned groups: 10 Greasy, 20 Syndicate, 10 Salty, 15 Retail, 44 dispersed reservations.

Validation and unresolved work:
- Release builds pass; rebuilt DLL deployed directly to active release path and host restarted.
- First match confirmed unarmed death reports guns=0 (materials only), and tracked one-gun drops. Server reports approximately 30 ticks/sec, 34-37 ms worst frames in that sample.
- POI brush origins were zero. GetActorBounds also returned no usable centers in the next live test, leaving the 55 assigned bots waiting on spawn island. This is NOT a validated town-landing implementation yet.
- Latest build adds streaming loot package-name town-center fallback and bounded diagnostics. Requires new-match validation; current test was restarted for this correction.
- Actual pistol/burst visual cadence, increased cover frequency, late rarity gating and inventory switching still need focused live verification. Earlier random-loot behavior above is superseded by inventory-only drops.

## 2026-10-10 user-requested town rollback

The streaming-package fallback also failed because the 4.00 map packages use internal POI names. User explicitly requested reverting town assignments and continuing other fixes. Removed FindLandingGroup, all town/quota fields, startup POI diagnostics, and assignment-specific early glider behavior. ChooseLandingPoi is byte-for-byte restored to the pre-assignment implementation in workspace before-inventory-poi.h. Normal dispersed chest reservations and prior descent restored. No exact town quotas remain.

Inventory-only death drops, phase-4 legendary gate, pistol/burst timing and alternate cover placement remain installed. Added burst/cooldown reset on successful world gun pickup. Release/x64 rebuild passed and active server restarted. Source checks confirm no assignment/diagnostic symbols remain and death guns are sourced only from tracked inventory. Final rollback build gameplay verification remains pending; do not treat previous failed town tests as successful landing validation.

## 2026-10-10 combat pauses / burst effects / ramp pushes

- Removed the duplicate PlayWeaponFireFX call: replicated shot batch remains responsible for per-shot feedback. Semi/burst guns no longer hold bWeaponActivated continuously; stop-fire timing now runs every host frame instead of waiting for an AI decision slot.
- Reloads use collision-checked combat movement instead of StopMovementImmediately. Blocked strafing tries additional short safe headings. Dead targets trigger immediate resensing, while a current target receives retention preference. Armed pursuit no longer yields to secondary looting.
- Harvesting no longer treats the equipped pickaxe as an empty combat gun. Visible existing threats interrupt harvesting and restore the saved gun.
- Native-validated wood ramps added for opponents 180+ units above and within 3500 units, using the existing material budget/cooldown. Successful placement supplies a short uphill movement target; normal safe-zone priority and collision steering remain. Walls retain the alternate placement routine.
- Release/x64 build passed; active release DLL installed and host restarted. Town assignment remains reverted.
- Runtime/visual validation pending: ramp orientation and traversal, semi/burst stop animation and sustained pursuit have not yet been verified in a fresh match. No claim of full navigation/pathfinding or unstoppable aggression is made; collision, reload, ammunition and safe-zone constraints still apply.

## 2026-10-10 chest commitment, ramps, material drops and Season 4 progression

Bot changes:
- Eight-second chest-loot commitment after opening a chest; clears on successful gun pickup. Suppresses combat pursuit/escape during this short pickup window unless safe-zone rotation is required, giving the cached pickups time to appear.
- Ramp placement now also considered for longer pushes (every third build), with three candidate tiles, rather than only enemies 180 units higher. Previous live logs did contain successful ramps, but they were rare.
- Death wood drops capped at min(remaining materials, 50); internal building budget remains unchanged.

Progression:
- QueueProgression emits GUID-keyed durable local JSON events for connected-human bot kills, wins through the bot last-survivor bridge, and each 60 seconds alive in phase 4 while more than one player remains. No XP for bots/warmup/spectating/post-win idle time. Backend handles the queue off the game thread.
- Backend Utils/syndicateProgressionCore.js and syndicateProgression.js use included Season4 XP/stars/reward tables, persistent Mongo CAS updates and event-ID receipts. Awards: kill 100 XP, survival minute 50 XP, win 500 XP. Additional one battle star per 1000 gameplay XP keeps tiers progressing after season level cap; ten stars per tier, tier/level max 100.
- Enables local premium pass and grants previously unclaimed tier rewards with stored reward-tier marker. Existing levels/XP preserved. Generic athena profile query initializes entitlement/rewards for existing accounts; progression returned through normal profile refresh (no in-match XP popup implementation).
- dashy55 pass initialized at existing level 1 / tier 1; no test XP added to the player's profile.

Validation:
- Release/x64 compile passed; installed active DLL, hidden server restarted. Backend main process restarted hidden with Discord tokens blank.
- Pure tests: level thresholds, stars/tier advancement, reward repeat protection, invalid XP rejection, level/tier limits.
- Temporary Mongo profiles: concurrent awards, duplicate-event replay, durable queue consumption and persistence passed; test profiles/users removed.
- Running backend (separate process) consumed a test kill event, awarded 100 XP and reached level 2; queue acknowledged. No user test XP.
- Fresh-match gameplay observation and client battle-pass screen verification remain pending. Ramp orientation/traversal and chest commitment are implemented, not yet visually verified in this build.

## 2026-10-10 stair alignment / corpse collision / human outfits

User reports off-grid stairs, no damage after killing a shotgun bot, and lobby-selected outfit reverting to default in match.
- Stair horizontal snap now uses rotated stair SnapOffset (observed 0,256,0) rather than zero origin on both axes; wall grid logic retained. Native placement validation still applies. Visual alignment needs client verification.
- Bot native death bridge disables corpse actor collision after the original death event, preventing dead capsules/meshes from blocking subsequent traces. Added throttled bCanBeDamaged=false diagnostic and post-damage health-clamp/damageability diagnostic. This is a plausible obstruction fix, NOT proof that the reported damage issue is fully resolved.
- Every Season 4 ServerAcknowledgePossession applies the controller's selected Character/Hero specialization parts after ordinary customization, including later aircraft pawn possession. Loads missing soft-referenced parts, writes replicated state parts, invokes pawn ServerChoosePart and forces replication. No default character substituted if selected loadout is unavailable. Logs selected asset and applied part count for diagnosis.
- Candidate Release/x64 compiled successfully and DLL installed in active release directory; server restarted. Client selected outfit, stair seam alignment and post-elimination damage remain pending fresh-match verification.

## 2026-10-10 launcher cleanup / Quit / locker handoff

- Launcher fixed at 1280x720, NoResize, transparent borderless window; maximize control removed to eliminate native white frame/resizing.
- Middle news tile now uses supplied clean Tilted_Towers asset (bundled IslandClean.png), rather than cursor-containing screenshot crop.
- Launch switches to Quit after starting Fortnite. Quit stops tracked client, host, matchmaking and backend, clears launcher account session and returns to Launch. Process records validate PID, start ticks and executable path; does not enumerate/kill unrelated Fortnite or node processes. Game exit leaves Quit available for service cleanup.
- SessionProcesses tracks client/backend startup; GameServer tracks host/matchmaker. Existing local backend/matchmaker identities seeded at deployment.
- Removed both legacy and v2 Helix emergency notices from content pages.
- Logs confirmed controller outfit stayed CID_001 default while persisted locker selection was cid_125_athena_commando_m_tacticalwoodland (Battlehawk). Backend publishes validated selected CID to per-username hex-named local locker file on equip/query; Season 4 possession prefers that file before applying hero parts. No arbitrary asset path or external request accepted.
- Launcher and server builds pass (existing nullable warning in GameServer). Quit test passed for owned process termination and stale-PID rejection. Saved Battlehawk locker export verified without changing selection.
- Updated executable installed at desktop shortcut target; backend and host restarted. Visual white-frame removal, Quit end-to-end against real session, and in-match skin appearance require user verification.

## 2026-10-10: range, cover and hidden host update
- Removed the fixed 32 m perception / 30 m firing limit. AR range 100 m; sniper range 200 m. Inventory weapon selection prefers AR inside 100 m and sniper beyond 100 m, without creating free weapons or ammo.
- Visibility sensing sorts distance candidates and traces at most four per sense; actual shots still collide with terrain/buildings. Semi/burst cadence and real inventory drops remain intact.
- Unarmed bots retreat from a visible armed threat within 35 m; armed bots pursue/fight valid enemies. Chest-loot commitment remains.
- Removed the flat-ground ramp trigger; upward ramps require an enemy more than 250 units higher. Existing checked downward jumps remain.
- One 40% boxing attempt per encounter (12 seconds without an in-range visible enemy resets encounter). Up to four cardinal walls and a cone roof, native placement/material rules, bounded attempts. Hold movement while constructing. Wall cover cooldown 3 seconds.
- Added bounded water-surface cone crossing extension toward the destination, native structural support checks, 0.8 second checks, 32-piece cap and real wood usage. Requires a collision surface whose actor/class name identifies water/lake. This detection and actual traversability are NOT yet verified at Loot Lake in a live match; no unsupported walking is permitted.
- Host created with CreateProcess on a private, never-shown desktop (not switched to). Environment preserved, exact process tracking preserved. Removed delayed window-hiding polling.
Validation: C++ Release x64 and .NET Release builds pass. Private-desktop real Fortnite host reached joinable via normal frontend/injection/island sequence. No fresh live-match verification of long-range fights, completed boxes, lake crossing or full-match performance yet.

## 2026-10-10: persistent experience and loot/cover correction
- BotExperience.h adds a bounded 4096-record, Season 4.00-specific memory of successful chest interactions and directional route outcomes. Quantized coordinates contain no actor pointers or account data. Smoothed scores favor successful chests and routes; old failures can be unlearned. Collision checks remain mandatory.
- Runtime/Server/bot-experience-4.00.txt is loaded once and atomically replaced from a background writer every 30 seconds. Deleting it while the host is stopped resets learned preferences. This is persistent experience-based scoring, not neural training or a complete learned navigation mesh.
- First 60 seconds after landing prioritize obtaining a gun over threat-driven fleeing, except safe-zone urgency. Floor firearms with zero/default pickup ammo use the native magazine capacity on collection. Real pickup identity still drives inventory and drops.
- Ramp attempts now require >384 height difference, <1800 horizontal distance, prior cover, no pending boxing, a separate 12-second cooldown and a wall between successful ramps. Wall placement clears the ramp allowance; failed ramps back off five seconds.
- Unconfigured weapon falloff thresholds no longer produce zero damage for every nonzero range. Nonlethal damage notifications reconcile to the single shield-first calculated health/shield result; human-hit logs include expected/actual values. Live player damage remains pending verification; no claim of confirmed root cause.
- Validation: production Release x64 build passed. Standalone C++ tests passed persistence round-trip, wrong-version rejection, corrupt-input rejection, bounded storage, and recovery after failed routes. Real hidden host reached joinable and created the memory file. Waiting for a new match to verify hits, looting and wall/ramp balance.

## 2026-10-10: historical pass shop and confirmed damage setter failure
Evidence: live human hit logs repeatedly show damage=54 shield100->100 expected46, health100. Added Season4 reflected HealthSet attribute fallback when normal SetShield/SetHealth and notification reconciliation do not commit the expected value. Writes BaseValue/CurrentValue/Unclamped values only at reflected valid offsets and sends OnRep with previous attribute bytes. Keeps collision-gated damage/shield-first math and native lethal handling. C++ build passes, deployed hidden host joinable. Fresh human hit NOT yet verified; runtime currently human0 bots0. Check for 'damage attribute unavailable' and 'human hit' before claiming fixed.
Launcher Battle Pass tab installed (MainWindow.Passes.cs, AccountClient methods). Live authenticated catalog reads common_core wallet Shared/current-platform balance, grouped named items, 20-second refresh while open, explicit purchase confirmation and owned/insufficient-funds states. Prices are standard pass-plus-remaining-tier totals: S1 shop four items4400; S2 950+69*150=11300; S3 950+99*150=15800, explicitly not discounted Battle Bundle. User requested full unlock prices and seasons1,2,3. S1 technically Season Shop. Reward lists use local complete Jsons/Season2,3 data; cosmetic names from fortnite-api.com. Excludes obsolete challenge schedule/account XP bookkeeping objects, retains cosmetics/banners/currency/boost inventory. S2=53 and S3=79 aggregated reward entries, S1=4.
Backend Utils/syndicatePassShop.js uses single Mongo document compare-and-swap for both profiles+receipt, authenticated account identity only, no client-provided price/balance/rewards. Duplicate/replayed pass receipt returns owned with no charge. Existing cosmetics deduplicated; currency/token rewards granted once. Both profile revisions update for next game login. Banners route common_core. Shared per-account lock serializes Fortnite profile routes against shop writes; XP worker CAS still protects conflicts.
Validation: standalone Mongo test passes insufficient balance, unknown pass, simultaneous duplicate, concurrent different passes, replay and all reward presence. Live HTTP test passes unauthorized rejection, login, balance, deduction, persisted ownership, replay and logout using deleted isolated test account. No player charged. .NET build zero warnings/errors. WPF artifact rendered and visually checked (work/tilted/shop-preview.png); final display puts outfits before boosts. Installed launcher exe/dll hashes match candidates. Copying unchanged cobalt.dll was locked by existing client; launcher exe/dll updated successfully. Backend restarted hidden, launcher reopened. Backend sessions cleared by restart: user must sign in again.
References: https://www.fortnite.com/news/welcome-to-season-3-battle-pass-breakdown ; https://www.fortnite.com/news/renegade-raider-and-more-return-in-fortnites-og-season-shop
Damage follow-up: user reported 77/77 shield. Live reflection identified FortHealthSet.Health offset48, MaxHealth80, CurrentShield112, Shield144 (all 32-byte attributes); CurrentValue12 and BaseValue8. Shield is CAPACITY, not remaining shield. Previous fallback incorrectly wrote capacity. Corrected ALL bot integration reads to CurrentBotShield (CurrentShield.CurrentValue), all damage and bot recovery writes to CurrentShield, preserving Shield. Native GetShield is kept only for versions outside4.x. Added first-bot warmup self-test: current77, current0, unchanged capacity, then restores original attribute bytes. New build deployed and host ready; awaiting warmup join and test log. Earlier process-stop report was incorrect: sandboxed process enumeration hid running host/client; use require_escalated for process checks.
2026-10-10: Gameplay currency rewards added to existing deduplicated progression event transaction: kill100 V-Bucks, win200, survival0. Shared Currency:MtxGiveaway wallet, XP unchanged. Queue worker acquires shared account lock; profile writes and receipt remain atomic CAS. Isolated Mongo test passed exact amounts, concurrent duplicate/replay protection, survival0, retained XP and launcher balance compatibility. Test account removed. Backend restarted hidden; no historical rewards retroactively paid.

## 2026-10-10 chest supplies, pickaxe feedback, loot ramps, pass artwork
- Pickaxe swings use replicated weapon activation/shot feedback and keep the firing state alive for 0.45 seconds rather than stopping it the next frame. Visual client confirmation remains pending.
- Picking up a gun preserves the chest-loot commitment. Bots target and collect ammo plus wood/stone/metal, including partial stacks capped at 999. Full weapon inventory does not block supplies. Builds consume a tracked material stack; death material drops remain capped at 50 of each type.
- Nearby elevated loot can trigger native supported ramps with committed traversal and persistent route success/failure from actual height gained. Pursuit can place cover walls. Native grid/support validation retained.
- Battle Pass cards now show Season 1/2/3 artwork, tier badges, scrollable reward groups, and clear prices. WPF rendered at 1280x720 and inspected using current catalog with a fictional preview balance.
- C++ and launcher Release builds passed. Installed both; host startup reached joinable and matchmaking ready. In-match swing, supply collection, and ramp traversal still need gameplay verification. No claim that these behaviors are fully tested in a live match.
- Outstanding prior issue: human shield damage logs still need investigation; this change does not claim to fix that issue.
