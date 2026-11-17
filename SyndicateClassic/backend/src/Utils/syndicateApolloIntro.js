// Chapter 2 Season 1 entrance (Dev Kit, Fortnite 11.00): the first time an account starts 11.00 it gets what
// players got at launch in October 2019. The season cinematic plays, then Fortnite's own "Apollo intro" skips the
// lobby and queues straight into a match, and the match server throws the player out of the bus as soon as it is
// over the island.
//
// How it fits together:
//  * Cloud storage (DefaultRuntimeOptions.ini) for 11.00, while the asking account has not had its intro:
//    bEnableApolloIntro and bForceApolloIntroSkipSubgameSelect (11.00's built-in launch flow, Playlist_Intro_Apollo_*
//    which the matchmaker plays as Solo),
//    ApolloIntroShowMovie (the intro's own Chapter 2 cinematic before it queues).
//  * Matchmaking: that account's first 11.00 ticket writes Runtime\Server\apollo-intro.json for the match server
//    (Erbium: UpdateApolloIntro) and marks the account done in %LOCALAPPDATA%\ProjectTilted\UserData\
//    apollo-intro-seen.json. That lives outside the install so it survives launcher updates (the intro plays
//    once per account, not once per update). To see it again, remove the account from that file.
import fs from 'fs';
import path from 'path';
import jwt from 'jsonwebtoken';
import { dirname } from 'dirname-filename-esm';
import { clientBuild } from './syndicateBuild.js';

const __dirname = dirname(import.meta);
const runtime = path.resolve(__dirname, '../../../Runtime/Server');
// The "already watched the intro" list must survive launcher updates, which replace the install (and the
// Runtime folder inside it). Keep it in the persistent UserData dir like TravelKeys; fall back to Runtime
// only when there is no LOCALAPPDATA (non-Windows dev). The old in-install list is migrated once on read, so
// accounts that already saw the intro are not shown it again after updating.
const persistentDir = path.join(process.env.LOCALAPPDATA || path.resolve(__dirname, '../../..'), 'ProjectTilted', 'UserData');
const seenFile = path.join(persistentDir, 'apollo-intro-seen.json');
const legacySeenFile = path.join(runtime, 'apollo-intro-seen.json');
const introFile = path.join(runtime, 'apollo-intro.json');

function readSeen() {
  let accounts = [];
  try { const value = JSON.parse(fs.readFileSync(seenFile, 'utf8')); if (Array.isArray(value.accounts)) accounts = value.accounts; } catch { }
  // One-time migration of a list left by a pre-update install in Runtime\Server. Fold it into the durable
  // list, then rename it so it is only processed once.
  try {
    if (fs.existsSync(legacySeenFile)) {
      const old = JSON.parse(fs.readFileSync(legacySeenFile, 'utf8'));
      const merged = Array.from(new Set([...accounts, ...(Array.isArray(old.accounts) ? old.accounts : [])]));
      writeJson(seenFile, { accounts: merged });
      fs.renameSync(legacySeenFile, legacySeenFile + '.migrated');
      accounts = merged;
    }
  } catch { }
  return accounts;
}
function writeJson(file, value) {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  const temp = file + '.' + process.pid + '.tmp';
  fs.writeFileSync(temp, JSON.stringify(value, null, 1));
  fs.renameSync(temp, file);
}

// The account behind a request's bearer token (cloud storage routes are not behind verifyToken).
// Fortnite fetches the system files without the player's token, so when there is none the account that signed in
// the launcher is starting the game for is used (the backend runs on the player's own PC; system files are fetched
// before Fortnite signs in), else the account that signed in last.
export function accountFromRequest(req) {
  try {
    const header = String(req.headers.authorization || '');
    const raw = header.replace(/^bearer\s+/i, '').replace(/^eg1~/, '');
    if (raw && global.JWT_SECRET) { const sub = jwt.verify(raw, global.JWT_SECRET)?.sub; if (sub) return sub; }
  } catch { }
  // The player the launcher is starting the game for (its login code was made seconds ago).
  const launch = global.syndicateLaunchAccount;
  if (launch?.accountId && Date.now() - launch.at < 10 * 60 * 1000) return launch.accountId;
  const tokens = Array.isArray(global.accessTokens) ? global.accessTokens : [];
  return tokens.length ? tokens[tokens.length - 1].accountId || null : null;
}

export function introPending(accountId, userAgent) {
  return clientBuild(userAgent) === '11.00' && !!accountId && !readSeen().includes(accountId);
}

// DefaultRuntimeOptions.ini text for this request: the normal text, plus the intro switches when it is due.
export function runtimeOptionsWithIntro(text, req) {
  // 11.00 Arena: show the game's own points counter. Placement points come from the match server's
  // ClientReportTournamentPlacementPointsScored, and the event comes from ClientNotifyMatchEntered
  // (syndicateArenaSession.js); the client skips its own "is this a live Epic tournament" check.
  if (['8.50', '9.40', '10.31', '10.40', '11.00'].includes(clientBuild(req.headers['user-agent'])))
    text = (text || '') + `
[/Script/FortniteGame.FortRuntimeOptions]
bUseServerTournamentPlacementNotifications=true
bUsePlayingEventIds=true
bClientIgnoreIsTournamentCheck=true
`;
  if (!introPending(accountFromRequest(req), req.headers['user-agent'])) return text;
  return (text || '') + `
[/Script/FortniteGame.FortRuntimeOptions]
bEnableApolloIntro=true
bForceApolloIntroSkipSubgameSelect=true
TriagedApolloIntro=false
ApolloIntroShowMovie=true
ApolloIntroQueueTimeoutSeconds=120
ApolloIntroMaxRetries=3
ApolloIntroSecondsBetweenRetries=5
ApolloIntroPlaylist_NewPlayers=Playlist_Intro_Apollo_NewPlayer
ApolloIntroPlaylist_PastPlayers=Playlist_Intro_Apollo_PastPlayer
ApolloIntroPlaylist_CurrentPlayers=Playlist_Intro_Apollo_CurrentPlayer
bSkipTrailerMovie=false
bAlwaysPlayTrailerMovie=false
`;
}

// True while this account's first 11.00 match is still to come.
export function introDueFor(accountId, build) {
  return build === '11.00' && !!accountId && !readSeen().includes(accountId);
}

// Called for each local matchmaking ticket. The account's first 11.00 match is its intro match.
export function claimIntroMatch(accountId, build) {
  try {
    if (build !== '11.00' || !accountId) return false;
    const seen = readSeen();
    if (seen.includes(accountId)) return false;
    writeJson(introFile, { accountId, created: Date.now(), expires: Date.now() + 15 * 60 * 1000 });
    seen.push(accountId);
    writeJson(seenFile, { accounts: seen });
    return true;
  } catch { return false; }
}
