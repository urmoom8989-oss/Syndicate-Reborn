import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import {activateSeason} from '../syndicateSeasonState.js';
import {ensureChallenges} from '../syndicateChallenges.js';
import {ensureSeasonChallenges} from '../syndicateSeasonChallenges.js';
import {applyProgression} from '../syndicateProgressionCore.js';
import {applyMaxProgress,restoreMaxProgress,seasonMaxed} from '../syndicateMaxProgress.js';

const defaults = JSON.parse(fs.readFileSync(new URL('../../Profiles/DefaultProfiles/athena.json', import.meta.url), 'utf8'));
const coreDefaults = JSON.parse(fs.readFileSync(new URL('../../Profiles/DefaultProfiles/common_core.json', import.meta.url), 'utf8'));
const strip = p => { const c = structuredClone(p); delete c.rvn; delete c.commandRevision; delete c.updated; return c; };
const quests = p => Object.values(p.items).filter(i => /^quest:/i.test(i.templateId));

// A Season 4 player part way through the pass, with some challenge progress.
function player() {
  const athena = structuredClone(defaults), common_core = structuredClone(coreDefaults);
  activateSeason(athena, 4);
  Object.assign(athena.stats.attributes, {level: 23, xp: 410, book_level: 17, book_xp: 6, book_purchased: true, tilted_season4_reward_tier: 17, tilted_season4_free_reward_tier: 17, tilted_season4_paid_reward_tier: 17});
  ensureChallenges(athena);
  const first = quests(athena).find(q => q.attributes.quest_state === 'Active');
  const key = Object.keys(first.attributes).find(k => k.startsWith('completion_')); first.attributes[key] = 1;
  return {athena, common_core};
}

test('Unlock all maxes the season and turning it off restores it exactly', () => {
  const profiles = player(), p = profiles.athena, before = strip(p);
  p.stats.attributes.tilted_unlock_all = true;
  assert.equal(applyMaxProgress(p, 4), true);
  const a = p.stats.attributes;
  assert.deepEqual([a.level, a.xp, a.book_level, a.book_xp, a.tilted_season4_paid_reward_tier], [100, 0, 100, 0, 100]);
  assert.ok(quests(p).length > quests(before).length, 'level-gated weeks are granted');
  assert.equal(quests(p).filter(q => q.attributes.quest_state === 'Active').length, 0);
  assert.ok(seasonMaxed(a, 4));
  // A match while maxed changes nothing and claims no tier reward.
  const items = Object.keys(p.items).length, vbucks = JSON.stringify(profiles.common_core.items);
  applyProgression(profiles, 5000);
  assert.deepEqual([a.level, a.xp, a.book_level, a.book_xp], [100, 0, 100, 0]);
  assert.equal(Object.keys(p.items).length, items); assert.equal(JSON.stringify(profiles.common_core.items), vbucks);
  // Applying again keeps the first snapshot.
  applyMaxProgress(p, 4);
  a.tilted_unlock_all = false;
  assert.equal(restoreMaxProgress(p), 1);
  delete p.stats.attributes.tilted_unlock_all;
  assert.deepEqual(strip(p), before);
});

test('every maxed season is restored, the active one and the stored ones', () => {
  const {athena: p} = player();
  ensureSeasonChallenges(p, 4);
  p.stats.attributes.tilted_unlock_all = true;
  applyMaxProgress(p, 4);
  activateSeason(p, 2); ensureSeasonChallenges(p, 2);
  const season2 = strip(p).stats.attributes;
  applyMaxProgress(p, 2);
  assert.equal(p.stats.attributes.book_level, 70);
  assert.equal(restoreMaxProgress(p), 2);
  assert.equal(p.stats.attributes.level, season2.level);
  assert.equal(p.stats.attributes.book_level, season2.book_level);
  const stored = p.stats.attributes.tilted_season_states['4'].attributes;
  assert.deepEqual([stored.level, stored.xp, stored.book_level, stored.book_xp], [23, 410, 17, 6]);
  assert.equal(p.stats.attributes.tilted_maxed_before, undefined);
});

test('a season with no challenges granted yet ends with all of them complete', () => {
  const p = structuredClone(defaults);
  p.stats.attributes.tilted_unlock_all = true; p.stats.attributes.season_num = 4;
  applyMaxProgress(p, 4);
  assert.ok(quests(p).length > 50);
  assert.equal(quests(p).filter(q => q.attributes.quest_state === 'Active').length, 0);
});

test('nothing is maxed while Unlock all is off', () => {
  const {athena: p} = player(), before = strip(p);
  assert.equal(applyMaxProgress(p, 4), false);
  assert.equal(restoreMaxProgress(p), 0);
  assert.deepEqual(strip(p), before);
});
