# Bot survival and combat priority

Low-health bots no longer wander: they flee, hide and heal. The same layer decides which combat
mode a bot is in, so health is always checked before aggression.

| File | What it does |
|---|---|
| `Erbium/Erbium/Private/SpectraBots/BotSurvival.h` | Decisions: mode priority, flee/hide state machine, retreat-spot scoring. Pure C++, tested by `Validation/survival-test.cpp`. |
| `SpectraBotsIntegration.h` → `UpdateSurvival()`, `FindRetreatSpot()`, `SurvivalTuning()` | Reads health/threat/heals into a `Snapshot`, searches retreat spots with traces, carries out walls, movement, crouch and healing. |
| Main bot tick | Calls `UpdateSurvival()` before the panic box, healing, Arena hold, looting and `UpdateCombat()`. |
| `BotBuildFight.h` | The build-fight mode (shot blocking, height contest, shotgun peek loop) — see `BUILD-FIGHT-BOTS.md`. |

## Priority, every tick

```
FLEE_OR_HIDE   health < lowHealthThreshold and shield < 25, with a threat around
     >  BUILD_FIGHT   opponent engaged (visible or shooting) inside engage range
     >  BOX_UP        sudden burst (>= 35 damage in ~1.5 s) from someone we cannot see
     >  PATROL        loot, rotate, follow squad
```

1. `UpdateSurvival()` runs first on every decision. It updates the damage window (last ~1.5 s), builds the snapshot and asks `Arbitrate()` for the mode. Health is the first check: a critical bot enters FLEE_OR_HIDE on the same tick and every offensive routine is dropped (ramp pushes, box pushes, wall takes, Arena hold box, build-fight loop, harvesting).
2. A critical bot still **fights** when running would be worse: a visible opponent closer than `CornerDistance` (turning around gets you shot in the back), no reachable retreat spot at all, or an opponent who is even weaker.
3. Entry needs a live threat (visible opponent, being shot, or more than 5 damage in the last moments; storm damage does not count). FLEE_OR_HIDE has hysteresis: it lasts until health+shield reaches `RecoverHealth`, the bot is cornered, `MaxFleeTime` passes, or it stayed unseen for `HideHold` seconds with nothing left to heal with. Leaving without recovering starts an 8 s cooldown (being shot can re-trigger it after 4 s of fighting), so bots never flip-flop.
4. Otherwise the tick continues as before: an active build fight cancels a reflex box-up, then the panic box, healing, Arena hold, loot and `UpdateCombat()` (where the build-fight module runs).

## FLEE_OR_HIDE

* **Fallback** – outdoors and exposed: one wall between us and the shooter before turning away.
* **Move** – sprint to the best retreat spot. Outdoors under fire: another wall behind us every `FleeBuildInterval` after moving 250 units (double-wall retreat).
* **Hide** – at the spot: crouch, stop, face the threat.
* **Heal** – while unseen: shields/bandages/medkits from the bot's own items, with their normal use times (Seasons 6+, where bots carry heals). Being hit stops the heal.

### Retreat spot search (`FindRetreatSpot`)

While moving, or when spotted, every `ReplanInterval` (0.75 s while no spot exists; never while hidden at a spot) the bot samples 21 points around itself, away from the threat (0°, ±35°, ±70°, ±105°):

* **Inside a structure** (solid roof above): 350 / 700 / 1100 units — rooms, corners, behind interior walls and furniture.
* **Outside**: 900 / 1600 / 2400 units — trees, rocks, hills.

Each point is ground-checked, and the eye-line trace ignores the shooter's own body and never counts a pawn as cover. Route and roof checks only run for the three best points (about 50 traces per search). (walkable slope, not water or lava, inside the next circle, not a huge drop/climb). Then one trace from the opponent's eye decides whether it is hidden, and whether the blocker is natural cover (terrain, tree, rock, house wall) or a player piece. `Pick()` prefers hidden > natural cover > staying inside when indoors > directly away > farther from the threat > shorter sprint, and never picks a point toward the opponent. No acceptable point means cornered, so the bot fights.

## Tuning

Defaults from `SyndicateSurvival::ForSkill()`:

| Parameter | Easy | Medium | Hard |
|---|---|---|---|
| lowHealthThreshold | 25 | 30 | 35 |
| recoverHealth (health+shield) | 70 | 70 | 80 |
| retreatSpeedMultiplier | 1 | 1 | 1 |
| buildAggressionRate | 1 | 1 | 1 |
| fallback / retreat walls | no | every 1.2 s | every 0.8 s |
| cornerDistance | 600 | 450 | 350 |

Skill "bot" keeps the old behaviour.

* **retreatSpeedMultiplier** is a fraction of the normal sprint (0.5–1; 1 = sprint, below 0.8 = run). A bot can never move faster than a player.
* **buildAggressionRate** scales the build-fight module: build chance × rate, build interval ÷ rate (0.25–2, never faster than one piece per 0.25 s). 1 keeps the build-fight skill defaults; explicit `TILTED_BF_*` values still win.

Server-wide overrides (environment variables on the game server, read once):
`TILTED_BOT_FLEE_OFF=1`, `TILTED_BOT_LOW_HEALTH`, `TILTED_BOT_RECOVER_HEALTH`, `TILTED_BOT_RETREAT_SPEED`, `TILTED_BOT_BUILD_AGGRESSION` (and the build-fight `TILTED_BF_*` ones).

## Rules kept

No teleporting, no speed above sprint, no free healing (carried items only, normal use times; on seasons where bots carry no heals, they hide and then re-engage), every wall is a normal placed piece that costs materials and can be shot. Storm damage still wins: a bot in the storm keeps rotating instead of fleeing, and a fleeing bot that has to rotate follows the zone (fire off) instead of hiding outside the circle.

## Cost

Only fleeing bots search: 21 points × 2 traces, plus route and roof checks for the best three, at most once per `ReplanInterval`.

## Bot log

`survival: build-fight -> flee-or-hide (health=22 shield=0)`, `survival: retreat spot found (14 candidates, outdoors, hidden)`, `survival: wall between us before retreating`, `heal: started Athena_ShieldSmall …`, `survival: flee-or-hide -> patrol (…)`.

## Tests

```
cd game-server/Validation
g++ -std=c++17 -O1 -Wall -Wextra -o survival-test survival-test.cpp && ./survival-test
```
