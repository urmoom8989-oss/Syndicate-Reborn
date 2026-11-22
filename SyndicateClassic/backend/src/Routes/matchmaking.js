import {claimIntroMatch,introDueFor} from '../Utils/syndicateApolloIntro.js';
import {writeArenaSession} from '../Utils/syndicateArenaSession.js';
import {clientBuild} from '../Utils/syndicateBuild.js';
import {writePartyBotsFor} from '../Utils/syndicatePartyBots.js';
import {createRequire} from 'node:module';
const require=createRequire(import.meta.url);
const playlists=require('../../Matchmaker/playlists.cjs');
const teamFill=require('../../Matchmaker/fill.cjs');
import {remoteHost,prepareRemote,startLanBridge} from '../Utils/syndicateLan.js';
import {modeEnabled,modeName,stwEnabledFor} from '../Utils/syndicateSeasonModes.js';
import Profile from '../User/Mongodb/Schema/profiles.js';
import {publishCharacter,preseasonMatchProfile} from '../Utils/syndicateLocker.js';
import express from "express";
import fs from "node:fs";
import path from "node:path";
import { v4 as uuidv4 } from "uuid";
import { verifyToken } from "../User/tokenManager/tokenVerify.js";
import User from "../User/Mongodb/Schema/user.js";

const app = express.Router();
startLanBridge(async username=>!!await User.findOne({username_lower:username}).select("_id").lean());
let buildUniqueId = {};

if (!global.parties) global.parties = {};

function makeId() {
  return uuidv4().replace(/-/gi, "").toUpperCase();
}

function getMatchmakerIp() {
  const raw = (process.env.MATCHMAKER_IP || "127.0.0.1:81").trim();
  if (raw.startsWith("ws://") || raw.startsWith("wss://")) return raw;
  return `ws://${raw}`;
}

function getGameServerInfo() {
  const remote=remoteHost();if(remote)return {serverAddress:remote,serverPort:7777};
  const firstServer = (process.env.GAMESERVER_IP || "127.0.0.1:7777")
    .split(",")
    .map((entry) => entry.trim())
    .find((entry) => entry.length > 0);

  const parts = (firstServer || "127.0.0.1:7777").split(":");
  const serverAddress = parts[0] || "127.0.0.1";
  const parsedPort = Number(parts[1]);
  // Fast queue: the live match server may be on 7778 (server.json); the address stays the configured one.
  const serverPort = playlists.hostPort() ?? (Number.isFinite(parsedPort) && parsedPort > 0 ? parsedPort : 7777);

  return { serverAddress, serverPort };
}

async function getUsernameFromAccountId(accountId) {
  if (!accountId) return "";

  const user = await User.findOne({ accountId }).lean();
  return user?.username || "";
}

app.get("/getTeamMember/:username", async (req, res) => {
  const username = (req.params.username || "").trim();
  if (!username) {
    res.type("text/plain").send("");
    return;
  }

  const search = username.toLowerCase();
  const user = await User.findOne({
    $or: [{ username }, { username_lower: search }],
  }).lean();

  if (!user?.accountId) {
    res.type("text/plain").send("");
    return;
  }

  const party = Object.values(global.parties).find(
    (p) =>
      Array.isArray(p.members) &&
      p.members.some((member) => member.account_id === user.accountId)
  );

  if (!party || !Array.isArray(party.members)) {
    res.type("text/plain").send("");
    return;
  }

  const teammate = party.members.find((member) => member.account_id !== user.accountId);
  if (!teammate) {
    res.type("text/plain").send("");
    return;
  }

  let teammateName =
    teammate?.meta?.["urn:epic:member:dn_s"] ||
    teammate?.meta?.["Default:MemberName_s"] ||
    "";

  if (!teammateName) {
    teammateName = await getUsernameFromAccountId(teammate.account_id);
  }

  res.type("text/plain").send(teammateName || "");
});

app.get("/fortnite/api/matchmaking/session/findPlayer/*", (req, res) => {
  res.status(200).end();
});

