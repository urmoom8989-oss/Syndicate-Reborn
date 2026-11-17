# Syndicate

Private Fortnite server for Chapter 1 builds: Pre-season 1.7.2, Season 1 1.8.2, 1.11, 3.5.1, 4.5, 5.10, 6.20, 7.40, 8.50, 9.40, 10.31 and Chapter 2 Season 1 (11.00). Chapter 2 Season 2 (12.50) is wired in the launcher (season 12, card/hero art, build detection, host/arena allow-list) but has NO backend yet — no season-12 battle pass, challenges, loot or bots — so it falls back on launch until that and the 12.50 game files exist.

## Layout

| Folder | What it is | How it's built |
|---|---|---|
| `game-server/` | Erbium game server DLL (C++, Visual Studio). Holds the bots (`Erbium/Erbium/Private/SpectraBots`), revive code (`Erbium/FortniteGame/Public/SyndicateDBNORecovery.h`) and match rules (`FortGameMode.cpp`, `FortPlayerControllerAthena.cpp`). Also contains `SyndicateStudio/` — a separate C++ DLL injected into the game client by Syndicate Studio for direct UE4 access (actor queries, camera, raycasting) via named pipe. And `ErbiumClient/` for client-side mods (custom FOV, turbo build). All three are in `Erbium.sln`. | `build-scripts/build-erbium.ps1` |
| `launcher/` | WPF launcher (`launcher-source/SyndicateClassic/*.cs`) and the single-file bundle that packs it (`Program.cs`, `pack.py`, `deliver.py`). Season card art is in `launcher-source/season-art/seasonN.png`. | `build-scripts/build-launcher.ps1` |
| `backend/` | Node.js backend (Express, MongoDB, XMPP) and the `Matchmaker/`. | `npm install`, then `start.bat` |
| `public-release/` | Packer and updater for the public download. | `build-public-release.ps1` |
| `build-scripts/` | The scripts from the "Updated Files" packages. | |

On the host PC the scripts are run from `Desktop\Syndicate - Updated Files\N - ...`. They copy a package's `source-files` over the real source tree (`Source\Erbium\Erbium`, `SyndicateClassic`, `Syndicate-backend`). This repo already has those files merged in.

The install's backend folder is `Syndicate-backend` (renamed from `Helix-backend-main` on 2026-10-31). The launcher renames old installs on start (`BackendFolder.Migrate` in `LauncherPaths.cs`), and every script, the component manifest and Erbium's keychain lookup accept either name.

## Keeping main in sync with the host PC

`main` should always match what is on the host PC. Whenever files are copied into the host's `Syndicate - Updated Files` folders, commit the same change to `main` and push it, so anyone who downloads the source gets the same thing. Profile icons live in `launcher/launcher-source/avatars/` (on the PC: `3 - Launcher fixes\avatars\`).

## Dev workflow (both developers)

- Each PC has a `Syndicate` folder from the dev kit (`MAKE DEV KIT.cmd`, see README.md) and a GitHub Desktop clone of this repo.
- To update: pull, then run `UPDATE FROM GIT.cmd` (`dev/update-from-git.ps1`). It copies backend files (and their `Launcher\components` copies + manifest hashes), rebuilds the launcher via `build-scripts/build-launcher.ps1 -Root` and the game server via `build-scripts/build-erbium.ps1 -Root` when their files changed.
- Launcher art and the installer live in `launcher/launcher-source/` (`season-art/`, `avatars/`, `pack-art/`, `logo.png`, `InstallUpdate.ps1`).

## Rules

- Never commit `.env`, `jwt.secret`, `tokens.json`, ClientSettings saves, the `Runtime/` folder (accounts, logs, locker), `node_modules`, PDBs, built DLLs or Fortnite game builds. `.gitignore` covers these.
- Arena bots must not teleport or get artificial protection.
- No anti-virus evasion. No CET-disabling environment lines.

## Things worth knowing

- Erbium `FortniteVersion` values: 1.7.2 = 1.72 (CL 3700114), 1.8.2 = 1.82 (CL 3741772). Both are UE 4.16. `EarlyChapterOne()` in SpectraBots covers 1.72 to 1.82.
- Season 1 XMPP runs through `backend/src/Utils/syndicateLegacyXmpp.js`. It listens on TCP 7071, uses TLS 1.0 and forwards to ws 70.
  - `launcher/.../LegacyXmppTrust.cs` swaps an expired root cert inside the game exe for Syndicate's own cert. It keeps a `.tilted-original` backup.
  - The matching key is built into `syndicateLegacyXmpp.js`, so keep this repo private.
- Radmin/LAN joins: the guest moves its account to the host through `/travel` on port 3552 (`syndicateTravel.js`). Keys live in `%LOCALAPPDATA%\ProjectTilted\UserData\TravelKeys`.
- Direct play (no Radmin, friends only): the host's launcher opens TCP 70/3551/3552/6969 and UDP 7777 with UPnP (`DirectPlay.cs`) and makes a `TILT-` share code (public IP + 128-bit join key). Internet requests without the key are refused (`backend/src/Utils/syndicateDirect.js`; `Runtime/Server/direct-host.json` on the host, `remote-host.json` `key` on the guest).
  - The host presses Allow for each new player (`direct-pending.json` -> `direct-approved.json`, tied to the code). Codes expire after 6 hours; the launcher closes direct play after 30 minutes with no let-in friend talking to the host (`direct-activity.json`).
- The item shop rotates every 6 hours (`syndicateShopRotation.js`). Battle pass, challenge and Save the World items never go on sale.
- Tester launcher: `MAKE TESTER RELEASE.cmd` builds `build-public-release.ps1 -Tester` (gate in `public-release/TesterGate.cs`, relay `/tester/*` in `relay/worker.js`). Only the Discord Tester role opens it; it updates from the relay. Setup steps: `relay/README.md`.
