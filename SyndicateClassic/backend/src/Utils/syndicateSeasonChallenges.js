// Syndicate: Battle Pass challenges for Seasons 2, 3 and 6-X (Season 4 and 5 keep their own modules).
// The game server exports each build's own challenge data (schedules -> bundles -> quests ->
// objectives with their native stat rows) to Runtime/Server/challenge-data/season-N.json, and
// streams the player's native stat events ("stats" events). Objectives progress here exactly
// like the in-game quest manager matches them: stat type + required target/source/context tags
// + condition. Match/win/kill events also cover objectives that have no stat rows.
import fs from 'node:fs';
import {fileURLToPath} from 'node:url';

const folder = fileURLToPath(new URL('../../../Runtime/Server/challenge-data/', import.meta.url));
const seasonXpTables = new Map();
function seasonXpTable(season) {
  if (!seasonXpTables.has(season)) {
    let rows = [];
    try { rows = JSON.parse(fs.readFileSync(new URL(`../local/Battlepass/Data/Season${season}/SeasonXP.json`, import.meta.url), 'utf8')); } catch {}
    seasonXpTables.set(season, Array.isArray(rows) ? rows : []);
  }
  return seasonXpTables.get(season);
}
// All Season XP earned this season (levels reached plus progress into the current level).
function totalSeasonXp(a, season) {
  const level = Math.max(1, Number(a.level) || 1); let total = Math.max(0, Number(a.xp) || 0);
  for (const row of seasonXpTable(season)) if (row.Level >= 1 && row.Level < level) total += Number(row.XpToNextLevel) || 0;
  return total;
}
export const challengeSeasons = new Set([2, 3, 6, 7, 8, 9, 10, 11, 12]);
const cache = new Map();
const epoch = '2018-01-01T00:00:00.000Z';

export function seasonData(season) {
  if (!challengeSeasons.has(season)) return null;
  return loadChallengeData(season);
}
// Any season's exported data (also Seasons 4 and 5, whose weekly challenges have their own modules:
// only their daily challenges come from here).
// Seasons whose build exports no usable challenge data get an authored set shipped in the backend.
// Season 2 (1.11) had no weekly challenges, so its dailies are authored in local/Challenges/season2.json.
const bundledChallenges = {2: new URL('../local/Challenges/season2.json', import.meta.url)};
export function loadChallengeData(season) {
  if (!Number.isInteger(season) || season < 1 || season > 12) return null;
  let file = folder + 'season-' + season + '.json';
  let stat; try { stat = fs.statSync(file); } catch {
    // No runtime export yet: fall back to the authored set if one ships for this season.
    const bundled = bundledChallenges[season];
    if (!bundled) return null;
    try { file = fileURLToPath(bundled); stat = fs.statSync(file); } catch { return null; }
  }
  const hit = cache.get(season);
  if (hit && hit.mtime === stat.mtimeMs) return hit.data;
  try {
    const data = JSON.parse(fs.readFileSync(file, 'utf8'));
    if (!data || typeof data.quests !== 'object' || Number(data.season) !== season) return null;
    data.questMap = new Map(Object.entries(data.quests).map(([id, q]) => [id.toLowerCase(), {id, ...q}]));
    data.bundleMap = new Map((data.bundles || []).map(b => [b.id, b]));
    for (const q of data.questMap.values()) for (const o of q.objectives || []) for (const s of o.stats || []) {
      for (const k of ['target', 'source', 'context']) s[k] = (s[k] || []).map(t => String(t).toLowerCase());
      s.inline = (s.inline || []).map(i => ({...i, tag: String(i.tag || '').toLowerCase()}));
    }
    cache.set(season, {mtime: stat.mtimeMs, data});
    return data;
  } catch { return null; }
}

