// node --test src/Utils/test/syndicateSeasonReset.test.js
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { passItems, resetView, applySeasonReset, restoreRemovedPassItems, removedPassItems } from '../syndicateSeasonReset.js';
import { activateSeason, captureSeason } from '../syndicateSeasonState.js';
import { applyProgression, grantMissingPassCosmetics } from '../syndicateProgressionCore.js';

const S7_TIER1 = passItems(7).filter(i => i.tier === 1 && i.track === 'paid' && /^AthenaCharacter:/i.test(i.templateId));

function account({ season = 7, level = 40, tier = 60, unlockAll = false } = {}) {
  const key = 'tilted_season' + season + '_';
  const athena = { rvn: 1, items: {}, stats: { attributes: {
    tilted_active_season: season, season_num: season, level, xp: 10, book_level: tier, book_xp: 3, book_purchased: true,
    [key + 'free_reward_tier']: tier, [key + 'paid_reward_tier']: tier, tilted_unlock_all: unlockAll,
    favorite_character: '', favorite_dance: ['', '', '', '', '', ''], favorite_itemwraps: [],
  } } };
  athena.stats.attributes.tilted_season_states = { '4': { attributes: { level: 12, book_level: 20, book_purchased: true }, items: {} } };
  // Every pass cosmetic of the reached tiers, as applyProgression would have granted them.
  for (const item of passItems(season)) if (item.tier <= tier)
    athena.items['tilted_s' + season + '_' + item.track + '_' + item.tier + '_' + item.templateId] = { templateId: item.templateId, quantity: 1, attributes: { variants: [] } };
  athena.items['quest-1'] = { templateId: 'Quest:some_season7_quest', quantity: 1, attributes: { quest_state: 'Completed' } };
  const common_core = { rvn: 1, items: { 'Currency:MtxPurchased': { templateId: 'Currency:MtxPurchased', quantity: 500, attributes: { platform: 'Shared' } } }, stats: { attributes: {} } };
  return { accountId: 'a', profiles: { athena, common_core } };
}
const owns = (doc, id) => Object.values(doc.profiles.athena.items).some(i => i.templateId.toLowerCase() === id.toLowerCase());

test('every reset season has pass cosmetics, at their first tier', () => {
  for (const season of [2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12]) assert.ok(passItems(season).length > 20, 'season ' + season);
  assert.ok(S7_TIER1.length >= 1);
  const ids = passItems(7).map(i => i.id);
  assert.equal(new Set(ids).size, ids.length);
});

test('removing picked items unequips them and keeps them out of the locker', () => {
  const doc = account();
  const outfit = S7_TIER1[0].templateId;
  const key = Object.keys(doc.profiles.athena.items).find(k => doc.profiles.athena.items[k].templateId === outfit);
  doc.profiles.athena.stats.attributes.favorite_character = key;
  const result = applySeasonReset(doc, 7, { remove: [outfit] });
  assert.equal(result.removed, 1);
  assert.equal(owns(doc, outfit), false);
  assert.equal(doc.profiles.athena.stats.attributes.favorite_character, '');
  assert.ok(removedPassItems(doc.profiles.athena.stats.attributes, 7).has(outfit.toLowerCase()));
  // The launch-time top-up must not put it back.
  assert.equal(grantMissingPassCosmetics(doc.profiles.athena), 0);
  assert.equal(owns(doc, outfit), false);
  // Put back returns it (tier 1 is reached).
  assert.equal(restoreRemovedPassItems(doc.profiles, 7), 1);
  assert.equal(owns(doc, outfit), true);
  assert.equal(removedPassItems(doc.profiles.athena.stats.attributes, 7).size, 0);
});

test('a progress reset starts the season over and pass V-Bucks are not paid twice', () => {
  const doc = account({ tier: 100 });
  const before = doc.profiles.common_core.items['Currency:MtxPurchased'].quantity;
  const all = resetView(doc, 7).selected.items.filter(i => i.owned).map(i => i.templateId);
  const result = applySeasonReset(doc, 7, { resetProgress: true, remove: all });
  const a = doc.profiles.athena.stats.attributes;
  assert.equal(result.reset, true);
  assert.equal(a.book_level, 1);
  assert.equal(a.level, 0);
  assert.equal(a.tilted_season7_paid_reward_tier, undefined);
  assert.equal(a.tilted_season7_paid_currency_through, 100);
  assert.equal(owns(doc, 'Quest:some_season7_quest'), false);
  assert.equal(resetView(doc, 7).selected.owned, 0);
  assert.equal(removedPassItems(a, 7).size, 0);
  // Earn tiers again: cosmetics come back, V-Bucks do not.
  applyProgression(doc.profiles, 400000);
  assert.ok(a.book_level > 1);
  assert.equal(owns(doc, S7_TIER1[0].templateId), true);
  assert.equal(doc.profiles.common_core.items['Currency:MtxPurchased'].quantity, before);
});

test('resetting a season that is not active only replaces its saved state', () => {
  const doc = account();
  applySeasonReset(doc, 4, { resetProgress: true });
  const a = doc.profiles.athena.stats.attributes;
  assert.equal(a.tilted_season_states['4'].attributes.book_level, 1);
  assert.equal(a.book_level, 60); // Season 7 untouched
  activateSeason(doc.profiles.athena, 4);
  assert.equal(a.book_level, 1);
  assert.equal(a.tilted_season_states['7'].attributes.book_level, 60);
});

test('complete pass purchases are refunded and removed on reset', () => {
  const doc = account();
  doc.syndicatePassPurchases = { season7: { price: 15800 } };
  const result = applySeasonReset(doc, 7, { resetProgress: true });
  assert.equal(result.refund, 15800);
  assert.deepEqual(result.unsetPurchases, ['season7']);
  assert.equal(doc.profiles.common_core.items['Currency:MtxPurchased'].quantity, 500 + 15800);
});

test('Unlock All and empty requests are refused', () => {
  assert.throws(() => applySeasonReset(account({ unlockAll: true }), 7, { resetProgress: true }), /Progression/);
  assert.throws(() => applySeasonReset(account(), 7, { remove: ['AthenaCharacter:not_a_pass_item'] }), /Nothing to change/);
  assert.throws(() => applySeasonReset(account(), 1, { resetProgress: true }), /cannot be reset/);
});

test('Season 2 knight shields leave with their outfit', () => {
  const doc = account({ season: 2, tier: 70 });
  const knight = passItems(2).find(i => /cid_035_athena_commando_m_medieval/i.test(i.templateId));
  assert.ok(knight, 'Black Knight is in the Season 2 pass');
  doc.profiles.athena.items.shield = { templateId: 'AthenaBackpack:bid_004_blackknight', quantity: 1, attributes: {} };
  applySeasonReset(doc, 2, { remove: [knight.templateId] });
  assert.equal(owns(doc, 'AthenaBackpack:bid_004_blackknight'), false);
  assert.ok(captureSeason(doc.profiles.athena));
});
