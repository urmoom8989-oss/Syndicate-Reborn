# Match telemetry schema (v1)

The game server (`game-server/.../SpectraBots/SyndicateTelemetry.h`) writes one file per match:
`Runtime/Server/telemetry/<matchId>.jsonl`, one JSON object per line. The backend
(`backend/src/Utils/telemetry/`) reads, validates and analyses these files.

## Envelope (every line)

| field | type | meaning |
|---|---|---|
| `v` | int | schema version (1). The backend rejects other versions until `upgradeEvent` maps them. |
| `match` | string | match id `YYYYMMDD-HHMMSS-xxxxxxxx` (UTC start + random) |
| `seq` | int | 1, 2, 3… per match. Gaps = lost lines, repeats = duplicates. |
| `id` | string | `<match>:<seq>`: stable event id, used as evidence everywhere |
| `t` | number | server time in seconds since the map loaded |
| `ts` | string | UTC wall clock, ISO 8601 |
| `type` | string | one of the types below |

Positions (`pos`) are `[x, y, z]` in game units (centimetres). `poi` is the nearest named location
(`Athena_POI_*` level, prefix removed) within 90 m, or `""`. `grid` is the 100 m cell `"x,y"`.
Actors are `{"kind":"bot"|"human"|"none","id":"b3"|"h1a2b3c4d5e6f","team":N}`. Human ids are
FNV-1a hashes of the player name with a random per-install salt (`telemetry/salt.txt`): stable on
one PC, not reversible with a name list, never the name itself.

## Event types

| type | when | fields |
|---|---|---|
| `match_start` | game phase reaches the bus | `fortniteVersion`, `map`, `playlist`, `mode` (solo/duo/squad/50v50), `arena`, `humans`, `bots`, `players`, `pois`, `schema`, `botVersion`, `serverBuild` |
| `landed` | first time on the ground after the bus | `actor`, `pos`, `poi`, `grid`; bots also `targetPoi`, `targetError` (cm from the chosen spot), `assignedRegion`, `assignedWeight`, `strategy` (hot / assigned / spread / squad_follow / unassigned) |
| `bot_state` | a bot switches activity (at most one line per bot per 0.75 s) | `bot`, `from`, `to`, `held` (seconds in `from`), `flips` (quick switches folded into this line), `health`, `shield`, `pos`, `enemyDist`, `enemyVisible`, `hasGun`, `shots`, `hits` |
| `bot_stuck` | a bot trying to travel (goal more than 7 m away, not breaking a wall, fighting, reviving, healing, building or boxed) stays within 5 m for 6 s | `bot`, `pos`, `poi`, `grid`, `state`, `recovery` (escape_route / unstick / wide_detour), `strike` (stalls in a row near this spot within 30 s), `goalDist` |
| `downed` | a human is knocked | `victim`, `killer`, `cause`, `weapon`, `pos`, `poi`, `grid`, `distance` |
| `elimination` | anyone is eliminated | `victim`, `killer` (or null), `cause` (weapon / storm / other), `weapon`, `pos`, `poi`, `grid`, `distance`; bot victims also `victimState`, `hadGun`, `shots`, `hits`, `place`, `alive`; human victims killed by a bot also `killerState` |
| `player_left` | a human leaves a bot match | `actor`, `alive` |
| `storm_phase` | the storm phase changes | `from`, `to`, `alive`, `seconds`, `stats` (storm/combat/fall deaths, builds ok/failed, stuck, heals, mobility, edits during the phase) |
| `server_perf` | every 10 s (host health) | `ticksPerSecond`, `worstFrameMs`, `botMs`, `replicationMs`, `bots` |
| `build_fight` | a bot's build fight ends (3 s back in neutral, target changed or lost, enemy down) | `bot`, `skill`, `enemy` (bot/human), `trigger` (why the first non-neutral decision was made), `states` (shot-block / height-contest / box-fight seen), `outcome` (enemy_down / bot_down / traded / took_height / disengaged), `end`, `seconds`, `ok` and `failed` (count per action), `sequence` (action codes in order, `x` after a failed one: W wall, R ramp cover, C contest ramp, K cone on enemy, T top cover, S swap shotgun, A swap rifle, P peek, V recover), `distStart`, `heightStart`/`heightEnd` (opponent Z minus ours), `heightBest` (our best height above them), `healthStart`/`healthEnd`, `shieldStart`/`shieldEnd`, `matsStart` |
| `checkpoint` | every 30 s | `alive`, `humansAlive`, `botsAlive`, `stormPhase`, `gamePhase` |
| `match_end` | one team left, or game phase EndGame | `reason` (winner / endgame / superseded), `winningTeam`, `winnerKind` (human / bot / mixed), `humansAlive`, `botsAlive`, `dropped` (lines the writer had to drop) |