const questKey = id => 'tilted_bundlequest_' + id.toLowerCase();
const cosmeticTypes = /^Athena(Character|Backpack|Pickaxe|Glider|Dance|ItemWrap|LoadingScreen|SkyDiveContrail|MusicPack|Pet|PetCarrier|Spray|Emoji|Toy)$/i;
const lastPart = tag => String(tag || '').split('.').pop();
function findItem(items, templateId) {
  const lower = templateId.toLowerCase();
  return Object.entries(items).find(([, i]) => String(i.templateId || '').toLowerCase() === lower);
}
function addItem(items, templateId, qty, stack) {
  const found = findItem(items, templateId);
  if (found) {
    const item = found[1];
    if (stack) item.quantity = (Number(item.quantity) || 0) + qty;
    // Earned now, so turning "Unlock all" off keeps it.
    if (item.attributes?.tilted_unlock_all) delete item.attributes.tilted_unlock_all;
    return item;
  }
  let key = 'tilted_reward_' + templateId.toLowerCase().replace(/[^a-z0-9_:.-]/g, ''); while (items[key]) key += '_';
  items[key] = {templateId, quantity: qty, attributes: {item_seen: false, level: 1, xp: 0, variants: [], favorite: false}};
  return items[key];
}
// Gives a completed challenge's rewards. Battle Stars go straight to the pass; Season XP is returned.
// Quest rewards (next stages) are granted by grantChain.
function grantRewards(profile, definition, includeStars = true) {
  const a = profile.stats.attributes, items = profile.items; let stars = 0, xp = 0;
  for (const reward of definition.rewards || []) {
    const type = String(reward.type || ''), name = String(reward.name || ''), qty = Math.max(1, Math.min(100000, Number(reward.qty) || 1));
    if (!name) continue;
    const lower = name.toLowerCase();
    if (lower.includes('battlestar')) { if (includeStars) stars += qty; continue; }
    if (lower.includes('seasonalxp') || lower.includes('athenaseasonxp')) { if (includeStars) xp += qty; continue; }
    if (/^quest$/i.test(type)) continue;
    if (cosmeticTypes.test(type)) { addItem(items, type + ':' + name, 1, false); continue; }
    if (/varianttoken/i.test(type)) {
      addItem(items, 'CosmeticVariantToken:' + name, 1, false);
      if (reward.item && reward.channel && reward.tag) {
        const cosmetic = findItem(items, reward.item)?.[1]; if (!cosmetic) continue;
        cosmetic.attributes ??= {}; const variants = cosmetic.attributes.variants ??= [];
        const channel = lastPart(reward.channel), tag = lastPart(reward.tag);
        let entry = variants.find(v => String(v.channel).toLowerCase() === channel.toLowerCase());
        if (!entry) variants.push(entry = {channel, active: tag, owned: []});
        if (!entry.owned.includes(tag)) entry.owned.push(tag);
      }
      continue;
    }
    if (/^token$/i.test(type) || /token/i.test(type)) { addItem(items, 'Token:' + name, qty, true); continue; }
  }
  if (stars) a.book_xp = (Number(a.book_xp) || 0) + stars;
  return {stars, xp};
}
// Grant a Completed quest's rewards when "Collect Rewards" is pressed, if completion never granted them.
// A quest that reached Completed via grantQuest (objectives already met on grant) skips the reward sweep and
// so never got grantRewards: its stars/items/XP were silently lost until now. Idempotent: an already-rewarded
// quest (tilted_rewarded set) is a no-op, so this can never double-grant. Items + Battle Stars are granted here;
// the returned season XP is applied by the caller via applyProgression.
export function rewardOnClaim(athena, questItem, season) {
  const a = questItem?.attributes;
  if (!a || a.tilted_rewarded) return {granted: false, xp: 0};
  const data = loadChallengeData(season); if (!data) return {granted: false, xp: 0};
  const definition = data.questMap.get(String(questItem.templateId || '').slice(6).toLowerCase());
  if (!definition) return {granted: false, xp: 0};
  const given = grantRewards(athena, definition, true);
  a.tilted_rewarded = 2;
  return {granted: true, xp: given.xp || 0};
}
// --- challenges with no in-game stat (counted from the player's other challenges) ------------
const weekBundle = id => /week/i.test(id);
const metaObjective = /season_xp_gained|complete_?weekly_?challenges|complete_daily_challenges|cumulative_token0*\d+$|cumulative_final_quest0*\d+$|completequests/;
function metaValue(name, count, ctx) {
  if (/season_xp_gained/.test(name)) return ctx.seasonXp;
  if (/complete_daily_challenges/.test(name)) return ctx.dailyDone;
  if (/complete_?weekly_?challenges/.test(name)) return ctx.weeklyDone;
  let m = /cumulative_token0*(\d+)$/.exec(name); if (m) return ctx.weeksDone;
  m = /cumulative_final_quest0*(\d+)$/.exec(name); if (m) return ctx.cumulativeDone.has(Number(m[1])) ? 1 : 0;
  if (/completequests/.test(name)) return count > ctx.ownBundleSize ? ctx.allDone : ctx.ownBundleDone;
  return null;
}
function progressMeta(profile, data, season) {
  const items = profile.items, a = profile.stats.attributes;
  const byId = new Map(Object.values(items).filter(i => /^quest:/i.test(i.templateId || '')).map(i => [i.templateId.slice(6).toLowerCase(), i]));
  const isDone = id => done(byId.get(String(id).toLowerCase()));
  const isMeta = id => (data.questMap.get(String(id).toLowerCase())?.objectives || []).some(o => !o.stats?.length && metaObjective.test(o.name));
  let weeklyDone = 0, weeksDone = 0, allDone = 0; const bundleOf = new Map();
  for (const bundle of data.bundles || []) {
    for (const q of bundle.quests || []) bundleOf.set(String(q).toLowerCase(), bundle);
    if (!weekBundle(bundle.id)) continue;
    const real = (bundle.quests || []).filter(q => !isMeta(q));
    const completed = real.filter(isDone).length; weeklyDone += completed;
    if (real.length && completed === real.length) weeksDone++;
  }
  for (const [id, item] of byId) if (done(item) && !isMeta(id) && !item.attributes?.tilted_daily) allDone++;
  const cumulativeDone = new Set();
  for (const [id, item] of byId) { const m = /cumulative_quest0*(\d+)$/.exec(id); if (m && done(item)) cumulativeDone.add(Number(m[1])); }
  const ctx = {seasonXp: totalSeasonXp(a, season), weeklyDone, weeksDone, allDone, cumulativeDone, dailyDone: Math.max(0, Number(a.tilted_quest_dailies_completed) || 0)};
  let stars = 0, changed = false;
  for (const [id, item] of byId) {
    if (item.attributes?.quest_state !== 'Active') continue;
    const definition = data.questMap.get(id); if (!definition) continue;
    const bundle = bundleOf.get(id), own = (bundle?.quests || []).filter(q => !isMeta(q));
    ctx.ownBundleSize = own.length; ctx.ownBundleDone = own.filter(isDone).length;
    for (const o of definition.objectives || []) {
      if (o.stats?.length) continue;
      const value = metaValue(o.name, o.count, ctx); if (value === null) continue;
      const key = 'completion_' + o.name, next = Math.min(o.count, Math.max(Number(item.attributes[key]) || 0, value));
      if (next !== item.attributes[key]) { item.attributes[key] = next; changed = true; }
    }
    if (questComplete(definition, item.attributes)) {
      item.attributes.quest_state = 'Completed'; item.attributes.last_state_change_time = new Date().toISOString();
      stars += grantRewards(profile, definition).stars; item.attributes.tilted_rewarded = 2; changed = true;
    }
  }
  return {changed, stars};
}
const done = q => ['Completed', 'Claimed'].includes(q?.attributes?.quest_state);
function questComplete(definition, attributes) {
  const objectives = definition.objectives || [];
  const met = objectives.filter(o => (Number(attributes['completion_' + o.name]) || 0) >= o.count).length;
  // "Complete N of these" challenges (e.g. ammo boxes in 7 of 19 named locations) need only N.
  const needed = Number(definition.needed) > 0 && Number(definition.needed) < objectives.length ? Number(definition.needed) : objectives.length;
  return objectives.length > 0 && met >= needed;
}
let chapterTwo = false; // set by ensureSeasonChallenges for Season 11+
function grantQuest(items, definition, bundleKey) {
  const key = questKey(definition.id);
  let pair = Object.entries(items).find(([, i]) => i.templateId?.toLowerCase() === ('quest:' + definition.id).toLowerCase());
  const id = pair?.[0] || key;
  // Chapter 2 builds hide quests created before the season (the 2018 placeholder): they get the real grant time.
  const created = chapterTwo ? new Date().toISOString() : epoch;
  if (!pair) items[id] = {templateId: 'Quest:' + definition.id, quantity: 1, attributes: {quest_state: 'Active', creation_time: created, last_state_change_time: created, item_seen: false, level: -1, xp: 0, max_level_bonus: 0, sent_new_notification: false, xp_reward_scalar: 1, favorite: false}};
  else if (chapterTwo && items[id].attributes.creation_time === epoch) { items[id].attributes.creation_time = created; if (items[id].attributes.last_state_change_time === epoch) items[id].attributes.last_state_change_time = created; }
  const q = items[id].attributes;
  if (bundleKey) q.challenge_bundle_id = bundleKey;
  // Fields Epic's profile always had on quests; the Chapter 2 (11.00) challenge list skips quests without them.
  q.playlists ??= []; q.challenge_linked_quest_given ??= ''; q.challenge_linked_quest_parent ??= '';
  q.quest_pool ??= ''; q.bucket ??= ''; q.quest_rarity ??= 'uncommon'; q.xp ??= 0; q.favorite ??= false;
  for (const o of definition.objectives || []) q['completion_' + o.name] ??= 0;
  if (questComplete(definition, q) && q.quest_state === 'Active') q.quest_state = 'Completed';
  return id;
}
// Granted quest ids for a bundle, following staged challenges to their current stage.
function grantChain(items, data, definition, bundleKey, out, depth = 0) {
  const id = grantQuest(items, definition, bundleKey); out.push(id);
  if (depth > 8 || !done(items[id])) return;
  for (const next of definition.next || []) {
    const n = data.questMap.get(String(next).toLowerCase());
    if (n) grantChain(items, data, n, bundleKey, out, depth + 1);
  }
}

