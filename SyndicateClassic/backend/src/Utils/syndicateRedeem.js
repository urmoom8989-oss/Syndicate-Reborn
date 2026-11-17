// Syndicate redeem codes (launcher Settings > Redeem code). Exclusive items are not in Unlock all, the shops or
// the full locker; a one-time code gives one to the signed-in player.
//
// Codes (TILT-ABCD-EFGH-JKLM-NPQR) are made by the Syndicate Discord bot (/code create, admins only)
// and checked by the Cloudflare Worker (relay/worker.js, POST /redeem). The Worker marks a code used the
// first time anyone redeems it, so it works once for everyone, on any PC. If the item cannot be added
// here afterwards, the code is given back (POST /redeem/release) so it is not lost.
import {randomUUID} from 'node:crypto';
import Profile from '../User/Mongodb/Schema/profiles.js';
import User from '../User/Mongodb/Schema/user.js';
import {withAccountLock} from './syndicateAccountLock.js';
import {addRewardGift} from './syndicatePurchaseGift.js';
import {ensureSharedWallet, walletId} from './syndicateShopWallet.js';
import {REWARDS} from './syndicateRedeemItems.js';
export {REWARDS, REDEEM_ONLY, redeemedItems} from './syndicateRedeemItems.js';

// Redeem codes need your own relay: set TILTED_REDEEM_URL. Empty = redeem codes are off.
const SERVER = (process.env.TILTED_REDEEM_URL || '').replace(/\/+$/, '');

export function normalizeCode(code) {
  let body = String(code || '').toUpperCase().replace(/[^A-Z0-9]/g, '');
  if (body.startsWith('TILT')) body = body.slice(4);
  return body;
}

async function post(path, data) {
  const controller = new AbortController(); const timer = setTimeout(() => controller.abort(), 15000);
  try {
    const resp = await fetch(SERVER + path, {method: 'POST', headers: {'content-type': 'application/json'}, body: JSON.stringify(data), signal: controller.signal});
    let body = null; try { body = await resp.json(); } catch {}
    return {status: resp.status, body};
  } catch { return {status: 0, body: null}; }
  finally { clearTimeout(timer); }
}

export async function redeem(accountId, code) {
  const body = normalizeCode(code);
  if (body.length !== 16) throw new Error('That is not a valid code. Codes look like TILT-ABCD-EFGH-JKLM-NPQR.');
  return withAccountLock(accountId, async () => {
    const user = await User.findOne({accountId}).lean();
    // Ask the code server. It answers once with the reward; every later try for that code is refused.
    const answer = await post('/redeem', {code: body, accountId, username: user?.username || ''});
    if (answer.status === 0) throw new Error('Could not reach the Syndicate code server. Check your internet connection and try again.');
    if (!answer.body?.ok) throw new Error(answer.body?.message || 'That code is not valid.');
    const rewardId = answer.body.reward, reward = REWARDS[rewardId];
    const giveBack = () => post('/redeem/release', {code: body, claim: answer.body.claim});
    // V-Bucks code: the relay sends reward like "vbucks:1000". Credit the shared wallet instead of a locker item.
    // Each code is still one-time (the relay enforces that), so we do NOT dedupe on syndicateRedeemed - different
    // V-Bucks codes stack. On any failure the code is released so it is not lost.
    const vb = /^vbucks:(\d+)$/i.exec(rewardId || '');
    if (vb) {
      const amount = parseInt(vb[1], 10);
      if (!Number.isSafeInteger(amount) || amount <= 0 || amount > 1000000) { await giveBack(); throw new Error('That V-Bucks code is not valid.'); }
      const title = answer.body.title || (amount.toLocaleString() + ' V-Bucks');
      try {
        for (let attempt = 0; attempt < 6; attempt++) {
          const doc = await Profile.collection.findOne({accountId});
          if (!doc?.profiles?.common_core) throw new Error('Account profile is missing.');
          const c = doc.profiles.common_core, cr = c.rvn;
          ensureSharedWallet(c);
          if (!Number.isSafeInteger(c.items[walletId].quantity + amount)) throw new Error('V-Bucks balance would overflow.');
          c.items[walletId].quantity += amount;
          c.rvn = (c.rvn || 0) + 1; c.commandRevision = (c.commandRevision || 0) + 1; c.updated = new Date().toISOString();
          const saved = await Profile.collection.updateOne({accountId, 'profiles.common_core.rvn': cr}, {$set: {'profiles.common_core': c}});
          if (saved.modifiedCount) return {reward: rewardId, title, message: title + ' added to your account. Restart Fortnite\'s lobby to see your new balance.'};
        }
        throw new Error('Your account is busy. Try again in a moment.');
      } catch (error) { await giveBack(); throw error; }
    }
    if (!reward) { await giveBack(); throw new Error('This launcher does not know that item yet. Update Syndicate, then redeem the code again.'); }
    try {
      for (let attempt = 0; attempt < 6; attempt++) {
        const doc = await Profile.collection.findOne({accountId});
        if (!doc?.profiles?.athena || !doc.profiles.common_core) throw new Error('Account profile is missing.');
        const a = doc.profiles.athena, c = doc.profiles.common_core, ar = a.rvn, cr = c.rvn;
        a.items ??= {};
        if (doc.syndicateRedeemed?.[rewardId]) throw new Error('This player already has ' + reward.title + '. The code was not used, so you can give it to someone else.');
        const loot = [];
        for (const id of reward.items) {
          const existing = Object.entries(a.items).find(([, i]) => String(i?.templateId || '').toLowerCase() === id.toLowerCase());
          if (existing) { delete existing[1].attributes?.tilted_unlock_all; continue; } // keep it when Unlock all is turned off
          const guid = randomUUID();
          a.items[guid] = {templateId: id, quantity: 1, attributes: {item_seen: false, level: 1, xp: 0, variants: [], favorite: false}};
          loot.push({itemType: id, itemGuid: guid, itemProfile: 'athena', quantity: 1});
        }
        addRewardGift(c, loot, 'Project Tilted', reward.title + ' is in your locker.');
        for (const p of [a, c]) { p.rvn = (p.rvn || 0) + 1; p.commandRevision = (p.commandRevision || 0) + 1; p.updated = new Date().toISOString(); }
        const saved = await Profile.collection.updateOne({accountId, 'profiles.athena.rvn': ar, 'profiles.common_core.rvn': cr},
          {$set: {'profiles.athena': a, 'profiles.common_core': c, ['syndicateRedeemed.' + rewardId]: true}});
        if (saved.modifiedCount) {
          if (global.giftReceived) global.giftReceived[accountId] = true;
          return {reward: rewardId, title: reward.title, message: reward.title + ' is in your locker. Restart Fortnite\'s lobby to see it.' + (reward.note ? ' ' + reward.note : '')};
        }
      }
      throw new Error('Your locker is busy. Try again in a moment.');
    } catch (error) {
      await giveBack(); // not added here: the code stays usable
      throw error;
    }
  });
}