app.get(
  "/fortnite/api/game/v2/matchmakingservice/ticket/player/*",
  verifyToken,
  async (req, res) => {
    if (typeof req.query.bucketId !== "string") return res.status(400).end();
    if (req.query.bucketId.split(":").length !== 4) return res.status(400).end();

    const build=clientBuild(req.headers['user-agent']);
    try{playlists.requireBuild(build);}catch(error){return res.status(409).json({errorCode:"errors.com.syndicate.game_version",errorMessage:error.message});}
    let ticket,mode;
    try{mode=playlists.playlistMode(req.query.bucketId);}catch(error){return res.status(400).json({errorCode:"errors.com.syndicate.playlist",errorMessage:error.message});}
    // Chapter 2 entrance: an account's first local 11.00 match is Fortnite's own intro playlist, whatever was picked.
    if(!remoteHost() && introDueFor(req.user.accountId,build))mode='intro';
    // Fortnite keeps the intro switches it read at startup for the whole session, so after the entrance it can still
    // ask for Playlist_Intro_Apollo_*. Once the account has had its entrance (or on a host's PC), that is a normal Solo.
    else if(mode==='intro')mode='solo';
    // Launcher Settings > Game modes: a mode turned off for this season is not matched (the lobby hides it too).
    if(!modeEnabled(build,mode))return res.status(409).json({errorCode:"errors.com.syndicate.playlist",errorMessage:modeName(mode)+' is turned off for this season. Turn it on in the Syndicate launcher: Settings > Game modes.'});
    if(remoteHost()){
      try{
        const user=await User.findOne({accountId:req.user.accountId}).lean();
        const profile=await Profile.findOne({accountId:req.user.accountId}).lean();
        if(!user||!profile)throw Error('Local account missing');
        await publishCharacter(req.user.accountId,profile.profiles.athena);
        const name=Buffer.from(user.username_lower,'utf8').toString('hex');
        const text=fs.readFileSync(new URL(`../../../Runtime/Server/locker/${name}.loadout`,import.meta.url),'utf8');
        const loadout=Object.fromEntries(text.split(/\r?\n/).filter(Boolean).map(l=>l.split('=')));
        const status=await prepareRemote(req.user.accountId,user.username,loadout,mode,build,teamFill.requestedFill(req.query,mode));
        ticket=playlists.reserveRemote(mode,req.user.accountId,remoteHost(),status.ticket,undefined,build);
      }catch(error){return res.status(503).json({errorCode:'errors.com.syndicate.match_host',errorMessage:error.message});}
    }
    // Refresh the saved outfit right before a local match; the game server reads it on spawn.
    if(!ticket && build!=='1.11')try{const profile=await Profile.findOne({accountId:req.user.accountId}).lean();if(profile?.profiles?.athena)await publishCharacter(req.user.accountId,build==='1.7.2'?preseasonMatchProfile(profile.profiles.athena):profile.profiles.athena);}catch(error){console.warn('[Syndicate] Could not save outfit for the match:',error.message);}
    if(!ticket)try{ticket=playlists.reserve(mode,req.user.accountId,undefined,Date.now(),build);}catch(error){return res.status(409).json({errorCode:"errors.com.syndicate.match_busy",errorMessage:error.message});}
    // Chapter 2 entrance: this account's first 11.00 match throws it out of the bus (syndicateApolloIntro.js).
    if(!ticket.host)claimIntroMatch(req.user.accountId,build);
    // Arena on 11.00: tell the match server which Epic tournament event and division window this player is in,
    // so it can put the game's own Arena points counter on screen (syndicateArenaSession.js).
    if(!ticket.host && mode==='arenasolo')await writeArenaSession(req.user.accountId,build).catch(()=>{});
    {const fill=teamFill.requestedFill(req.query,mode);ticket.fill=ticket.reused?(ticket.fill===true||fill):fill;delete ticket.reused;}
    playlists.write(playlists.ticketPath(ticket.id),ticket);
    buildUniqueId[req.user.accountId] = req.query.bucketId.split(":")[0];
    // Arena: remember which tournament event window the client queued for; Season X reads it from the session to show the points HUD.
    global.syndicateArenaQueue??={};global.syndicateArenaQueue[req.user.accountId]={eventId:req.query['player.option.tournamentId']||'',windowId:req.query['player.option.windowId']||''};

    res.json({
      serviceUrl: (()=>{const url=new URL(getMatchmakerIp()); if(!url.pathname)url.pathname="/"; url.searchParams.set("ticket",ticket.id); return url.href;})(),
      ticketType: "mms-player",
      payload: "69=",
      signature: "420=",
    });
  }
);

app.get(
  "/fortnite/api/game/v2/matchmaking/account/:accountId/session/:sessionId",
  (req, res) => {
    res.json({
      accountId: req.params.accountId,
      sessionId: req.params.sessionId,
      // Dev launcher (Save the World beacon): a real 32-byte AES key; Erbium's host uses the same bytes
      // (Misc.cpp, SyndicateWelcomeHook). Anything shorter is ignored and every packet fails to encrypt.
      // Also when Save the World is turned on for this season in the launcher (syndicateSeasonModes.js).
      key: stwEnabledFor(req.headers["user-agent"]) ? Buffer.from("SyndicateStwDevKey0123456789ABCD").toString("base64") : "none",
    });
  }
);

