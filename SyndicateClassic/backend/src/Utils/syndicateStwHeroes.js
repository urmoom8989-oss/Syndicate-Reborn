// Syndicate: Save the World heroes in Battle Royale. While the launcher's "Unlock all" is on, on Pre-season to
// Chapter 2 Season 1 (Season X 10.40 included), a player can pick an STW hero (stats.attributes.tilted_stw_hero = "HID_..."). Fortnite's lobby only lists Battle Royale outfits,
// so the pick is made in the launcher; syndicateLocker.js writes it to the match server's locker file (hero=HID_...)
// and the match server puts that hero's head and body on the player (Erbium RestoreSyndicateOutfit). A build
// that lacks the hero keeps the selected outfit. Turning Unlock all off clears the pick.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const folder = fileURLToPath(new URL('../../../Runtime/Server/cosmetics/', import.meta.url));
const bundledFile = fileURLToPath(new URL('../local/Cosmetics/stw-heroes.json', import.meta.url));
export const validHeroId = id => typeof id === 'string' && /^HID_[A-Za-z0-9_]{3,116}$/.test(id) && !/test|npc|_tbd|_dev_/i.test(id);
// One entry per look: the rarity/tier versions of a hero (_SR_T05, _R_T01...) share their parts.
const lookOf = id => id.replace(/_(C|UC|R|VR|SR)_T0\d$/i, '');
const rank = id => ['C', 'UC', 'R', 'VR', 'SR'].indexOf((id.match(/_(C|UC|R|VR|SR)_T0\d$/i) || [])[1]?.toUpperCase());

function readIds(file, pick) { try { return pick(JSON.parse(fs.readFileSync(file, 'utf8'))).filter(validHeroId); } catch { return []; } }
let cache = {stamp: '', list: []};
// The bundled 1.8.2 list plus every build's own export (heroes-season-N.json), one per look.
export function stwHeroes() {
  let files = [];
  try { files = fs.readdirSync(folder).filter(n => /^heroes-season-\d+\.json$/.test(n)).sort(); } catch {}
  const stamp = files.map(n => { try { return n + fs.statSync(path.join(folder, n)).mtimeMs; } catch { return n; } }).join('|');
  if (stamp === cache.stamp && cache.list.length) return cache.list;
  const ids = [...readIds(bundledFile, d => d.heroes || []), ...files.flatMap(n => readIds(path.join(folder, n), d => (d.heroes || []).map(h => h?.id)))];
  const best = new Map();
  for (const id of ids) { const look = lookOf(id).toLowerCase(), cur = best.get(look); if (!cur || rank(id) > rank(cur) || (rank(id) === rank(cur) && id > cur)) best.set(look, id); }
  cache = {stamp, list: [...best.values()].sort((a, b) => a.localeCompare(b)).map(id => ({id, name: heroName(id)}))};
  return cache.list;
}
// Save the World heroes come with Unlock all (Edwin, 2026-11-10: the Syndicate launcher has no separate switch).
// Pre-season to Chapter 2 Season 1 (11.00) since 2026-11-14: the client module registers the hero outfits on every
// one of those builds (SyndicateStwLocker.inl) and the match server puts the hero on the player on any build.
// tilted_active_season is the season the player last launched.
export const stwMaxSeason = 11;
const stwSeasonOk = attributes => { const s = Number(attributes?.tilted_active_season); return !Number.isFinite(s) || s <= stwMaxSeason; };
// Unlock all alone is Battle Royale cosmetics only; the heroes need the separate "Save the World skins" option too.
export const stwSkinsOn = attributes => !!attributes?.tilted_unlock_all && !!attributes?.tilted_stw_skins && stwSeasonOk(attributes);
// The heroes one old build's locker lists, matching what SyndicateClient.dll registers there: Season 2 (1.11) every hero
// of its own export (heroes-season-2.json), one per look; Pre-season and Season 1 (1.7.2 / 1.8.2) the bundled list.
// Season 3 (3.5) the same way from heroes-season-3.json.
// Season 4 to Chapter 2 Season 1 the same way from heroes-season-N.json (the match server writes it after a match).
const ownExport = {'1.11': 2, '2.5': 2, '3.5.1': 3, '4.5': 4, '5.10': 5, '6.20': 6, '7.40': 7, '8.50': 8, '9.40': 9, '10.31': 10, '10.40': 10, '11.00': 11};
// Pre-season (1.7.2) to Chapter 2 Season 1 (11.00), Season X 10.40 included: its own match server reads hero= from
// the locker file too (ServerAndConsole/PlayerLocker.h).
export const stwLockerBuilds = ['1.7.2', '1.8.2', '1.11', '2.5', '3.5.1', '4.5', '5.10', '6.20', '7.40', '8.50', '9.40', '10.31', '10.40', '11.00'];
export function stwHeroesForBuild(build) {
  const own = ownExport[build] ? readIds(path.join(folder, 'heroes-season-' + ownExport[build] + '.json'), d => (d.heroes || []).map(h => h?.id)) : [];
  if (!own.length) return [...bundledIds()].sort((a, b) => a.localeCompare(b)).map(id => ({id, name: heroName(id)}));
  const best = new Map();
  for (const id of own) { const look = lookOf(id).toLowerCase(), cur = best.get(look); if (!cur || rank(id) > rank(cur) || (rank(id) === rank(cur) && id > cur)) best.set(look, id); }
  return [...best.values()].sort((a, b) => a.localeCompare(b)).map(id => ({id, name: heroName(id)}));
}
let bundledCache = null;
const bundledIds = () => bundledCache ??= new Set(readIds(bundledFile, d => d.heroes || []));
// "HID_Commando_008_FoundersM_SR_T05" -> "Commando · 008 FoundersM".
export function heroName(id) {
  const [type, ...rest] = lookOf(id).replace(/^HID_/i, '').split('_');
  return rest.length ? type + ' · ' + rest.join(' ') : type;
}
// Pre-season to Season 2 (1.7.2-1.11) also list the heroes in Fortnite's locker: SyndicateClient.dll registers each
// hero as an outfit in those clients, and the backend adds them as profile items with these keys
// (syndicateLegacy111Profile.js). Equipping one saves the hero pick (Routes/Fortnite.js).
export const stwItemKey = id => 'tilted_stw_' + String(id).toLowerCase();
// The outfit's template name. Not the hero's own name: the game resolves "AthenaCharacter:HID_..." to the STW hero
// itself (not an outfit), and the locker hides it. SyndicateClient.dll registers this name (SyndicateStwLocker.inl).
export const stwTemplateName = id => 'SyndicateStw_' + id;
export function heroForItemKey(key) {
  if (typeof key !== 'string' || !key.startsWith('tilted_stw_')) return '';
  const lower = key.toLowerCase();
  return [...stwHeroes(), ...stwHeroesForBuild('1.8.2'), ...stwHeroesForBuild('1.11'), ...stwHeroesForBuild('3.5.1')].find(h => stwItemKey(h.id) === lower)?.id || '';
}
// The hero to put on the player in matches, or '' (Unlock all off, nothing picked, or not a hero id).
export function selectedHero(attributes) {
  return stwSkinsOn(attributes) && validHeroId(attributes.tilted_stw_hero) ? attributes.tilted_stw_hero : '';
}

