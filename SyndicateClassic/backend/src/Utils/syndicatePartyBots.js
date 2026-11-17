// Season 4 party bots: typing an unknown name into "Send friend request" creates a bot
// friend with that name. It accepts at once and shows as online so it can be invited.
// Season 4 parties run over XMPP messages between the players' games, so every message
// sent to a bot is written to Runtime/Server/party-bot-xmpp.log; the join handshake is
// built from that capture.
import fs from 'fs';
import path from 'path';
import crypto from 'crypto';
import { fileURLToPath } from 'url';
import { clientBuild } from './syndicateBuild.js';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const store = path.resolve(__dirname, '../local/tiltedPartyBots.json');
const runtime = path.resolve(__dirname, '../../../Runtime/Server');
const domain = 'prod.ol.epicgames.com';

function load() {
  try { return JSON.parse(fs.readFileSync(store, 'utf8')); } catch { return {}; }
}
function save(bots) {
  try { fs.mkdirSync(path.dirname(store), { recursive: true }); fs.writeFileSync(store, JSON.stringify(bots, null, 1)); } catch {}
}
const bots = load(); // name_lower -> { accountId, displayName }

export function partyBotsEnabled(userAgent) { return clientBuild(userAgent) === '4.5'; }
export function validBotName(name) { return typeof name === 'string' && /^[A-Za-z0-9_.\- ]{3,16}$/.test(name); }
export function findBotById(accountId) { return Object.values(bots).find((b) => b.accountId === accountId) || null; }
export function findBotByName(name) { return bots[String(name).toLowerCase()] || null; }

function presenceStatus() {
  return JSON.stringify({
    Status: 'Battle Royale Lobby - 1 / 4',
    bIsPlaying: false,
    bIsJoinable: false,
    bHasVoiceSupport: false,
    SessionId: '',
    Properties: { FortPartySize_i: 1, FortSubGame_i: 1, InUnjoinableMatch_b: false },
  });
}

function captureLog(line) {
  try { fs.appendFileSync(path.join(runtime, 'party-bot-xmpp.log'), `[${new Date().toISOString()}] ${line}\n`); } catch {}
}

// Season 1-4 outfits a party bot can wear (picked once per bot, at random).
const SKINS = ['CID_017_Athena_Commando_M','CID_028_Athena_Commando_F','CID_032_Athena_Commando_M_Medieval','CID_035_Athena_Commando_M_Medieval',
  'CID_039_Athena_Commando_F_Disco','CID_040_Athena_Commando_M_District','CID_047_Athena_Commando_F_HolidayReindeer','CID_051_Athena_Commando_M_HolidayElf',
  'CID_069_Athena_Commando_F_PinkBear','CID_071_Athena_Commando_M_Wukong','CID_084_Athena_Commando_M_Assassin','CID_088_Athena_Commando_M_SpaceBlack',
  'CID_090_Athena_Commando_M_Tactical','CID_097_Athena_Commando_F_RockerPunk','CID_101_Athena_Commando_M_Stealth'];

function botJid(bot) { return `${bot.accountId}@${domain}/V2:Fortnite:WIN::SyndicatePartyBot`; }

// Send a party message from the bot to a real player's game.
function sendFromBot(bot, toAccountId, type, payload) {
  const receiver = (global.Clients || []).find((c) => c.accountId === toAccountId && !c.isPartyBot);
  if (!receiver) return;
  const body = JSON.stringify({ type, payload, timestamp: new Date().toISOString() });
  const id = crypto.randomBytes(16).toString('hex').toUpperCase();
  const esc = (t) => t.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  receiver.client.send(`<message from="${botJid(bot)}" id="${id}" to="${receiver.jid}" xmlns="jabber:client"><body>${esc(body)}</body></message>`);
  captureLog(`from ${bot.displayName}: ${body.slice(0, 3000)}`);
}

// Member data in the Season 4 format (copied from what the player's own game sends).
function memberAttrs(bot, ready) {
  const cid = bot.skin || SKINS[0];
  const hid = 'Hero:' + cid.toLowerCase().replace(/^cid_/, 'hid_');
  return {
    Location_s: 'PreLobby', MatchmakingLevel_U: '0', ZoneInstanceId_s: '', CurrentCharXP_U: '0', HeroId_s: '',
    HeroTypeRefName_s: hid, CharacterGender_s: /_f(_|$)/i.test(cid) ? 'Female' : 'Male', HomeBaseVersion_U: '1',
    PlatformUniqueId_s: 'INVALID', Platform_s: 'WIN',
    CosmeticLoadout_j: { CosmeticLoadout: { backpackPersistentName: '', bannerIconId: 'StandardBanner' + (1 + (parseInt(bot.accountId.slice(0, 2), 16) % 30)), bannerColorId: 'DefaultColor' + (1 + (parseInt(bot.accountId.slice(2, 4), 16) % 20)),
      emotePersistentName: 'AthenaDance:eid_wave', battlePassLevel: 1, battlePassSelfBoostXp: 0, battlePassFriendBoostXp: 0,
      athenaCharacterVariantInfo: { cosmeticItemDef: `AthenaCharacterItemDefinition'/Game/Athena/Items/Cosmetics/Characters/${cid}.${cid}'`, cosmeticVariantChannels: [] } } },
    bPreloadedAthena_b: true, bReadyAthena_b: !!ready, PlatformSessionId_s: '', EmoteSection_U: '-1', CrossplayPreference_s: 'OptedIn',
    NumAliveAthena_U: '0', UtcTimeStartedMatchAthena_s: '0001-01-01T00:00:00.000Z',
  };
}
function sendMemberData(bot, to, attrs) {
  bot.rev = (bot.rev || 0) + 1;
  sendFromBot(bot, to, 'com.epicgames.party.memberdata', { partyId: bot.party?.id, payload: { Rev: bot.rev, Attrs: attrs } });
}
function leaveParty(bot, kicked) {
  if (!bot.party) return;
  sendFromBot(bot, bot.party.leader, 'com.epicgames.party.memberexited', { partyId: bot.party.id, memberId: bot.accountId, wasKicked: !!kicked });
  bot.party = null;
}

