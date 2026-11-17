# Syndicate components used by Syndicate

| Component | Where it lives in this repo | How it is produced |
|---|---|---|
| Syndicate backend (accounts, profiles, locker, store, matchmaking) | `backend/` | `npm install`, then `npm start` |
| Matchmaker | `backend/Matchmaker/` | started by the backend/launcher |
| Erbium game server (Chapter 1) | `game-server/Erbium/` | `build-scripts/build-erbium.ps1` |
| SyndicateClient (client gameplay/display mods) | `game-server/ErbiumClient/` | `SyndicateClient.vcxproj` (Release x64) |
| SyndicatePadAssist (controller sprint/turbo build) | `launcher/launcher-source/pad-assist/` | `SyndicatePadAssist.vcxproj` (Release x64) |
| cobalt.dll (Epic web redirect) | Release artifact only | prebuilt component shipped with Syndicate |
| Syndicate launcher source | `launcher/launcher-source/` | .NET/WPF build |

Project rules keep built files out of git. Ship the compiled DLLs/EXE in GitHub
Releases, not in the repository:

- `Erbium.dll`
- `cobalt.dll`
- `TiltedClient.dll`
- `TiltedPadAssist.dll`
- `SyndicateLauncher.exe`

`backend/.env`, `jwt.secret`, `tokens.json`, `node_modules/` and player/runtime
data are also excluded. Use `backend/.env.example` and `npm install`.