app.get(
  "/fortnite/api/matchmaking/session/:sessionId",
  verifyToken,
  async (req, res) => {
    const ticket=playlists.readTicket(req.params.sessionId);
    const party=Object.values(global.parties).find(p=>p.members?.some(m=>m.account_id===req.user.accountId) && p.members?.some(m=>m.account_id===ticket?.accountId));
    if(!ticket || clientBuild(req.headers['user-agent'])!==(ticket.build||'4.5') || (ticket.accountId!==req.user.accountId && !party) || !await playlists.ticketReady(ticket.id))return res.status(409).json({errorCode:"errors.com.syndicate.match_not_ready",errorMessage:"The selected playlist is not ready. Please retry matchmaking."});
    const live=playlists.hostStatus();
    if(live && live.Accepting && live.Mode===ticket.mode){
      const ids=party?party.members.map(m=>m.account_id):[req.user.accountId];
      const members=await User.find({accountId:{$in:ids}}).select('username').lean();
      for(const member of members)teamFill.writeSelection(playlists.runtime,member.username,ticket.fill===true,ticket.mode,live.Pid);
      // Season 4 party bots invited by this player join their team in the match.
      if(clientBuild(req.headers['user-agent'])==='4.5'){const me=members.find(m=>m.accountId===req.user.accountId)||await User.findOne({accountId:req.user.accountId}).select('username').lean();if(me)writePartyBotsFor(req.user.accountId,me.username,playlists.runtime,live.Pid);}
    }
    const gameServerInfo = getGameServerInfo();

    res.json({
      id: req.params.sessionId,
      ownerId: makeId(),
      ownerName: "[DS]fortnite-liveeugcec1c2e30ubrcore0a-z8hj-1968",
      serverName: "[DS]fortnite-liveeugcec1c2e30ubrcore0a-z8hj-1968",
      serverAddress: gameServerInfo.serverAddress,
      serverPort: gameServerInfo.serverPort,
      maxPublicPlayers: 220,
      openPublicPlayers: 175,
      maxPrivatePlayers: 0,
      openPrivatePlayers: 0,
      attributes: {
        REGION_s: "EU",
        GAMEMODE_s: "FORTATHENA",
        ALLOWBROADCASTING_b: true,
        SUBREGION_s: "GB",
        DCID_s: "FORTNITE-LIVEEUGCEC1C2E30UBRCORE0A-14840880",
        tenant_s: "Fortnite",
        MATCHMAKINGPOOL_s: "Any",
        STORMSHIELDDEFENSETYPE_i: 0,
        HOTFIXVERSION_i: 0,
        PLAYLISTNAME_s: playlists.playlistNames[ticket.mode],
        SESSIONKEY_s: makeId(),
        TENANT_s: "Fortnite",
        BEACONPORT_i: 15009,
        ...((()=>{const q=global.syndicateArenaQueue?.[req.user.accountId];return q&&q.eventId&&/^arena/.test(ticket.mode||'')?{EVENT_s:q.eventId,EVENTWINDOW_s:q.windowId,TOURNAMENTID_s:q.eventId,EVENTWINDOWID_s:q.windowId}:{};})()),
      },
      publicPlayers: [],
      privatePlayers: [],
      totalPlayers: 45,
      allowJoinInProgress: false,
      shouldAdvertise: false,
      isDedicated: false,
      usesStats: false,
      allowInvites: false,
      usesPresence: false,
      allowJoinViaPresence: true,
      allowJoinViaPresenceFriendsOnly: false,
      buildUniqueId: buildUniqueId[req.user.accountId] || "0",
      lastUpdated: new Date().toISOString(),
      started: false,
    });
  }
);

app.post("/fortnite/api/matchmaking/session/*/join", (req, res) => {
  res.status(204).end();
});