export function ensureSeasonChallenges(profile, season) {
  const data = seasonData(season); if (!data) return false;
  chapterTwo = season >= 11;
  profile.items ??= {};
  const items = profile.items, a = profile.stats.attributes, before = JSON.stringify(items) + '|' + a.book_xp;
  grantSchedules(profile, data);
  // Challenges completed before every reward type was supported: give their item rewards now
  // (their Battle Stars were already given).
  for (const item of Object.values(items)) {
    if (!/^quest:/i.test(item.templateId || '') || !done(item) || item.attributes.tilted_rewarded) continue;
    const definition = data.questMap.get(item.templateId.slice(6).toLowerCase()); if (!definition) continue;
    grantRewards(profile, definition, false); item.attributes.tilted_rewarded = 2;
  }
  for (let pass = 0; pass < 4; pass++) { if (!progressMeta(profile, data, season).changed) break; grantSchedules(profile, data); }
  return before !== JSON.stringify(items) + '|' + a.book_xp;
}
// Grants the season's schedules, bundles and next challenge stages only: no rewards, no meta progress
// (syndicateMaxProgress.js completes the quests itself).
export function grantSeasonQuests(profile, season) {
  const data = seasonData(season); if (!data) return false;
  chapterTwo = season >= 11; profile.items ??= {};
  grantSchedules(profile, data);
  return true;
}
const placeholderBundle = (data, bundle) => (bundle.quests || []).length > 0 && (bundle.quests || []).every(id => {
  const q = data.questMap.get(String(id).toLowerCase());
  return !q || (q.objectives || []).every(o => /(^|_)wip$/i.test(o.name));
});
function grantSchedules(profile, data) {
  const items = profile.items, a = profile.stats.attributes;
  for (const schedule of data.schedules || []) {
    if (/paid/i.test(schedule.id) && !a.book_purchased) continue;
    const sid = 'tilted_schedule_' + schedule.id.toLowerCase(), granted = [];
    for (const entry of schedule.entries || []) {
      const bundle = data.bundleMap.get(entry.bundle); if (!bundle) continue;
      // Unfinished bundles in the build's files (11.00 weeks 3-8: one 'quest_wip' placeholder quest) show as
      // empty tiles: leave them out, and drop them from profiles that already have them.
      if (placeholderBundle(data, bundle)) { delete items['tilted_bundle_' + bundle.id.toLowerCase()]; continue; }
      const bid = 'tilted_bundle_' + bundle.id.toLowerCase(), quests = [];
      for (const questId of bundle.quests || []) {
        const definition = data.questMap.get(String(questId).toLowerCase());
        if (definition) grantChain(items, data, definition, bid, quests);
      }
      const completed = quests.filter(id => done(items[id])).length;
      items[bid] = {templateId: 'ChallengeBundle:' + bundle.id, quantity: 1, attributes: {...(items[bid]?.attributes || {}), has_unlock_by_completion: false, num_quests_completed: completed, num_quests_completed_max: completed, num_progress_quests_completed: completed, max_allowed_bundle_level: 0, num_granted_bundle_quests: quests.length, grantedquestinstanceids: quests, challenge_bundle_schedule_id: sid, item_seen: items[bid]?.attributes?.item_seen ?? false, level: chapterTwo ? 0 : 1, xp: 0, max_level_bonus: 0, favorite: false}};
      granted.push(bid);
    }
    // Chapter 2: the schedule unlocks when it is first granted (Epic's profiles use the real time; quests unlock
    // by days from it), kept from then on.
    const prior = items[sid]?.attributes?.unlock_epoch;
    const unlock = chapterTwo ? (prior && prior !== epoch ? prior : new Date(Date.now() - 70 * 86400000).toISOString()) : epoch;
    items[sid] = {templateId: 'ChallengeBundleSchedule:' + schedule.id, quantity: 1, attributes: {...(items[sid]?.attributes || {}), unlock_epoch: unlock, favorite: false, granted_bundles: granted, item_seen: items[sid]?.attributes?.item_seen ?? false, level: 1, xp: 0, max_level_bonus: 0}};
  }
  for (const questId of data.loose || []) {
    const definition = data.questMap.get(String(questId).toLowerCase());
    if (definition) grantChain(items, data, definition, null, []);
  }
}

