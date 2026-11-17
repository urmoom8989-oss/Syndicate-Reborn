#!/usr/bin/env bash
# Syndicate © 2026 Syndicate. All rights reserved. Proprietary — see LICENSE.
#
# syndicate-play.sh — the Linux "Play" pipeline (v1, Steam Deck / Proton).
#
# Reproduces what the Windows launcher's Play button does, inside ONE Proton prefix so
# everything shares localhost:
#   1. start the bundled MongoDB (mongod.exe) on 127.0.0.1:27028
#   2. start the bundled backend (node.exe src/index.js) on 127.0.0.1:3551
#   3. launch Fortnite (FortniteClient-Win64-Shipping.exe) under Proton with Syndicate's args
#   4. inject cobalt.dll (the Epic->Syndicate redirect) with TiltedInject.exe, in the same prefix
#
# It writes everything it does to a log you can photograph:  ~/syndicate-play.log
#
# This is v1 and UNTESTED end to end — it is meant to be run, watched, and iterated on.
# Edit the three paths below to match your Deck, then run it (or double-click the
# syndicate-play.desktop shortcut). Season 6+ also needs the EAC stub (not in v1 yet).

set -u
LOG="$HOME/syndicate-play.log"
exec > >(tee "$LOG") 2>&1
echo "==== Syndicate Play  $(date) ===="

# ---------------------------------------------------------------------------
# EDIT THESE THREE PATHS (use the real locations on your Deck)
# ---------------------------------------------------------------------------
# The Syndicate install (the folder with Dependencies/, Syndicate-backend/, Launcher/):
TILTED_ROOT="${TILTED_ROOT:-$HOME/SyndicateClassic}"
# The Fortnite build (the folder with FortniteGame/ and Engine/ inside):
GAME_ROOT="${GAME_ROOT:-$HOME/Fortnite-4.5}"
# Where to keep the Proton prefix for Syndicate (created automatically):
WINEPREFIX_DIR="${WINEPREFIX_DIR:-$HOME/.tilted-prefix}"
# ---------------------------------------------------------------------------

# Derived paths (match PortableServices.cs / LauncherPaths.cs on Windows).
NODE_EXE="$TILTED_ROOT/Dependencies/Node/node.exe"
MONGOD_EXE="$TILTED_ROOT/Dependencies/MongoDB/mongod.exe"
BACKEND_DIR="$TILTED_ROOT/Syndicate-backend"
[ -d "$BACKEND_DIR" ] || BACKEND_DIR="$TILTED_ROOT/Helix-backend-main"   # old folder name
LAUNCHER_DIR="$TILTED_ROOT/Launcher"
COBALT_DLL="$LAUNCHER_DIR/cobalt.dll"
INJECTOR_EXE="$LAUNCHER_DIR/TiltedInject.exe"
GAME_EXE="$GAME_ROOT/FortniteGame/Binaries/Win64/FortniteClient-Win64-Shipping.exe"
DB_DIR="$TILTED_ROOT/Runtime/Database"

# Prefer umu (Proton without Steam); fall back to plain wine if that's all there is.
if command -v umu-run >/dev/null 2>&1; then
  RUN=(umu-run)
  echo "Runtime: umu-run (Proton)"
elif command -v wine >/dev/null 2>&1; then
  RUN=(wine)
  echo "Runtime: wine (umu-run not found — install umu-launcher for Proton)"
else
  echo "FATAL: neither umu-run nor wine is installed. Install umu-launcher."
  exit 1
fi
export WINEPREFIX="$WINEPREFIX_DIR"
export GAMEID="${GAMEID:-0}"        # umu uses this; 0 is fine for a non-Steam title
mkdir -p "$WINEPREFIX_DIR" "$DB_DIR"

# Convert a Linux path to the Z: path Wine sees.
winpath() { echo "Z:${1//\//\\}"; }

check() { if [ ! -e "$1" ]; then echo "MISSING: $1  ($2)"; MISSING=1; fi; }
MISSING=0
echo "---- checking files ----"
check "$NODE_EXE"     "bundled Node"
check "$MONGOD_EXE"   "bundled MongoDB"
check "$BACKEND_DIR"  "backend folder"
check "$COBALT_DLL"   "redirect DLL (cobalt.dll)"
check "$INJECTOR_EXE" "injector (build TiltedInject.exe from launcher/linux/inject)"
check "$GAME_EXE"     "Fortnite client"
if [ "$MISSING" = 1 ]; then
  echo "Fix the paths at the top of this script (or the missing files) and run again."
  exit 1
