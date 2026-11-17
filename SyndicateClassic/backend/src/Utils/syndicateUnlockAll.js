// Syndicate: "Unlock all cosmetics" launcher option.
// Source list: the exact cosmetics of a build, once its game server has exported them to
// Runtime/Server/cosmetics/season-N.json; until then the bundled list in
// src/local/Cosmetics/all-cosmetics.json (items a build does not have are ignored by the game).
// While the option is on, every listed cosmetic the player does not already own is added to
// their locker, marked tilted_unlock_all. Turning it off removes only those marked items, so
// cosmetics the player owned before are kept.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {REDEEM_ONLY} from './syndicateRedeemItems.js';
import {DEFAULT_ITEM} from './syndicateSeasonState.js';
import {UNRELEASED_UMBRELLA} from './syndicateVictoryRewards.js';

const folder = fileURLToPath(new URL('../../../Runtime/Server/cosmetics/', import.meta.url));
const bundledFile = fileURLToPath(new URL('../local/Cosmetics/all-cosmetics.json', import.meta.url));
let cache = {stamp: '', items: []}, bundledCache = null;
const typeOk = id => /^Athena(Character|Backpack|Pickaxe|Glider|Dance|ItemWrap|SkyDiveContrail|LoadingScreen|MusicPack):[A-Za-z0-9_.-]{3,120}$/.test(id);

function readList(file) {
  try { const data = JSON.parse(fs.readFileSync(file, 'utf8')); return (Array.isArray(data.items) ? data.items : []).filter(id => typeof id === 'string' && typeOk(id)); } catch { return []; }
}
export function bundledCosmetics() { return bundledCache ??= readList(bundledFile); }
// The list used for one season: that build's own export when present, otherwise the bundled list.
export function cosmeticsFor(season) {
  const own = Number.isInteger(season) ? readList(path.join(folder, 'season-' + season + '.json')) : [];
  return own.length ? own : bundledCosmetics();
}
// Only the build's own export (empty until a match has run on it).
export function ownCosmetics(season) {
  return Number.isInteger(season) ? readList(path.join(folder, 'season-' + season + '.json')) : [];
}
export function exportedCosmetics() {
  let files = [];
  try { files = fs.readdirSync(folder).filter(n => /^season-\d+\.json$/.test(n)).sort(); } catch { return []; }
  const stamp = files.map(n => { try { return n + fs.statSync(path.join(folder, n)).mtimeMs; } catch { return n; } }).join('|');
  if (stamp === cache.stamp) return cache.items;
  const seen = new Map();
  for (const name of files) {
    try {
      const data = JSON.parse(fs.readFileSync(path.join(folder, name), 'utf8'));
      for (const id of Array.isArray(data.items) ? data.items : []) if (typeof id === 'string' && typeOk(id) && !seen.has(id.toLowerCase())) seen.set(id.toLowerCase(), id);
    } catch {}
  }
  cache = {stamp, items: [...seen.values()]};
  return cache.items;
}

// Outfit styles of one build, exported by its game server (Runtime/Server/cosmetics/styles-season-N.json):
// {"styles":{"AthenaCharacter:CID_...":[{"channel":"Material","options":["Mat1","Mat2"]}]}}.
const styleCache = new Map();
export function stylesFor(season, dir = folder) {
  const file = path.join(dir, 'styles-season-' + season + '.json');
  let stamp = 0; try { stamp = fs.statSync(file).mtimeMs; } catch { return new Map(); }
  const cached = styleCache.get(file); if (cached?.stamp === stamp) return cached.map;
  const map = new Map();
  try {
    const data = JSON.parse(fs.readFileSync(file, 'utf8'));
    for (const [id, channels] of Object.entries(data.styles || {})) {
      if (!typeOk(id) || !Array.isArray(channels)) continue;
      const clean = channels.filter(c => /^[A-Za-z0-9_]{1,48}$/.test(c?.channel || '') && Array.isArray(c.options))
        .map(c => ({channel: c.channel, options: c.options.filter(o => typeof o === 'string' && /^[A-Za-z0-9_]+(?:\.[A-Za-z0-9_]+)*$/.test(o) && o.length <= 48).slice(0, 128)}))
        .filter(c => c.options.length);
      if (clean.length) map.set(id.toLowerCase(), clean);
    }
  } catch {}
  styleCache.set(file, {stamp, map});
  return map;
}

