# Syndicate warmup bots

The Season 2 integration is in `Erbium/Erbium/Private/SpectraBots/SpectraBotsIntegration.h`, called by the world net driver's game-thread tick. It targets Fortnite 2.x without Spectra 10.40 fixed addresses or Phoebe assets.

## Spawning and appearance

The lobby fills at four bots per second up to 100 by default. `TILTED_BOT_COUNT` overrides the count from 0 to 100. Bots use player starts with a small horizontal scatter. An Athena controller and player state register each bot in the alive-player roster and HUD count.

Default female commando head and body assets are explicitly loaded before a pawn is created. Missing required assets delay spawning. Character parts are assigned to the player state, then visualization and native customization are refreshed after possession. Each bot receives a pickaxe and the name Syndicate Bot (#N).

## Movement repair — October 8, 2026

A separate local AI controller possesses each pawn. The Athena controller remains in the roster. This allows normal CharacterMovement without a client connection. Bots start in Falling to settle onto collision, then receive walking input toward player-start destinations. Gravity and collision determine their height along the route.

The direct-location fallback was removed: it interpolated through the air and bypassed terrain collision. Blocked bots choose another direction after a displacement check over 1.5 seconds. This is simple warmup roaming, not combat AI or obstacle-aware pathfinding.

Existing bot guards for the human-player bus countdown, storm quest events, and item mutators remain in place.

## Operation and validation

Build `Erbium/Erbium.vcxproj` in Release/x64. Syndicate Launcher's hidden server loads `Erbium/x64/Release/Erbium.dll`; restart the server after rebuilding.

`Runtime/Server/ready.pid.bot.log` records count, location, velocity, movement mode, and `ai=1` when the intended AI controller owns the pawn, plus client-join diagnostics.

The repair compiled successfully. A five-bot local match showed a visible roaming character and active walking physics. The pre-repair source backup is in the Codex workspace at `work/tilted/bots-before-floating-fix.h`.
