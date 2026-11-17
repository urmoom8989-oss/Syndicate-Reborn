# Match intelligence, bot learning and landing analytics

What was built, how the data flows, what it can and cannot tell you, and how to verify it.

## Architecture (as found)

- **Launcher**: WPF .NET 8 (`launcher/launcher-source/SyndicateClassic`). It starts the backend and a hidden match server (`GameServer.cs`) and passes settings through environment variables.
- **Match server**: Erbium (C++, source in `game-server/`) inside a headless Fortnite server process we start. Bots (`SpectraBots/SpectraBotsIntegration.h`) run here, on the server's game thread. This is the authoritative place: it sees every landing, elimination and bot decision.
- **Backend**: Node/Express + MongoDB (`backend/`). It already consumed durable per-event files from the server (`progression-queue`). Routes in `src/Routes` load automatically.
- **Existing telemetry**: a text bot log and per-storm-phase "match report" lines; XP events with match ids. There was no structured per-match event stream, replay, or analysis.

## Data flow

```
match server (Erbium, our code)
  └─ SyndicateTelemetry.h ── background writer ──> Runtime/Server/telemetry/<match>.jsonl
backend (Node)
  └─ telemetry/store.js      validate, dedupe, gaps, crash recovery
     report.js / weaknesses.js / landing.js / playstyle.js / benchmark.js / dataset.js
     Routes/syndicateTelemetry.js   /tilted/telemetry/*  (this PC only + developer token)
     upload.js (opt-in) + Developer Tools test ──> discord.js:
        Dev Kit PC with TILTED_DISCORD_WEBHOOK in Helix-backend-main.env ──> Discord (?wait=true)
        otherwise ──> Cloudflare relay ──> Discord   (webhook only in the Worker secret)
launcher
  └─ Settings > Match data   explanation + switches (record, share, retention)
     Developer Tools (Dev Kit builds only): diagnostics window, Send test message
     DiagnosticsWindow       reports, weaknesses, landing, playstyles, versions, datasets, export
```

## Implemented

| Phase | Feature | Where |
|---|---|---|
| 1 | Match lifecycle: start, landings, knocks, eliminations (human and bot), player left, storm phases, 30 s checkpoints, end (winner / endgame) | `SyndicateTelemetry.h`, hooks in `SpectraBotsIntegration.h` |
| 1 | JSONL schema v1, stable event ids, seq numbers, schema version, server build and bot version | `SCHEMA.md` |
| 1 | Crash safety: background append + flush per batch; unfinished files recovered as `terminated_unexpectedly`; duplicate/gap/out-of-order/malformed detection | `store.js` |
| 1 | Readable match reports with timeline, encounters, decisions, issues, suspected mistakes, next steps, evidence ids | `report.js` |
| 1 | Opt-in upload through the existing relay, once per match, retry with backoff, counts only | `upload.js` |
| 1 | Retention (days + storage cap), delete one/all, audit log | `store.js`, route |
| 2 | Bot decision logging: activity switches with health, shield, enemy distance/visibility, gun, accuracy, quick flips; stuck events | `TelemetryBotState`, `bot_stuck` |
| 2 | Weakness detection: storm deaths, caught looting, unarmed deaths, pursuit deaths, low accuracy, stuck hotspots, oscillation, landing misses, no response at range. Each finding: severity, confidence, versions, counts, evidence, lead-up timeline, hypothesis + alternatives, experiment, test case, success criterion | `weaknesses.js` |
| 3 | Playstyle profiles by pseudonym (labels only after 3+ matches, observed vs inferred vs verified) and how humans beat bots | `playstyle.js` |
| 3 | Landing intelligence per Fortnite version: landings, share, avg/median per match, contested rate, opponents, first engagement, early eliminations, survival 60/90/180 s, Wilson intervals, insufficient-data flags, separate rankings (hottest, lowest contest, deadliest, safest, strategic value, least popular), heatmap cells, bot strategy outcomes, trends, holdout prediction check | `landing.js` |
| 4 | Versioned training datasets (landing + decision examples, outcomes for successes and failures, deterministic train/holdout split, validation that rejects identifying fields) | `dataset.js` |
| 4/5 | Bot version comparison with a release gate (sample size + significance; developer approval still required) | `benchmark.js` |

## Example: from an event to an evaluated fix

1. **Observed**: across 40 recorded 4.50 matches the weakness view lists *"Bots are killed while looting or harvesting: 212 of 610 combat deaths (35%, CI 31-39%)"*, severity high, with evidence such as `20261027-181502-9f3a11c2:388` (the elimination above: bot `b17` killed by a human at 6.4 m while `looting`, no gun).
2. **Investigate**: the lead-up timeline shows `b17` landed at `City_001` (strategy `hot`), switched `skydiving -> looting`, and never switched to `fighting` before dying. The landing tab shows City_001 contested in 90% of matches with 48% early elimination.
3. **Hypothesis (unverified)**: the loot routine does not yield to a nearby enemy. Alternatives: enemies approach from outside sight range; hot drops are lethal for everyone.
4. **Change**: in a candidate build, interrupt looting when an enemy is visible within 30 m or damage is taken; bump `SyndicateTelemetry::BotVersion`.
5. **Evaluate**: play 10+ matches on each version (same Fortnite version and mode), then Bot versions > compare `spectra-2026.10.27` vs the candidate. The gate passes only if "caught looting" improves and storm deaths, human win rate and accuracy do not get significantly worse. Then a developer approves the release. Rolling back means shipping the previous Erbium build (bot version is in every match file, so the regression is visible).

