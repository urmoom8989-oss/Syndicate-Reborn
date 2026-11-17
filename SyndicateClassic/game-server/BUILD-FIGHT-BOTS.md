# Bot build fights

Bots answer build battles with a small state machine instead of only boxing up when shot.

| File | What it does |
|---|---|
| `Erbium/Erbium/Private/SpectraBots/BotBuildFight.h` | The decisions. Pure C++, no engine types; tested by `Validation/build-fight-test.cpp`. |
| `SpectraBotsIntegration.h` → `UpdateBuildFight()` and helpers | Reads the world into a `Snapshot`, calls `Decide()`, carries out the action with the existing `BuildCover` / `BuildCone` / `BotEditOwnPiece` / inventory equip. |
| `UpdateCombat()` | Calls `UpdateBuildFight()` right after target selection. While a build fight is on, the older ad-hoc piece control (edit peek, cones, 90s, Hard box push, ramp rush) is switched off so the two never fight over the same tick. |

Runs on Seasons 3–X (`TacticalBotSeason()`); Pre-season to Season 2 stay season-accurate. Skill "bot" has it off, Easy only blocks open shots, Medium and Hard get everything.

## States

```
                 opponent builds toward/above us (and we can build)
   Neutral ─────────────────────────────────────────────► HeightContest
     │  ▲                                                     │ height won (we are WinMargin above),
     │  │ out of EngageRange / threat gone                     │ height cap, or out of mats
     │  │                                                     ▼
     │  └──────────── ShotBlock ◄── open angle / under fire ── (any)
     │                    │ distance < ShotgunRange or we broke through their wall
     └────────────────────┴──────────────► BoxFight  (Build → Swap → Peek → Shoot → Re-cover → Peek …)
```

* **ShotBlock** – the opponent has an open line to us (or is hitting us / aiming at us): place a wall (or a ramp if they are above) *before anything else*, fire is blocked until covered. Behind cover the bot edit-peeks (window at range), shoots for the peek window, then the wall resets.
* **HeightContest** – opponent ramps up or stands above us within `ContestRange`: ramp toward them (far) or 90 beside them (close), protective wall when shot while climbing, roof over us when directly underneath, and a cone on top of their head to stop their next ramp. Ends when we are `WinMargin` above them, the height cap is reached, or after 6 s without progress.
* **BoxFight** – within ~1.5 tiles, or right after breaking their wall: strict loop **Build → Swap to shotgun → Peek (edit) → Shoot (`PeekWindow`) → Re-cover**, repeating from Peek. No mats: skips the build steps and keeps shooting. Broke through their wall: shotgun immediately.
* **Neutral** – normal combat code; at mid/long range the module only swaps to the AR/rifle once (`RifleRange`).

## How one decision is made

Every tick `Decide()` lowers its timers; a real decision happens only every `DecisionInterval` (Hard 0.12 s, Medium 0.2 s, Easy 0.3 s). Between decisions the last "hold fire / own the weapon" verdict stays in force, so a "cover first" decision cannot be undone by the next frame.

1. **Stimuli** – a *new* threat (under fire, or an open angle while visible) or a *new* climb starts a reaction delay (`ReactionDelay` ± 20 %) and one roll against `BuildChance`. A failed roll means the bot does not build for that stimulus (it fights like a non-builder).
2. **State** – priority: HeightContest (if they climb and we can build) → BoxFight (close or broke through) → ShotBlock (threatened and can build) → stay in ShotBlock while behind our cover → Neutral.
3. **Reaction gate** – until the delay runs out the bot keeps doing what it did (normal combat), except in box fights where it is already engaged.
4. **Action** – one action per decision, by state (see above). Build actions also respect `BuildInterval` and the existing build backoff, ramp height cap and material reserve (`MinMats`).
5. **Execute** – the integration layer performs it and reports success back (`Report()`), which advances the box loop (e.g. a successful wall moves Build → Swap).

Shooting vs building per tick: the module returns `BlockFire` whenever cover is missing and can still be built, while building/swapping/editing, and between peeks. Otherwise (Neutral, or the Shoot step of a peek) the normal aim/fire code runs, with its own skill spread and fire cadence.

## Tuning

Defaults per skill are in `SyndicateBuildFight::ForSkill()`:

| Knob | Easy | Medium | Hard |
|---|---|---|---|
| ReactionDelay (s) | 0.8 | 0.45 | 0.28 |
| BuildChance | 0.45 | 0.75 | 0.95 |
| DecisionInterval (s) | 0.3 | 0.2 | 0.12 |
| BuildInterval (s) | 1.2 | 0.55 | 0.3 |
| ShotgunRange (units, tile 512) | – | 640 | 768 |
| RifleRange | 1400 | 1400 | 1300 |
| EngageRange | 1800 | 2200 | 2600 |
| Height contest / box loop | off | on | on |

Server-wide overrides (environment variables on the game server process, read once):
`TILTED_BF_OFF=1`, `TILTED_BF_REACTION`, `TILTED_BF_BUILD_CHANCE`, `TILTED_BF_SHOTGUN_RANGE`, `TILTED_BF_RIFLE_RANGE`, `TILTED_BF_DECISION`.

## Cost

Only bots with an opponent inside `EngageRange` (+400) run it. A decision costs three traces (line to the opponent, ground under the opponent, terrain clearance under us) plus an inventory scan; between decisions it is a few float updates.

## Never stuck

* Two failed placements in a row (cone, wall, ramp) end building for that stimulus; the bot fights without it.
* Two failed edits stop peek attempts until the state changes; cover it cannot edit sends it back to normal combat after 2 s.
* A height contest needs the opponent actually building; it ends after 6 s, and a new one waits 8 s unless they keep climbing.
* The edit-peek timer runs before the module every tick, so an opened edit always closes.
* Module memory resets when the target changes or the module was not run for 2 s.

## Rules kept

No teleporting, no damage immunity, no free pieces: every wall/ramp/cone goes through the same native placement check, costs materials and can be shot. Arena third-party holding (`AllowPursuit=false`) turns contest ramps into walls instead of pushes.

## Bot log

`build fight: neutral -> box-fight (box: build)` on state changes, and one line per action (`build fight: wall between us (open angle: block line of sight)`, `swapped to shotgun`, `edit peek`, `re-covered`, `90 to retake height`, `cone above opponent` …).

## Tests

```
cd game-server/Validation
g++ -std=c++17 -O1 -Wall -Wextra -o build-fight-test build-fight-test.cpp && ./build-fight-test
```
Covers: (17 cases) wall first under open fire, no open-field firing, reaction delay, build roll, height contest (ramps, cone denial, exit when above), height cap, strict box-loop order and repeat, rifle swap without spam, shotgun on break-through, no-mats fallback, disabled tier / far targets, decision throttling, plus the review regressions (shooting through its own peek, failing placements, non-building opponent on a hill, uneditable cover, no-shotgun weapon lock, failed peek shoot window).