// --- matching (mirrors FortQuestManager::SendStatEvent__Internal) ---------------------------
const has = (list, tag) => list.includes(tag);
const hasAll = (list, required) => required.every(t => list.includes(t));
function condition(text, stat) {
  if (!text) return true;
  // "Target.Tags HasTag X && Source.Tags MissingTag Y || ..." evaluated left to right.
  const tokens = text.split(/(&&|\|\|)/).map(t => t.trim()).filter(Boolean);
  let result = null, op = '&&';
  for (const token of tokens) {
    if (token === '&&' || token === '||') { op = token; continue; }
    const m = /^(Target|Source|Context)\.Tags\s+(HasTag|MissingTag)\s+(\S+)$/i.exec(token);
    let value = true; // unknown condition forms do not block progress
    if (m) {
      const list = m[1].toLowerCase() === 'target' ? stat.tt : m[1].toLowerCase() === 'source' ? stat.st : stat.ct;
      value = has(list, m[3].toLowerCase()); if (m[2].toLowerCase() === 'missingtag') value = !value;
    }
    result = result === null ? value : op === '&&' ? (result && value) : (result || value);
  }
  return result ?? true;
}
function statMatches(row, stat, season) {
  if (Number(row.type) !== Number(stat.t)) return false;
  if (row.inline?.length || row.target === undefined) {
    for (const check of row.inline || []) {
      const list = check.kind === 1 ? stat.st : check.kind === 2 ? stat.ct : stat.tt;
      const present = has(list, check.tag);
      if (check.require ? !present : present) return false;
    }
  } else {
    // "DamageDone.Total" is a tag the real game adds to every damage event; the match server's damage events do not
    // carry it, so "deal 1,000 damage" challenges never moved. A damage event is damage done by definition.
    if (!hasAll(stat.tt, row.target) || !hasAll(stat.st, row.source.filter(t => !t.startsWith('damagedone.')))) return false;
    if (season >= 10 && !hasAll(stat.ct, row.context)) return false;
  }
  return condition(row.cond, stat);
}
const modeOk = (name, mode) => !/solo|duo|squad/.test(name) || (name.includes('solo') && mode === 'solo') || (name.includes('duo') && mode === 'duo') || (name.includes('squad') && mode === 'squad');
const family = weapon => { const w = String(weapon || '').toLowerCase(); return /pistol_(scavenger|autoheavysuppressed)/.test(w) ? 'smg' : w.includes('assault_lmg') ? 'lmg' : w.includes('assault') ? 'assault' : w.includes('shotgun') ? 'shotgun' : w.includes('sniper') ? 'sniper' : w.includes('smg') ? 'smg' : w.includes('pistol') ? 'pistol' : ''; };
// Objectives without stat rows: progress from the server's match/win/kill events by name.
function fallbackDelta(name, event) {
  const n = name.toLowerCase();
  if (event.kind === 'match') {
    if (!modeOk(n, event.mode)) return 0;
    const place = Number(event.place) || 100, players = Number(event.players) || 0;
    if (n.includes('outlive')) return Math.max(0, Math.min(100, players - place));
    const top = /top_?(\d+)/.exec(n); if (top) return place <= Number(top[1]) ? 1 : 0;
    if (/play_?(a_)?match/.test(n)) return 1;
  }
  if (event.kind === 'win' && /win|victory/.test(n) && modeOk(n, event.mode)) return 1;
  if (event.kind === 'kill' && /kill|elim|killingblow/.test(n) && !/damage/.test(n)) {
    const want = ['shotgun', 'sniper', 'pistol', 'smg', 'assault', 'lmg'].find(f => n.includes(f));
    return !want || want === family(event.weapon) ? 1 : 0;
  }
  return 0;
}