// Every style of an outfit becomes owned. For an outfit the player already had, its styles before
// the option was turned on are kept in tilted_variants_before so turning it off restores them.
export function grantAllStyles(item, channels) {
  const a = item.attributes ??= {};
  let changed = false;
  if (!a.tilted_unlock_all && a.tilted_variants_before === undefined) a.tilted_variants_before = JSON.parse(JSON.stringify(a.variants || []));
  a.variants = Array.isArray(a.variants) ? a.variants : [];
  for (const {channel, options} of channels) {
    let v = a.variants.find(x => x?.channel === channel);
    if (!v) { v = {channel, active: options[0], owned: []}; a.variants.push(v); changed = true; }
    if (!Array.isArray(v.owned)) { v.owned = []; changed = true; }
    for (const o of options) if (!v.owned.includes(o)) { v.owned.push(o); changed = true; }
    if (!v.owned.includes(v.active)) { v.active = options[0]; changed = true; }
  }
  if (!changed && !a.tilted_unlock_all && JSON.stringify(a.tilted_variants_before) === JSON.stringify(a.variants)) delete a.tilted_variants_before;
  return changed;
}

// Adds the missing cosmetics (and every outfit style) while the option is on. Returns how many
// items were added or got new styles.
// Styles "Unlock all" never gives: they were sold on their own (Purple Skull Trooper came only in the OG Halloween
// bundle, which the launcher shop still sells). Regular and green-glow Skull Trooper stay.
const withheldStyles = {'athenacharacter:cid_030_athena_commando_m_halloween': {ClothingColor: ['Mat1']}};

// Edit styles we always grant for certain outfits, even when that build's game server did not export them to
// styles-season-N.json. Without this, Unlock all gave the Ghoul Trooper but the pink OG edit style (ClothingColor
// Mat1) was not selectable. Matches the hardcoded pass-shop grant (syndicatePassShop.js) for the same outfit, and is
// harmless on builds that lack the variant (the client ignores an owned style it does not have).
const manualStyles = {
  'athenacharacter:cid_029_athena_commando_f_halloween': [{channel: 'ClothingColor', options: ['Mat0', 'Mat1', 'Mat2']}], // Ghoul Trooper (pink = Mat1)
};
// Combine the build's exported channels with any manual ones, unioning options per channel (no duplicates).
function mergeStyleChannels(exported, manual) {
  const byChannel = new Map();
  for (const c of [...(exported || []), ...(manual || [])]) {
    if (!c?.channel || !Array.isArray(c.options)) continue;
    const got = byChannel.get(c.channel) || {channel: c.channel, options: []};
    for (const o of c.options) if (!got.options.includes(o)) got.options.push(o);
    byChannel.set(c.channel, got);
  }
  return [...byChannel.values()];
}
function withholdStyles(item, keep) {
  const rule = withheldStyles[String(item?.templateId || '').toLowerCase()]; if (!rule || keep) return false;
  const a = item.attributes || {}; let changed = false;
  for (const [channel, hidden] of Object.entries(rule)) {
    const before = (Array.isArray(a.tilted_variants_before) ? a.tilted_variants_before : []).find(x => x?.channel === channel)?.owned || [];
    const v = (a.variants || []).find(x => x?.channel === channel); if (!v || !Array.isArray(v.owned)) continue;
    for (const style of hidden) {
      if (before.includes(style) || !v.owned.includes(style)) continue; // owned before Unlock all: keep it
      v.owned = v.owned.filter(o => o !== style); changed = true;
      if (v.active === style) v.active = v.owned[0] || 'Mat0';
    }
  }
  return changed;
}
// Not real Battle Royale cosmetics: NPC/henchman outfits and their emotes, the Creative mannequin, placeholders and
// test/dev items some builds export. Unlock all never adds them, and takes back any an older version added.
export const JUNK_ITEM = /:(?:[a-z]+_)?npc_|_npc(?:_|$)|placeholder|mannequin|_(?:test|debug|dev|tbd|dummy|proto)(?:_|$)|^[a-z]+:(?:cid|bid|eid|pickaxe_id|glider_id)_(?:test|debug|dev|tbd|random|null|dummy|proto)/i;
// Save the World cosmetics (hero backpacks, STW loading screens and music): only with the "Save the World skins" option.
export const STW_ITEM = /:(?:bid_stwhero|lsid_stw|musicpack_\w*stw)/i;
// Items only a redeem code gives (syndicateRedeem.js) are skipped, and taken back if an older Unlock all added
// them, unless this player redeemed them (`redeemed`: lower-case template ids).
export function applyUnlockAll(profile, season, purchases = {}, redeemed = new Set()) {
  const a = profile?.stats?.attributes; if (!a?.tilted_unlock_all) return 0;
  profile.items ??= {};
  let removed = 0;
  for (const [key, item] of Object.entries(profile.items)) {
    const id = String(item?.templateId || '').toLowerCase();
    if (item?.attributes?.tilted_unlock_all !== true) continue;
    const full = String(item?.templateId || '');
    if ((REDEEM_ONLY.has(id) && !redeemed.has(id)) || JUNK_ITEM.test(full) || (!a.tilted_stw_skins && STW_ITEM.test(full))) { delete profile.items[key]; removed++; unequip(profile, key, id); }
  }
  const owned = new Set(Object.values(profile.items).map(i => String(i.templateId || '').toLowerCase()));
  let added = 0;
  for (const id of cosmeticsFor(season)) {
    const lower = id.toLowerCase(); if (owned.has(lower) || (REDEEM_ONLY.has(lower) && !redeemed.has(lower))) continue;
    // Items the launcher's "Remove default outfits" / "Remove unreleased win umbrellas" strip right after: adding
    // them here changed the profile on every QueryProfile (add, then strip), so the locker reloaded every few
    // seconds and jumped back to the top of the list.
    if (JUNK_ITEM.test(id) || (!a.tilted_stw_skins && STW_ITEM.test(id))) continue;
    if ((a.tilted_hide_defaults && DEFAULT_ITEM.test(id)) || (a.tilted_hide_win_umbrellas && UNRELEASED_UMBRELLA.test(id))) continue;
    let key = 'tilted_unlockall_' + lower; while (profile.items[key]) key += '_';
    profile.items[key] = {templateId: id, quantity: 1, attributes: {item_seen: true, level: 1, xp: 0, variants: [], favorite: false, tilted_unlock_all: true}};
    owned.add(lower); added++;
  }
  const styles = Number.isInteger(season) ? stylesFor(season) : new Map();
  // Run even when the build exported no styles file, so the manual edit styles (e.g. the Pink Ghoul Trooper) still apply.
  for (const item of Object.values(profile.items)) {
    const id = String(item?.templateId || '').toLowerCase();
    const channels = mergeStyleChannels(styles.get(id), manualStyles[id]);
    if (channels.length && grantAllStyles(item, channels)) added++;
    if (withholdStyles(item, !!purchases?.['x-og-halloween'])) added++;
  }
  return added + removed;
}
function unequip(profile, key, id) {
  const gone = v => typeof v === 'string' && (v.toLowerCase() === key.toLowerCase() || v.toLowerCase() === id);
  const a = profile.stats.attributes;
  for (const field of ['favorite_character', 'favorite_backpack', 'favorite_pickaxe', 'favorite_glider', 'favorite_skydivecontrail', 'favorite_loadingscreen', 'favorite_musicpack'])
    if (gone(a[field])) a[field] = '';
  for (const item of Object.values(profile.items)) {
    const slots = item?.attributes?.locker_slots_data?.slots; if (!slots) continue;
    for (const slot of Object.values(slots)) if (Array.isArray(slot?.items)) slot.items = slot.items.map(v => gone(v) ? '' : v);
  }
}