Bot activities (`bot_state.to`), most specific first: `eliminated`, `bus`, `skydiving`, `knocked`,
`reviving`, `healing` (using a consumable), `fleeing` (survival retreat to a hiding spot), `boxing`
(panic box), `build_fighting`, `holding` (arena: holding a boxed position), `repositioning` (a long
planned walk, e.g. to high ground), `escaping_stuck` (short stuck recovery), `building`, `fighting`,
`pursuing`, `rotating`, `farming` (gathering materials), `looting`, `harvesting` (a pickaxe swing that
is not part of another activity), `alert` (looting paused by a threat, no opponent in view), `roaming`.
A swing at an obstacle while looting, chasing or rotating counts as that activity. Bot builds from
before 2026-10-27 only report the older set (`escaping_stuck` then also covered fleeing). They are
derived from the state the bot AI acts on. The AI does not score alternatives, so no candidate list exists to log.

## Example lines

```json
{"v":1,"match":"20261027-181502-9f3a11c2","seq":1,"id":"20261027-181502-9f3a11c2:1","t":61.20,"ts":"2026-10-27T18:15:02.114Z","type":"match_start","fortniteVersion":"4.50","map":"Athena","playlist":"Playlist_DefaultSolo","mode":"solo","arena":false,"humans":1,"bots":99,"players":100,"pois":18,"schema":1,"botVersion":"spectra-2026.10.27","serverBuild":"Oct 27 2026 17:02:11"}
{"v":1,"match":"20261027-181502-9f3a11c2","seq":214,"id":"20261027-181502-9f3a11c2:214","t":118.40,"ts":"2026-10-27T18:15:59.301Z","type":"landed","actor":{"kind":"bot","id":"b17","skill":2,"team":20},"pos":[-1203,23880,2210],"poi":"City_001","grid":"-1,2","targetPoi":"City_001","targetError":412,"assignedRegion":0,"assignedWeight":8,"strategy":"hot"}
{"v":1,"match":"20261027-181502-9f3a11c2","seq":388,"id":"20261027-181502-9f3a11c2:388","t":141.90,"ts":"2026-10-27T18:16:22.800Z","type":"elimination","victim":{"kind":"bot","id":"b17","skill":2,"team":20},"killer":{"kind":"human","id":"h3c09e1f2a7b4","team":1},"cause":"weapon","weapon":"WID_Shotgun_Standard_Athena_UC_Ore_T03","pos":[-1150,23710,2210],"poi":"City_001","grid":"-1,2","distance":640,"victimState":"looting","hadGun":false,"shots":0,"hits":0,"place":87,"alive":86}
```

## Reliability

- Lines are appended by a background thread (the game thread never touches the disk) and flushed
  each batch. A crash loses at most about a second of events.
- A file without `match_end` is recovered as `terminated_unexpectedly` with everything up to the
  last line. The launcher closing does not matter: the server writes the file.
- The backend drops duplicate ids, sorts by `seq`, reports gaps (`missing_events`), malformed and
  invalid lines, and never throws on bad input.
- Limits: 32 MB per match file, 20 000 queued lines, 16 KB per line.
- Uploads (opt-in) are idempotent per match id (`uploaded.json`) and retried with backoff.

## Analyses and their definitions

See `landing.js` (hot-drop definitions at the top), `weaknesses.js` (each finding: observed facts,
hypothesis, alternatives, experiment, test case, success criterion, evidence ids), `playstyle.js`
(labels only with 3+ matches), `benchmark.js` (release gate) and `dataset.js` (training examples,
deterministic 80/20 split by match id).

Rates carry a 95% Wilson interval and the sample size. Places with fewer than `minMatches`
(default 5) matches are marked "insufficient data", which is different from "nobody lands here".

## Troubleshooting

| symptom | check |
|---|---|
| no files | Settings > Match data > "Record matches" is on; the server was started by the launcher (it sets `TILTED_SERVER_READY_FILE`); the game server DLL was rebuilt after this change |
| file ends early | look in `Runtime/Server/ready.pid.bot.log` at the last event's time for a crash |
| `missing_events` | the server crashed between queueing and writing, or the disk was full |
| diagnostics window says "token" | start the backend once; it creates `telemetry/dev-token` |
| uploads failing | Status tab: `upload.failing` shows the last error and next retry |
