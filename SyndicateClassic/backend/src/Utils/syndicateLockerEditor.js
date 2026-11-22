import fs from 'node:fs';
import {cosmeticsFor, stylesFor, grantAllStyles} from './syndicateUnlockAll.js';
import {unequip} from './syndicateSeasonReset.js';

// Launcher cosmetics editor (ViewModels/MainViewModel.Cosmetics.cs): every cosmetic a season's game files have, with
// its name and type, and which ones the player owns. Locker edits add or remove items (outfits get all their styles);
// default items stay, and a removed item that was equipped is unequipped.
const names = JSON.parse(fs.readFileSync(new URL('../local/syndicate-cosmetic-names.json', import.meta.url), 'utf8'));
const TYPES = {
  AthenaCharacter: 'Outfit', AthenaBackpack: 'Back bling', AthenaPickaxe: 'Pickaxe', AthenaGlider: 'Glider',
  AthenaDance: 'Emote', AthenaSkyDiveContrail: 'Contrail', AthenaItemWrap: 'Wrap', AthenaLoadingScreen: 'Loading screen',
  AthenaMusicPack: 'Music', AthenaSpray: 'Spray', AthenaEmoji: 'Emoticon', AthenaToy: 'Toy', AthenaPetCarrier: 'Pet',
};
const PROTECTED = /default|eid_dancemoves|eid_boogiedown$/i;
export function describe(id) {
  const [type, rest = ''] = String(id).split(':');
  return {id, name: names[rest.toLowerCase()] || rest, type: TYPES[type] || type.replace(/^Athena/, '')};
}
export function catalogFor(season) {
  const seen = new Set();
  return cosmeticsFor(season).filter(id => { const k = id.toLowerCase(); if (seen.has(k)) return false; seen.add(k); return true; }).map(describe);
}
export function ownedIds(athena) {
  return Object.values(athena?.items || {}).map(i => i?.templateId).filter(id => typeof id === 'string' && /^Athena/i.test(id)).map(id => id.toLowerCase());
}
export function editLocker(athena, season, {add = [], remove = []} = {}) {
  athena.items ??= {};
  const known = new Map(cosmeticsFor(season).map(id => [id.toLowerCase(), id]));
  const owned = new Map(Object.entries(athena.items).filter(([, i]) => typeof i?.templateId === 'string').map(([key, i]) => [i.templateId.toLowerCase(), key]));
  const styles = stylesFor(season);
  let added = 0, removed = 0;
  for (const raw of add) {
    const key = String(raw).toLowerCase(), id = known.get(key);
    if (!id || owned.has(key)) continue;
    const item = {templateId: id, attributes: {max_level_bonus: 0, level: 1, item_seen: true, xp: 0, variants: [], favorite: false}, quantity: 1};
    const channels = styles.get(key);
    if (channels) { grantAllStyles(item, channels); delete item.attributes.tilted_variants_before; }
    athena.items[id] = item; owned.set(key, id); added++;
  }
  const goneKeys = new Set(), goneIds = new Set();
  for (const raw of remove) {
    const key = String(raw).toLowerCase(), itemKey = owned.get(key);
    if (!itemKey || PROTECTED.test(key)) continue;
    delete athena.items[itemKey]; owned.delete(key); goneKeys.add(itemKey.toLowerCase()); goneIds.add(key); removed++;
  }
  if (removed) unequip(athena, v => typeof v === 'string' && (goneKeys.has(v.toLowerCase()) || goneIds.has(v.toLowerCase())));
  return {added, removed};
}