Note: these example numbers illustrate the format. No real match has been recorded with this build yet.

## Tests run

- `game-server/Validation/telemetry-test.cpp` (writer: envelope, seq/ids, escaping, NaN, checkpoints, pseudonyms, new file per match). Compiled with MSVC 2022 and run: **all checks passed**.
- `backend`: `npm test` → `node --test src/Utils/telemetry/test/telemetry.test.js`. **15/15 pass**: normal match and report accuracy, wins/losses, human death, early departure, crash recovery, duplicates, gaps, malformed and out-of-order lines, schema validation, Wilson math, weakness detection and thresholds, landing percentages/counts/contested/early survival/insufficient data/determinism, trends, playstyle thresholds, version gate and regression detection, dataset validation and split, file retention/deletion/storage limit, 50k-event match, upload opt-in/once-only/retry/pseudonym stripping, access control.
- Not run here: building Erbium.dll and the launcher (the launcher build is run by `UPDATE FROM GIT.cmd`), and a real match. The engine hooks were checked by reading.

## Configuration

- Settings > Match data (stored in `Runtime/Server/telemetry/settings.json`): record matches (default on, local only), share summaries (default **on**, counts only, can be turned off), retention days (30), storage cap (500 MB). 
- Environment: `TILTED_TELEMETRY=0` (set by the launcher when recording is off). `TILTED_TELEMETRY_DIR` overrides the backend's folder (tests/dev).
- Discord: on a Dev Kit PC put `TILTED_DISCORD_WEBHOOK=<webhook>` in `Helix-backend-main.env` (never committed; the public packer leaves `.env` out). Without it, messages go through the Cloudflare relay (`discord.js`), whose Worker holds the webhook as the secret `DISCORD_WEBHOOK`. The webhook is never in source, logs or API answers (errors are redacted).

## Dev Kit vs public builds

- `build-launcher.ps1` (used by UPDATE FROM GIT) compiles with the `TILTED_DEVKIT` symbol: Settings > Match data > Developer Tools is always there, for anyone on any PC, with no switch and no restart.
- `build-launcher.ps1 -Public` compiles without it. `build-public-release.ps1` builds that public launcher, checks it does not contain the Dev Kit marker, swaps it in only while packing, then restores the Dev Kit launcher.
- The backend's `/tilted/telemetry` API answers 404 unless the Dev Kit launcher started it with `TILTED_DEVKIT=1`; public launchers set it to empty, so a user environment variable or `.env` line cannot turn it on there.

## Limitations and dependencies

- **No fixed-seed simulator**: benchmarks compare real recorded matches. Repeatable scenario tests need a headless scenario runner (future work).
- **Not measured**: loot quality, distance to the next safe zone at landing, bus route, damage events without a knock/elimination, human aim. These metrics are marked "not measured" rather than estimated.
- **Bot candidates**: the bot AI does not score alternative actions, so the decision log has the selected activity and observations, not candidate scores.
- **Bot landing adaptation** (using landing statistics as priors for bot drop choice) is not switched on: it needs enough real matches first, then a candidate build evaluated through the gate. Live bots must only use historical priors, never live hidden positions.
- **Human landing detection** needs the player to have been seen in the bus (players who join late are not counted as landings).
- **Uploads** go through the existing relay, which allows 1 report per minute per IP and 10 lines per report.

## Authorization and compatibility questions (for the repo owners)

- Syndicate runs private servers for old Fortnite builds. Epic Games' terms of service and EULA do not permit private servers or modified clients. This telemetry adds nothing on the client: it records only what our own server code sees. The underlying project's standing with Epic's terms is unresolved and is the owners' decision.
- Players should be told that recording exists (Settings > Match data explains it). If Syndicate is distributed publicly, decide whether recording should default to off and whether a first-run notice is needed under applicable privacy laws.
- The salt makes pseudonyms non-reversible on other PCs, but someone with access to a PC's telemetry folder and a name list could match names on that PC. Treat the folder as private.

## How to verify

1. Pull this branch, close Syndicate, run `UPDATE FROM GIT.cmd` (rebuilds the launcher and Erbium).
2. Play one local match with bots to the end.
3. Check `Runtime/Server/telemetry/` for a new `.jsonl` file (one line per event).
4. Settings > Match data > Developer Tools > Open developer diagnostics (or Send test message). The Matches tab lists the match; open its report.
5. Close the server mid-match in another game: its report says `terminated_unexpectedly` with events up to the cut.
6. In `backend/` run `npm test`.
