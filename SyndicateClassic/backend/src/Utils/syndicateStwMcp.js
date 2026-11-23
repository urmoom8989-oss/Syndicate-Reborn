// Syndicate: the Save the World tabs. Runs LawinServer's Save the World profile operations (syndicateStwMcp.cjs:
// heroes and hero loadouts, survivor squads, schematics and the armory, research, upgrades, expeditions, the
// collection book, llamas, quest rewards and daily quests) against the account's profiles in MongoDB. Without
// them each of these answered "no changes" (stwDevNoOp), so nothing in those tabs did anything.
// Only Save the World profiles come here (campaign, theater0, outpost0, metadata, the collection books, and
// Pre-season's profile0), and the llama store's purchases; Battle Royale's never do.
import { createRequire } from 'module';
import Profile from '../User/Mongodb/Schema/profiles.js';
import { stwDevEnabled, stwDevUpdate, stwCampaignState } from './syndicateStwDev.js';
import { debitShopWallet, ensureSharedWallet } from './syndicateShopWallet.js';

const require = createRequire(import.meta.url);
const { ops } = require('./syndicateStwMcp.cjs');
const { lawinResponse } = require('./syndicateStwLawin.cjs');

const STW_ONLY = new Set(['campaign', 'theater0', 'outpost0', 'metadata', 'collection_book_people0', 'collection_book_schematics0', 'collections']);
// profile0 is also Battle Royale's on Pre-season to Season 2: its quest login stays Routes/Fortnite.js's (the
// Pre-season tutorial), the rest of these are Save the World's.
const PROFILE0_OWN = new Set(['ClientQuestLogin']);

// LawinServer's Save the World storefronts (llamas and event heroes), added to the catalog by stwStorefronts().
let stwOffers = null;
export function stwStorefronts() {
  return (lawinResponse('catalog.json').storefronts || []).filter(store => /^(cardpack|stw)/i.test(store?.name || ''));
}
const stwOffer = (offerId) => {
  try { stwOffers ??= new Set(stwStorefronts().flatMap(store => (store.catalogEntries || []).map(entry => entry.offerId))); }
  catch { return false; }
  return !!offerId && stwOffers.has(offerId);
};

// A purchase is Save the World's only for those storefronts (Battle Royale's shop stays Routes/Fortnite.js's).
export const stwOperation = (operation, profileId, body) => operation === 'PurchaseCatalogEntry'
  ? ['common_core', 'profile0'].includes(profileId) && stwOffer(body?.offerId)
  : !!ops[operation] && (STW_ONLY.has(profileId) || (profileId === 'profile0' && !PROFILE0_OWN.has(operation)));

function ensure(profile, id) {
  profile.profileId ??= id;
  profile.rvn ??= 0;
  profile.commandRevision ??= 0;
  profile.items ??= {};
  profile.stats ??= {};
  profile.stats.attributes ??= {};
  return profile;
}

const noChanges = (profile, id) => ({ profileRevision: profile?.rvn || 0, profileId: id, profileChangesBaseRevision: profile?.rvn || 0,
  profileChanges: [], profileCommandRevision: profile?.commandRevision || 0, serverTime: new Date().toISOString(), responseVersion: 1 });