// Weapon kill challenges ("pickaxe eliminations", "shotgun eliminations") need a weapon tag on the kill that the
// match server's kill events do not carry. The kill event names the weapon in hand, so those come from it.
const weaponWords = ['pickaxe', 'shotgun', 'sniper', 'pistol', 'smg', 'assault', 'lmg', 'rocket', 'grenade', 'crossbow', 'minigun'];
function weaponKillDelta(objective, event) {
  if (event.kind !== 'kill') return 0;
  const wanted = (objective.stats || []).filter(row => Number(row.type) === 0).flatMap(row => (row.source || []).filter(t => t.startsWith('weapon.')));
  if (!wanted.length) return 0;
  const weapon = String(event.weapon || '').toLowerCase(), fam = family(weapon);
  return wanted.some(tag => {
    const word = weaponWords.find(w => tag.includes(w));
    if (!word) return false;
    if (word === 'pickaxe') return /pickaxe|harvest/.test(weapon);
    if (word === 'rocket' || word === 'grenade') return weapon.includes('launcher') || weapon.includes(word);
    return fam ? fam === word : weapon.includes(word);
  }) ? 1 : 0;
}
// End-of-match objectives (stat type 23, e.g. "play matches with at least one elimination": "Athena.Kills >= 1",
// "Athena.Rank <=10 && Athena.MaxTeamSize <2"). Evaluated from the match result and the kills counted this match.
// null: the objective has no such conditions, or uses a value we do not know (the name-based rules apply then).
function matchConditionDelta(objective, event, kills) {
  const rows = (objective.stats || []).filter(row => Number(row.type) === 23 && /Athena\.\w+\s*[<>=]/.test(row.cond || ''));
  if (!rows.length || event.kind !== 'match' || /win|victory/i.test(objective.name)) return null; // wins come from the win event
  const teamSize = event.mode === 'squad' ? 4 : event.mode === 'duo' ? 2 : 1;
  const values = {kills, rank: Number(event.place) || 100, maxteamsize: teamSize};
  let known = true;
  const passes = row => {
    const ctx = (row.context || []).find(t => /^athena\.playlist\.(solo|duo|squad)/.test(t));
    if (ctx && !ctx.endsWith(event.mode)) return false;
    let result = null, op = '&&';
    for (const token of row.cond.split(/(&&|\|\|)/).map(t => t.trim()).filter(Boolean)) {
      if (token === '&&' || token === '||') { op = token; continue; }
      const m = /^Athena\.(\w+)\s*(<=|>=|==|<|>)\s*(-?\d+)$/i.exec(token);
      const value = m ? values[m[1].toLowerCase()] : undefined;
      if (value === undefined) { known = false; return false; }
      const n = Number(m[3]), ok = m[2] === '<=' ? value <= n : m[2] === '>=' ? value >= n : m[2] === '==' ? value === n : m[2] === '<' ? value < n : value > n;
      result = result === null ? ok : op === '&&' ? (result && ok) : (result || ok);
    }
    return !!result;
  };
  const hit = rows.some(passes);
  return known ? (hit ? 1 : 0) : null;
}

