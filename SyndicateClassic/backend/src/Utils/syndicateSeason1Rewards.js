import {randomUUID} from 'node:crypto';

// Pre-season (1.7.2) and Season 1 (1.8.2) had no Battle Pass. Two of the Season Shop rewards were free and were
// handed out automatically on reaching a Season Level: the Battle Bus banner at level 10 and the Mako glider at
// level 25 (the paid, level-gated items are in syndicateSeason1Shop.js). Nothing granted the free two, so the panels
// stayed empty. This grants them from the account's current Season Level, once each.
const freeRewards = [
  {level: 10, id: 'HomebaseBannerIcon:brseason01'},   // Battle Bus banner
  {level: 25, id: 'AthenaGlider:Glider_Warthog'},     // Mako glider
];

// Grant any free Season 1 reward the player has reached and does not own yet. Returns the loot entries granted
// (for a CatalogPurchase-style notification), or an empty array when nothing changed.
export function grantSeason1FreeRewards(athena) {
  if (!athena?.stats?.attributes) return [];
  const level = Math.max(0, Number(athena.stats.attributes.level) || 0);
  athena.items ??= {};
  const owned = new Set(Object.values(athena.items).map(i => String(i?.templateId || '').toLowerCase()));
  const granted = [];
  for (const reward of freeRewards) {
    if (level < reward.level || owned.has(reward.id.toLowerCase())) continue;
    const guid = randomUUID();
    athena.items[guid] = {templateId: reward.id, quantity: 1, attributes: {item_seen: false, variants: []}};
    owned.add(reward.id.toLowerCase());
    granted.push({itemType: reward.id, itemGuid: guid, itemProfile: 'athena', quantity: 1});
  }
  return granted;
}