// Removes only the cosmetics the option added, and unequips them. Returns how many were removed.
export function removeUnlockAll(profile) {
  profile.items ??= {};
  const removedKeys = new Set(), removedIds = new Set();
  for (const [key, item] of Object.entries(profile.items))
    if (item?.attributes?.tilted_unlock_all === true) { removedKeys.add(key.toLowerCase()); removedIds.add(String(item.templateId).toLowerCase()); delete profile.items[key]; }
  // Outfits the player owned before: put their own styles back.
  let restored = 0;
  for (const item of Object.values(profile.items)) {
    const a = item?.attributes; if (!a || a.tilted_variants_before === undefined) continue;
    a.variants = Array.isArray(a.tilted_variants_before) ? a.tilted_variants_before : [];
    delete a.tilted_variants_before; restored++;
  }
  if (!removedKeys.size) return restored;
  const gone = v => typeof v === 'string' && (removedKeys.has(v.toLowerCase()) || removedIds.has(v.toLowerCase()));
  const a = profile.stats.attributes;
  for (const field of ['favorite_character', 'favorite_backpack', 'favorite_pickaxe', 'favorite_glider', 'favorite_skydivecontrail', 'favorite_loadingscreen', 'favorite_musicpack'])
    if (gone(a[field])) a[field] = '';
  for (const field of ['favorite_dance', 'favorite_itemwraps'])
    if (Array.isArray(a[field])) a[field] = a[field].map(v => gone(v) ? '' : v);
  for (const item of Object.values(profile.items)) {
    const slots = item?.attributes?.locker_slots_data?.slots; if (!slots) continue;
    for (const slot of Object.values(slots)) if (Array.isArray(slot?.items)) slot.items = slot.items.map(v => gone(v) ? '' : v);
  }
  return removedKeys.size + restored;
}
