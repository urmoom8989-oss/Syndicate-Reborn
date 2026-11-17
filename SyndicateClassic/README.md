# Syndicate (source)

Private Fortnite Chapter 1 server: launcher, backend, game server (Erbium) and release tools.
See `CLAUDE.md` for the layout and rules.

## Dev setup (once per PC)

1. Get the **dev kit** zip from the host (made with `MAKE DEV KIT.cmd`) and unzip it. You get a `Syndicate` folder with everything that isn't in Git: the built launcher, backend `node_modules`, MongoDB and the game server.
2. Install the **.NET 8 SDK** (https://dotnet.microsoft.com/download/dotnet/8.0). To build the game server too, install **Visual Studio 2022** with "Desktop development with C++".
3. Clone this repo with **GitHub Desktop**.
4. Double-click **`UPDATE FROM GIT.cmd`** in the clone. The first time it asks for your `Syndicate` folder, then remembers it.
5. Start Syndicate with `Syndicate\Launcher\SyndicateLauncher.exe`.

## Every update

1. GitHub Desktop: **Fetch origin**, then **Pull origin**.
2. Double-click **`UPDATE FROM GIT.cmd`**.

It copies new backend files in and rebuilds the launcher and game server only when their files changed. `UPDATE FROM GIT.cmd -Force` rebuilds everything. Accounts, `.env`, settings and Fortnite builds are never touched.

## Making a dev kit

Close Syndicate, then double-click **`MAKE DEV KIT.cmd`**. It writes `Syndicate-Dev-Kit-<date>.zip` to the Desktop. It leaves out accounts, logs, `.env`, `jwt.secret`, `tokens.json`, ClientSettings saves, your game-folder paths, backups and Fortnite builds. Send it privately (Discord / Google Drive), never on GitHub.

## Public release

1. Close Syndicate and Fortnite, then double-click **`MAKE PUBLIC RELEASE.cmd`**. It updates your `Syndicate` folder from this clone, asks for the version number and writes the EXE to `Desktop\Project-Syndicate-Public-Release`. Nothing is uploaded.
2. Add that version's changes to `public-release/release-notes.json`, test the EXE once, then double-click **`PUBLISH PUBLIC RELEASE.cmd`**. It uploads the EXE to the public GitHub repo and posts to Discord, asking before each step (`-Preview` shows both without publishing).

`MAKE PRIVATE RELEASE.cmd` makes a tester EXE with updates turned off.