// Save the World, Seasons 2 and up: it runs on its own match server, started on the mission's zone (launcher "stw"
// playlist, Erbium SYNDICATE_STW_HOST); Battle Royale servers never run the Save the World code. This asks the
// launcher for one (playlist-request.json, or restart.request for another mission) and waits for it to be up.
const STW_WAIT_MS = 45000;
const stwAlive = pid => { try { process.kill(pid, 0); return true; } catch { return false; } };
function stwServerUp(skipPid) {
  const state = playlists.read(path.join(playlists.runtime, 'server.json'));
  if (!state || String(state.Mode).toLowerCase() !== 'stw' || state.Pid === skipPid || !stwAlive(state.Pid)) return null;
  try { return fs.readFileSync(path.join(playlists.runtime, state.Ready || 'ready.pid'), 'utf8').trim() === String(state.Pid) ? state : null; }
  catch { return null; }
}
async function stwServerFor(build, theme, wait) {
  const dir = playlists.runtime, now = Date.now();
  const state = playlists.read(path.join(dir, 'server.json'));
  const running = state && String(state.Mode).toLowerCase() === 'stw' && stwAlive(state.Pid);
  global.syndicateStwServer ??= {pid: 0, theme: '', old: 0};
  const known = global.syndicateStwServer;
  if (running && theme && known.pid === state.Pid && known.theme && known.theme !== theme) {
    // Another mission: the server loads its zone when it starts, so start it again.
    fs.writeFileSync(path.join(dir, 'restart.request'), `${state.Pid} stw-mission`);
    global.syndicateStwServer = {pid: 0, theme, old: state.Pid};
    console.log('[Syndicate STW] another mission picked: restarting the Save the World server');
  } else if (!running) {
    const request = playlists.read(path.join(dir, 'playlist-request.json'));
    if (!request || request.Mode !== 'stw' || request.Expires <= now) {
      playlists.write(path.join(dir, 'playlist-request.json'), {Id: uuidv4(), Mode: 'stw', Build: build, Expires: now + 600000});
      global.syndicateStwServer = {pid: 0, theme, old: 0};
      console.log('[Syndicate STW] asked the launcher for a Save the World server');
    }
  }
  const until = Date.now() + (wait ? STW_WAIT_MS : 0);
  for (;;) {
    const up = stwServerUp(global.syndicateStwServer.old);
    if (up) { global.syndicateStwServer = {pid: up.Pid, theme: global.syndicateStwServer.theme || theme, old: 0}; return up; }
    if (Date.now() >= until) return null;
    await new Promise(resolve => setTimeout(resolve, 1000));
  }
}
// The session the client searched for, with every criterion it searched by, pointing at the local match server.
function stwSession(body) {
  const info = getGameServerInfo();
  const attributes = {REGION_s: 'NA', SUBREGION_s: 'NA', ALLOWBROADCASTING_b: true, DCID_s: 'SYNDICATE-STW', tenant_s: 'Fortnite',
    TENANT_s: 'Fortnite', MATCHMAKINGPOOL_s: 'Any', STORMSHIELDDEFENSETYPE_i: 0, HOTFIXVERSION_i: 0, BEACONPORT_i: 15009,
    ADDRESS_s: info.serverAddress};
  for (const c of body?.criteria || []) {
    if (!c?.key || c.type === 'NOT_EQUAL') continue;
    attributes[c.key] = Array.isArray(c.value) ? c.value[0] : c.value;
  }
  return {
    id: uuidv4().replace(/-/g, ''), ownerId: uuidv4().replace(/-/g, ''), ownerName: '[DS]syndicate-stw', serverName: '[DS]syndicate-stw',
    serverAddress: info.serverAddress, serverPort: info.serverPort, maxPublicPlayers: 4, openPublicPlayers: 4, maxPrivatePlayers: 0,
    openPrivatePlayers: 0, attributes, publicPlayers: [], privatePlayers: [], totalPlayers: 0, allowJoinInProgress: true,
    shouldAdvertise: false, isDedicated: true, usesStats: false, allowInvites: false, usesPresence: false, allowJoinViaPresence: true,
    allowJoinViaPresenceFriendsOnly: false, buildUniqueId: String(body?.buildUniqueId || '0'), lastUpdated: new Date().toISOString(),
    started: false,
  };
}