// Party handshake, from the bot's side (Season 4 parties are run by the leader's game).
function handleInbound(bot, xml) {
  const text = String(xml);
  captureLog(`to ${bot.displayName}: ${text.slice(0, 4000)}`);
  const from = /from="([^"@]+)@/.exec(text)?.[1];
  const raw = /<body>([\s\S]*)<\/body>/.exec(text)?.[1];
  if (!from || !raw) return;
  let msg;
  try { msg = JSON.parse(raw.replace(/&quot;/g, '"').replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&amp;/g, '&')); } catch { return; }
  const p = msg.payload || {};
  switch (msg.type) {
    case 'com.epicgames.party.invitation':
      bot.party = { id: p.partyId, leader: from, key: p.accessKey, buildId: p.buildId };
      // Same order a real game uses: ask whether the party can be joined, then ask to join.
      sendFromBot(bot, from, 'com.epicgames.party.queryjoinability', { partyId: p.partyId, displayName: bot.displayName, platform: 'WIN', accessKey: p.accessKey, appId: p.appId || 'Fortnite', buildId: p.buildId, joinData: { Rejoin: false } });
      setTimeout(() => sendFromBot(bot, from, 'com.epicgames.party.joinrequest', { partyId: p.partyId, displayName: bot.displayName, platform: 'WIN', accessKey: p.accessKey, appId: p.appId || 'Fortnite', buildId: p.buildId, joinData: { Rejoin: false } }), 400);
      break;
    case 'com.epicgames.party.joinrequest.approved':
      bot.rev = 0;
      sendFromBot(bot, from, 'com.epicgames.party.joinacknowledged', { partyId: p.partyId });
      break;
    case 'com.epicgames.party.joinacknowledged.response':
      if (bot.party) sendMemberData(bot, from, memberAttrs(bot, bot.leaderReady));
      break;
    case 'com.epicgames.party.memberdata':
      // The leader's own lobby state: mirror Ready / Not Ready.
      if (bot.party && from === bot.party.leader && p.payload?.Attrs && 'bReadyAthena_b' in p.payload.Attrs) {
        bot.leaderReady = !!p.payload.Attrs.bReadyAthena_b;
        sendMemberData(bot, from, { bReadyAthena_b: bot.leaderReady });
      }
      break;
    case 'com.epicgames.party.memberexited':
      if (p.memberId === bot.accountId) leaveParty(bot, p.wasKicked);
      else if (bot.party && p.memberId === bot.party.leader) bot.party = null;
      break;
    default:
      // Kicking a member (any "kick" message addressed to the bot) makes the bot leave.
      if (/kick/i.test(msg.type) && bot.party && (!p.memberId || p.memberId === bot.accountId || p.kickedId === bot.accountId)) leaveParty(bot, true);
      break;
  }
}

// Bots in a player's party, written for the game server so they join that player's team.
export function writePartyBotsFor(accountId, username, runtimeDir, pid) {
  const members = Object.values(bots).filter((b) => b.party && b.party.leader === accountId);
  const dir = path.join(runtimeDir, 'party-bots'); const file = path.join(dir, Buffer.from(String(username).toLowerCase(), 'utf8').toString('hex') + '.txt');
  try {
    fs.mkdirSync(dir, { recursive: true });
    if (!members.length) { try { fs.unlinkSync(file); } catch {} return; }
    const lines = ['pid=' + pid, 'expires=' + Math.floor(Date.now() / 1000 + 600)].concat(members.slice(0, 3).map((b) => `bot=${b.displayName.replace(/[|\r\n=]/g, '')}|${b.skin || ''}`));
    fs.writeFileSync(file, lines.join('\n') + '\n');
    captureLog(`match: ${members.map((b) => b.displayName).join(', ')} join ${username}'s team`);
  } catch (e) { captureLog('match file error: ' + e.message); }
}

// A stand-in XMPP connection: messages to the bot are answered by the handshake above.
function attachClient(bot) {
  if (!global.Clients) return;
  if (global.Clients.find((c) => c.accountId === bot.accountId)) return;
  if (!bot.skin) bot.skin = SKINS[crypto.randomInt(SKINS.length)];
  const jid = botJid(bot);
  global.Clients.push({
    client: { send: (xml) => { try { handleInbound(bot, xml); } catch (e) { captureLog('error: ' + e.message); } }, close() {} },
    accountId: bot.accountId,
    displayName: bot.displayName,
    token: '',
    jid,
    resource: 'V2:Fortnite:WIN::SyndicatePartyBot',
    lastPresenceUpdate: { away: false, status: presenceStatus() },
    isPartyBot: true,
  });
}

export function attachAllBots() { for (const b of Object.values(bots)) attachClient(b); }

export function ensureBot(name) {
  if (!validBotName(name)) return null;
  const key = name.toLowerCase();
  if (!bots[key]) {
    const accountId = crypto.createHash('md5').update('tilted-party-bot:' + key).digest('hex');
    bots[key] = { accountId, displayName: name, skin: SKINS[crypto.randomInt(SKINS.length)] };
    save(bots);
    captureLog(`created bot ${name} (${accountId})`);
  }
  attachClient(bots[key]);
  return bots[key];
}

// global.Clients is created when the XMPP server starts; reattach saved bots once it exists.
const timer = setInterval(() => { if (global.Clients) { attachAllBots(); clearInterval(timer); } }, 1000);