// Express handler for /fortnite/api/game/v2/profile/*/client/:operation (Routes/Fortnite.js); anything else goes on.
export async function stwMcp(req, res, next) {
  const operation = req.params.operation, profileId = String(req.query.profileId || '');
  if (!stwDevEnabled(req.headers['user-agent']) || !stwOperation(operation, profileId, req.body)) return next();
  const accountId = req.user?.accountId;
  if (!accountId) return next();
  try {
    let doc = await Profile.collection.findOne({ accountId });
    if (!doc?.profiles) return next();
    // Save the World access, the tutorial start and the unlock after it (syndicateStwDev.js).
    const update = stwDevUpdate(doc, req.headers['user-agent'], { operation, profileId });
    if (update) {
      await Profile.collection.updateOne({ accountId }, { $set: update });
      doc = await Profile.collection.findOne({ accountId });
    }
    if (!doc.profiles[profileId]) return next();

    // During the tutorial the quest login hands out nothing (no daily quests before the tutorial is played).
    if (operation === 'ClientQuestLogin' && stwCampaignState(doc) === 'tutorial')
      return res.json({ ...noChanges(doc.profiles[profileId], profileId),
        profileChanges: [{ changeType: 'fullProfileUpdate', profile: doc.profiles[profileId] }] });

    // Llamas bought with V-Bucks come out of the shared wallet (syndicateShopWallet.js), as the item shop's do;
    // LawinServer's own subtraction is undone below. Not enough V-Bucks: no purchase.
    let price = 0, wallet = null;
    if (operation === 'PurchaseCatalogEntry') {
      req.body.currencySubType ??= ''; // LawinServer reads it; some builds leave it out
      const offer = stwStorefronts().flatMap(store => store.catalogEntries || []).find(entry => entry.offerId === req.body?.offerId);
      const cost = offer?.prices?.[0];
      if (cost && String(cost.currencyType).toLowerCase() === 'mtxcurrency')
        price = Math.max(0, Number(cost.finalPrice) || 0) * Math.min(100, Math.max(1, Number(req.body?.purchaseQuantity) || 1));
      const core = doc.profiles.common_core;
      if (price > 0) {
        if (!core) return next();
        ensureSharedWallet(core);
        const balance = Object.values(core.items || {}).filter(item => /^currency:mtx/i.test(item?.templateId || '')).reduce((sum, item) => sum + (Number(item.quantity) || 0), 0);
        if (balance < price) return res.status(400).json({ errorCode: 'errors.com.epicgames.modules.gameplayutils.not_enough_mtx',
          errorMessage: `Purchase requires ${price} V-Bucks but only ${balance} are available.`, messageVars: [String(price), String(balance)],
          numericErrorCode: 12720, originatingService: 'fortnite', intent: 'prod-live' });
      }
      const buyer = doc.profiles[profileId];
      ensure(buyer, profileId).stats.attributes.current_mtx_platform ??= 'EpicPC';
      wallet = Object.fromEntries(Object.entries(buyer.items).filter(([, item]) => /^currency:mtx/i.test(item?.templateId || '')).map(([key, item]) => [key, item.quantity]));
    }

    const changed = new Set();
    const tpf = (id) => {
      id = String(id || '').replace(/\.json$/i, '');
      if (!doc.profiles[id]) doc.profiles[id] = { profileId: id, accountId, created: new Date().toISOString(), version: 'no_version', wipeNumber: 1 };
      return ensure(doc.profiles[id], id);
    };
    const save = (profile) => {
      const id = Object.keys(doc.profiles).find(key => doc.profiles[key] === profile) || profile?.profileId;
      if (id) { profile.updated = new Date().toISOString(); changed.add(id); }
    };
    // LawinServer's handlers answer through res.json / res.status / res.end; keep the answer until the save is done.
    let status = 200, body;
    const reply = {
      status(code) { status = code; return reply; },
      json(value) { body = value; return reply; },
      send(value) { body = value; return reply; },
      sendStatus(code) { status = code; return reply; },
      end() { return reply; },
    };
    try {
      await ops[operation](req, reply, tpf, save);
    } catch (error) {
      console.error(`[Syndicate STW] ${operation} (${profileId}) failed: ${error.message}; answered with no changes`);
      return res.json(noChanges(doc.profiles[profileId], profileId));
    }
    if (wallet) {
      const buyer = doc.profiles[profileId];
      for (const [key, quantity] of Object.entries(wallet)) if (buyer.items[key]) buyer.items[key].quantity = quantity;
      if (price > 0) {
        const paid = debitShopWallet(doc.profiles.common_core, price);
        if (!paid.ok) return res.status(400).json({ errorCode: 'errors.com.epicgames.modules.gameplayutils.not_enough_mtx',
          errorMessage: 'Not enough V-Bucks.', numericErrorCode: 12720, originatingService: 'fortnite', intent: 'prod-live' });
        changed.add('common_core');
        // Pre-season to Season 2 read the balance from profile0 (Routes/Fortnite.js keeps it in step).
        const mirror = Object.values(doc.profiles.profile0?.items || {}).find(item => /^currency:mtxpurchased$/i.test(item?.templateId || ''));
        if (mirror) { mirror.quantity = Object.values(doc.profiles.common_core.items).filter(item => /^currency:mtx/i.test(item?.templateId || '')).reduce((sum, item) => sum + (Number(item.quantity) || 0), 0); changed.add('profile0'); }
      }
    }
    if (changed.size) {
      const set = {};
      for (const id of changed) set[`profiles.${id}`] = doc.profiles[id];
      await Profile.collection.updateOne({ accountId }, { $set: set });
    }
    if (body === undefined) return res.status(status).end();
    return res.status(status).json(body);
  } catch (error) {
    next(error);
  }
}
