// node --test src/Utils/test/syndicateAutoFriends.test.js
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { befriendConnected, joinablePresence, joinableResponse } from '../syndicateAutoFriends.js';

test('lobby presence is made joinable; matches and odd bodies are left alone', () => {
  const lobby = JSON.stringify({ Status: 'Battle Royale Lobby - 1 / 4', bIsPlaying: false, bIsJoinable: false, Properties: { FortPartySize_i: 1, 'party.joininfodata.286331153_j': { bIsPrivate: true, notAcceptingReason: 3 } } });
  const fixed = JSON.parse(joinablePresence(lobby));
  assert.equal(fixed.bIsJoinable, true);
  assert.equal(fixed.Properties['party.joininfodata.286331153_j'].bIsPrivate, false);
  assert.equal(fixed.Properties['party.joininfodata.286331153_j'].notAcceptingReason, 0);
  const playing = JSON.stringify({ bIsPlaying: true, bIsJoinable: false, Properties: {} });
  assert.equal(joinablePresence(playing), playing);
  const unjoinable = JSON.stringify({ bIsPlaying: false, bIsJoinable: false, Properties: { InUnjoinableMatch_b: true } });
  assert.equal(joinablePresence(unjoinable), unjoinable);
  assert.equal(joinablePresence('{}'), '{}');
  assert.equal(joinablePresence('not json {'), 'not json {');
});

test('only "party is private" refusals become joins', () => {
  const msg = (rejectionType) => JSON.stringify({ type: 'com.epicgames.party.queryjoinability.response', payload: { isJoinable: false, rejectionType, resultParam: '' } });
  const fixed = JSON.parse(joinableResponse(msg(5)));
  assert.equal(fixed.payload.isJoinable, true);
  assert.equal(fixed.payload.rejectionType, 0);
  assert.equal(joinableResponse(msg(2)), msg(2), 'a full party stays full');
  const other = JSON.stringify({ type: 'com.epicgames.party.memberjoined', payload: {} });
  assert.equal(joinableResponse(other), other);
});

test('players on the same backend become friends once, bots and blocked players are skipped', async () => {
  const lists = { A: { accepted: [], blocked: [] }, B: { accepted: [], blocked: [] }, C: { accepted: [], blocked: [{ accountId: 'A' }] } };
  const store = { findOne: ({ accountId }) => ({ lean: async () => ({ accountId, list: lists[accountId] }) }) };
  const calls = [];
  const api = {
    sendFriendReq: async (a, b) => { calls.push(['send', a, b]); },
    acceptFriendReq: async (from, to) => { calls.push(['accept', from, to]); lists[to].accepted.push({ accountId: from }); lists[from].accepted.push({ accountId: to }); },
  };
  const clients = [{ accountId: 'A' }, { accountId: 'B' }, { accountId: 'B' }, { accountId: 'C' }, { accountId: 'BOT', isPartyBot: true }];
  assert.deepEqual(await befriendConnected('A', clients, api, store), ['B']);
  assert.deepEqual(calls, [['send', 'A', 'B'], ['accept', 'B', 'A']]);
  assert.deepEqual(await befriendConnected('A', clients, api, store), [], 'already friends: nothing to do');
  process.env.TILTED_AUTO_FRIENDS = '0';
  assert.deepEqual(await befriendConnected('B', [{ accountId: 'Z' }], api, store), []);
  delete process.env.TILTED_AUTO_FRIENDS;
});