// Season 3 and later read the normal athena profile, so the hero outfits are added to what it is sent (never saved):
// one locker outfit per hero while STW skins are on, and the picked hero shown as the equipped outfit (Season 7+ also
// in the cosmetic locker's Character slot).
export function withStwHeroes(profile, build) {
  if (!profile?.items || !stwSkinsOn(profile.stats?.attributes) || !stwLockerBuilds.includes(build)) return profile;
  const p = {...profile, items: {...profile.items}, stats: {...profile.stats, attributes: {...profile.stats.attributes}}};
  for (const hero of stwHeroesForBuild(build)) p.items[stwItemKey(hero.id)] = {templateId: 'AthenaCharacter:' + stwTemplateName(hero.id), quantity: 1, attributes: {item_seen: true, level: 1, xp: 0, variants: [], favorite: false}};
  const picked = selectedHero(p.stats.attributes);
  if (picked) {
    p.stats.attributes.favorite_character = stwItemKey(picked);
    for (const [key, item] of Object.entries(p.items)) {
      const slots = item?.attributes?.locker_slots_data?.slots;
      if (String(item?.templateId).toLowerCase() !== 'cosmeticlocker:cosmeticlocker_athena' || !slots?.Character) continue;
      const copy = JSON.parse(JSON.stringify(item)); copy.attributes.locker_slots_data.slots.Character.items = ['AthenaCharacter:' + stwTemplateName(picked)];
      p.items[key] = copy;
    }
  }
  return p;
}
// Season 7+ locker: "AthenaCharacter:SyndicateStw_HID_..." -> "HID_...", or ''. Also takes the name older client
// modules registered (TiltedStw_), so a slot saved by one of those still resolves.
export function heroForTemplate(template) {
  const m = /^AthenaCharacter:(?:SyndicateStw|TiltedStw)_(HID_[A-Za-z0-9_]+)$/i.exec(String(template || ''));
  return m && validHeroId(m[1]) ? m[1] : '';
}