export function progressSeasonChallenges(profile, event, season, dailyOnly = false) {
  const data = dailyOnly ? loadChallengeData(season) : seasonData(season); if (!data || !event) return {stars: 0, xp: 0};
  const a = profile.stats.attributes, items = profile.items || {};
  const stats = event.kind === 'stats' && Array.isArray(event.stats) ? event.stats.slice(0, 400).map(s => ({
    t: Number(s.t), n: Math.max(0, Math.min(100000, Number(s.n) || 0)),
    tt: (s.tt || []).map(t => String(t).toLowerCase()), st: (s.st || []).map(t => String(t).toLowerCase()), ct: (s.ct || []).map(t => String(t).toLowerCase())
  })) : [];
  const match = String(event.match || event.id || '');
  // Eliminations in the current match, for end-of-match conditions such as "Athena.Kills >= 1".
  if (event.kind === 'kill' && event.match) {
    if (a.tilted_kills_match !== match) { a.tilted_kills_match = match; a.tilted_kills_count = 0; }
    a.tilted_kills_count = (Number(a.tilted_kills_count) || 0) + 1;
  }
  const matchKills = a.tilted_kills_match === match ? Number(a.tilted_kills_count) || 0 : 0;
  let stars = 0, xp = 0;
  for (const item of Object.values(items)) {
    if (!/^quest:/i.test(item.templateId || '') || item.attributes?.quest_state !== 'Active') continue;
    if (dailyOnly && !item.attributes?.tilted_daily) continue;
    const definition = data.questMap.get(item.templateId.slice(6).toLowerCase()); if (!definition) continue;
    const q = item.attributes;
    const deltas = (definition.objectives || []).map(o => {
      let delta = 0;
      if (o.stats?.length) {
        for (const s of stats) if (o.stats.some(row => statMatches(row, s, season))) delta += s.n;
        // Match results (play / outlive / top N / win) are not native stat events on these servers.
        if (event.kind === 'match' || event.kind === 'win') delta += matchConditionDelta(o, event, matchKills) ?? fallbackDelta(o.name, event);
        delta += weaponKillDelta(o, event);
      }
      else delta = fallbackDelta(o.name, event);
      return delta;
    });
    if (!deltas.some(d => d > 0)) continue;
    if (definition.singleMatch && q.tilted_match !== match && !questComplete(definition, q))
      for (const o of definition.objectives) q['completion_' + o.name] = 0;
    // "Once per match" counts one step per match for small counters (visit/land in different
    // matches); amount goals such as "deal 100 damage" keep their full amount.
    const once = definition.oncePerMatch && definition.objectives.every(o => o.count <= 10);
    if (once && q.tilted_once_match === match) continue;
    let changed = false;
    definition.objectives.forEach((o, i) => {
      if (!deltas[i]) return;
      const key = 'completion_' + o.name, old = Number(q[key]) || 0;
      const next = Math.min(o.count, old + (once ? Math.min(deltas[i], 1) : deltas[i]));
      if (next !== old) { q[key] = next; changed = true; }
    });
    q.tilted_match = match;
    if (!changed) continue;
    if (once) q.tilted_once_match = match;
    q.last_state_change_time = new Date().toISOString();
    if (questComplete(definition, q)) {
      q.quest_state = 'Completed';
      const given = grantRewards(profile, definition); stars += given.stars; xp += given.xp; q.tilted_rewarded = 2;
      if (q.tilted_daily) a.tilted_quest_dailies_completed = (Number(a.tilted_quest_dailies_completed) || 0) + 1;
    }
  }
  if (!dailyOnly) ensureSeasonChallenges(profile, season);
  return {stars, xp: Math.min(xp, 1000000)};
}

