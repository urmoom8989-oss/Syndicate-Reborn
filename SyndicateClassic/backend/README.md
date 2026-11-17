# Syndicate backend (Syndicate)

This is the Syndicate Node backend used by the Syndicate launcher for local accounts,
profiles, lockers, matchmaking and the Chapter 1 match server.

## Run it

1. Copy `.env.example` to `.env` and adjust the ports/IPs if needed.
2. Install dependencies:

   ```powershell
   npm install
   ```

3. Start it:

   ```powershell
   npm start
   ```

The launcher looks for this folder as `Syndicate-backend` (with `src/index.js`,
`package.json` and `node_modules`). The live install uses
`Documents\Syndicate\Syndicate-backend`.

## Supported builds

The matchmaker allow-list includes every Syndicate Chapter 1 build:

`1.7.2`, `1.8.2`, `1.8`, `1.11`, `3.5.1`, `4.5`, `5.10`, `6.20`, `7.40`,
`8.50`, `9.40`, **`10.40`** (Season X), `11.00`, `12.50`.

`10.40` is mapped to Season 10 everywhere the backend maps a build to a season
(Battle Pass, store, arena, STW heroes, custom maps, lobby/runtime options).

## Not in git

Project rules keep these out of the repository:

- `node_modules/` — run `npm install`.
- `.env`, `jwt.secret`, `tokens.json` — local secrets.
- Built binaries (`Erbium.dll`, `cobalt.dll`, `TiltedClient.dll`,
  `TiltedPadAssist.dll`) — ship them in GitHub Releases.

The source for the game server, client mods and pad assist is in
`SyndicateClassic/game-server` and `SyndicateClassic/launcher/launcher-source`.
