import test from 'node:test';
import assert from 'node:assert/strict';
import {VBUCKS_PACKS,LEGACY_VBUCKS_PACKS,packsForBuild,packsWith,vbucksStorefront,vbucksBulkOffers,vbucksPrices,packFor,isLoopback,noteCheckoutAccount,localAccount} from '../syndicateVbucksStore.js';

test('the STORE tab sells the four Chapter 1 V-Bucks packs at their real prices', () => {
  const store = vbucksStorefront('10.40');
  assert.equal(store.name, 'CurrencyStorefront');
  assert.deepEqual(VBUCKS_PACKS.map(p => [p.quantity, p.cents]), [[1000, 999], [2800, 2499], [5000, 3999], [13500, 9999]]);
  for (const entry of store.catalogEntries) {
    assert.equal(entry.prices[0].currencyType, 'RealMoney');
    assert.match(entry.appStoreId[1], /^app-SyndicateVbucks\d+$/); // Syndicate's own ids, never Epic's
    assert.ok(entry.metaInfo.some(m => m.key === 'MtxQuantity'));
    assert.deepEqual(entry.itemGrants, []);
  }
});

test('Seasons 1 to 6 sell the 7,500 pack in place of the 5,000 pack (their files only picture that one)', () => {
  assert.deepEqual(LEGACY_VBUCKS_PACKS.map(p => [p.quantity, p.bonus, p.cents]), [[1000, 0, 999], [2800, 300, 2499], [7500, 1500, 5999], [13500, 3500, 9999]]);
  for (const build of ['1.8.2', '2.5', '3.5.1', '4.5', '5.10', '6.20']) assert.equal(packsForBuild(build), LEGACY_VBUCKS_PACKS, build);
  for (const build of ['7.40', '8.50', '10.40', '12.50', '', undefined]) assert.equal(packsForBuild(build), VBUCKS_PACKS, String(build));
  const old = vbucksStorefront('3.5.1').catalogEntries.map(e => e.appStoreId[1]);
  assert.deepEqual(old, ['app-SyndicateVbucks1000', 'app-SyndicateVbucks2800', 'app-SyndicateVbucks7500', 'app-SyndicateVbucks13500']);
  assert.equal(packFor('app-SyndicateVbucks7500').cents, 5999); // still found when a newer season asks
  assert.equal(packsWith(packFor('app-SyndicateVbucks7500')), LEGACY_VBUCKS_PACKS);
  assert.equal(packsWith(packFor('app-SyndicateVbucks1000')), VBUCKS_PACKS);
});

test('prices are given the way the game reads them (unitPrice in cents), never as -0.01', () => {
  const prices = vbucksPrices({lineOffers: [{offerId: 'app-SyndicateVbucks1000', quantity: 1}, {offerId: 'app-SyndicateVbucks7500', quantity: 2}]});
  assert.deepEqual(prices.lineOffers.map(o => [o.offerId, o.price.currencyCode, o.price.unitPrice, o.price.originalUnitPrice, o.price.discount]),
    [['app-SyndicateVbucks1000', 'USD', 999, 999, 0], ['app-SyndicateVbucks7500', 'USD', 5999, 5999, 0]]);
  assert.deepEqual(prices.lineOffers[0].appliedRules, []);
});

test('the catalog and price services answer for the packs only', () => {
  const offers = vbucksBulkOffers(['app-SyndicateVbucks2800', 'something-else']);
  assert.deepEqual(Object.keys(offers), ['app-SyndicateVbucks2800']);
  assert.equal(offers['app-SyndicateVbucks2800'].currentPrice, 2499);
  const prices = vbucksPrices({lineOffers: [{offerId: 'app-SyndicateVbucks13500', quantity: 1}, {offerId: 'other', quantity: 1}]});
  assert.equal(prices.lineOffers.length, 1);
  assert.equal(prices.totalPrice.discountPrice, 9999);
  assert.equal(vbucksPrices({lineOffers: [{offerId: 'other'}]}), null);
  assert.equal(vbucksPrices({lineOffers: [{offerId: 'other'}]}, 499).totalPrice.discountPrice, 499); // dev placeholder
});

test('packs are found by store id or catalog offer id', () => {
  assert.equal(packFor('APP-SYNDICATEVBUCKS1000').quantity, 1000);
  assert.equal(packFor(VBUCKS_PACKS[2].offerId).quantity, 5000);
  assert.equal(packFor('nope'), null);
});

test('only this PC is remembered as the buyer', () => {
  assert.equal(isLoopback('::ffff:127.0.0.1'), true);
  assert.equal(isLoopback('192.168.1.20'), false);
  noteCheckoutAccount('192.168.1.20', 'guest');
  noteCheckoutAccount('127.0.0.1', 'me');
  assert.equal(localAccount(), 'me');
});
