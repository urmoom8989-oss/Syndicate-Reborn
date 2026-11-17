// Arena: the game's own in-match points counter only shows when the client is told it entered an Epic
// tournament event (AFortPlayerStateAthena::ClientNotifyMatchEntered(EventId, EventWindowId)). Epic's servers
// did that; here the backend writes, at matchmaking, the event and division window each player is queued for
// (the same ids /api/v1/events serves from local/events/Events.json), and the match server (Erbium,
// NotifyArenaMatchEntered) sends the RPC to that player when they join.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import Profile from '../User/Mongodb/Schema/profiles.js';
import User from '../User/Mongodb/Schema/user.js';
import {arenaDivision} from './syndicateArena.js';

const file = fileURLToPath(new URL('../../../Runtime/Server/arena-session.json', import.meta.url));
export const arenaEventId = 'cjdz2smhk34dtjpq8x3jh91n'; // LG_ARENA_SOLO in local/events/Events.json
const seasons = {'8.50': 8, '9.40': 9, '10.31': 10, '10.40': 10, '11.00': 11, '12.50': 12};

export async function writeArenaSession(accountId, build) {
  const season = seasons[build]; if (!season) return;
  const user = await User.findOne({accountId}).lean(); if (!user?.username) return;
  const doc = await Profile.findOne({accountId}, {'profiles.athena.stats.attributes.arena_hype': 1}).lean();
  const hype = Math.max(0, Number(doc?.profiles?.athena?.stats?.attributes?.arena_hype) || 0);
  const division = Math.min(10, arenaDivision(hype));
  let data = {players: {}};
  try { data = JSON.parse(fs.readFileSync(file, 'utf8')); if (!data || typeof data.players !== 'object') data = {players: {}}; } catch {}
  const now = Date.now();
  for (const [name, entry] of Object.entries(data.players)) if (now - (entry?.at || 0) > 60 * 60 * 1000) delete data.players[name];
  data.players[String(user.username).toLowerCase()] = {eventId: arenaEventId, windowId: `Arena_S${season}_Division${division}_LGSolo`, at: now};
  fs.mkdirSync(path.dirname(file), {recursive: true});
  const temp = file + '.' + process.pid + '.tmp';
  fs.writeFileSync(temp, JSON.stringify(data, null, 1));
  fs.renameSync(temp, file);
}
