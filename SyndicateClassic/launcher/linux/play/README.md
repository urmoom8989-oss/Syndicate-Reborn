# Syndicate Play pipeline (Linux / Steam Deck) — v1

`syndicate-play.sh` is the "Play" button as a script: it starts the database, starts the
backend, launches Fortnite under Proton, and injects the redirect — the four things the
Windows launcher does so a season actually boots. Once this works, the Linux launcher's
Play button just runs it; for now we run it directly to get it working.

**This is v1 and untested end to end.** Expect a round or two of running it and reading the
log before it boots a game. That's normal for this kind of bring-up.

## What you need on the Deck first

- A **Syndicate install** (the folder with `Dependencies/`, `Syndicate-backend/`,
  `Launcher/`) — the same files the Windows release uses. `Dependencies/Node/node.exe`,
  `Dependencies/MongoDB/mongod.exe` and `Launcher/cobalt.dll` must be present.
- A **Fortnite build** (Season 4 / 4.5 to start — pre–Season 6 needs no EAC stub).
- **`TiltedInject.exe`** built from `launcher/linux/inject/` and placed in `Launcher/`
  next to `cobalt.dll`.
- **umu-launcher** (`umu-run`) for Proton without Steam. (Plain `wine` works as a fallback
  but Proton is what the Deck has.)

## Run it

1. Open `syndicate-play.sh` and set the three paths at the top (`TILTED_ROOT`, `GAME_ROOT`,
   `WINEPREFIX_DIR`) to the real locations on your Deck.
2. In Dolphin, right-click `syndicate-play.sh` → **Properties → Permissions → check "Is
   executable"**. Then **double-click it → "Execute in Terminal."**
3. Watch the terminal. Everything is also saved to **`~/syndicate-play.log`** — take a phone
   photo of that if something fails, and we fix the next thing.

## What success looks like at each stage (the log marks them)

- `[1/4]` Mongo up on 27028.
- `[2/4]` `backend is up.`
- `[3/4]` the game window appears (Fortnite loading/login).
- `[4/4]` `Injected OK` from the injector — the game is now pointed at the local backend.

Reaching a Fortnite login/menu screen that talks to the local backend is the milestone.
Auto-login (the exchange code) and the Season 6+ EAC stub are the next layers after this
boots.
