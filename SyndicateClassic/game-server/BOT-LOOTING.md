# Bot early-game looting and the reactive combat interrupt

Bots no longer get a free loadout. They land with nothing, loot their POI (Point of Interest, the town they drop into) for a real loadout, swap guns for better ones, and drop looting the moment a fight starts.

| File | What it does |
|---|---|
| `Erbium/Erbium/Private/SpectraBots/BotLootPlan.h` | Decisions: gun roles, loadout goal, pickup/swap choice, loot scoring, the phase machine and the interrupt. Pure C++, tested by `Validation/loot-plan-test.cpp`. |
| `SpectraBotsIntegration.h` → `LootTuning()`, `BotLoadout()`, `PoiLootLeft()`, `UpdateLootPhase()` | Reads the game state into the planner's signals, runs the planner, and carries out the interrupt. |
| `SpectraBotsIntegration.h` → `UpdateMatchActions()` | The existing loot routine: scanning, walking through doors and rooms, opening chests, picking up guns. It now uses the planner to choose and swap guns. |

## Priority in the bot tick

```
FLEE_OR_HIDE / HEAL  >  FIGHT / BUILD_FIGHT  >  BOX_UP  >  LOOT_POI  >  FARM_MATERIALS  >  PATROL / ROTATE
```

The survival, heal, panic-box and combat layers run first. The planner only decides what the bot does once none of them has taken the tick.

## 1. No instant grants

* **Removed:** the starting supply kit (heals, fort, throwables, grappler, rockets), the first-zone shotgun + AR grant and the 400-of-each starting materials. Bots land with `StartMaterials` (0) and nothing else.
* **Optional last resort:** with `TILTED_BOT_EMERGENCY_GUN=1`, a bot still unarmed 180 s after landing gets one automatic weapon. It is off by default.
* **Death drops** are what the bot actually carried.

## 2. Loadout goal and inventory upgrades

* **Goal:** one AR or SMG, one shotgun, at least one heal item, and 100+ materials in total.
* **Roles:** each gun gets a role from its definition name: AR/SMG, shotgun, sniper, pistol or explosive. Within a role, the value is rarity × 10 plus a category rank. ARs rank above SMGs, and pump/heavy shotguns above tactical ones.
* **Pickup choice** (`Consider`):
  * Unarmed: take any gun.
  * Same role already carried: take it only if it is better. It replaces the worst gun of that role.
  * Free slot: a core role (AR/SMG or shotgun) is always taken. Extras (sniper, pistol, explosive) are taken only once both core roles are filled.
  * Full inventory: a missing core role replaces the weakest extra. An AR or shotgun is never dropped for an extra.
* **Swap:** the choice is checked again at the gun, because the inventory may have changed on the way. The old gun drops at the bot's feet only after the new one is equipped, the same way a player swaps. Log: `loot: swapped … for … (why)`.
* **Order:** guns that fill a missing role cost 0.45× their distance, upgrades 0.7×, plain extras 1.2×. Height above 150 units costs 1.5× (stairs, upper floors). The first heal stack is looked for at twice the normal reach.

## 3. Scanning and pathfinding (finding chests, floor loot and the way into houses)

The bot uses the existing loot routine, so the movement is the same proven code:

* **Scan:** every 0.65 s (staggered per team), from the per-frame actor cache (`CachedActors`):
  * Floor pickups within 1.3 k units, or 2.7 k while the loadout is incomplete.
  * Unsearched chests within 9 k while the loadout is incomplete.
  * Supply drops (some squads only).
  * Chests are scored by distance, a learned "this chest was reachable" memory and a penalty when another bot already claimed it. Height limits keep bots from chasing loot on a roof they cannot reach.