app.post("/fortnite/api/matchmaking/session/matchMakingRequest", async (req, res) => {
  // Save the World (dev launcher): Pre-season's tutorial matchmakes here (an MCP session search) and got no
  // sessions back ("No matches available"). Log what it asks for, so the tutorial session can be built to match.
  const stwOn = stwEnabledFor(req.headers['user-agent']); // Dev Kit, or Save the World on for this season
  if (stwOn) console.log('[Syndicate STW] matchMakingRequest ' + JSON.stringify(req.body));
  // The request (logged above) asks for an EMPTY dedicated server: GAMEMODE_s FORTEMPTY, maxCurrentPlayers 0,
  // REGION_s NA, MATCHMAKINGPOOL_s Desktop/Any. In 2017 the client then reserved that empty server through its
  // beacon and the server loaded the mission (the tutorial) for it. Dev launcher: offer the local host as that
  // empty server, matching every criterion, so the next step (the beacon reservation) can be seen and built.
  const wantsEmpty = (req.body?.criteria || []).some(c => c?.key === 'GAMEMODE_s' && c?.value === 'FORTEMPTY');
  // The mission the player picked (any season): its zone theme tells the match server which map and game mode to
  // load when the empty server is reserved (Erbium Misc.cpp SyndicateStwRequestedTheme reads stw-zone.json).
  let theme = '';
  if (stwOn) {
    try {
      const zone = (req.body?.criteria || []).find(c => c?.key === 'ZONEINSTANCEID_s')?.value;
      theme = zone ? JSON.parse(zone)?.zoneThemeClass || '' : '';
      if (typeof theme === 'string' && /^\/Game\/[A-Za-z0-9_\/.]+$/.test(theme)) {
        fs.mkdirSync(playlists.runtime, {recursive: true});
        fs.writeFileSync(path.join(playlists.runtime, 'stw-zone.json'), JSON.stringify({zoneThemeClass: theme, at: Date.now()}));
        console.log('[Syndicate STW] mission zone theme ' + theme);
      }
    } catch {}
  }
  // Seasons 2 and up: the Save the World server (see stwServerFor). A mission search waits for it and gets it back.
  {
    const build = clientBuild(req.headers['user-agent'] || '');
    if (stwOn && build && !/^1\./.test(build)) {
      const searching = wantsEmpty || !!theme;
      const host = await stwServerFor(build, theme, searching);
      if (!host || !searching) {
        if (searching) console.log('[Syndicate STW] the Save the World server is not up yet; answered with no sessions');
        return res.json([]);
      }
      const session = stwSession(req.body);
      console.log(`[Syndicate STW] offering the Save the World server (PID ${host.Pid}): ${JSON.stringify(session.attributes)}`);
      return res.json([session]);
    }
  }
  // Local tutorial (dev launcher, stw-local.txt): no host. Tell the client's SyndicateStudio.dll to open the
  // tutorial map in the player's own game, and offer no server.
  const stwShared = process.env.LOCALAPPDATA ? process.env.LOCALAPPDATA + '/ProjectTilted' : '';
  if (stwOn && wantsEmpty && stwShared && fs.existsSync(stwShared + '/stw-local.flag')) {
    fs.writeFileSync(stwShared + '/stw-local-go.txt', String(Date.now()));
    console.log('[Syndicate STW] local tutorial: told the client to open the tutorial map');
    return res.json([]);
  }
  if (stwOn && wantsEmpty) {
    const info = getGameServerInfo();
    const region = (req.body.criteria.find(c => c?.key === 'REGION_s')?.value) || 'NA';
    const session = {
      // Epic ids are plain 32-character hex. A prefixed id made the client reject the session
      // ("Num Valid / Total Search Results 0 / 1" in FortniteGame_2.log).
      id: uuidv4().replace(/-/g, ''),
      ownerId: uuidv4().replace(/-/g, ''),
      ownerName: '[DS]syndicate-stw-local',
      serverName: '[DS]syndicate-stw-local',
      serverAddress: info.serverAddress,
      serverPort: info.serverPort,
      maxPublicPlayers: 4,
      openPublicPlayers: 4,
      maxPrivatePlayers: 0,
      openPrivatePlayers: 0,
      attributes: {
        REGION_s: region,
        GAMEMODE_s: 'FORTEMPTY',
        ALLOWBROADCASTING_b: true,
        SUBREGION_s: region,
        DCID_s: 'SYNDICATE-STW-LOCAL',
        tenant_s: 'Fortnite',
        MATCHMAKINGPOOL_s: 'Any',
        STORMSHIELDDEFENSETYPE_i: 0,
        HOTFIXVERSION_i: 0,
        // No SESSIONKEY_s: with one, the client's beacon sends an encryption token and the host (which has no
        // matching online session) answers "encryption failure [InvalidParams] Unable to get user or session".
        TENANT_s: 'Fortnite',
        BEACONPORT_i: 15009,
        ADDRESS_s: info.serverAddress,
      },
      publicPlayers: [],
      privatePlayers: [],
      totalPlayers: 0,
      allowJoinInProgress: false,
      shouldAdvertise: false,
      isDedicated: true,
      usesStats: false,
      allowInvites: false,
      usesPresence: false,
      allowJoinViaPresence: true,
      allowJoinViaPresenceFriendsOnly: false,
      buildUniqueId: String(req.body.buildUniqueId || '0'),
      lastUpdated: new Date().toISOString(),
      started: false,
    };
    console.log(`[Syndicate STW] offering the local host as an empty server: ${JSON.stringify(session)}`);
    return res.json([session]);
  }
  res.json([]);
});

export default app;