// --- Daily challenges (every season whose build has them) ---------------------------------------
// The game server exports the build's own daily quests ("daily" in season-N.json). Each day, finished
// dailies are cleared and new ones are added until three are active (one reroll a day). They progress
// from the same stat events as weekly challenges and give their own rewards.
const dayNumber = now => Math.floor(now.getTime() / 86400000);
export function dailyList(season) {
  const data = loadChallengeData(season);
  return data ? (data.daily || []).filter(id => data.questMap.has(String(id).toLowerCase())) : [];
}
function newDaily(items, data, id, day, now) {
  const definition = data.questMap.get(String(id).toLowerCase());
  const key = 'tilted_daily_' + definition.id.toLowerCase() + '_' + day;
  const q = {quest_state: 'Active', creation_time: now.toISOString(), last_state_change_time: now.toISOString(), item_seen: false, level: -1, xp: 0, max_level_bonus: 0, sent_new_notification: false, favorite: false, quest_rarity: 'uncommon', tilted_daily: true};
  for (const o of definition.objectives || []) q['completion_' + o.name] = 0;
  items[key] = {templateId: 'Quest:' + definition.id, quantity: 1, attributes: q};
  return key;
}
export function ensureDailyChallenges(profile, season, now = new Date()) {
  const data = loadChallengeData(season); const list = dailyList(season); if (!data || !list.length) return false;
  profile.items ??= {}; const items = profile.items, a = profile.stats.attributes, before = JSON.stringify(items) + '|' + JSON.stringify(a.quest_manager);
  const inList = new Set(list.map(id => id.toLowerCase()));
  // Daily quests from before (random ids that never progressed): kept when this build has them, removed otherwise.
  for (const [key, item] of Object.entries(items)) {
    const id = String(item.templateId || '');
    if (!/^quest:athenadaily/i.test(id) || item.attributes?.tilted_daily) continue;
    if (inList.has(id.slice(6).toLowerCase()) && item.attributes?.quest_state === 'Active') { item.attributes.tilted_daily = true; continue; }
    delete items[key];
  }
  const day = dayNumber(now);
  const activeNow = () => Object.values(items).filter(i => i.attributes?.tilted_daily && i.attributes.quest_state === 'Active').length;
  const newDay = Number(a.tilted_quest_daily_day) !== day;
  // Fill dailies on a new day, OR whenever the player has none (self-heals a profile that got its day
  // stamped without ever receiving challenges, e.g. before this build had the season's challenge data).
  if (newDay || activeNow() === 0) {
    if (newDay) for (const [key, item] of Object.entries(items)) if (item.attributes?.tilted_daily && done(item)) delete items[key];
    const active = () => Object.values(items).filter(i => i.attributes?.tilted_daily && i.attributes.quest_state === 'Active');
    const offset = day % list.length;
    for (let i = 0; i < list.length && active().length < 3; i++) {
      const id = list[(offset * 3 + i) % list.length];
      if (active().some(q => q.templateId.slice(6).toLowerCase() === id.toLowerCase())) continue;
      newDaily(items, data, id, day, now);
    }
    a.tilted_quest_daily_day = day;
    a.quest_manager = {...(a.quest_manager || {}), dailyLoginInterval: now.toISOString(), dailyQuestRerolls: 1};
  }
  return before !== JSON.stringify(items) + '|' + JSON.stringify(a.quest_manager);
}
// Swaps one active daily for another the player does not have (one reroll a day).
export function rerollDailyChallenge(profile, season, questItemId, now = new Date()) {
  const data = loadChallengeData(season); const list = dailyList(season);
  const items = profile.items || {}, a = profile.stats.attributes, old = items[questItemId];
  if (!data || !list.length || !old?.attributes?.tilted_daily || old.attributes.quest_state !== 'Active') throw new Error('That is not an active daily challenge.');
  const manager = a.quest_manager ??= {};
  if ((Number(manager.dailyQuestRerolls) || 0) <= 0) throw new Error('No rerolls left today.');
  const owned = new Set(Object.values(items).filter(i => i.attributes?.tilted_daily && i.attributes.quest_state === 'Active').map(i => i.templateId.slice(6).toLowerCase()));
  const choices = list.filter(id => !owned.has(id.toLowerCase())); if (!choices.length) throw new Error('No other daily challenge to swap in.');
  const pick = choices[Math.floor(Math.random() * choices.length)];
  delete items[questItemId];
  const key = newDaily(items, data, pick, dayNumber(now) + '_r' + Date.now().toString(36), now);
  manager.dailyQuestRerolls = (Number(manager.dailyQuestRerolls) || 0) - 1;
  return key;
}