fi
echo "all files found."

# 1) MongoDB -----------------------------------------------------------------
echo "---- [1/4] starting MongoDB (127.0.0.1:27028) ----"
"${RUN[@]}" "$MONGOD_EXE" --dbpath "$(winpath "$DB_DIR")" --bind_ip 127.0.0.1 --port 27028 \
  --logpath "$(winpath "$DB_DIR/mongodb.log")" --logappend &
MONGO_PID=$!
sleep 8   # give mongod time to come up under Proton

# 2) Backend -----------------------------------------------------------------
echo "---- [2/4] starting backend (127.0.0.1:3551) ----"
# The backend reads its config from a .env file (same as PortableServices.cs writes on
# Windows). Write it, then run node plainly. Also export the vars so they're in the
# process environment either way.
cat > "$BACKEND_DIR/.env" <<ENV
PORT=3551
XMPP_PORT=70
MONGO_URI=mongodb://127.0.0.1:27028/project_tilted
MATCHMAKER_IP=127.0.0.1:6969
GAMESERVER_IP=127.0.0.1:7777
MAIN_SEASON=4
EVENT_TYPE=Arena
BACKEND_NAME=Syndicate
BOT_TOKEN=
DISCORD_TOKEN=
ENV
export PORT=3551 XMPP_PORT=70 MONGO_URI="mongodb://127.0.0.1:27028/project_tilted" \
  MATCHMAKER_IP=127.0.0.1:6969 GAMESERVER_IP=127.0.0.1:7777 MAIN_SEASON=4 BACKEND_NAME="Project Tilted"
( cd "$BACKEND_DIR" && "${RUN[@]}" "$NODE_EXE" "$(winpath "$BACKEND_DIR/src/index.js")" ) &
BACKEND_PID=$!
echo "waiting for the backend to answer on 127.0.0.1:3551 ..."
for i in $(seq 1 45); do
  if curl -fsS "http://127.0.0.1:3551/" >/dev/null 2>&1 || curl -fsS "http://127.0.0.1:3551/unknown" >/dev/null 2>&1; then
    echo "backend is up."
    BACKEND_UP=1; break
  fi
  sleep 2
done
if [ "${BACKEND_UP:-0}" != 1 ]; then
  echo "WARNING: backend did not answer in 90s. Launching the game anyway so we can see how far it gets."
fi

# 3) Launch Fortnite ---------------------------------------------------------
echo "---- [3/4] launching Fortnite ----"
GAME_ARGS="-game -epicapp=Fortnite -epicenv=Prod -epiclocale=en-us -epicportal -skippatchcheck -USEALLAVAILABLECORES -nosplash"
# NOTE: no -AUTH exchange code yet (that needs a signed-in player via launcher/exchange).
# Without it the game should still reach the login screen through the redirect — that alone
# proves render + redirect + backend are working. Auto-login is the next layer.
export OPENSSL_ia32cap=":~0x20000000"   # ConfigureSyndicateCompatibility
cd "$(dirname "$GAME_EXE")" || true
"${RUN[@]}" "$GAME_EXE" $GAME_ARGS &
GAME_PID=$!
echo "game process started (pid $GAME_PID). Waiting for the window to come up ..."

# 4) Inject the redirect -----------------------------------------------------
echo "---- [4/4] injecting cobalt.dll (redirect) ----"
sleep 20   # let the game create its process/window before injecting
"${RUN[@]}" "$INJECTOR_EXE" "FortniteClient-Win64-Shipping.exe" "$(winpath "$COBALT_DLL")" || \
  echo "injection step returned an error (see above)."

echo "==== pipeline reached the end. Watch the screen; full log is at $LOG ===="
echo "If Fortnite shows a login/menu screen, render+redirect work. If it closed, the log above says how far it got."
wait "$GAME_PID" 2>/dev/null
echo "Fortnite exited. Stopping backend and database."
kill "$BACKEND_PID" "$MONGO_PID" 2>/dev/null
