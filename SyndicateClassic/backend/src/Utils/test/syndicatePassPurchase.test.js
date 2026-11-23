import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import {activateSeason,syncPassOwnership,unlockSeasonPass,passBought} from '../syndicateSeasonState.js';
import {purchasePass,PASS_PRICE,BUNDLE_PRICE} from '../syndicatePassPurchase.js';
import {tierCatalog,passOfferKind} from '../syndicateTierPurchase.js';
import {purchaseSeason2,season2PassOffer} from '../syndicateSeason2Pass.js';

const read = name => JSON.parse(fs.readFileSync(new URL(name, import.meta.url), 'utf8'));
const athenaDefaults = read('../../Profiles/DefaultProfiles/athena.json');
const coreDefaults = read('../../Profiles/DefaultProfiles/common_core.json');
const pass = season => read(`../../local/Battlepass/Jsons/Season${season}.json`);
const vbucks = c => c.items['Currency:MtxPurchased'].quantity;

function player(season, balance = 5000) {
  const athena = structuredClone(athenaDefaults), common_core = structuredClone(coreDefaults);
  for (const [id, item] of Object.entries(common_core.items)) if (/^currency:mtx/i.test(item.templateId || id)) delete common_core.items[id];
  common_core.items['Currency:MtxPurchased'] = {templateId: 'Currency:MtxPurchased', quantity: balance, attributes: {platform: 'Shared'}};
  activateSeason(athena, season);
  return {athena, common_core};
}

test('a season starts without the Battle Pass, and a free one from an old save is taken back', () => {
  const {athena} = player(8);
  assert.equal(syncPassOwnership(athena, 8), false);
  assert.equal(athena.stats.attributes.book_purchased, false);
  athena.stats.attributes.book_purchased = true;
  assert.equal(syncPassOwnership(athena, 8), true);
  assert.equal(athena.stats.attributes.book_purchased, false);
});

test('the Battle Pass storefront sells the pass, the bundle and tiers', () => {
  const p = pass(8), store = tierCatalog(8);
  assert.equal(store.name, 'BRSeason8');
  const price = id => store.catalogEntries.find(e => e.offerId === id)?.prices[0].finalPrice;
  assert.equal(price(p.battlePassOfferId), 950);
  assert.equal(price(p.battleBundleOfferId), 2800);
  assert.equal(price(p.tierOfferId), 150);
  assert.equal(passOfferKind(8, p.battlePassOfferId), 'pass');
  assert.equal(passOfferKind(8, p.battleBundleOfferId), 'bundle');
  assert.equal(passOfferKind(8, p.tierOfferId), null);
  for (const season of [3, 4, 5, 6, 7, 9, 10, 11, 12]) assert.equal(tierCatalog(season).catalogEntries.length, 3, 'Season ' + season);
});

test('buying the Battle Pass costs 950 V-Bucks and gives the paid rewards reached', () => {
  const profiles = player(8), a = profiles.athena.stats.attributes;
  a.book_level = 5;
  const owned = Object.keys(profiles.athena.items).length;
  assert.equal(purchasePass(profiles, 8, pass(8).battlePassOfferId, 1), 'pass');
  assert.equal(vbucks(profiles.common_core) >= 5000 - PASS_PRICE, true);
  assert.equal(a.book_purchased, true);
  assert.equal(passBought(profiles.athena, 8), true);
  assert.ok(Object.keys(profiles.athena.items).length > owned);
  assert.throws(() => purchasePass(profiles, 8, pass(8).battlePassOfferId, 1), /already own/);
  assert.equal(syncPassOwnership(profiles.athena, 8), false);
});

test('the Battle Bundle costs 2,800 V-Bucks and adds 25 tiers', () => {
  const profiles = player(10), a = profiles.athena.stats.attributes;
  a.book_level = 3;
  purchasePass(profiles, 10, pass(10).battleBundleOfferId, 1);
  assert.equal(a.book_purchased, true);
  assert.equal(a.book_level, 28);
  assert.ok(vbucks(profiles.common_core) <= 5000 - BUNDLE_PRICE + 2000);
});

test('without enough V-Bucks nothing is bought', () => {
  const profiles = player(8, 100);
  assert.throws(() => purchasePass(profiles, 8, pass(8).battlePassOfferId, 1), /Not enough V-Bucks/);
  assert.equal(profiles.athena.stats.attributes.book_purchased, false);
  assert.equal(passBought(profiles.athena, 8), false);
  assert.equal(vbucks(profiles.common_core), 100);
});

test('a bought pass stays owned across season switches, other seasons do not get it', () => {
  const profiles = player(8);
  purchasePass(profiles, 8, pass(8).battlePassOfferId, 1);
  activateSeason(profiles.athena, 9);
  syncPassOwnership(profiles.athena, 9);
  assert.equal(profiles.athena.stats.attributes.book_purchased, false);
  activateSeason(profiles.athena, 8);
  syncPassOwnership(profiles.athena, 8);
  assert.equal(profiles.athena.stats.attributes.book_purchased, true);
});

test('the launcher complete pass counts as bought', () => {
  const {athena} = player(7);
  unlockSeasonPass(athena, 7);
  assert.equal(passBought(athena, 7), true);
  assert.equal(syncPassOwnership(athena, 7), false);
  assert.equal(athena.stats.attributes.book_purchased, true);
});

test('the Season 2 Battle Pass costs 950 V-Bucks', () => {
  const profiles = player(2);
  purchaseSeason2(profiles, season2PassOffer, 1);
  assert.equal(profiles.athena.stats.attributes.book_purchased, true);
  assert.equal(passBought(profiles.athena, 2), true);
  assert.throws(() => purchaseSeason2(profiles, season2PassOffer, 1), /already own/);
  const poor = player(2, 500);
  assert.throws(() => purchaseSeason2(poor, season2PassOffer, 1), /Not enough V-Bucks/);
});
