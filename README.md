# Syndicate Source Code

The full source of Syndicate, a private Fortnite server project for Chapter 1 (Season X / 10.40 base, plus Erbium-based Chapter 1 seasons): launcher, game servers, bot AI, backend and the in-game Syndicate menu. The project is finished and released as-is for the community.

- No support or updates are promised. Fortnite, its art, music and the Burbank font belong to Epic Games; you need your own legally obtained Fortnite builds.
- Nothing here connects to the original team's servers. Discord webhooks, the Cloudflare relay and redeem codes are off until you set your own (see `SyndicateClassic/backend/.env.example`, `TILTED_DISCORD_WEBHOOK`, `TILTED_RELAY_URL`, `TILTED_REDEEM_URL`, and `error-webhook.txt` next to the launcher).
- MIT licensed (see `LICENSE`): use, change and share it, keeping the copyright notice.

## Layout

- `SyndicateClassic/` — the former Project Tilted source (Erbium game server, launcher, Node backend, release tools), imported from `d4shyb0i/tilted-source` at `b2ba897`. See `SyndicateClassic/README.md` and `SyndicateClassic/CLAUDE.md`. Not merged with the main code yet.

Syndicate:

- `ServerAndConsole/` — C++ game-server DLL (`10.40.vcxproj`, builds `Syndicate1040.dll`), bot AI, looting, vehicles, match flow. Uses the generated Unreal SDK in `ServerAndConsole/SDK/` and MinHook.
- `Launcher/` — C# WPF launcher (`SyndicateLauncher.sln`, .NET 8) and the Node.js backend (`BackendSource/`, plus runtime data under `SyndicateLauncher/Runtime/backend/`).
- `PresetExamples/` — sample bot difficulty presets (`.fnp`).

## Dev setup (once per PC)

1. Get the **dev kit** zip from the host (made with `MAKE DEV KIT.cmd`) and unzip it. You get a `Syndicate` folder with the files that aren't in Git: the built launcher (`SyndicateLauncher.exe`) and `Runtime\` with the game DLLs and `lawinserver.exe`.
2. Install the **.NET 8 SDK** (https://dotnet.microsoft.com/download/dotnet/8.0). To build the game server too, install **Visual Studio** (2022 or 2026) with "Desktop development with C++".
3. Clone this repo with **GitHub Desktop**.
4. Double-click **`UPDATE FROM GIT.cmd`** in the clone. The first time it asks for your `Syndicate` folder, then remembers it.
5. Start Syndicate with `Syndicate\SyndicateLauncher.exe`.

## Every update

1. GitHub Desktop: **Fetch origin**, then **Pull origin**.
2. Double-click **`UPDATE FROM GIT.cmd`**.

It copies changed runtime files (backend data, configs) into `Syndicate\Runtime`, rebuilds `Syndicate1040.dll` and `SyndicateConsoleRecovery.dll` when `ServerAndConsole/` changed, refreshes `Runtime\dlls\manifest.json` so the launcher accepts the new DLLs, and rebuilds `SyndicateLauncher.exe` when the launcher or runtime changed. Replaced files are kept in `Syndicate\_backups\`. `UPDATE FROM GIT.cmd -Force` rebuilds everything. Launcher profiles, lockers and settings (in `%APPDATA%\SyndicateLauncher`) are never touched.

## Making a dev kit

Close Syndicate and Fortnite, then double-click **`MAKE DEV KIT.cmd`**. It zips your `Syndicate` folder to `Desktop\Syndicate-Dev-Kit-<date>.zip`, leaving out backups and logs. Send it privately (Discord / Google Drive), never on GitHub.

The first time, the host makes the `Syndicate` folder from an existing Syndicate build: copy the launcher's `dist` folder (the one with the launcher exe and `Runtime\`) to `Documents\Syndicate`, run `UPDATE FROM GIT.cmd` once, then `MAKE DEV KIT.cmd`.

## Building

- **Server DLL:** open `ServerAndConsole/10.40.vcxproj` in Visual Studio and build `Release | x64`.
- **Launcher:** open `Launcher/SyndicateLauncher.sln` and build, or run `Launcher/build.bat`.

Build output (`bin/`, `obj/`, `x64/`, `dist/`), `.exe` and `.dll` files are not tracked. The launcher's runtime expects the built `Syndicate1040.dll` and other DLLs in `Launcher/SyndicateLauncher/Runtime/dlls/` and the backend executable in `Launcher/SyndicateLauncher/Runtime/backend/`. Publish built binaries as GitHub Releases.
