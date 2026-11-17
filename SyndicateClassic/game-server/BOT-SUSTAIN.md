# Bot sustain: consumables, quick heals, material farming

Bots no longer get health and shields for free. They carry real items, use them with real use
times, take a quick heal in a lull mid-fight, and farm materials when they run low.

| File | What it does |
|---|---|
| `Erbium/Erbium/Private/SpectraBots/BotSustain.h` | Decisions: item table, item choice, safety window, heal planner, farming rules, prop classification. Pure C++, tested by `Validation/sustain-test.cpp`. |
| `SpectraBotsIntegration.h` → `UpdateBotHealing()`, `BotHealSituation()`, `UpdateSustainClocks()` | Reads the situation, starts/continues/cancels/completes an item use, applies the result only at the end. |
| `SpectraBotsIntegration.h` → `UpdateFarming()`, `EnsureFarmIndex()` | Prop index and the farm routine (uses the normal pickaxe swing). |

## Priority in the bot tick

```
FLEE_OR_HIDE / HEAL  >  BUILD_FIGHT  >  BOX_UP  >  LOOT_POI  >  FARM_MATERIALS  >  PATROL / ROTATE
```

* The survival layer runs first; a fleeing bot heals through the same item-use code once hidden.
* `UpdateBotHealing()` runs before combat, so a safe window is used before the next engagement.
* Farming runs only when every combat state, chest securing, regroup, rotation and storm checks have passed, and before normal looting/patrol movement.

## 1. Consumables

| Item | Use time | Effect |
|---|---|---|
| Mini shield (`Athena_ShieldSmall`) | 2 s | +25 shield, up to 50 |
| Big shield (`Athena_Shields`) | 5 s | +50 shield |
| Bandage (`Athena_Bandage`) | 3.5 s | +15 health, up to 75 |
| Medkit (`Athena_Medkit`) | 10 s | health to 100 |
| Chug Jug (`Athena_PurpleStuff`) | 15 s | health and shield to 100 |

* **Inventory**: items are tracked in the bot's supplies. They come from what bots pick up off the floor (the starting kit roll is off by default, see BOT-LOOTING.md) (chests, eliminations), on every season now (before only Seasons 6+). Stack limits as in BR (minis 6, bandages 15, bigs/medkits 3, chug 1).
* **Choice**: health ≤ `healThreshold` (critical): health first (chug when both bars are low and there is time, else medkit, else bandages). Health above 75: shields first (minis up to 50, then bigs). In between: bandages up to 75, then shields. Only items that fit the current safe window are considered.
* **Use**: the bot stops, crouches and stands still for the full use time; the result is applied only at the end. Moving more than 60 units, taking damage, an opponent coming into view, being shot at, the storm, or the zone forcing a move cancels it, and the item is kept.
* **No free refills**: the 150-second instant full heal, the 60-second instant shield and the instant landing shield are off (back with `TILTED_BOT_PASSIVE_REGEN=1`). Bots only have what they carry: chest and floor loot. If the item is gone when the use finishes, nothing is applied.
* A knock ends a use in progress (the item is kept). Before a long item in the open the bot finishes its protective box first; on Seasons 1–5, which have no panic box, it waits for cover instead.

### How safety is judged before a long timer (`SafeUseTime`)

1. **Never** while an opponent is in view, while being shot or aimed at, or in the storm.
2. **Time to contact**: distance to the known opponent ÷ (their closing speed, but at least sprint speed 550 u/s). Cover adds 2.5 s (they must get through the box). With nobody known it is unlimited.
3. **Zone**: when the bot must rotate soon, the window shrinks to what the rotation allows.
4. **Mid-fight / recent pressure**: in combat, or within 4 s of pressure, only quick items (≤ 3.5 s: minis, bandages), only in cover, and only after 1.75 s without pressure or damage.
5. **Long items** (big shield, medkit, chug) need 4 s without damage or pressure. In the open with an opponent within 3000 units, only quick items. Before a long item in the open the bot boxes up first (no building while using an item) and starts the heal once covered.
6. A 1.5 s safety margin is always kept on top of the use time.

"Cover" = the opponent's eye line to the bot is blocked by something that is not a pawn, the bot is holding a box, or there is a roof over it.

## 2. Mid-fight quick heal

Boxed (or otherwise covered), the opponent stops pushing: not visible, not shooting or aiming at us, not closing in, for 1.75 s (Hard 1.5 s, Easy 2.5 s). The bot then pops a mini or a bandage. It tops shields only up to `minShieldsBeforeFight`, unless health is critical. Renewed pressure cancels the pop, and so does an unseen opponent who will arrive before the item finishes (distance ÷ closing speed); the build-fight loop takes over again afterwards (peek / reposition).

## 3. Material farming

* **When**: wood+stone+metal below `targetMaterialCount`, and not in any fight, flee, heal, revive, storm, rotation or regroup, and the bot already has at least two weapons. It farms until `farmUntil` (target + 250).
* **What**: every map prop that drops resources, with its own `ResourceType` (wood/stone/metal). That is what the game pays per hit, and the harvest credit now uses the same field. House structure (walls, floors, roofs, stairs, doors, windows, pillars, beams, rails, chimneys …), chests, ammo boxes, vending machines and other containers are never farmed. Class names are only a fallback for builds without `ResourceType`, matched as whole words (no "car" in "Scarecrow").
* **Priority**: wood first, for fast boxes, until a solid wood stack. Stone and metal from the late game (circle 4+), when holding a box, or once wood is plentiful.
* **How**: the best prop within 2500 units is chosen by distance, the wanted material and size, skipping props another bot is farming. The bot walks there and swings the normal pickaxe; materials per hit are unchanged. Props that do not take damage, that cannot be reached within 15 s, or whose route needed an escape go on a farm-only skip list for 2 minutes (the navigation list stays separate). Only props within 250 units of height are considered, so bots do not ramp up hills to farm. A bot with a usable heal heals before farming.
* **Cost**: the prop index is built once per match (one actor scan; retried after 60 s only if it came back empty). A bot's search reads only nearby 40 m grid cells, every 2 s while it has no target.

## Tuning

Defaults by skill:

| Parameter | Bot | Easy | Medium | Hard |
|---|---|---|---|---|
| healThreshold | 50 | 50 | 50 | 50 |
| minShieldsBeforeFight | 0 | 25 | 50 | 75 |
| quick-heal quiet time (s) | 3 | 2.5 | 1.75 | 1.5 |
| targetMaterialCount | 300 | 400 | 500 | 600 |
| farm until | 450 | 600 | 750 | 900 |

Server-wide overrides (environment variables on the game server, read once):

* `TILTED_BOT_SUSTAIN_OFF=1`: old behaviour (no item AI, no farming, instant refills).
* `TILTED_BOT_HEAL_THRESHOLD`, `TILTED_BOT_MIN_SHIELDS`, `TILTED_BOT_TARGET_MATS`.
* `TILTED_BOT_PASSIVE_REGEN=1`: keep the item AI but bring back the instant refills.

## Bot log

`heal: started Athena_ShieldSmall (2.0s, quick heal: pressure stopped) …`, `heal: used …`, `heal: cancelled Athena_Medkit (opponent in view), item kept`, `heal: boxing up before a long heal`, `farm: indexed N farmable props`, `farm: heading to BP_Tree_… (want wood, …)`, `farm: done (…)`.

## Tests

```
cd game-server/Validation
g++ -std=c++17 -O1 -Wall -Wextra -o sustain-test sustain-test.cpp && ./sustain-test
```