* **Getting inside:** `RouteThroughEntrance` finds the house's doorway or opening, lines the bot up and walks it through. `OpenNearbyDoors` opens closed doors on the way. Loot on a higher floor makes the bot use the stairs, or build a ramp if needed. Targets it cannot reach (40 s, or 20 s for loot far overhead) go on a skip list for 30 s.
* **Opening and picking up:** a chest is opened through the server's real loot system (`SpawnLootHook`), so its items fall on the floor. The bot then stays near the chest for 12 s to collect what it wants from the floor (guns, heals, ammo, materials).
* **POI cleared:** checked every 2 s. The POI counts as cleared when no unsearched, unclaimed chest is left within 3000 units of the landing spot and no useful floor gun is left within 1500 units. It uses the same filters as the loot scan: season pool, rarity, height limits, line of sight and the skip list.

## 4. Phases

| Phase | When |
|---|---|
| COMBAT | Any threat (see 5). Stays until 3 s without a threat. |
| ROTATE (urgent) | In the storm, or a rotation from circle 3 on, or the safe zone is more than 12 k away. |
| FARM_MATERIALS | Guns and heals done, materials short, and the POI cleared (or half the loot time used). |
| LOOT_POI | Loadout incomplete (up to 240 s, then the bot moves on even if unarmed, so it never stays looting at the storm edge), or loadout complete but the POI not yet cleared (first 120 s). An early, non-urgent rotation waits during this phase. |
| FARM / ROTATE | Looted: farm if below the material target, otherwise rotate/patrol. |

## 5. Instant transition from looting to combat

Each decision, `UpdateLootPhase` builds the signals:

* **Damaged:** health or shield dropped in the last second. Storm damage does not count.
* **Shot at:** incoming fire was tracked recently.
* **Opponent in sight:** the bot's current visible enemy, or `NearbyEnemy` (line of sight within 4000 units).
* **Nearest gunfire:** players whose ammo counter just dropped, and enemy bots that are firing. Pickaxe swings do not count.

An unarmed bot never enters COMBAT. It has nothing to fight with, so it keeps searching for a gun. If it is hurt, the survival layer makes it flee.

`Next()` checks for a threat **before** anything else. If any signal is set (gunfire counts within 2500 units), the phase becomes COMBAT on that same call, and `Interrupt` is true exactly once. On that tick the glue:

1. Drops the loot target, door route and chest wait.
2. Stops harvesting and farming.
3. Resets the combat scan so it runs right away.
4. Equips the shotgun if the threat is within 1200 units and one is carried, otherwise the AR/SMG.

The rest of the same tick then reaches `UpdateCombat` and the build-fight, box-up and flee layers, which run before any looting. While the phase is COMBAT, the POI loot push (`NeedLoot`) and farming are off. If a real fight is found, the tick ends there. If only distant gunfire was heard and no opponent is found, the bot can still grab loot next to it with its gun out. There is no one-frame delay and no "finish opening this chest first". A pending chest is simply picked up again once the fight has been quiet for 3 s, if it is still worth it.

Log: `loot: interrupted (took damage), shotgun ready, enemy 640, gunfire 640`, then `loot: combat -> loot-poi (…)` once the fight has settled.

## Tuning

Server-wide overrides (environment variables on the game server, read once):

* `TILTED_BOT_FREE_LOADOUT=1`: old behaviour (starting supplies, first-zone gun grant, 400 materials each).
* `TILTED_BOT_EMERGENCY_GUN=1`: one gun for a bot still unarmed after 180 s.
* `TILTED_BOT_START_MATS=<n>`: materials of each type at landing (default 0).
* `TILTED_BOT_LOOT_TIME=<s>`: maximum time in LOOT_POI (default 240, 30–900).

Other defaults are in `SyndicateLoot::Tuning`: PoiRadius 3000, HearRadius 2500, ThreatQuiet 3 s, CloseRange 1200, MinMaterials 100, MinHealItems 1.

## Tests

```
cd game-server/Validation
g++ -std=c++17 -O1 -Wall -Wextra -o loot-plan-test loot-plan-test.cpp && ./loot-plan-test
```
