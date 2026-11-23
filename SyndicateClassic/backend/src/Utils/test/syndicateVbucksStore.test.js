import test from 'node:test';
import assert from 'node:assert/strict';
import {VBUCKS_PACKS,vbucksStorefront,vbucksBulkOffers,vbucksPrices,packFor,isLoopback,noteCheckoutAccount,localAccount} from '../syndicateVbucksStore.js';

test('the STORE tab sells the four Chapter 1 V-Bucks packs at their real prices', () => {
  const store = vbucksStorefront();
  assert.equal(store.name, 'CurrencyStorefront');
  assert.deepEqual(VBUCKS_PACKS.map(p => [p.quantity, p.cents]), [[1000, 999], [2800, 2499], [5000, 3999], [13500, 9999]]);
  for (const entry of store.catalogEntries) {
    assert.equal(entry.prices[0].currencyType, 'RealMoney');
    assert.match(entry.appStoreId[1], /^app-SyndicateVbucks\d+$/); // Syndicate's own ids, never Epic's
    assert.ok(entry.metaInfo.some(m => m.key === 'MtxQuantity'));
    assert.deepEqual(entry.itemGrants, []);
  }
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
