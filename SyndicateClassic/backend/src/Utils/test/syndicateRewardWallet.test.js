import test from 'node:test';
import assert from 'node:assert/strict';
import {rewardCentsFor,rewardBalance,addReward,spendReward,formatDollars,REWARD_KEY} from '../syndicateRewardWallet.js';
import {vbucksAwards} from '../syndicateVictoryRewards.js';

test('matches pay $5 a kill and $10 a win in Syndicate Rewards, and no V-Bucks', () => {
  assert.equal(rewardCentsFor.kill, 500);
  assert.equal(rewardCentsFor.win, 1000);
  assert.equal(vbucksAwards.kill, 0);
  assert.equal(vbucksAwards.win, 0);
});

test('rewards add up and pay for packs only when there is enough', () => {
  const core = {items: {}};
  assert.equal(rewardBalance(core), 0);
  addReward(core, rewardCentsFor.kill); addReward(core, rewardCentsFor.kill); addReward(core, rewardCentsFor.win);
  assert.equal(core.stats.attributes[REWARD_KEY], 2000);
  assert.equal(formatDollars(rewardBalance(core)), '$20.00');
  assert.equal(spendReward(core, 2499), false);
  assert.equal(rewardBalance(core), 2000);
  assert.equal(spendReward(core, 999), true);
  assert.equal(formatDollars(rewardBalance(core)), '$10.01');
  assert.equal(formatDollars(1234567), '$12,345.67');
});
