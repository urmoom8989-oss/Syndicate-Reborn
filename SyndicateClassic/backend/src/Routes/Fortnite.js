import {topUpPurchasedPacks} from '../Utils/syndicatePassShop.js';
import {syncChapter2Tier,grantMissingPassCosmetics} from '../Utils/syndicateProgressionCore.js';
import {grantOwnedBundlePieces} from '../Utils/syndicateShopRotation.js';
import {purchaseSeason1Item} from '../Utils/syndicateSeason1Purchase.js';
import {ownedSnapshot,addedLoot,addRewardGift,grantMissingSetPieces,SET_PIECE_FIX_VERSION,TILTED_SENDER_ID} from '../Utils/syndicatePurchaseGift.js';
import {purchaseSeason2Item} from '../Utils/syndicateSeason2Purchase.js';
import {applyProgression} from '../Utils/syndicateProgressionCore.js';
import {season2PassOffer,season2TierOffer,purchaseSeason2} from '../Utils/syndicateSeason2Pass.js';
import {activateSeason,activeSeason,ensureSharedDefaults,applyHideDefaults,unlockSeasonPass} from '../Utils/syndicateSeasonState.js';
import {applyHideWinUmbrellas} from '../Utils/syndicateVictoryRewards.js';
import {ensureSeasonChallenges,ensureDailyChallenges,rerollDailyChallenge,rewardOnClaim} from '../Utils/syndicateSeasonChallenges.js';
import {applyUnlockAll} from '../Utils/syndicateUnlockAll.js';
import {applyMaxProgress} from '../Utils/syndicateMaxProgress.js';
import {heroForItemKey,heroForTemplate,withStwHeroes,stwSkinsOn,stwLockerBuilds} from '../Utils/syndicateStwHeroes.js';
import {customOutfitForItemKey,withCustomOutfits,customOutfitBuilds} from '../Utils/syndicateCustomOutfits.js';
import {redeemedItems} from '../Utils/syndicateRedeemItems.js';
import {ensureSeason5Bundles} from '../Utils/syndicateSeason5Bundles.js';
import {clientBuild,isSeasonTwoBuild} from '../Utils/syndicateBuild.js';
import {grantSeason1FreeRewards} from '../Utils/syndicateSeason1Rewards.js';
import {stripNonBaseBanners,BANNER_FIX_VERSION} from '../Utils/syndicateBaseBanners.js';
import {project111Profile,project111Response} from '../Utils/syndicateLegacy111Profile.js';
import {ensureSeason4Styles,applyOwnedVariant} from '../Utils/syndicateSeason4Styles.js';
import {ensureChallenges} from '../Utils/syndicateChallenges.js';
import {legacyAthenaStats} from '../Utils/syndicateCareerCore.js';
import {tierOfferId,season5TierOfferId,purchaseTiers,tierOfferIds,tierSeason} from '../Utils/syndicateTierPurchase.js';
import { debitShopWallet,ensureSharedWallet } from '../Utils/syndicateShopWallet.js';
import {travelActive} from '../Utils/syndicateTravel.js';
import {acquireAccountLock} from '../Utils/syndicateAccountLock.js';
import { publishCharacter } from '../Utils/syndicateLocker.js';
import { awardAccount } from '../Utils/syndicateProgression.js';
import { stwDevUpdate, stwDevNoOp, stwDevEnabled, STW_PROFILES } from '../Utils/syndicateStwDev.js';
import express from "express";
const app = express.Router();
// Season 3 to Chapter 2 Season 1: Save the World heroes as locker outfits while Unlock all and STW skins are on
// (syndicateStwHeroes.js). 1.7.2-1.11 get them from project111Profile instead.
const stwProfileBuilds=stwLockerBuilds.filter(b=>!['1.7.2','1.8.2','1.11','2.5'].includes(b));
app.use('/fortnite/api/game/v2/profile/:accountId', (req,res,next)=>{
 const stwBuild=clientBuild(req.headers['user-agent']);
 if(!stwProfileBuilds.includes(stwBuild)&&!customOutfitBuilds.includes(stwBuild))return next();
 const send=res.json.bind(res);
 res.json=(body)=>{
  try{for(const r of [body,...(body?.multiUpdate||[])])if(r?.profileId==='athena')for(const c of r.profileChanges||[])if(c.changeType==='fullProfileUpdate'&&c.profile){
   if(stwProfileBuilds.includes(stwBuild))c.profile=withStwHeroes(c.profile,stwBuild);
   c.profile=withCustomOutfits(c.profile,stwBuild); // Syndicate custom outfits (syndicateCustomOutfits.js)
  }}
  catch(error){console.warn('[Syndicate] Save the World hero outfits skipped:',error.message);}
  return send(body);
 };
 next();
});
// Scope the old client's wire format without changing persisted Season 4 ownership.
app.use('/fortnite/api/game/v2/profile/:accountId', (req,res,next)=>{
 const legacyBuild=clientBuild(req.headers['user-agent']);
 if(!['1.11','2.5','1.8.2','1.7.2'].includes(legacyBuild))return next();
 const send=res.json.bind(res);
 res.json=(body)=>{
  project111Response(body,async()=>{
   if(!req.user?.accountId)return null;
   const doc=await Profile.collection.findOne({accountId:req.user.accountId});
   return doc?.profiles?.athena;
  },async()=>{
   if(!req.user?.accountId)return null;
   const doc=await Profile.collection.findOne({accountId:req.user.accountId});
   return doc?.profiles?.common_core;
  },async()=>{
   if(!req.user?.accountId)return null;
   const doc=await Profile.collection.findOne({accountId:req.user.accountId});
   return doc?.profiles?.profile0;
  },legacyBuild).then(send,next);
  return res;
 };
 next();
});

const LOCKER_OPERATIONS=new Set(['MarkItemSeen','EquipBattleRoyaleCustomization','SetCosmeticLockerSlot','SetItemFavoriteStatus','SetItemFavoriteStatusBatch','SetItemArchivedStatusBatch','SetBattleRoyaleBanner','SetCosmeticLockerBanner','SetCosmeticLockerName','PutModularCosmeticLoadout']);
app.use('/fortnite/api/game/v2/profile/:accountId',verifyToken,async(req,res,next)=>{
 if(req.user.accountId!==req.params.accountId)return res.status(403).end();
 const release=await acquireAccountLock(req.params.accountId);let done=false;
 const finish=()=>{if(!done){done=true;release();}};res.once('finish',finish);res.once('close',finish);
 if(res.destroyed){finish();return;}
 if(travelActive(req.params.accountId)){finish();return res.status(409).json({errorCode:'errors.com.syndicate.account_in_use',errorMessage:'This account is playing on another host.'});}
 const visitor=await User.collection.findOne({accountId:req.params.accountId},{projection:{travelGuest:1}});
 if(visitor?.travelGuest&&visitor.travelGuest.status!=='active'){finish();return res.status(401).end();}
 // Locker clicks (equip, mark seen, favorite, banner) skip the season sync below. It reads the whole profile twice
 // and runs every season fix-up on each call; the locker sends MarkItemSeen for each page of new items while you
 // scroll, so "Saving Selection" queued behind those, and any revision bump the sync made turned the reply into a
 // full profile update that rebuilt the outfit list and scrolled it back to the equipped skin. QueryProfile and
 // ClientQuestLogin (lobby load, locker open) still run it.
 if(LOCKER_OPERATIONS.has(String(req.path.split('/').pop()||''))){next();return;}
 try{
  const walletDoc=await Profile.collection.findOne({accountId:req.user.accountId});
  if(walletDoc?.profiles?.common_core){
   const p=walletDoc.profiles.common_core,rvn=p.rvn;
   const changed=ensureSharedWallet(p);
   // The 1.7.2-1.11 clients read their V-Bucks from profile0, not common_core, so a 0 there means the Season
   // shop shows "you only have 0" even with a full balance. Keep profile0's stored wallet mirroring the shared
   // balance; common_core stays the profile that is actually debited on a purchase.
   const p0=walletDoc.profiles.profile0;let p0changed=false;
   if(p0){
    const total=Object.values(p.items||{}).filter(i=>/^currency:mtx/i.test(i.templateId||'')).reduce((s,i)=>s+(Number(i.quantity)||0),0);
    p0.items??={};
    const entry=Object.values(p0.items).find(i=>/^currency:mtxpurchased$/i.test(i.templateId||''));
    if(entry){if(entry.quantity!==total){entry.quantity=total;entry.attributes={...(entry.attributes||{}),platform:'Shared'};p0changed=true;}}
    else{p0.items['Currency:MtxPurchased']={templateId:'Currency:MtxPurchased',quantity:total,attributes:{platform:'Shared'}};p0changed=true;}
   }
   if(changed||p0changed){
    if(changed){p.rvn++;p.commandRevision++;p.updated=new Date().toISOString();}
    if(p0changed){p0.rvn=(Number(p0.rvn)||0)+1;p0.commandRevision=(Number(p0.commandRevision)||0)+1;p0.updated=new Date().toISOString();}
    const set={};if(changed)set['profiles.common_core']=p;if(p0changed)set['profiles.profile0']=p0;
    const saved=await Profile.collection.updateOne({accountId:req.user.accountId,'profiles.common_core.rvn':rvn},{$set:set});
    if(!saved.matchedCount){finish();return res.status(409).json({errorCode:'errors.com.syndicate.profile_busy'});}
   }
  }
  const build=clientBuild(req.headers['user-agent']);
  const later={'6.20':6,'7.40':7,'8.50':8,'9.40':9,'10.31':10,'10.40':10,'11.00':11,'12.50':12};
  // Legacy builds (Season 1 = 1.7.2/1.8.2, Season 2 = 1.11): dump what the account holds, to diagnose missing
  // skins and "have V-Bucks but the shop says 0". Runs here, before the season block, which 1.8.2 never enters.
  if(build==='1.8.2'||build==='1.7.2'||isSeasonTwoBuild(build))try{
   const dbg=await Profile.collection.findOne({accountId:req.user.accountId});const dp=dbg?.profiles?.athena;
   if(dp){const legacySeason=isSeasonTwoBuild(build)?2:1;const cc=dbg.profiles.common_core||{items:{}};const at=dp.stats.attributes;
    const currencyIn=prof=>Object.values(prof?.items||{}).filter(i=>/^currency:/i.test(i.templateId||'')).map(i=>({templateId:i.templateId,quantity:i.quantity,platform:i.attributes?.platform}));
    const owned=Object.values(dp.items||{}).map(i=>i.templateId).filter(t=>/^Athena/i.test(t||'')).sort();
    const quests=Object.values(dp.items||{}).filter(i=>/^(Quest|ChallengeBundle|ChallengeBundleSchedule):/i.test(i.templateId||'')).map(i=>({templateId:i.templateId,state:i.attributes?.quest_state,daily:!!i.attributes?.tilted_daily}));
    fs.writeFileSync(new URL('../../../Runtime/Server/challenge-data/profile-debug-season-'+legacySeason+'.json',import.meta.url),JSON.stringify({build,level:at.level,questDailyDay:at.tilted_quest_daily_day,questCount:quests.length,quests,commonCoreCurrency:currencyIn(cc),athenaCurrency:currencyIn(dp),profile0Currency:currencyIn(dbg.profiles.profile0),athenaRvn:dp.rvn,walletRvn:cc.rvn,passPurchases:Object.keys(dbg.syndicatePassPurchases||{}),equipped:{character:at.favorite_character,pickaxe:at.favorite_pickaxe,glider:at.favorite_glider,backpack:at.favorite_backpack,dance:at.favorite_dance},ownedCount:owned.length,owned},null,1));}
  }catch{}
  if(isSeasonTwoBuild(build)||build==='3.5.1'||build==='4.5'||build==='5.10'||later[build]){
   const doc=await Profile.collection.findOne({accountId:req.user.accountId});
   if(doc?.profiles?.athena){const p=doc.profiles.athena,rvn=p.rvn;
    const season=later[build]||(isSeasonTwoBuild(build)?2:build==='3.5.1'?3:build==='5.10'?5:4);
    const switched=activateSeason(p,season);const defaults=ensureSharedDefaults(p);
    // Bought this season's complete pass in the launcher: the Battle Pass is owned with every tier.
    const passIds=season===11?['chapter2-season1','season11']:season===12?['chapter2-season2','season12']:['season'+season];
    const passUnlocked=passIds.some(id=>!!doc.syndicatePassPurchases?.[id]) && unlockSeasonPass(p,season);
    // Challenges for this season (Season 4 keeps its own route below).
    const challenges=season===5?ensureSeason5Bundles(p):ensureSeasonChallenges(p,season);
    // Chapter 2 challenge check: what the profile holds (schedules, bundles, quests), for comparing with the game.
    if(season>=11)try{fs.writeFileSync(new URL('../../../Runtime/Server/challenge-data/profile-debug-season-'+season+'.json',import.meta.url),JSON.stringify(Object.fromEntries(Object.entries(p.items).filter(([,i])=>/^(Quest|ChallengeBundle|ChallengeBundleSchedule):/i.test(i.templateId||''))),null,1));}catch{}
    // Daily challenges from the build's own daily quests (Season 4 keeps its own).
    const dailies=season!==4&&ensureDailyChallenges(p,season);
    // Launcher "Unlock all cosmetics": add newly exported cosmetics while the option is on.
    const unlocked=applyUnlockAll(p,season,doc.syndicatePassPurchases||{},redeemedItems(doc))>0;
    // ...and holds this season at its last level and tier with every challenge complete (reverted when it is turned off).
    const maxed=applyMaxProgress(p,season);
    // Packs from the launcher shop that gained items since they were bought.
    const packsTopped=topUpPurchasedPacks(doc);
    const tierSynced=syncChapter2Tier(p);
    // Battle Pass cosmetics of reached tiers that never made it into the locker.
    const passFixed=grantMissingPassCosmetics(p)>0;
    // Bundle pieces (back bling, pickaxe, glider) of shop outfits bought before they were granted.
    const bundleFixed=grantOwnedBundlePieces(p)>0;
    // Launcher "Remove default outfits from locker": strip the free defaults (runs last, after anything that adds them).
    const defaultsHidden=applyHideDefaults(p);
    // Launcher "Remove unreleased win umbrellas": strip the Bronze/Gold milestone umbrellas.
    const umbrellasHidden=applyHideWinUmbrellas(p);
    const freePass=((isSeasonTwoBuild(build)||build==='5.10') && !p.stats.attributes.book_purchased) || (build==='3.5.1' && (!p.stats.attributes.book_purchased || (p.stats.attributes.tilted_season3_free_reward_tier||0)<Math.max(1,p.stats.attributes.book_level||1) || (p.stats.attributes.tilted_season3_paid_reward_tier||0)<Math.max(1,p.stats.attributes.book_level||1)));
    if(switched||defaults||freePass||challenges||dailies||unlocked||maxed||passUnlocked||packsTopped||tierSynced||passFixed||bundleFixed||defaultsHidden||umbrellasHidden){const cr=doc.profiles.common_core.rvn;
     if(freePass)applyProgression(doc.profiles,0);else {p.rvn++;p.commandRevision++;p.updated=new Date().toISOString();}
     const updates={'profiles.athena':p};if(freePass)updates['profiles.common_core']=doc.profiles.common_core;
     const result=await Profile.collection.updateOne({accountId:req.user.accountId,'profiles.athena.rvn':rvn,'profiles.common_core.rvn':cr},{$set:updates});
     if(!result.matchedCount){finish();return res.status(409).json({errorCode:'errors.com.syndicate.profile_busy'});}
    }
   }
  }
  next();
 }catch(error){finish();next(error);}
});

import Profile from "../User/Mongodb/Schema/profiles.js";
import User from "../User/Mongodb/Schema/user.js";
import profileManager from "../User/profile.js";
import Friends from "../User/Mongodb/Schema/friends.js";
import Utils from "../Utils/Utils.js";
import log from "../Utils/log.js";
import { verifyToken } from "../User/tokenManager/tokenVerify.js";
import fs from "fs";
import { createRequire } from "module";
import axios from "axios";
import { v4 } from "uuid";
const require = createRequire(import.meta.url);
global.giftReceived = {};
global.codes = [""];

// One time per account: items an older purchase should have included (Raptor's back bling; Frostbite's back
// bling, pickaxe and glider) are granted, with the reward pop-up. Runs before the profile is returned.
// Daily challenge reroll (the circular arrow on a daily challenge): one a day, for seasons whose
// daily challenges come from the build's export (Season 4 keeps its own).
app.post('/fortnite/api/game/v2/profile/*/client/FortRerollDailyQuest',verifyToken,async(req,res,next)=>{
 const later={'1.11':2,'3.5.1':3,'5.10':5,'6.20':6,'7.40':7,'8.50':8,'9.40':9,'10.31':10,'10.40':10,'11.00':11,'12.50':12};
 const season=later[clientBuild(req.headers['user-agent'])];
 if(!season||req.query.profileId!=='athena')return next();
 try{
  const doc=await Profile.collection.findOne({accountId:req.user.accountId});if(!doc?.profiles?.athena)return res.sendStatus(404);
  const p=doc.profiles.athena,base=p.rvn;
  const added=rerollDailyChallenge(p,season,String(req.body?.questId||''));
  p.rvn++;p.commandRevision++;p.updated=new Date().toISOString();
  const saved=await Profile.collection.updateOne({accountId:req.user.accountId,'profiles.athena.rvn':base},{$set:{'profiles.athena':p}});
  if(!saved.matchedCount)return res.status(409).json({errorCode:'errors.com.syndicate.profile_busy'});
  res.json({profileRevision:p.rvn,profileId:'athena',profileChangesBaseRevision:base,profileChanges:[{changeType:'fullProfileUpdate',profile:p}],
   notifications:[{type:'dailyQuestReroll',primary:true,newQuestId:p.items[added].templateId.slice(6)}],profileCommandRevision:p.commandRevision,serverTime:new Date().toISOString(),responseVersion:1});
 }catch(error){res.status(400).json({errorCode:'errors.com.syndicate.reroll_failed',errorMessage:error.message});}
});
// "Collect Rewards" on a finished challenge (Season 1 and 2 lobbies send ClaimQuestReward; it used to fail with
// "Operation ClaimQuestReward not valid"). The backend already gave the rewards when the challenge was completed,
// so claiming only marks it Claimed.
app.post('/fortnite/api/game/v2/profile/*/client/ClaimQuestReward',verifyToken,async(req,res,next)=>{
 if(req.query.profileId!=='athena')return next();
 try{
  for(let attempt=0;attempt<4;attempt++){
   const doc=await Profile.collection.findOne({accountId:req.user.accountId});if(!doc?.profiles?.athena)return res.sendStatus(404);
   const p=doc.profiles.athena,base=p.rvn,id=String(req.body?.questId||'');
   const item=p.items?.[id]||Object.values(p.items||{}).find(i=>/^quest:/i.test(i?.templateId||'')&&i.templateId.toLowerCase()===('quest:'+id).toLowerCase());
   if(item?.attributes?.quest_state==='Completed'){
    // Pay the reward now if completion never did (a quest that reached Completed via grantQuest skipped the
    // reward sweep, so its stars/items/XP were lost). Idempotent via tilted_rewarded, so no double-grant.
    const owed=rewardOnClaim(p,item,activeSeason(p));
    let applied=null;
    if(owed.granted){try{applied=applyProgression(doc.profiles,owed.xp||0);}catch{}} // season XP + convert stars to tiers
    item.attributes.quest_state='Claimed';item.attributes.last_state_change_time=new Date().toISOString();
    // applyProgression already bumped athena's revision when it held the season at max; otherwise bump here.
    if(applied&&applied.maxed){p.updated=new Date().toISOString();}else{p.rvn++;p.commandRevision++;p.updated=new Date().toISOString();}
    const saved=await Profile.collection.updateOne({accountId:req.user.accountId,'profiles.athena.rvn':base},{$set:{'profiles.athena':p}});
    if(!saved.matchedCount)continue;
   }
   return res.json({profileRevision:p.rvn,profileId:'athena',profileChangesBaseRevision:base,profileChanges:[{changeType:'fullProfileUpdate',profile:p}],profileCommandRevision:p.commandRevision,serverTime:new Date().toISOString(),responseVersion:1});
  }
  res.status(409).json({errorCode:'errors.com.syndicate.profile_busy'});
 }catch(error){next(error);}
});
app.post(['/fortnite/api/game/v2/profile/*/client/QueryProfile','/fortnite/api/game/v2/profile/*/client/ClientQuestLogin'],verifyToken,async(req,res,next)=>{
 if(!['athena','common_core'].includes(req.query.profileId))return next();
 try{
  const doc=await Profile.collection.findOne({accountId:req.user.accountId},{projection:{'profiles.athena':1,'profiles.common_core':1,syndicateSetPieceFix:1}});
  if(!doc?.profiles?.athena?.items || !doc.profiles.common_core?.items || (doc.syndicateSetPieceFix||0)>=SET_PIECE_FIX_VERSION)return next();
  const a=doc.profiles.athena,c=doc.profiles.common_core,ar=a.rvn,cr=c.rvn;
  const loot=grantMissingSetPieces(a);
  const set={syndicateSetPieceFix:SET_PIECE_FIX_VERSION};
  if(loot.length){
   addRewardGift(c,loot,'SYNDICATE','Items missing from an earlier purchase.');
   for(const p of [a,c]){p.rvn=(p.rvn||0)+1;p.commandRevision=(p.commandRevision||0)+1;p.updated=new Date().toISOString();}
   set['profiles.athena']=a;set['profiles.common_core']=c;
  }
  await Profile.collection.updateOne({accountId:req.user.accountId,'profiles.athena.rvn':ar,'profiles.common_core.rvn':cr},{$set:set}); // a busy profile tries again next login
 }catch(error){console.log('[SyndicateSetPieceFix] '+error.message);}
 next();
});
// One-time per account: remove the Founder / Influencer / Newsletter / Other / STW and extra-Standard banner icons
// the old default profile handed out, leaving only the 20 base banners plus earned season banners.
app.post(['/fortnite/api/game/v2/profile/*/client/QueryProfile','/fortnite/api/game/v2/profile/*/client/ClientQuestLogin'],verifyToken,async(req,res,next)=>{
 if(!['athena','common_core'].includes(req.query.profileId))return next();
 try{
  for(let attempt=0;attempt<4;attempt++){
   const doc=await Profile.collection.findOne({accountId:req.user.accountId},{projection:{profiles:1,syndicateBannerFix:1}});
   if(!doc?.profiles||(doc.syndicateBannerFix||0)>=BANNER_FIX_VERSION)break;
   const cc=doc.profiles.common_core,p0=doc.profiles.profile0,cr=cc?.rvn;
   const changed=stripNonBaseBanners(doc.profiles);
   const set={syndicateBannerFix:BANNER_FIX_VERSION};
   if(changed){for(const pr of [cc,p0,doc.profiles.athena])if(pr){pr.rvn=(pr.rvn||0)+1;pr.commandRevision=(pr.commandRevision||0)+1;pr.updated=new Date().toISOString();}
    if(cc)set['profiles.common_core']=cc;if(p0)set['profiles.profile0']=p0;if(doc.profiles.athena)set['profiles.athena']=doc.profiles.athena;}
   const saved=await Profile.collection.updateOne({accountId:req.user.accountId,'profiles.common_core.rvn':cr},{$set:set});
   if(saved.matchedCount)break; // otherwise busy: retry
  }
 }catch(error){console.log('[SyndicateBaseBanners] '+error.message);}
 next();
});
app.post(['/fortnite/api/game/v2/profile/*/client/QueryProfile','/fortnite/api/game/v2/profile/*/public/QueryProfile'],verifyToken,async(req,res,next)=>{
 if(req.query.profileId!=='athena_stats')return next();
 try{
  const doc=await Profile.collection.findOne({accountId:req.user.accountId});
  if(!doc)return res.status(404).json({errorCode:'errors.com.syndicate.profile_missing'});
  const p=legacyAthenaStats(req.user.accountId,doc.profiles.athena);
  res.json({profileRevision:p.rvn,profileId:p.profileId,profileChangesBaseRevision:p.rvn,profileChanges:[{changeType:'fullProfileUpdate',profile:p}],profileCommandRevision:p.commandRevision,serverTime:new Date().toISOString(),responseVersion:1});
 }catch(error){next(error);}
});


// Pre-season / Season 1: hand out the free Season Shop rewards (Battle Bus banner at level 10, Mako glider at
// level 25) the moment the player's Season Level reaches them. Idempotent, so it also catches accounts already
// past those levels. Season-1 builds only; later builds keep their own progression.
app.post(['/fortnite/api/game/v2/profile/*/client/QueryProfile','/fortnite/api/game/v2/profile/*/client/ClientQuestLogin'],verifyToken,async(req,res,next)=>{
 if(req.query.profileId!=='athena'||!['1.7.2','1.8.2'].includes(clientBuild(req.headers['user-agent'])))return next();
 try{
  for(let attempt=0;attempt<4;attempt++){
   const doc=await Profile.collection.findOne({accountId:req.user.accountId},{projection:{'profiles.athena':1}});if(!doc?.profiles?.athena)break;
   const a=doc.profiles.athena,base=a.rvn;
   if(!grantSeason1FreeRewards(a).length)break;
   a.rvn++;a.commandRevision=(a.commandRevision||0)+1;a.updated=new Date().toISOString();
   const saved=await Profile.collection.updateOne({accountId:req.user.accountId,'profiles.athena.rvn':base},{$set:{'profiles.athena':a}});
   if(saved.matchedCount)break; // otherwise the profile was busy: retry
  }
 }catch(error){console.log('[SyndicateSeason1Rewards] '+error.message);}
 next();
});

app.post(['/fortnite/api/game/v2/profile/*/client/QueryProfile','/fortnite/api/game/v2/profile/*/client/ClientQuestLogin','/fortnite/api/game/v2/profile/*/client/UpdateQuestClientObjectives'],verifyToken,async(req,res,next)=>{
 if(req.query.profileId!=='athena'||Utils.GetVersion(req).season!==4)return next();
 try{
  for(let attempt=0;attempt<4;attempt++){
   const doc=await Profile.collection.findOne({accountId:req.user.accountId});if(!doc)return res.sendStatus(404);
   const p=doc.profiles.athena,base=p.rvn;
   if(ensureChallenges(p)){
    p.rvn++;p.commandRevision++;p.updated=new Date().toISOString();
    const saved=await Profile.collection.updateOne({accountId:req.user.accountId,'profiles.athena.rvn':base},{$set:{'profiles.athena':p}});
    if(!saved.matchedCount)continue;
   }
   // Objective progress comes from the trusted game-server queue; client counts cannot mint rewards.
   return res.json({profileRevision:p.rvn,profileId:'athena',profileChangesBaseRevision:base,profileChanges:[{changeType:'fullProfileUpdate',profile:p}],profileCommandRevision:p.commandRevision,serverTime:new Date().toISOString(),responseVersion:1});
  }
  res.status(409).json({errorCode:'errors.com.syndicate.profile_busy'});
 }catch(error){next(error);}
});

app.get("/links/api/fn/mnemonic/:playlist/related", async (req, res) => {
  const response = {
    parentLinks: [],
    links: {},
  };

  const playlist = req.params.playlist;
  if (playlist) {
    const results = discovery.v2.Panels[1].Pages[0].results;

    for (const item of results) {
      const linkData = item.linkData;
      if (linkData.mnemonic === playlist) {
        response.links[playlist] = linkData;
        break;
      }
    }
  }

  res.json(response);
});

app.post("/api/v2/discovery/surface/*", async (req, res) => {
  const discovery = JSON.parse(
    fs.readFileSync("./src/local/Discovery/Discovery.json", "utf-8")
  );
  res.json(discovery.v2);
});

app.post("/api/v1/discovery/surface/*", async (req, res) => {
  const discovery = JSON.parse(
    fs.readFileSync("./src/local/Discovery/Discovery.json", "utf-8")
  );
  res.json(discovery.v1);
});

app.get("/fortnite/api/discovery/accessToken/:branch", async (req, res) => {
  res.json({
    branchName: req.params.branch,
    appId: "Fortnite",
    token: "syndicateaccesstoken",
  });
});

app.get("/links/api/fn/mnemonic/*", async (req, res) => {
  const discovery = JSON.parse(
    fs.readFileSync("./src/local/Discovery/Discovery.json", "utf-8")
  );

  const mnemonic = req.params[0];
  const results = discovery.v2.Panels[1].Pages[0].results;

  const found = results.find((item) => item.linkData.mnemonic === mnemonic);

  if (found) {
    res.json(found.linkData);
  } else {
    res.status(404).json({ error: "Mnemonic not found" });
  }
});

app.post("/api/v1/discovery/surface/:accountId", async (req, res) => {
  const discovery = JSON.parse(
    fs.readFileSync("./src/local/Discovery/Discovery.json", "utf-8")
  );
  res.json(discovery);
});

app.get("/api/v1/public/tags", (req, res) => {
  const tagList = JSON.parse(
    fs.readFileSync("./src/local/PlayerTags/Tags.json", "utf-8")
  );

  if (req.query.cursor) {
    tagList.cursor = req.query.cursor;
  } else {
    tagList.cursor = null;
  }

  res.status(200).json(tagList);
});

app.put("/profile/play_region", async (req, res) => {
  res.json({
    namespace: "Fortnite",
    play_region: "NA_EAST",
  });
});

app.get(
  "/fortnite/api/game/v2/privacy/account/:accountId",
  async (req, res) => {
    res.json({
      accountId: req.params.accountId,
      optOutOfPublicLeaderboards: false,
    });
  }
);

app.post("/api/v1/assets/Fortnite/*/*", async (req, res) => {
  const { FortCreativeDiscoverySurface } = req.body;

  if (FortCreativeDiscoverySurface === 0) {
    const discovery_api_assets = require("../local/Discovery/Creative.json");
    res.json(discovery_api_assets);
  } else {
    res.json({
      FortCreativeDiscoverySurface: {
        meta: {
          promotion: FortCreativeDiscoverySurface || 0,
        },
        assets: {},
      },
    });
  }
});

app.post("/api/v1/links/lock-status/:accountId/check", async (req, res) => {
  const response = {
    results: [],
    hasMore: false,
  };

  const { accountId } = req.params;
  const { linkCodes } = req.body;

  if (Array.isArray(linkCodes)) {
    for (const linkCode of linkCodes) {
      response.results.push({
        playerId: accountId,
        linkCode,
        lockStatus: "UNLOCKED",
        lockStatusReason: "NONE",
        isVisible: true,
      });
    }
  }

  res.json(response);
});

app.get("/hotconfigs/v2/livefn.json", async (req, res) => {
  res.json({
    HotConfigData: [
      {
        AppId: "livefn",
        EpicApp: "FortniteLivefn",
        Modules: [
          {
            ModuleName: "GameServiceMcp",
            Endpoints: {
              Android: "fngw-mcp-gc-livefn.ol.epicgames.com",
              DedicatedServer: "fngw-mcp-ds-livefn.ol.epicgames.com",
              Default: "fngw-mcp-gc-livefn.ol.epicgames.com",
              IOS: "fngw-mcp-gc-livefn.ol.epicgames.com",
              Linux: "fngw-mcp-gc-livefn.ol.epicgames.com",
              Mac: "fngw-mcp-gc-livefn.ol.epicgames.com",
              PS4: "fngw-mcp-gc-livefn.ol.epicgames.com",
              PS5: "fngw-mcp-gc-livefn.ol.epicgames.com",
              Switch: "fngw-mcp-gc-livefn.ol.epicgames.com",
              Windows: "fngw-mcp-gc-livefn.ol.epicgames.com",
              XB1: "fngw-mcp-gc-livefn.ol.epicgames.com",
              XSX: "fngw-mcp-gc-livefn.ol.epicgames.com",
              XboxOneGDK: "fngw-mcp-gc-livefn.ol.epicgames.com",
            },
          },
        ],
      },
    ],
  });
});

app.post(
  "/fortnite/api/game/v2/profile/*/client/SetAffiliateName",
  verifyToken,
  async (req, res) => {
    const profiles = await Profile.findOne({ accountId: req.user.accountId });
    if (!(await profileManager.validateProfile(req.query.profileId, profiles)))
      return Utils.createError(
        "errors.com.epicgames.modules.profiles.operation_forbidden",
        `Unable to find template configuration for profile ${req.query.profileId}`,
        [req.query.profileId],
        12813,
        undefined,
        403,
        res
      );
    let profile = profiles?.profiles[req.query.profileId];
    if (req.query.profileId != "common_core")
      return Utils.createError(
        "errors.com.epicgames.modules.profiles.invalid_command",
        `SetAffiliateName is not valid on ${req.query.profileId} profile`,
        ["SetAffiliateName", req.query.profileId],
        12801,
        undefined,
        400,
        res
      );
    const memory = Utils.GetVersion(req);
    let ApplyProfileChanges = [];
    let BaseRevision = profile.rvn;
    let ProfileRevisionCheck =
      memory.build >= 12.2 ? profile.commandRevision : profile.rvn;
    let QueryRevision = req.query.rvn || -1;
    if (typeof req.body.affiliateName != "string")
      return ValidationError("affiliateName", "a string", res);

    profile.stats.attributes.mtx_affiliate = req.body.affiliateName;
    profile.stats.attributes.mtx_affiliate_set_time = new Date().toISOString();

    ApplyProfileChanges.push({
      changeType: "statModified",
      name: "mtx_affiliate",
      value: profile.stats.attributes.mtx_affiliate,
    });
    ApplyProfileChanges.push({
      changeType: "statModified",
      name: "mtx_affiliate_set_time",
      value: profile.stats.attributes.mtx_affiliate_set_time,
    });

    if (ApplyProfileChanges.length > 0) {
      profile.rvn += 1;
      profile.commandRevision += 1;
      profile.updated = new Date().toISOString();
      await profiles?.updateOne({
        $set: { [`profiles.${req.query.profileId}`]: profile },
      });
    }
    if (QueryRevision != ProfileRevisionCheck) {
      ApplyProfileChanges = [
        {
          changeType: "fullProfileUpdate",
          profile: profile,
        },
      ];
    }
    res.json({
      profileRevision: profile.rvn || 0,
      profileId: req.query.profileId,
      profileChangesBaseRevision: BaseRevision,
      profileChanges: ApplyProfileChanges,
      profileCommandRevision: profile.commandRevision || 0,
      serverTime: new Date().toISOString(),
      responseVersion: 1,
    });
  }
);
app.post(
  "/fortnite/api/game/v2/profile/*/client/SetReceiveGiftsEnabled",
  verifyToken,
  async (req, res) => {
    const profiles = await Profile.findOne({ accountId: req.user.accountId });
    if (!(await profileManager.validateProfile(req.query.profileId, profiles)))
      return Utils.createError(
        "errors.com.epicgames.modules.profiles.operation_forbidden",
        `Unable to find template configuration for profile ${req.query.profileId}`,
        [req.query.profileId],
        12813,
        undefined,
        403,
        res
      );
    let profile = profiles?.profiles[req.query.profileId];
    if (req.query.profileId != "common_core")
      return Utils.createError(
        "errors.com.epicgames.modules.profiles.invalid_command",
        `SetReceiveGiftsEnabled is not valid on ${req.query.profileId} profile`,
        ["SetReceiveGiftsEnabled", req.query.profileId],
        12801,
        undefined,
        400,
        res
      );
    const memory = Utils.GetVersion(req);
    let ApplyProfileChanges = [];
    let BaseRevision = profile.rvn;
    let ProfileRevisionCheck =
      memory.build >= 12.2 ? profile.commandRevision : profile.rvn;
    let QueryRevision = req.query.rvn || -1;
    if (typeof req.body.bReceiveGifts != "boolean")
      return ValidationError("bReceiveGifts", "a boolean", res);
    profile.stats.attributes.allowed_to_receive_gifts = req.body.bReceiveGifts;
    ApplyProfileChanges.push({
      changeType: "statModified",
      name: "allowed_to_receive_gifts",
      value: profile.stats.attributes.allowed_to_receive_gifts,
    });
    if (ApplyProfileChanges.length > 0) {
      profile.rvn += 1;
      profile.commandRevision += 1;
      profile.updated = new Date().toISOString();
      await profiles?.updateOne({
        $set: { [`profiles.${req.query.profileId}`]: profile },
      });
    }
    if (QueryRevision != ProfileRevisionCheck) {
      ApplyProfileChanges = [
        {
          changeType: "fullProfileUpdate",
          profile: profile,
        },
      ];
    }
    res.json({
      profileRevision: profile.rvn || 0,
      profileId: req.query.profileId,
      profileChangesBaseRevision: BaseRevision,
      profileChanges: ApplyProfileChanges,
      profileCommandRevision: profile.commandRevision || 0,
      serverTime: new Date().toISOString(),
      responseVersion: 1,
    });
  }
);
// app.post(
//   "/fortnite/api/game/v2/profile/*/client/GiftCatalogEntry",
//   verifyToken,
//   async (req, res) => {
//     const profiles = await Profile.findOne({ accountId: req.user.accountId });
//     if (!(await profileManager.validateProfile(req.query.profileId, profiles)))
//       return Utils.createError(
//         "errors.com.epicgames.modules.profiles.operation_forbidden",
//         `Unable to find template configuration for profile ${req.query.profileId}`,
//         [req.query.profileId],
//         12813,
//         undefined,
//         403,
//         res
//       );
//     let profile = profiles?.profiles[req.query.profileId];
//     let athena = profiles?.profiles["athena"];
//     if (req.query.profileId != "common_core")
//       return Utils.createError(
//         "errors.com.epicgames.modules.profiles.invalid_command",
//         `GiftCatalogEntry is not valid on ${req.query.profileId} profile`,
//         ["GiftCatalogEntry", req.query.profileId],
//         12801,
//         undefined,
//         400,
//         res
//       );
//     const memory = Utils.GetVersion(req);
//     let Notifications = [];
//     let ApplyProfileChanges = [];
//     let BaseRevision = profile.rvn || 0;
//     let ProfileRevisionCheck =
//       memory.build >= 12.2 ? profile.commandRevision : profile.rvn;
//     let QueryRevision = req.query.rvn || -1;
//     let validGiftBoxes = [
//       "GiftBox:gb_default",
//       "GiftBox:gb_giftwrap1",
//       "GiftBox:gb_giftwrap2",
//       "GiftBox:gb_giftwrap3",
//     ];
//     let missingFields = checkFields(
//       ["offerId", "receiverAccountIds", "giftWrapTemplateId"],
//       req.body
//     );
//     if (missingFields.fields.length > 0)
//       return Utils.createError(
//         "errors.com.epicgames.validation.validation_failed",
//         `Validation Failed. [${missingFields.fields.join(
//           ", "
//         )}] field(s) is missing.`,
//         [`[${missingFields.fields.join(", ")}]`],
//         1040,
//         undefined,
//         400,
//         res
//       );
//     if (typeof req.body.offerId != "string")
//       return ValidationError("offerId", "a string", res);
//     if (!Array.isArray(req.body.receiverAccountIds))
//       return ValidationError("receiverAccountIds", "an array", res);
//     if (typeof req.body.giftWrapTemplateId != "string")
//       return ValidationError("giftWrapTemplateId", "a string", res);
//     if (typeof req.body.personalMessage != "string")
//       return ValidationError("personalMessage", "a string", res);
//     if (req.body.personalMessage.length > 100)
//       return Utils.createError(
//         "errors.com.epicgames.string.length_check",
//         `The personalMessage you provided is longer than 100 characters, please make sure your personal message is less than 100 characters long and try again.`,
//         undefined,
//         16027,
//         undefined,
//         400,
//         res
//       );
//     if (!validGiftBoxes.includes(req.body.giftWrapTemplateId))
//       return Utils.createError(
//         "errors.com.epicgames.giftbox.invalid",
//         `The giftbox you provided is invalid, please provide a valid giftbox and try again.`,
//         undefined,
//         16027,
//         undefined,
//         400,
//         res
//       );
//     if (
//       req.body.receiverAccountIds.length < 1 ||
//       req.body.receiverAccountIds.length > 5
//     )
//       return Utils.createError(
//         "errors.com.epicgames.item.quantity.range_check",
//         `You need to atleast gift to 1 person and can not gift to more than 5 people.`,
//         undefined,
//         16027,
//         undefined,
//         400,
//         res
//       );
//     if (checkIfDuplicateExists(req.body.receiverAccountIds))
//       return Utils.createError(
//         "errors.com.epicgames.array.duplicate_found",
//         `There are duplicate accountIds in receiverAccountIds, please remove the duplicates and try again.`,
//         undefined,
//         16027,
//         undefined,
//         400,
//         res
//       );
//     let sender = await Friends.findOne({
//       accountId: req.user.accountId,
//     }).lean();
//     for (let receiverId of req.body.receiverAccountIds) {
//       if (typeof receiverId != "string")
//         return Utils.createError(
//           "errors.com.epicgames.array.invalid_string",
//           `There is a non-string object inside receiverAccountIds, please provide a valid value and try again.`,
//           undefined,
//           16027,
//           undefined,
//           400,
//           res
//         );
//       if (
//         !sender?.list.accepted.find((i) => i.accountId == receiverId) &&
//         receiverId != req.user.accountId
//       )
//         return Utils.createError(
//           "errors.com.epicgames.friends.no_relationship",
//           `User ${req.user.accountId} is not friends with ${receiverId}`,
//           [req.user.accountId, receiverId],
//           28004,
//           undefined,
//           403,
//           res
//         );
//     }
//     if (!profile.items) profile.items = {};
//     if (!athena.items) athena.items = {};
//     let findOfferId = Utils.getOfferID(req.body.offerId,req.headers["user-agent"]);
//     if (!findOfferId)
//       return Utils.createError(
//         "errors.com.epicgames.fortnite.id_invalid",
//         `Offer ID (id: '${req.body.offerId}') not found`,
//         [req.body.offerId],
//         16027,
//         undefined,
//         400,
//         res
//       );
//     switch (true) {
//       case /^BR(Daily|Weekly)Storefront$/.test(findOfferId.name):
//         if (
//           findOfferId.offerId.prices[0].currencyType.toLowerCase() ==
//           "mtxcurrency"
//         ) {
//           let paid = false;
//           let price =
//             findOfferId.offerId.prices[0].finalPrice *
//             req.body.receiverAccountIds.length;
//           for (let key in profile.items) {
//             if (
//               !profile.items[key].templateId
//                 .toLowerCase()
//                 .startsWith("currency:mtx")
//             )
//               continue;
//             let currencyPlatform = profile.items[key].attributes.platform;
//             if (
//               currencyPlatform.toLowerCase() !=
//                 profile.stats.attributes.current_mtx_platform.toLowerCase() &&
//               currencyPlatform.toLowerCase() != "shared"
//             )
//               continue;
//             if (profile.items[key].quantity < price)
//               return Utils.createError(
//                 "errors.com.epicgames.currency.mtx.insufficient",
//                 `You can not afford this item (${price}), you only have ${profile.items[key].quantity}.`,
//                 [`${price}`, `${profile.items[key].quantity}`],
//                 1040,
//                 undefined,
//                 400,
//                 res
//               );
//             profile.items[key].quantity -= price;
//             ApplyProfileChanges.push({
//               changeType: "itemQuantityChanged",
//               itemId: key,
//               quantity: profile.items[key].quantity,
//             });

//             paid = true;
//             break;
//           }
//           if (!paid && price > 0)
//             return Utils.createError(
//               "errors.com.epicgames.currency.mtx.insufficient",
//               `You can not afford this item.`,
//               [],
//               1040,
//               undefined,
//               400,
//               res
//             );
//           if (/^BR(Daily|Weekly)Storefront$/.test(findOfferId.name)) {
//             global.codes = JSON.parse(
//               fs.readFileSync("responses/SAC.json").toString()
//             );
//             var SAC = global.codes.findIndex(
//               (f) =>
//                 f.code.toLowerCase() ==
//                 profile.stats.attributes.mtx_affiliate.toLowerCase()
//             );
//             if (SAC != -1) {
//               var code = global.codes[SAC].accountId;
//               var x = await Profile.findOne({ accountId: code });
//               var u = await User.findOne({ accountId: code });

//               var a = x?.profiles["common_core"];
//               a.items["Currency:MtxPurchased"].quantity += Math.floor(
//                 findOfferId.offerId.prices[0].finalPrice *
//                   req.body.receiverAccountIds.length *
//                   0.2
//               );
//               await x?.updateOne({ $set: { [`profiles.common_core`]: a } });

//               await axios.post(
//                 "http://127.0.0.1:3551/fortnite/api/game/v3/profile/*/client/emptygift",
//                 {
//                   offerId: "e406693aa12adbc8b04ba7e6409c8ab3d598e8c3",
//                   currency: "MtxCurrency",
//                   currencySubType: "",
//                   expectedTotalPrice: "0",
//                   gameContext: "",
//                   receiverAccountIds: [u.accountId],
//                   giftWrapTemplateId: "GiftBox:gb_makegood",
//                   personalMessage: "Your personal message here",
//                   accountId: code,
//                   playerName: u.username,
//                 }
//               );
//             }
//           }
//         }
//         for (let receiverId of req.body.receiverAccountIds) {
//           const receiverProfiles = await Profile.findOne({
//             accountId: receiverId,
//           });
//           let athena = receiverProfiles?.profiles["athena"];
//           let common_core = receiverProfiles?.profiles["common_core"];
//           if (!athena.items) athena.items = {};
//           if (!common_core.stats.attributes.allowed_to_receive_gifts)
//             return Utils.createError(
//               "errors.com.epicgames.user.gift_disabled",
//               `User ${receiverId} has disabled receiving gifts.`,
//               [receiverId],
//               28004,
//               undefined,
//               403,
//               res
//             );
//           for (let itemGrant of findOfferId.offerId.itemGrants) {
//             for (let itemId in athena.items) {
//               if (
//                 itemGrant.templateId.toLowerCase() ==
//                 athena.items[itemId].templateId.toLowerCase()
//               )
//                 return Utils.createError(
//                   "errors.com.epicgames.modules.gamesubcatalog.purchase_not_allowed",
//                   `User ${receiverId} already owns this item.`,
//                   [receiverId],
//                   28004,
//                   undefined,
//                   403,
//                   res
//                 );
//             }
//           }
//         }
//         for (let receiverId of req.body.receiverAccountIds) {
//           const receiverProfiles = await Profile.findOne({
//             accountId: receiverId,
//           });
//           let athena = receiverProfiles?.profiles["athena"];
//           let common_core =
//             receiverId == req.user.accountId
//               ? profile
//               : receiverProfiles?.profiles["common_core"];
//           let giftBoxItemID = v4();
//           let giftBoxItem = {
//             templateId: req.body.giftWrapTemplateId,
//             attributes: {
//               fromAccountId: req.user.accountId,
//               lootList: [],
//               params: {
//                 userMessage: req.body.personalMessage,
//               },
//               level: 1,
//               giftedOn: new Date().toISOString(),
//             },
//             quantity: 1,
//           };
//           if (!athena.items) athena.items = {};
//           if (!common_core.items) common_core.items = {};
//           for (let value of findOfferId.offerId.itemGrants) {
//             const ID = v4();
//             const Item = {
//               templateId: value.templateId,
//               attributes: {
//                 item_seen: false,
//                 variants: [],
//               },
//               quantity: 1,
//             };
//             athena.items[ID] = Item;
//             giftBoxItem.attributes.lootList.push({
//               itemType: Item.templateId,
//               itemGuid: ID,
//               itemProfile: "athena",
//               quantity: 1,
//             });
//           }
//           common_core.items[giftBoxItemID] = giftBoxItem;
//           if (receiverId == req.user.accountId)
//             ApplyProfileChanges.push({
//               changeType: "itemAdded",
//               itemId: giftBoxItemID,
//               item: common_core.items[giftBoxItemID],
//             });
//           athena.rvn += 1;
//           athena.commandRevision += 1;
//           athena.updated = new Date().toISOString();
//           common_core.rvn += 1;
//           common_core.commandRevision += 1;
//           common_core.updated = new Date().toISOString();
//           await receiverProfiles?.updateOne({
//             $set: {
//               [`profiles.athena`]: athena,
//               [`profiles.common_core`]: common_core,
//             },
//           });
//           global.giftReceived[receiverId] = true;
//           Utils.SendXMPPMessage(
//             {
//               type: "com.epicgames.gift.received",
//               payload: {},
//               timestamp: new Date().toISOString(),
//             },
//             receiverId
//           );
//         }
//         break;
//     }
//     if (
//       ApplyProfileChanges.length > 0 &&
//       !req.body.receiverAccountIds.includes(req.user.accountId)
//     ) {
//       profile.rvn += 1;
//       profile.commandRevision += 1;
//       profile.updated = new Date().toISOString();
//       await profiles?.updateOne({
//         $set: { [`profiles.${req.query.profileId}`]: profile },
//       });
//     }
//     if (QueryRevision != ProfileRevisionCheck) {
//       ApplyProfileChanges = [
//         {
//           changeType: "fullProfileUpdate",
//           profile: profile,
//         },
//       ];
//     }
//     res.json({
//       profileRevision: profile.rvn || 0,
//       profileId: req.query.profileId,
//       profileChangesBaseRevision: BaseRevision,
//       profileChanges: ApplyProfileChanges,
//       notifications: Notifications,
//       profileCommandRevision: profile.commandRevision || 0,
//       serverTime: new Date().toISOString(),
//       responseVersion: 1,
//     });
//   }
// );

app.post(
  "/fortnite/api/game/v2/profile/*/client/RemoveGiftBox",
  verifyToken,
  async (req, res) => {
    const profiles = await Profile.findOne({ accountId: req.user.accountId });
    if (!(await profileManager.validateProfile(req.query.profileId, profiles)))
      return Utils.createError(
        "errors.com.epicgames.modules.profiles.operation_forbidden",
        `Unable to find template configuration for profile ${req.query.profileId}`,
        [req.query.profileId],
        12813,
        undefined,
        403,
        res
      );
    let profile = profiles?.profiles[req.query.profileId];
    if (
      req.query.profileId != "common_core" &&
      req.query.profileId != "profile0"
    )
      return Utils.createError(
        "errors.com.epicgames.modules.profiles.invalid_command",
        `RemoveGiftBox is not valid on ${req.query.profileId} profile`,
        ["RemoveGiftBox", req.query.profileId],
        12801,
        undefined,
        400,
        res
      );
    const memory = Utils.GetVersion(req);
    let ApplyProfileChanges = [];
    let BaseRevision = profile.rvn;
    let ProfileRevisionCheck =
      memory.build >= 12.2 ? profile.commandRevision : profile.rvn;
    let QueryRevision = req.query.rvn || -1;
    if (
      typeof req.body.giftBoxItemId == "string" &&
      profile.items[req.body.giftBoxItemId]
    ) {
      if (!profile.items[req.body.giftBoxItemId])
        return Utils.createError(
          "errors.com.epicgames.fortnite.id_invalid",
          `Item (id: '${req.body.giftBoxItemId}') not found`,
          [req.body.giftBoxItemId],
          16027,
          undefined,
          400,
          res
        );
      if (
        !profile.items[req.body.giftBoxItemId].templateId.startsWith("GiftBox:")
      )
        return Utils.createError(
          "errors.com.epicgames.fortnite.id_invalid",
          `The specified item id is not a giftbox.`,
          [req.body.giftBoxItemId],
          16027,
          undefined,
          400,
          res
        );
      delete profile.items[req.body.giftBoxItemId];
      ApplyProfileChanges.push({
        changeType: "itemRemoved",
        itemId: req.body.giftBoxItemId,
      });
    }
    if (Array.isArray(req.body.giftBoxItemIds)) {
      for (let giftBoxItemId of req.body.giftBoxItemIds) {
        if (typeof giftBoxItemId != "string") continue;
        if (!profile.items[giftBoxItemId]) continue;
        if (!profile.items[giftBoxItemId].templateId.startsWith("GiftBox:"))
          continue;
        delete profile.items[giftBoxItemId];
        ApplyProfileChanges.push({
          changeType: "itemRemoved",
          itemId: giftBoxItemId,
        });
      }
    }
    if (ApplyProfileChanges.length > 0) {
      profile.rvn += 1;
      profile.commandRevision += 1;
      profile.updated = new Date().toISOString();
    }
    if (QueryRevision != ProfileRevisionCheck) {
      ApplyProfileChanges = [
        {
          changeType: "fullProfileUpdate",
          profile: profile,
        },
      ];
    }
    res.json({
      profileRevision: profile.rvn || 0,
      profileId: req.query.profileId,
      profileChangesBaseRevision: BaseRevision,
      profileChanges: ApplyProfileChanges,
      profileCommandRevision: profile.commandRevision || 0,
      serverTime: new Date().toISOString(),
      responseVersion: 1,
    });
    if (ApplyProfileChanges.length > 0)
      await profiles?.updateOne({
        $set: { [`profiles.${req.query.profileId}`]: profile },
      });
  }
);
app.post('/fortnite/api/game/v2/profile/*/client/PurchaseCatalogEntry',verifyToken,async(req,res,next)=>{
 const build=clientBuild(req.headers['user-agent']);
 if(!['1.11','2.5','1.8.2','1.7.2'].includes(build))return next();
 if(![season2PassOffer,season2TierOffer].includes(req.body.offerId))return next();
 if(!['common_core','profile0'].includes(req.query.profileId))return res.sendStatus(400);
 try{
  const doc=await Profile.collection.findOne({accountId:req.user.accountId});if(!doc)return res.sendStatus(404);
  const ar=doc.profiles.athena.rvn,cr=doc.profiles.common_core.rvn;
  purchaseSeason2(doc.profiles,req.body.offerId,req.body.purchaseQuantity);
  const saved=await Profile.collection.updateOne({accountId:req.user.accountId,'profiles.athena.rvn':ar,'profiles.common_core.rvn':cr},{$set:{'profiles.athena':doc.profiles.athena,'profiles.common_core':doc.profiles.common_core}});
  if(!saved.matchedCount)return res.status(409).json({errorCode:'errors.com.syndicate.profile_busy'});
  const response=(p,id,base)=>({profileRevision:p.rvn,profileId:id,profileChangesBaseRevision:base,profileChanges:[{changeType:'fullProfileUpdate',profile:{...p,profileId:id}}],profileCommandRevision:p.commandRevision});
  return res.json({...response(doc.profiles.common_core,req.query.profileId,cr),multiUpdate:[response(doc.profiles.athena,'athena',ar)],serverTime:new Date().toISOString(),responseVersion:1});
 }catch(error){res.status(400).json({errorCode:'errors.com.syndicate.purchase_failed',errorMessage:error.message});}
});
app.post("/fortnite/api/game/v2/profile/*/client/PurchaseCatalogEntry",verifyToken,async(req,res,next)=>{
 const build=clientBuild(req.headers['user-agent']);
 const tierSeasonNum=tierSeason(build);if(req.body.offerId!==tierOfferIds[tierSeasonNum])return next();
 if(isSeasonTwoBuild(clientBuild(req.headers["user-agent"])))return res.status(400).json({errorCode:'errors.com.syndicate.wrong_season_offer'});
 if(!['common_core','profile0'].includes(req.query.profileId))return res.status(400).json({errorCode:'errors.com.syndicate.invalid_profile',errorMessage:'Purchase tiers on common_core.'});
 try{
  // Retried when the profile changed under us (a lobby refresh saving at the same moment): that used to fail the
  // purchase with "Profile changed", which the game shows as "can't buy tiers".
  for(let attempt=0;;attempt++){
  const doc=await Profile.collection.findOne({accountId:req.user.accountId});
  if(!doc)throw Error('Account profile missing.');
  const a=doc.profiles.athena.rvn,c=doc.profiles.common_core.rvn;
  if(tierSeasonNum!==4 && Number(doc.profiles.athena.stats.attributes.season_num)!==tierSeasonNum)throw Error(`Open Season ${tierSeasonNum===10?'X':tierSeasonNum} before buying its tiers.`);
  const before=ownedSnapshot(doc.profiles.athena);
  purchaseTiers(doc.profiles,req.body.purchaseQuantity);
  {const n=req.body.purchaseQuantity;addRewardGift(doc.profiles.common_core,addedLoot(doc.profiles.athena,before),'BATTLE PASS',`${n} tier${n===1?'':'s'} purchased. You are now tier ${doc.profiles.athena.stats.attributes.book_level}.`);}
  const saved=await Profile.collection.updateOne({accountId:req.user.accountId,'profiles.athena.rvn':a,'profiles.common_core.rvn':c},{$set:{'profiles.athena':doc.profiles.athena,'profiles.common_core':doc.profiles.common_core}});
  if(!saved.modifiedCount){if(attempt<4)continue;throw Error('Profile changed. Please retry.');}
  const p=doc.profiles.common_core;
  return res.json({profileRevision:p.rvn,profileId:req.query.profileId,profileChangesBaseRevision:c,profileChanges:[{changeType:'fullProfileUpdate',profile:p}],profileCommandRevision:p.commandRevision,multiUpdate:[{profileRevision:doc.profiles.athena.rvn,profileId:'athena',profileChangesBaseRevision:a,profileChanges:[{changeType:'fullProfileUpdate',profile:doc.profiles.athena}],profileCommandRevision:doc.profiles.athena.commandRevision}],serverTime:new Date().toISOString(),responseVersion:1});
  }
 }catch(error){return res.status(400).json({errorCode:'errors.com.syndicate.tier_purchase',errorMessage:error.message});}
});

// Pre-season / Season 1 Season Shop (syndicateSeason1Shop.js): each item needs its Season Level. The generic handler
// below only checked V-Bucks, so Renegade Raider (level 20) could be bought at level 5.
app.post('/fortnite/api/game/v2/profile/*/client/PurchaseCatalogEntry',verifyToken,async(req,res,next)=>{
 if(!['1.7.2','1.8.2'].includes(clientBuild(req.headers['user-agent'])))return next();
 const offer=typeof req.body?.offerId==='string'?Utils.getOfferID(req.body.offerId,req.headers['user-agent']):null;
 if(offer?.name!=='BRSeasonStorefront')return next();
 if(!['common_core','profile0'].includes(req.query.profileId))return res.status(400).json({errorCode:'errors.com.syndicate.invalid_profile'});
 try{
  const doc=await Profile.collection.findOne({accountId:req.user.accountId});if(!doc)return res.sendStatus(404);
  const ar=doc.profiles.athena.rvn,cr=doc.profiles.common_core.rvn;
  const loot=purchaseSeason1Item(doc.profiles,offer.offerId,req.body.purchaseQuantity);
  const saved=await Profile.collection.updateOne({accountId:req.user.accountId,'profiles.athena.rvn':ar,'profiles.common_core.rvn':cr},{$set:{'profiles.athena':doc.profiles.athena,'profiles.common_core':doc.profiles.common_core}});
  if(!saved.matchedCount)return res.status(409).json({errorCode:'errors.com.syndicate.profile_busy'});
  const response=(p,id,base)=>({profileRevision:p.rvn,profileId:id,profileChangesBaseRevision:base,profileChanges:[{changeType:'fullProfileUpdate',profile:{...p,profileId:id}}],profileCommandRevision:p.commandRevision});
  return res.json({...response(doc.profiles.common_core,req.query.profileId,cr),multiUpdate:[response(doc.profiles.athena,'athena',ar)],notifications:[{type:'CatalogPurchase',primary:true,lootResult:{items:loot}}],serverTime:new Date().toISOString(),responseVersion:1});
 }catch(error){return res.status(400).json({errorCode:'errors.com.syndicate.purchase_failed',errorMessage:error.message,numericErrorCode:16027});}
});
app.post('/fortnite/api/game/v2/profile/*/client/PurchaseCatalogEntry',verifyToken,async(req,res,next)=>{
 if(!isSeasonTwoBuild(clientBuild(req.headers['user-agent'])))return next();
 if(!['common_core','profile0'].includes(req.query.profileId))return res.status(400).json({errorCode:'errors.com.syndicate.invalid_profile'});
 try{
  const offer=Utils.getOfferID(req.body.offerId,req.headers['user-agent']);
  if(!offer || !/^BR(Daily|Weekly)Storefront$/.test(offer.name))throw Error('Offer not found in the Season 2 shop');
  const doc=await Profile.collection.findOne({accountId:req.user.accountId});if(!doc)return res.sendStatus(404);
  const ar=doc.profiles.athena.rvn,cr=doc.profiles.common_core.rvn;
  const loot=purchaseSeason2Item(doc.profiles,offer.offerId,req.body.purchaseQuantity);
  const saved=await Profile.collection.updateOne({accountId:req.user.accountId,'profiles.athena.rvn':ar,'profiles.common_core.rvn':cr},{$set:{'profiles.athena':doc.profiles.athena,'profiles.common_core':doc.profiles.common_core}});
  if(!saved.matchedCount)return res.status(409).json({errorCode:'errors.com.syndicate.profile_busy'});
  const response=(p,id,base)=>({profileRevision:p.rvn,profileId:id,profileChangesBaseRevision:base,profileChanges:[{changeType:'fullProfileUpdate',profile:p}],profileCommandRevision:p.commandRevision});
  const primary=req.query.profileId==='profile0'?doc.profiles.profile0:doc.profiles.common_core;
  return res.json({...response(primary,req.query.profileId,req.query.profileId==='profile0'?primary.rvn:cr),multiUpdate:[response(doc.profiles.athena,'athena',ar)],notifications:[{type:'CatalogPurchase',primary:true,lootResult:{items:loot}}],serverTime:new Date().toISOString(),responseVersion:1});
 }catch(error){return res.status(400).json({errorCode:'errors.com.syndicate.purchase_failed',errorMessage:error.message});}
});
app.post(
  "/fortnite/api/game/v2/profile/*/client/PurchaseCatalogEntry",
  verifyToken,
  async (req, res) => {
    const profiles = await Profile.findOne({ accountId: req.user.accountId });
    if (!(await profileManager.validateProfile(req.query.profileId, profiles)))
      return Utils.createError(
        "errors.com.epicgames.modules.profiles.operation_forbidden",
        `Unable to find template configuration for profile ${req.query.profileId}`,
        [req.query.profileId],
        12813,
        undefined,
        403,
        res
      );
    let profile = profiles?.profiles[req.query.profileId];
    let athena = profiles?.profiles["athena"];
    if (
      req.query.profileId != "common_core" &&
      req.query.profileId != "profile0"
    )
      return Utils.createError(
        "errors.com.epicgames.modules.profiles.invalid_command",
        `PurchaseCatalogEntry is not valid on ${req.query.profileId} profile`,
        ["PurchaseCatalogEntry", req.query.profileId],
        12801,
        undefined,
        400,
        res
      );
    let MultiUpdate = [
      {
        profileRevision: athena.rvn || 0,
        profileId: "athena",
        profileChangesBaseRevision: athena.rvn || 0,
        profileChanges: [],
        profileCommandRevision: athena.commandRevision || 0,
      },
    ];
    const memory = Utils.GetVersion(req);
    let Notifications = [];
    let ApplyProfileChanges = [];
    let BaseRevision = profile.rvn;
    let ProfileRevisionCheck =
      memory.build >= 12.2 ? profile.commandRevision : profile.rvn;
    let QueryRevision = req.query.rvn || -1;
    let missingFields = checkFields(["offerId"], req.body);
    var ItemExists = false;
    if (missingFields.fields.length > 0)
      return Utils.createError(
        "errors.com.epicgames.validation.validation_failed",
        `Validation Failed. [${missingFields.fields.join(
          ", "
        )}] field(s) is missing.`,
        [`[${missingFields.fields.join(", ")}]`],
        1040,
        undefined,
        400,
        res
      );
    if (typeof req.body.offerId != "string")
      return ValidationError("offerId", "a string", res);
    if (typeof req.body.purchaseQuantity != "number")
      return ValidationError("purchaseQuantity", "a number", res);
    if (req.body.purchaseQuantity < 1)
      return Utils.createError(
        "errors.com.epicgames.validation.validation_failed",
        `Validation Failed. 'purchaseQuantity' is less than 1.`,
        ["purchaseQuantity"],
        1040,
        undefined,
        400,
        res
      );
    if (!profile.items) profile.items = {};
    if (!athena.items) athena.items = {};
    let findOfferId = Utils.getOfferID(req.body.offerId,req.headers["user-agent"]);
    if (!findOfferId)
      return Utils.createError(
        "errors.com.epicgames.fortnite.id_invalid",
        `Offer ID (id: '${req.body.offerId}') not found`,
        [req.body.offerId],
        16027,
        undefined,
        400,
        res
      );
    var value = findOfferId;

    if (value.name.startsWith("BRSeason")) {
      if (!Number.isNaN(Number(value.name.split("BRSeason")[1]))) {
        var offer = findOfferId.offerId;

        if (offer) {
          if (MultiUpdate.length == 0) {
            MultiUpdate.push({
              profileRevision: athena.rvn || 0,
              profileId: "athena",
              profileChangesBaseRevision: athena.rvn || 0,
              profileChanges: [],
              profileCommandRevision: athena.commandRevision || 0,
            });
          }

          var Season = value.name.split("BR")[1];
          var BattlePass = require(`../local/BattlePass/Jsons/${Season}.json`);
          const boolLevel = athena.stats.attributes.book_level;

          if (BattlePass) {
            if (
              BattlePass.battlePassOfferId == offer.offerId ||
              BattlePass.battleBundleOfferId == offer.offerId
            ) {
              var lootList = [];
              var EndingTier = athena.stats.attributes.book_level;
              athena.stats.attributes.book_purchased = true;
              if (boolLevel == 0) {
                athena.stats.attributes.book_level += 1;
              }

              if (BattlePass.battleBundleOfferId == offer.offerId) {
                athena.stats.attributes.book_level += 25;
                if (athena.stats.attributes.book_level > 100)
                  athena.stats.attributes.book_level = 100;
                EndingTier = athena.stats.attributes.book_level;
                // Chapter 2: the tier is the season level (applyProgression sets book_level back to it), so the
                // bundle's 25 tiers are added to the level too; before, they vanished on the next match.
                if (Number(Season.replace("Season", "")) >= 11)
                  athena.stats.attributes.level = Math.max(Number(athena.stats.attributes.level) || 1, athena.stats.attributes.book_level);
              }

              for (var i = 0; i < EndingTier; i++) {
                var FreeTier = BattlePass.freeRewards[i] || {};
                var PaidTier = BattlePass.paidRewards[i] || {};

                for (var item in FreeTier) {
                  if (item.toLowerCase() == "token:athenaseasonxpboost") {
                    athena.stats.attributes.season_match_boost +=
                      FreeTier[item];

                    MultiUpdate[0].profileChanges.push({
                      changeType: "statModified",
                      name: "season_match_boost",
                      value: athena.stats.attributes.season_match_boost,
                    });
                  }

                  if (item.toLowerCase() == "token:athenaseasonfriendxpboost") {
                    athena.stats.attributes.season_friend_match_boost +=
                      FreeTier[item];

                    MultiUpdate[0].profileChanges.push({
                      changeType: "statModified",
                      name: "season_friend_match_boost",
                      value: athena.stats.attributes.season_friend_match_boost,
                    });
                  }

                  if (item.toLowerCase().startsWith("currency:mtx")) {
                    for (var key in profile.items) {
                      if (
                        profile.items[key].templateId
                          .toLowerCase()
                          .startsWith("currency:mtx")
                      ) {
                        if (
                          profile.items[
                            key
                          ].attributes.platform.toLowerCase() ==
                            profile.stats.attributes.current_mtx_platform.toLowerCase() ||
                          profile.items[
                            key
                          ].attributes.platform.toLowerCase() == "shared"
                        ) {
                          profile.items[key].quantity += FreeTier[item];
                          break;
                        }
                      }
                    }
                  }

                  if (
                    typeof item === "string" &&
                    item.toLowerCase().startsWith("homebasebanner")
                  ) {
                    for (var key in profile.items) {
                      if (
                        profile.items[key] &&
                        profile.items[key].templateId &&
                        typeof profile.items[key].templateId === "string" &&
                        profile.items[key].templateId.toLowerCase() ===
                          item.toLowerCase()
                      ) {
                        profile.items[key].attributes.item_seen = false;
                        ItemExists = true;

                        ApplyProfileChanges.push({
                          changeType: "itemAttrChanged",
                          itemId: key,
                          attributeName: "item_seen",
                          attributeValue:
                            profile.items[key].attributes.item_seen,
                        });
                      }
                    }

                    if (ItemExists == false) {
                      var ItemID = v4();
                      var Item = {
                        templateId: item,
                        attributes: { item_seen: false },
                        quantity: 1,
                      };

                      profile.items[ItemID] = Item;

                      ApplyProfileChanges.push({
                        changeType: "itemAdded",
                        itemId: ItemID,
                        item: Item,
                      });
                    }

                    ItemExists = false;
                  }

                  if (item.toLowerCase().startsWith("athena")) {
                    for (var key in athena.items) {
                      if (
                        athena.items[key].templateId.toLowerCase() ==
                        item.toLowerCase()
                      ) {
                        athena.items[key].attributes.item_seen = false;
                        ItemExists = true;

                        MultiUpdate[0].profileChanges.push({
                          changeType: "itemAttrChanged",
                          itemId: key,
                          attributeName: "item_seen",
                          attributeValue:
                            athena.items[key].attributes.item_seen,
                        });
                      }
                    }

                    if (ItemExists == false) {
                      var ItemID = v4();
                      var Item = {
                        templateId: item,
                        attributes: {
                          max_level_bonus: 0,
                          level: 1,
                          item_seen: false,
                          xp: 0,
                          variants: [],
                          favorite: false,
                        },
                        quantity: FreeTier[item],
                      };

                      athena.items[ItemID] = Item;

                      MultiUpdate[0].profileChanges.push({
                        changeType: "itemAdded",
                        itemId: ItemID,
                        item: Item,
                      });
                    }

                    ItemExists = false;
                  }

                  lootList.push({
                    itemType: item,
                    itemGuid: item,
                    quantity: FreeTier[item],
                  });
                }

                for (var item in PaidTier) {
                  if (item.toLowerCase() == "token:athenaseasonxpboost") {
                    athena.stats.attributes.season_match_boost +=
                      PaidTier[item];

                    MultiUpdate[0].profileChanges.push({
                      changeType: "statModified",
                      name: "season_match_boost",
                      value: athena.stats.attributes.season_match_boost,
                    });
                  }

                  if (item.toLowerCase() == "token:athenaseasonfriendxpboost") {
                    athena.stats.attributes.season_friend_match_boost +=
                      PaidTier[item];

                    MultiUpdate[0].profileChanges.push({
                      changeType: "statModified",
                      name: "season_friend_match_boost",
                      value: athena.stats.attributes.season_friend_match_boost,
                    });
                  }

                  if (item.toLowerCase().startsWith("currency:mtx")) {
                    for (var key in profile.items) {
                      if (
                        profile.items[key].templateId
                          .toLowerCase()
                          .startsWith("currency:mtx")
                      ) {
                        if (
                          profile.items[
                            key
                          ].attributes.platform.toLowerCase() ==
                            profile.stats.attributes.current_mtx_platform.toLowerCase() ||
                          profile.items[
                            key
                          ].attributes.platform.toLowerCase() == "shared"
                        ) {
                          ApplyProfileChanges.push({
                            changeType: "itemQuantityChanged",
                            itemId: key,
                            quantity: profile.items[key].quantity,
                          });
                          profile.items[key].quantity += PaidTier[item];
                          break;
                        }
                      }
                    }
                  }

                  if (
                    typeof item === "string" &&
                    item.toLowerCase().startsWith("homebasebanner")
                  ) {
                    for (var key in profile.items) {
                      if (
                        profile.items[key] &&
                        profile.items[key].templateId &&
                        typeof profile.items[key].templateId === "string" &&
                        profile.items[key].templateId.toLowerCase() ===
                          item.toLowerCase()
                      ) {
                        profile.items[key].attributes.item_seen = false;
                        ItemExists = true;

                        ApplyProfileChanges.push({
                          changeType: "itemAttrChanged",
                          itemId: key,
                          attributeName: "item_seen",
                          attributeValue:
                            profile.items[key].attributes.item_seen,
                        });
                      }
                    }

                    if (ItemExists == false) {
                      var ItemID = v4();
                      var Item = {
                        templateId: item,
                        attributes: { item_seen: false },
                        quantity: 1,
                      };

                      profile.items[ItemID] = Item;

                      ApplyProfileChanges.push({
                        changeType: "itemAdded",
                        itemId: ItemID,
                        item: Item,
                      });
                    }

                    ItemExists = false;
                  }

                  if (item.toLowerCase().startsWith("athena")) {
                    for (var key in athena.items) {
                      if (
                        athena.items[key].templateId.toLowerCase() ==
                        item.toLowerCase()
                      ) {
                        athena.items[key].attributes.item_seen = false;
                        ItemExists = true;

                        MultiUpdate[0].profileChanges.push({
                          changeType: "itemAttrChanged",
                          itemId: key,
                          attributeName: "item_seen",
                          attributeValue:
                            athena.items[key].attributes.item_seen,
                        });
                      }
                    }

                    if (ItemExists == false) {
                      var ItemID = v4();
                      var Item = {
                        templateId: item,
                        attributes: {
                          max_level_bonus: 0,
                          level: 1,
                          item_seen: false,
                          xp: 0,
                          variants: [],
                          favorite: false,
                        },
                        quantity: PaidTier[item],
                      };

                      athena.items[ItemID] = Item;

                      MultiUpdate[0].profileChanges.push({
                        changeType: "itemAdded",
                        itemId: ItemID,
                        item: Item,
                      });
                    }

                    ItemExists = false;
                  }

                  if (findOfferId.offerId.devName.endsWith(".SingleTier.01"))
                    for (var key in profile.items) {
                      if (
                        profile.items[key].templateId
                          .toLowerCase()
                          .startsWith("currency:mtx")
                      ) {
                        if (
                          profile.items[
                            key
                          ].attributes.platform.toLowerCase() ==
                            profile.stats.attributes.current_mtx_platform.toLowerCase() ||
                          profile.items[
                            key
                          ].attributes.platform.toLowerCase() == "shared"
                        ) {
                          profile.items[key].quantity -= 150;
                          ApplyProfileChanges.push({
                            changeType: "itemQuantityChanged",
                            itemId: key,
                            quantity: profile.items[key].quantity,
                          });
                          break;
                        }
                      }
                    }
                  lootList.push({
                    itemType: item,
                    itemGuid: item,
                    quantity: PaidTier[item],
                  });
                }
              }

              var GiftBoxID = v4();
              var GiftBox = {
                templateId:
                  Number(Season.split("Season")[1]) <= 4
                    ? "GiftBox:gb_battlepass"
                    : "GiftBox:gb_battlepasspurchased",
                attributes: {
                  max_level_bonus: 0,
                  fromAccountId: TILTED_SENDER_ID,
                  lootList: lootList,
                },
              };

              if (Number(Season.split("Season")[1]) > 2) {
                profile.items[GiftBoxID] = GiftBox;

                ApplyProfileChanges.push({
                  changeType: "itemAdded",
                  itemId: GiftBoxID,
                  item: GiftBox,
                });
              }

              MultiUpdate[0].profileChanges.push({
                changeType: "statModified",
                name: "book_purchased",
                value: athena.stats.attributes.book_purchased,
              });

              MultiUpdate[0].profileChanges.push({
                changeType: "statModified",
                name: "book_level",
                value: athena.stats.attributes.book_level,
              });
            }

            if (BattlePass.tierOfferId == offer.offerId) {
              var lootList = [];
              var StartingTier = athena.stats.attributes.book_level;
              var EndingTier;
              athena.stats.attributes.book_level +=
                req.body.purchaseQuantity || 1;
              EndingTier = athena.stats.attributes.book_level;

              for (var i = StartingTier; i < EndingTier; i++) {
                var FreeTier = BattlePass.freeRewards[i] || {};
                var PaidTier = BattlePass.paidRewards[i] || {};

                for (var item in FreeTier) {
                  if (item.toLowerCase() == "token:athenaseasonxpboost") {
                    athena.stats.attributes.season_match_boost +=
                      FreeTier[item];

                    MultiUpdate[0].profileChanges.push({
                      changeType: "statModified",
                      name: "season_match_boost",
                      value: athena.stats.attributes.season_match_boost,
                    });
                  }

                  if (item.toLowerCase() == "token:athenaseasonfriendxpboost") {
                    athena.stats.attributes.season_friend_match_boost +=
                      FreeTier[item];

                    MultiUpdate[0].profileChanges.push({
                      changeType: "statModified",
                      name: "season_friend_match_boost",
                      value: athena.stats.attributes.season_friend_match_boost,
                    });
                  }

                  if (item.toLowerCase().startsWith("currency:mtx")) {
                    for (var key in profile.items) {
                      if (
                        profile.items[key].templateId
                          .toLowerCase()
                          .startsWith("currency:mtx")
                      ) {
                        if (
                          profile.items[
                            key
                          ].attributes.platform.toLowerCase() ==
                            profile.stats.attributes.current_mtx_platform.toLowerCase() ||
                          profile.items[
                            key
                          ].attributes.platform.toLowerCase() == "shared"
                        ) {
                          profile.items[key].quantity += FreeTier[item];
                          break;
                        }
                      }
                    }
                  }

                  if (item.toLowerCase().startsWith("homebasebanner")) {
                    for (var key in profile.items) {
                      if (
                        profile.items[key].templateId.toLowerCase() ==
                        item.toLowerCase()
                      ) {
                        profile.items[key].attributes.item_seen = false;
                        ItemExists = true;

                        ApplyProfileChanges.push({
                          changeType: "itemAttrChanged",
                          itemId: key,
                          attributeName: "item_seen",
                          attributeValue:
                            profile.items[key].attributes.item_seen,
                        });
                      }
                    }

                    if (ItemExists == false) {
                      var ItemID = v4();
                      var Item = {
                        templateId: item,
                        attributes: { item_seen: false },
                        quantity: 1,
                      };

                      profile.items[ItemID] = Item;

                      ApplyProfileChanges.push({
                        changeType: "itemAdded",
                        itemId: ItemID,
                        item: Item,
                      });
                    }

                    ItemExists = false;
                  }

                  if (item.toLowerCase().startsWith("athena")) {
                    for (var key in athena.items) {
                      if (
                        athena.items[key].templateId.toLowerCase() ==
                        item.toLowerCase()
                      ) {
                        athena.items[key].attributes.item_seen = false;
                        ItemExists = true;

                        MultiUpdate[0].profileChanges.push({
                          changeType: "itemAttrChanged",
                          itemId: key,
                          attributeName: "item_seen",
                          attributeValue:
                            athena.items[key].attributes.item_seen,
                        });
                      }
                    }

                    if (ItemExists == false) {
                      var ItemID = v4();
                      var Item = {
                        templateId: item,
                        attributes: {
                          max_level_bonus: 0,
                          level: 1,
                          item_seen: false,
                          xp: 0,
                          variants: [],
                          favorite: false,
                        },
                        quantity: FreeTier[item],
                      };

                      athena.items[ItemID] = Item;

                      MultiUpdate[0].profileChanges.push({
                        changeType: "itemAdded",
                        itemId: ItemID,
                        item: Item,
                      });
                    }

                    ItemExists = false;
                  }

                  lootList.push({
                    itemType: NursingHome,
                    itemGuid: item,
                    quantity: FreeTier[item],
                  });
                }

                for (var item in PaidTier) {
                  if (item.toLowerCase() == "token:athenaseasonxpboost") {
                    athena.stats.attributes.season_match_boost +=
                      PaidTier[item];

                    MultiUpdate[0].profileChanges.push({
                      changeType: "statModified",
                      name: "season_match_boost",
                      value: athena.stats.attributes.season_match_boost,
                    });
                  }

                  if (item.toLowerCase() == "token:athenaseasonfriendxpboost") {
                    athena.stats.attributes.season_friend_match_boost +=
                      PaidTier[item];

                    MultiUpdate[0].profileChanges.push({
                      changeType: "statModified",
                      name: "season_friend_match_boost",
                      value: athena.stats.attributes.season_friend_match_boost,
                    });
                  }

                  if (item.toLowerCase().startsWith("currency:mtx")) {
                    for (var key in profile.items) {
                      if (
                        profile.items[key].templateId
                          .toLowerCase()
                          .startsWith("currency:mtx")
                      ) {
                        if (
                          profile.items[
                            key
                          ].attributes.platform.toLowerCase() ==
                            profile.stats.attributes.current_mtx_platform.toLowerCase() ||
                          profile.items[
                            key
                          ].attributes.platform.toLowerCase() == "shared"
                        ) {
                          profile.items[key].quantity += PaidTier[item];
                          ApplyProfileChanges.push({
                            changeType: "itemQuantityChanged",
                            itemId: key,
                            quantity: profile.items[key].quantity,
                          });
                          break;
                        }
                      }
                    }
                  }

                  if (item.toLowerCase().startsWith("homebasebanner")) {
                    for (var key in profile.items) {
                      if (
                        profile.items[key].templateId.toLowerCase() ==
                        item.toLowerCase()
                      ) {
                        profile.items[key].attributes.item_seen = false;
                        ItemExists = true;

                        ApplyProfileChanges.push({
                          changeType: "itemAttrChanged",
                          itemId: key,
                          attributeName: "item_seen",
                          attributeValue:
                            profile.items[key].attributes.item_seen,
                        });
                      }
                    }

                    if (ItemExists == false) {
                      var ItemID = v4();
                      var Item = {
                        templateId: item,
                        attributes: { item_seen: false },
                        quantity: 1,
                      };

                      profile.items[ItemID] = Item;

                      ApplyProfileChanges.push({
                        changeType: "itemAdded",
                        itemId: ItemID,
                        item: Item,
                      });
                    }

                    ItemExists = false;
                  }

                  if (item.toLowerCase().startsWith("athena")) {
                    for (var key in athena.items) {
                      if (
                        athena.items[key].templateId.toLowerCase() ==
                        item.toLowerCase()
                      ) {
                        athena.items[key].attributes.item_seen = false;
                        ItemExists = true;

                        MultiUpdate[0].profileChanges.push({
                          changeType: "itemAttrChanged",
                          itemId: key,
                          attributeName: "item_seen",
                          attributeValue:
                            athena.items[key].attributes.item_seen,
                        });
                      }
                    }

                    if (ItemExists == false) {
                      var ItemID = v4();
                      var Item = {
                        templateId: item,
                        attributes: {
                          max_level_bonus: 0,
                          level: 1,
                          item_seen: false,
                          xp: 0,
                          variants: [],
                          favorite: false,
                        },
                        quantity: PaidTier[item],
                      };

                      athena.items[ItemID] = Item;

                      MultiUpdate[0].profileChanges.push({
                        changeType: "itemAdded",
                        itemId: ItemID,
                        item: Item,
                      });
                    }

                    ItemExists = false;
                  }

                  if (findOfferId.offerId.devName.endsWith(".SingleTier.01"))
                    for (var key in profile.items) {
                      if (
                        profile.items[key].templateId
                          .toLowerCase()
                          .startsWith("currency:mtx")
                      ) {
                        if (
                          profile.items[
                            key
                          ].attributes.platform.toLowerCase() ==
                            profile.stats.attributes.current_mtx_platform.toLowerCase() ||
                          profile.items[
                            key
                          ].attributes.platform.toLowerCase() == "shared"
                        ) {
                          profile.items[key].quantity -= 150;
                          ApplyProfileChanges.push({
                            changeType: "itemQuantityChanged",
                            itemId: key,
                            quantity: profile.items[key].quantity,
                          });
                          break;
                        }
                      }
                    }
                  lootList.push({
                    itemType: item,
                    itemGuid: item,
                    quantity: PaidTier[item],
                  });
                }
              }

              var GiftBoxID = v4();
              var GiftBox = {
                templateId: "GiftBox:gb_battlepass",
                attributes: {
                  max_level_bonus: 0,
                  fromAccountId: TILTED_SENDER_ID,
                  lootList: lootList,
                },
              };

              if (Number(Season.split("Season")[1]) > 2) {
                profile.items[GiftBoxID] = GiftBox;

                ApplyProfileChanges.push({
                  changeType: "itemAdded",
                  itemId: GiftBoxID,
                  item: GiftBox,
                });
              }

              MultiUpdate[0].profileChanges.push({
                changeType: "statModified",
                name: "book_level",
                value: athena.stats.attributes.book_level,
              });
            }
          }
        }
      }
    }
    switch (true) {
      case /^BR(Daily|Weekly|Season)(Storefront|([0-9]{1,2}))$/.test(
        findOfferId.name
      ):
        Notifications.push({
          type: "CatalogPurchase",
          primary: true,
          lootResult: {
            items: [],
          },
        });
        for (let value of Utils.offerGrants(findOfferId.offerId)) {
          // A bundle you partly own still grants the items you are missing: skip any item already in the
          // locker (whatever its position in the offer) and grant the rest. Owning the back bling used to
          // abort the whole purchase, so the skin and other pieces were never granted.
          if (Object.values(athena.items).some(i => i.templateId?.toLowerCase() === value.templateId.toLowerCase())) continue;
          const ID = v4();
          const Item = {
            templateId: value.templateId,
            attributes: {
              item_seen: false,
              variants: [],
            },
            quantity: 1,
          };
          athena.items[ID] = Item;
          MultiUpdate[0].profileChanges.push({
            changeType: "itemAdded",
            itemId: ID,
            item: athena.items[ID],
          });
          Notifications[0].lootResult.items.push({
            itemType: Item.templateId,
            itemGuid: ID,
            itemProfile: "athena",
            quantity: 1,
          });
        }
        // Everything in this offer is already owned: do not charge for nothing.
        if (Notifications[0].lootResult.items.length === 0)
          return Utils.createError(
            "errors.com.epicgames.offer.already_owned",
            `You have already bought this item before.`,
            undefined,
            1040,
            undefined,
            400,
            res
          );
        // A bundle (several items in one offer) gets the reward pop-up listing what it gave.
        if (Notifications[0].lootResult.items.length > 1 && req.query.profileId === "common_core") {
          const gift = addRewardGift(profile, Notifications[0].lootResult.items.map(i => ({ ...i })), "ITEM SHOP", "Your bundle is ready.");
          if (gift) ApplyProfileChanges.push({ changeType: "itemAdded", itemId: gift[0], item: gift[1] });
        }
        for (var p of findOfferId.offerId.prices) {
          if (p.currencyType.toLowerCase() == "mtxcurrency") {
            // Debit the shared wallet (common_core). Legacy builds buy on profile0, which only mirrors that
            // balance, so debiting profile0 would be reset by the wallet sync and the V-Bucks never drop.
            const sharedWallet = req.query.profileId === "profile0" ? profiles.profiles.common_core : profile;
            const result = debitShopWallet(sharedWallet, findOfferId.offerId.devName.endsWith(".SingleTier.01") ? 0 : p.finalPrice);
            if (!result.ok) return Utils.createError(
              "errors.com.epicgames.currency.mtx.insufficient",
              `You can not afford this item (${p.finalPrice}), you only have ${result.balance}.`,
              [String(p.finalPrice), String(result.balance)], 1040, undefined, 400, res);
            ApplyProfileChanges.push(...result.changes);
          }
        }
        if (MultiUpdate[0].profileChanges.length > 0) {
          athena.rvn += 1;
          athena.commandRevision += 1;
          athena.updated = new Date().toISOString();
          MultiUpdate[0].profileRevision = athena.rvn;
          MultiUpdate[0].profileCommandRevision = athena.commandRevision;
        }
        break;
    }

    if (ApplyProfileChanges.length > 0) {
      profile.rvn += 1;
      profile.commandRevision += 1;
      profile.updated = new Date().toISOString();
    }
    if (QueryRevision != ProfileRevisionCheck) {
      ApplyProfileChanges = [
        {
          changeType: "fullProfileUpdate",
          profile: profile,
        },
      ];
    }
    if (ApplyProfileChanges.length > 0 || MultiUpdate[0].profileChanges.length > 0) {
      await profiles?.updateOne({
        $set: {
          [`profiles.${req.query.profileId}`]: profile,
          [`profiles.athena`]: athena,
          // Legacy purchases debit common_core (above), so it must be persisted too; the wallet sync mirrors
          // the new balance back into profile0 on the next request.
          ...(req.query.profileId === "profile0" ? { [`profiles.common_core`]: profiles.profiles.common_core } : {}),
        },
      });
    }
    res.json({
      profileRevision: profile.rvn || 0,
      profileId: req.query.profileId,
      profileChangesBaseRevision: BaseRevision,
      profileChanges: ApplyProfileChanges,
      notifications: Notifications,
      profileCommandRevision: profile.commandRevision || 0,
      serverTime: new Date().toISOString(),
      multiUpdate: MultiUpdate,
      responseVersion: 1,
    });

  }
);
app.post(
  "/fortnite/api/game/v2/profile/*/client/MarkItemSeen",
  verifyToken,
  async (req, res) => {
    const profiles = await Profile.findOne({ accountId: req.user.accountId });
    if (!(await profileManager.validateProfile(req.query.profileId, profiles)))
      return Utils.createError(
        "errors.com.epicgames.modules.profiles.operation_forbidden",
        `Unable to find template configuration for profile ${req.query.profileId}`,
        [req.query.profileId],
        12813,
        undefined,
        403,
        res
      );
    let profile = profiles?.profiles[req.query.profileId];
    const memory = Utils.GetVersion(req);
    if (req.query.profileId == "athena")
      profile.stats.attributes.season_num = memory.season;
    let ApplyProfileChanges = [];
    let BaseRevision = profile.rvn;
    let ProfileRevisionCheck =
      memory.build >= 12.2 ? profile.commandRevision : profile.rvn;
    let QueryRevision = req.query.rvn || -1;
    let missingFields = checkFields(["itemIds"], req.body);
    if (missingFields.fields.length > 0)
      return Utils.createError(
        "errors.com.epicgames.validation.validation_failed",
        `Validation Failed. [${missingFields.fields.join(
          ", "
        )}] field(s) is missing.`,
        [`[${missingFields.fields.join(", ")}]`],
        1040,
        undefined,
        400,
        res
      );
    if (!Array.isArray(req.body.itemIds))
      return ValidationError("itemIds", "an array", res);
    if (!profile.items) profile.items = {};
    for (let i in req.body.itemIds) {
      if (!profile.items[req.body.itemIds[i]]) continue;
      profile.items[req.body.itemIds[i]].attributes.item_seen = true;
      ApplyProfileChanges.push({
        changeType: "itemAttrChanged",
        itemId: req.body.itemIds[i],
        attributeName: "item_seen",
        attributeValue: true,
      });
    }
    if (ApplyProfileChanges.length > 0) {
      profile.rvn += 1;
      profile.commandRevision += 1;
      profile.updated = new Date().toISOString();
      //        await profiles.updateOne({ $set: { [`profiles.${req.query.profileId}`]: profile } });
    }
    if (QueryRevision != ProfileRevisionCheck) {
      ApplyProfileChanges = [
        {
          changeType: "fullProfileUpdate",
          profile: profile,
        },
      ];
    }
    res.json({
      profileRevision: profile.rvn || 0,
      profileId: req.query.profileId,
      profileChangesBaseRevision: BaseRevision,
      profileChanges: ApplyProfileChanges,
      profileCommandRevision: profile.commandRevision || 0,
      serverTime: new Date().toISOString(),
      responseVersion: 1,
    });
    if (ApplyProfileChanges.length > 0)
      await profiles?.updateOne({
        $set: { [`profiles.${req.query.profileId}`]: profile },
      });
  }
);
app.post(
  "/fortnite/api/game/v2/profile/*/client/SetItemFavoriteStatusBatch",
  verifyToken,
  async (req, res) => {
    const profiles = await Profile.findOne({ accountId: req.user.accountId });
    if (!(await profileManager.validateProfile(req.query.profileId, profiles)))
      return Utils.createError(
        "errors.com.epicgames.modules.profiles.operation_forbidden",
        `Unable to find template configuration for profile ${req.query.profileId}`,
        [req.query.profileId],
        12813,
        undefined,
        403,
        res
      );
    if (req.query.profileId != "athena")
      return Utils.createError(
        "errors.com.epicgames.modules.profiles.invalid_command",
        `SetItemFavoriteStatusBatch is not valid on ${req.query.profileId} profile`,
        ["SetItemFavoriteStatusBatch", req.query.profileId],
        12801,
        undefined,
        400,
        res
      );
    let profile = profiles?.profiles[req.query.profileId];
    const memory = Utils.GetVersion(req);
    if (req.query.profileId == "athena")
      profile.stats.attributes.season_num = memory.season;
    let ApplyProfileChanges = [];
    let BaseRevision = profile.rvn;
    let ProfileRevisionCheck =
      memory.build >= 12.2 ? profile.commandRevision : profile.rvn;
    let QueryRevision = req.query.rvn || -1;
    let missingFields = checkFields(["itemIds", "itemFavStatus"], req.body);
    if (missingFields.fields.length > 0)
      return Utils.createError(
        "errors.com.epicgames.validation.validation_failed",
        `Validation Failed. [${missingFields.fields.join(
          ", "
        )}] field(s) is missing.`,
        [`[${missingFields.fields.join(", ")}]`],
        1040,
        undefined,
        400,
        res
      );
    if (!Array.isArray(req.body.itemIds))
      return ValidationError("itemIds", "an array", res);
    if (!Array.isArray(req.body.itemFavStatus))
      return ValidationError("itemFavStatus", "an array", res);
    if (!profile.items) profile.items = {};
    for (let i in req.body.itemIds) {
      if (!profile.items[req.body.itemIds[i]]) continue;
      if (typeof req.body.itemFavStatus[i] != "boolean") continue;
      profile.items[req.body.itemIds[i]].attributes.favorite =
        req.body.itemFavStatus[i];
      ApplyProfileChanges.push({
        changeType: "itemAttrChanged",
        itemId: req.body.itemIds[i],
        attributeName: "favorite",
        attributeValue: profile.items[req.body.itemIds[i]].attributes.favorite,
      });
    }
    if (ApplyProfileChanges.length > 0) {
      profile.rvn += 1;
      profile.commandRevision += 1;
      profile.updated = new Date().toISOString();
      //        await profiles?.updateOne({ $set: { [`profiles.${req.query.profileId}`]: profile } });
    }
    if (QueryRevision != ProfileRevisionCheck) {
      ApplyProfileChanges = [
        {
          changeType: "fullProfileUpdate",
          profile: profile,
        },
      ];
    }
    res.json({
      profileRevision: profile.rvn || 0,
      profileId: req.query.profileId,
      profileChangesBaseRevision: BaseRevision,
      profileChanges: ApplyProfileChanges,
      profileCommandRevision: profile.commandRevision || 0,
      serverTime: new Date().toISOString(),
      responseVersion: 1,
    });
    if (ApplyProfileChanges.length > 0)
      await profiles?.updateOne({
        $set: { [`profiles.${req.query.profileId}`]: profile },
      });
  }
);
app.post(
  "/fortnite/api/game/v2/profile/*/client/SetBattleRoyaleBanner",
  verifyToken,
  async (req, res) => {
    const profiles = await Profile.findOne({ accountId: req.user.accountId });
    if (!(await profileManager.validateProfile(req.query.profileId, profiles)))
      return Utils.createError(
        "errors.com.epicgames.modules.profiles.operation_forbidden",
        `Unable to find template configuration for profile ${req.query.profileId}`,
        [req.query.profileId],
        12813,
        undefined,
        403,
        res
      );
    if (req.query.profileId != "athena")
      return Utils.createError(
        "errors.com.epicgames.modules.profiles.invalid_command",
        `SetBattleRoyaleBanner is not valid on ${req.query.profileId} profile`,
        ["SetBattleRoyaleBanner", req.query.profileId],
        12801,
        undefined,
        400,
        res
      );
    let profile = profiles?.profiles[req.query.profileId];
    const memory = Utils.GetVersion(req);
    if (req.query.profileId == "athena")
      profile.stats.attributes.season_num = memory.season;
    let ApplyProfileChanges = [];
    let BaseRevision = profile.rvn;
    let ProfileRevisionCheck =
      memory.build >= 12.2 ? profile.commandRevision : profile.rvn;
    let QueryRevision = req.query.rvn || -1;
    let missingFields = checkFields(
      ["homebaseBannerIconId", "homebaseBannerColorId"],
      req.body
    );
    if (missingFields.fields.length > 0)
      return Utils.createError(
        "errors.com.epicgames.validation.validation_failed",
        `Validation Failed. [${missingFields.fields.join(
          ", "
        )}] field(s) is missing.`,
        [`[${missingFields.fields.join(", ")}]`],
        1040,
        undefined,
        400,
        res
      );
    if (typeof req.body.homebaseBannerIconId != "string")
      return ValidationError("homebaseBannerIconId", "a string", res);
    if (typeof req.body.homebaseBannerColorId != "string")
      return ValidationError("homebaseBannerColorId", "a string", res);
    let bannerProfileId = memory.build < 3.5 ? "profile0" : "common_core";
    let HomebaseBannerIconID = "";
    let HomebaseBannerColorID = "";
    if (!profiles?.profiles[bannerProfileId].items) {
      if (profiles) {
        profiles.profiles[bannerProfileId].items = {};
      }
    }
    for (let itemId in profiles?.profiles[bannerProfileId].items) {
      let templateId =
        profiles?.profiles[bannerProfileId].items[itemId].templateId;
      if (
        templateId.toLowerCase() ==
        `HomebaseBannerIcon:${req.body.homebaseBannerIconId}`.toLowerCase()
      ) {
        HomebaseBannerIconID = itemId;
        continue;
      }
      if (
        templateId.toLowerCase() ==
        `HomebaseBannerColor:${req.body.homebaseBannerColorId}`.toLowerCase()
      ) {
        HomebaseBannerColorID = itemId;
        continue;
      }
      if (HomebaseBannerIconID && HomebaseBannerColorID) break;
    }
    if (!HomebaseBannerIconID)
      return Utils.createError(
        "errors.com.epicgames.fortnite.item_not_found",
        `Banner template 'HomebaseBannerIcon:${req.body.homebaseBannerIconId}' not found in profile`,
        [`HomebaseBannerIcon:${req.body.homebaseBannerIconId}`],
        16006,
        undefined,
        400,
        res
      );
    if (!HomebaseBannerColorID)
      return Utils.createError(
        "errors.com.epicgames.fortnite.item_not_found",
        `Banner template 'HomebaseBannerColor:${req.body.homebaseBannerColorId}' not found in profile`,
        [`HomebaseBannerColor:${req.body.homebaseBannerColorId}`],
        16006,
        undefined,
        400,
        res
      );
    if (!profile.items) profile.items = {};
    let activeLoadoutId =
      profile.stats.attributes.loadouts[
        profile.stats.attributes.active_loadout_index
      ];
    profile.stats.attributes.banner_icon = req.body.homebaseBannerIconId;
    profile.stats.attributes.banner_color = req.body.homebaseBannerColorId;
    profile.items[activeLoadoutId].attributes.banner_icon_template =
      req.body.homebaseBannerIconId;
    profile.items[activeLoadoutId].attributes.banner_color_template =
      req.body.homebaseBannerColorId;
    ApplyProfileChanges.push({
      changeType: "statModified",
      name: "banner_icon",
      value: profile.stats.attributes.banner_icon,
    });
    ApplyProfileChanges.push({
      changeType: "statModified",
      name: "banner_color",
      value: profile.stats.attributes.banner_color,
    });
    if (ApplyProfileChanges.length > 0) {
      profile.rvn += 1;
      profile.commandRevision += 1;
      profile.updated = new Date().toISOString();
      //        await profiles?.updateOne({ $set: { [`profiles.${req.query.profileId}`]: profile } });
    }
    if (QueryRevision != ProfileRevisionCheck) {
      ApplyProfileChanges = [
        {
          changeType: "fullProfileUpdate",
          profile: profile,
        },
      ];
    }
    res.json({
      profileRevision: profile.rvn || 0,
      profileId: req.query.profileId,
      profileChangesBaseRevision: BaseRevision,
      profileChanges: ApplyProfileChanges,
      profileCommandRevision: profile.commandRevision || 0,
      serverTime: new Date().toISOString(),
      responseVersion: 1,
    });
    if (ApplyProfileChanges.length > 0)
      await profiles?.updateOne({
        $set: { [`profiles.${req.query.profileId}`]: profile },
      });
  }
);
// Syndicate custom outfits (syndicateCustomOutfits.js): equipping one saves the pick with its base outfit as the
// character; equipping any other outfit clears the pick (and is then handled below as usual).
app.post('/fortnite/api/game/v2/profile/*/client/EquipBattleRoyaleCustomization',verifyToken,async(req,res,next)=>{
 const build=clientBuild(req.headers['user-agent']);
 if(req.query.profileId!=='athena'||req.body?.slotName!=='Character'||!customOutfitBuilds.includes(build))return next();
 try{
  for(let attempt=0;attempt<4;attempt++){
   const doc=await Profile.collection.findOne({accountId:req.user.accountId});if(!doc?.profiles?.athena)return next();
   const p=doc.profiles.athena,a=p.stats.attributes,base=p.rvn,outfit=customOutfitForItemKey(req.body?.itemToSlot);
   if(outfit?!a.tilted_unlock_all||!outfit.builds.includes(build):!a.tilted_custom_outfit)return next();
   if(outfit){
    a.tilted_custom_outfit=outfit.name;delete a.tilted_stw_hero;
    const template='AthenaCharacter:'+outfit.base;
    a.favorite_character=Object.entries(p.items||{}).find(([,item])=>String(item?.templateId).toLowerCase()===template.toLowerCase())?.[0]||template;
   }else delete a.tilted_custom_outfit;
   p.rvn++;p.commandRevision++;p.updated=new Date().toISOString();
   const saved=await Profile.collection.updateOne({accountId:req.user.accountId,'profiles.athena.rvn':base},{$set:{'profiles.athena':p}});
   if(!saved.matchedCount)continue;
   if(!outfit)return next();
   try{await publishCharacter(req.user.accountId,p);}catch(error){console.warn('[Syndicate] Could not save the custom outfit for the match:',error.message);}
   return res.json({profileRevision:p.rvn,profileId:'athena',profileChangesBaseRevision:base,profileChanges:[{changeType:'fullProfileUpdate',profile:p}],profileCommandRevision:p.commandRevision,serverTime:new Date().toISOString(),responseVersion:1});
  }
  res.status(409).json({errorCode:'errors.com.syndicate.profile_busy'});
 }catch(error){next(error);}
});
// Save the World heroes in the lockers (syndicateStwHeroes.js): equipping one saves the hero pick and keeps
// the outfit underneath; equipping any other outfit clears the pick (and is then handled below as usual).
app.post('/fortnite/api/game/v2/profile/*/client/EquipBattleRoyaleCustomization',verifyToken,async(req,res,next)=>{
 const build=clientBuild(req.headers['user-agent']);
 if(req.query.profileId!=='athena'||req.body?.slotName!=='Character'||!stwLockerBuilds.includes(build))return next();
 try{
  for(let attempt=0;attempt<4;attempt++){
   const doc=await Profile.collection.findOne({accountId:req.user.accountId});if(!doc?.profiles?.athena)return next();
   const p=doc.profiles.athena,a=p.stats.attributes,base=p.rvn,hero=heroForItemKey(String(req.body?.itemToSlot||''));
   if(hero?!stwSkinsOn(a):!a.tilted_stw_hero)return next();
   if(hero)a.tilted_stw_hero=hero;else delete a.tilted_stw_hero;
   p.rvn++;p.commandRevision++;p.updated=new Date().toISOString();
   const saved=await Profile.collection.updateOne({accountId:req.user.accountId,'profiles.athena.rvn':base},{$set:{'profiles.athena':p}});
   if(!saved.matchedCount)continue;
   if(!hero)return next();
   const legacy=['1.7.2','1.8.2','1.11','2.5'].includes(build);
   try{await publishCharacter(req.user.accountId,legacy?project111Profile(p,build):p);}catch(error){console.warn('[Syndicate] Could not save the hero for the match:',error.message);}
   return res.json({profileRevision:p.rvn,profileId:'athena',profileChangesBaseRevision:base,profileChanges:[{changeType:'fullProfileUpdate',profile:legacy?p:withStwHeroes(p,build)}],profileCommandRevision:p.commandRevision,serverTime:new Date().toISOString(),responseVersion:1});
  }
  res.status(409).json({errorCode:'errors.com.syndicate.profile_busy'});
 }catch(error){next(error);}
});
app.post(
  "/fortnite/api/game/v2/profile/*/client/EquipBattleRoyaleCustomization",
  verifyToken,
  async (req, res) => {
    // log.debug("Body: " + JSON.stringify(req.body));
    const profiles = await Profile.findOne({ accountId: req.user.accountId });
    if (!(await profileManager.validateProfile(req.query.profileId, profiles)))
      return Utils.createError(
        "errors.com.epicgames.modules.profiles.operation_forbidden",
        `Unable to find template configuration for profile ${req.query.profileId}`,
        [req.query.profileId],
        12813,
        undefined,
        403,
        res
      );
    if (req.query.profileId != "athena")
      return Utils.createError(
        "errors.com.epicgames.modules.profiles.invalid_command",
        `EquipBattleRoyaleCustomization is not valid on ${req.query.profileId} profile`,
        ["EquipBattleRoyaleCustomization", req.query.profileId],
        12801,
        undefined,
        400,
        res
      );
    let profile = profiles?.profiles[req.query.profileId];
    const memory = Utils.GetVersion(req);
    if (req.query.profileId == "athena")
      profile.stats.attributes.season_num = memory.season;
    let ApplyProfileChanges = [];
    let BaseRevision = profile.rvn;
    let ProfileRevisionCheck =
      memory.build >= 12.2 ? profile.commandRevision : profile.rvn;
    let QueryRevision = req.query.rvn || -1;
    let specialCosmetics = [
      "AthenaCharacter:cid_random",
      "AthenaBackpack:bid_random",
      "AthenaPickaxe:pickaxe_random",
      "AthenaGlider:glider_random",
      "AthenaSkyDiveContrail:trails_random",
      "AthenaItemWrap:wrap_random",
      "AthenaMusicPack:musicpack_random",
      "AthenaLoadingScreen:lsid_random",
    ];
    let missingFields = checkFields(["slotName"], req.body);
    if (missingFields.fields.length > 0)
      return Utils.createError(
        "errors.com.epicgames.validation.validation_failed",
        `Validation Failed. [${missingFields.fields.join(
          ", "
        )}] field(s) is missing.`,
        [`[${missingFields.fields.join(", ")}]`],
        1040,
        undefined,
        400,
        res
      );
    if (typeof req.body.itemToSlot != "string")
      return ValidationError("itemToSlot", "a string", res);
    if (typeof req.body.indexWithinSlot != "number" && memory.season > 1)
      return ValidationError("indexWithinSlot", "a number", res);
    if (typeof req.body.slotName != "string")
      return ValidationError("slotName", "a string", res);
    if (!profile.items) profile.items = {};
    if (!profile.items[req.body.itemToSlot] && req.body.itemToSlot) {
      let item = req.body.itemToSlot;
      if (!specialCosmetics.includes(item)) {
        return Utils.createError(
          "errors.com.epicgames.fortnite.id_invalid",
          `Item (id: '${req.body.itemToSlot}') not found`,
          [req.body.itemToSlot],
          16027,
          undefined,
          400,
          res
        );
      } else {
        if (!item.startsWith(`Athena${req.body.slotName}:`))
          return Utils.createError(
            "errors.com.epicgames.fortnite.id_invalid",
            `Cannot slot item of type ${
              item.split(":")[0]
            } in slot of category ${req.body.slotName}`,
            [item.split(":")[0], req.body.slotName],
            16027,
            undefined,
            400,
            res
          );
      }
    }
    if (profile.items[req.body.itemToSlot]) {
      if (
        !profile.items[req.body.itemToSlot].templateId.startsWith(
          `Athena${req.body.slotName}:`
        )
      )
        return Utils.createError(
          "errors.com.epicgames.fortnite.id_invalid",
          `Cannot slot item of type ${
            profile.items[req.body.itemToSlot].templateId.split(":")[0]
          } in slot of category ${req.body.slotName}`,
          [
            profile.items[req.body.itemToSlot].templateId.split(":")[0],
            req.body.slotName,
          ],
          16027,
          undefined,
          400,
          res
        );
      if(Utils.GetVersion(req).season===4)ensureSeason4Styles(profile);
      let Variants = req.body.variantUpdates;
      if (Array.isArray(Variants)) {
        for(const update of Variants)applyOwnedVariant(profile.items[req.body.itemToSlot],update);
        ApplyProfileChanges.push({
          changeType: "itemAttrChanged",
          itemId: req.body.itemToSlot,
          attributeName: "variants",
          attributeValue:
            profile.items[req.body.itemToSlot].attributes.variants,
        });
      }
    }
    let slotNames = [
      "Character",
      "Backpack",
      "Pickaxe",
      "Glider",
      "SkyDiveContrail",
      "MusicPack",
      "LoadingScreen",
    ];
    let activeLoadoutId =
      profile.stats.attributes.loadouts[
        profile.stats.attributes.active_loadout_index
      ];
    let templateId = profile.items[req.body.itemToSlot]
      ? profile.items[req.body.itemToSlot].templateId
      : req.body.itemToSlot;
    switch (req.body.slotName) {
      case "Dance":
        if (
          !profile.items[activeLoadoutId].attributes.locker_slots_data.slots[
            req.body.slotName
          ]
        )
          break;
        if (typeof req.body.indexWithinSlot != "number")
          return ValidationError("indexWithinSlot", "a number", res);
        if (req.body.indexWithinSlot >= 0 && req.body.indexWithinSlot <= 5) {
          profile.stats.attributes.favorite_dance[req.body.indexWithinSlot] =
            req.body.itemToSlot;
          profile.items[
            activeLoadoutId
          ].attributes.locker_slots_data.slots.Dance.items[
            req.body.indexWithinSlot
          ] = templateId;
          ApplyProfileChanges.push({
            changeType: "statModified",
            name: "favorite_dance",
            value: profile.stats.attributes["favorite_dance"],
          });
        }
        break;
      case "ItemWrap":
        if (
          !profile.items[activeLoadoutId].attributes.locker_slots_data.slots[
            req.body.slotName
          ]
        )
          break;
        if (typeof req.body.indexWithinSlot != "number")
          return ValidationError("indexWithinSlot", "a number", res);
        switch (true) {
          case req.body.indexWithinSlot >= 0 && req.body.indexWithinSlot <= 7:
            profile.stats.attributes.favorite_itemwraps[
              req.body.indexWithinSlot
            ] = req.body.itemToSlot;
            profile.items[
              activeLoadoutId
            ].attributes.locker_slots_data.slots.ItemWrap.items[
              req.body.indexWithinSlot
            ] = templateId;
            ApplyProfileChanges.push({
              changeType: "statModified",
              name: "favorite_itemwraps",
              value: profile.stats.attributes["favorite_itemwraps"],
            });
            break;
          case req.body.indexWithinSlot == -1:
            for (let i = 0; i < 7; i++) {
              profile.stats.attributes.favorite_itemwraps[i] =
                req.body.itemToSlot;
              profile.items[
                activeLoadoutId
              ].attributes.locker_slots_data.slots.ItemWrap.items[i] =
                templateId;
            }
            ApplyProfileChanges.push({
              changeType: "statModified",
              name: "favorite_itemwraps",
              value: profile.stats.attributes["favorite_itemwraps"],
            });
            break;
        }
        break;
      default:
        if (!slotNames.includes(req.body.slotName)) break;
        if (
          !profile.items[activeLoadoutId].attributes.locker_slots_data.slots[
            req.body.slotName
          ]
        )
          break;
        if (req.body.slotName == "Pickaxe" || req.body.slotName == "Glider") {
          if (!req.body.itemToSlot)
            return Utils.createError(
              "errors.com.epicgames.fortnite.id_invalid",
              `${req.body.slotName} can not be empty.`,
              [req.body.slotName],
              16027,
              undefined,
              400,
              res
            );
        }
        profile.stats.attributes[
          `favorite_${req.body.slotName}`.toLowerCase()
        ] = req.body.itemToSlot;
        profile.items[activeLoadoutId].attributes.locker_slots_data.slots[
          req.body.slotName
        ].items = [templateId];
        ApplyProfileChanges.push({
          changeType: "statModified",
          name: `favorite_${req.body.slotName}`.toLowerCase(),
          value:
            profile.stats.attributes[
              `favorite_${req.body.slotName}`.toLowerCase()
            ],
        });
        break;
    }
    if (ApplyProfileChanges.length > 0) {
      profile.rvn += 1;
      profile.commandRevision += 1;
      profile.updated = new Date().toISOString();
      //        await profiles?.updateOne({ $set: { [`profiles.${req.query.profileId}`]: profile } });
    }
    if (QueryRevision != ProfileRevisionCheck) {
      ApplyProfileChanges = [
        {
          changeType: "fullProfileUpdate",
          profile: profile,
        },
      ];
    }
    res.json({
      profileRevision: profile.rvn || 0,
      profileId: req.query.profileId,
      profileChangesBaseRevision: BaseRevision,
      profileChanges: ApplyProfileChanges,
      profileCommandRevision: profile.commandRevision || 0,
      serverTime: new Date().toISOString(),
      responseVersion: 1,
    });
    if (ApplyProfileChanges.length > 0)
      await profiles?.updateOne({
        $set: { [`profiles.${req.query.profileId}`]: profile },
      });
    // The match server's locker files are written after the reply, so "Saving Selection" doesn't wait for the disk.
    try{const build=clientBuild(req.headers['user-agent']);await publishCharacter(req.user.accountId,['1.11','2.5','1.8.2','1.7.2'].includes(build)?project111Profile(profile,build):profile);}
    catch(error){console.warn('[Syndicate] Could not save the locker for the match:',error.message);}
  }
);
app.post(
  "/fortnite/api/game/v2/profile/*/client/SetCosmeticLockerBanner",
  verifyToken,
  async (req, res) => {
    let profiles = await Profile.findOne({ accountId: req.user.accountId });
    if (!(await profileManager.validateProfile(req.query.profileId, profiles)))
      return Utils.createError(
        "errors.com.epicgames.modules.profiles.operation_forbidden",
        `Unable to find template configuration for profile ${req.query.profileId}`,
        [req.query.profileId],
        12813,
        undefined,
        403,
        res
      );
    if (req.query.profileId != "athena")
      return Utils.createError(
        "errors.com.epicgames.modules.profiles.invalid_command",
        `SetCosmeticLockerBanner is not valid on ${req.query.profileId} profile`,
        ["SetCosmeticLockerBanner", req.query.profileId],
        12801,
        undefined,
        400,
        res
      );
    let profile = profiles?.profiles[req.query.profileId];
    const memory = Utils.GetVersion(req);
    if (req.query.profileId == "athena")
      profile.stats.attributes.season_num = memory.season;
    let ApplyProfileChanges = [];
    let BaseRevision = profile.rvn;
    let ProfileRevisionCheck =
      memory.build >= 12.2 ? profile.commandRevision : profile.rvn;
    let QueryRevision = req.query.rvn || -1;
    let missingFields = checkFields(
      ["bannerIconTemplateName", "bannerColorTemplateName", "lockerItem"],
      req.body
    );
    if (missingFields.fields.length > 0)
      return Utils.createError(
        "errors.com.epicgames.validation.validation_failed",
        `Validation Failed. [${missingFields.fields.join(
          ", "
        )}] field(s) is missing.`,
        [`[${missingFields.fields.join(", ")}]`],
        1040,
        undefined,
        400,
        res
      );
    if (typeof req.body.lockerItem != "string")
      return ValidationError("lockerItem", "a string", res);
    if (typeof req.body.bannerIconTemplateName != "string")
      return ValidationError("bannerIconTemplateName", "a string", res);
    if (typeof req.body.bannerColorTemplateName != "string")
      return ValidationError("bannerColorTemplateName", "a string", res);
    if (!profile.items) profile.items = {};
    if (!profile.items[req.body.lockerItem])
      return Utils.createError(
        "errors.com.epicgames.fortnite.id_invalid",
        `Item (id: '${req.body.lockerItem}') not found`,
        [req.body.lockerItem],
        16027,
        undefined,
        400,
        res
      );
    if (
      profile.items[req.body.lockerItem].templateId.toLowerCase() !=
      "cosmeticlocker:cosmeticlocker_athena"
    )
      return Utils.createError(
        "errors.com.epicgames.fortnite.id_invalid",
        `lockerItem id is not a cosmeticlocker`,
        ["lockerItem"],
        16027,
        undefined,
        400,
        res
      );
    let bannerProfileId = "common_core";
    let HomebaseBannerIconID = "";
    let HomebaseBannerColorID = "";
    if (!profiles?.profiles[bannerProfileId].items) {
      if (profiles?.profiles[bannerProfileId]) {
        profiles.profiles[bannerProfileId].items = {};
      }
    }
    for (let itemId in profiles?.profiles[bannerProfileId].items) {
      let templateId =
        profiles?.profiles[bannerProfileId].items[itemId].templateId;
      if (
        templateId.toLowerCase() ==
        `HomebaseBannerIcon:${req.body.bannerIconTemplateName}`.toLowerCase()
      ) {
        HomebaseBannerIconID = itemId;
        continue;
      }
      if (
        templateId.toLowerCase() ==
        `HomebaseBannerColor:${req.body.bannerColorTemplateName}`.toLowerCase()
      ) {
        HomebaseBannerColorID = itemId;
        continue;
      }
      if (HomebaseBannerIconID && HomebaseBannerColorID) break;
    }
    if (!HomebaseBannerIconID)
      return Utils.createError(
        "errors.com.epicgames.fortnite.item_not_found",
        `Banner template 'HomebaseBannerIcon:${req.body.bannerIconTemplateName}' not found in profile`,
        [`HomebaseBannerIcon:${req.body.bannerIconTemplateName}`],
        16006,
        undefined,
        400,
        res
      );
    if (!HomebaseBannerColorID)
      return Utils.createError(
        "errors.com.epicgames.fortnite.item_not_found",
        `Banner template 'HomebaseBannerColor:${req.body.bannerColorTemplateName}' not found in profile`,
        [`HomebaseBannerColor:${req.body.bannerColorTemplateName}`],
        16006,
        undefined,
        400,
        res
      );
    profile.items[req.body.lockerItem].attributes.banner_icon_template =
      req.body.bannerIconTemplateName;
    profile.items[req.body.lockerItem].attributes.banner_color_template =
      req.body.bannerColorTemplateName;
    profile.stats.attributes.banner_icon = req.body.bannerIconTemplateName;
    profile.stats.attributes.banner_color = req.body.bannerColorTemplateName;
    ApplyProfileChanges.push({
      changeType: "itemAttrChanged",
      itemId: req.body.lockerItem,
      attributeName: "banner_icon_template",
      attributeValue:
        profile.items[req.body.lockerItem].attributes.banner_icon_template,
    });
    ApplyProfileChanges.push({
      changeType: "itemAttrChanged",
      itemId: req.body.lockerItem,
      attributeName: "banner_color_template",
      attributeValue:
        profile.items[req.body.lockerItem].attributes.banner_color_template,
    });
    if (ApplyProfileChanges.length > 0) {
      profile.rvn += 1;
      profile.commandRevision += 1;
      profile.updated = new Date().toISOString();
      //        await profiles?.updateOne({ $set: { [`profiles.${req.query.profileId}`]: profile } });
    }
    if (QueryRevision != ProfileRevisionCheck) {
      ApplyProfileChanges = [
        {
          changeType: "fullProfileUpdate",
          profile: profile,
        },
      ];
    }
    res.json({
      profileRevision: profile.rvn || 0,
      profileId: req.query.profileId,
      profileChangesBaseRevision: BaseRevision,
      profileChanges: ApplyProfileChanges,
      profileCommandRevision: profile.commandRevision || 0,
      serverTime: new Date().toISOString(),
      responseVersion: 1,
    });
    if (ApplyProfileChanges.length > 0)
      await profiles?.updateOne({
        $set: { [`profiles.${req.query.profileId}`]: profile },
      });
  }
);
// Season 7+ locker (SetCosmeticLockerSlot): slotting a Save the World hero outfit saves the hero pick (the outfit
// items only exist in what the client is sent, never in the saved profile); slotting any other outfit clears it.
app.post('/fortnite/api/game/v2/profile/*/client/SetCosmeticLockerSlot',verifyToken,async(req,res,next)=>{
 const build=clientBuild(req.headers['user-agent']);
 if(req.query.profileId!=='athena'||req.body?.category!=='Character'||!stwLockerBuilds.includes(build))return next();
 try{
  const hero=heroForTemplate(req.body?.itemToSlot);
  for(let attempt=0;attempt<4;attempt++){
   const doc=await Profile.collection.findOne({accountId:req.user.accountId});if(!doc?.profiles?.athena)return next();
   const p=doc.profiles.athena,a=p.stats.attributes,base=p.rvn;
   if(hero?!stwSkinsOn(a):!a.tilted_stw_hero)return next();
   if(hero)a.tilted_stw_hero=hero;else delete a.tilted_stw_hero;
   p.rvn++;p.commandRevision++;p.updated=new Date().toISOString();
   const saved=await Profile.collection.updateOne({accountId:req.user.accountId,'profiles.athena.rvn':base},{$set:{'profiles.athena':p}});
   if(!saved.matchedCount)continue;
   if(!hero)return next();
   try{await publishCharacter(req.user.accountId,p);}catch(error){console.warn('[Syndicate] Could not save the hero for the match:',error.message);}
   return res.json({profileRevision:p.rvn,profileId:'athena',profileChangesBaseRevision:base,profileChanges:[{changeType:'fullProfileUpdate',profile:withStwHeroes(p,build)}],profileCommandRevision:p.commandRevision,serverTime:new Date().toISOString(),responseVersion:1});
  }
  res.status(409).json({errorCode:'errors.com.syndicate.profile_busy'});
 }catch(error){next(error);}
});
app.post(
  "/fortnite/api/game/v2/profile/*/client/SetCosmeticLockerSlot",
  verifyToken,
  async (req, res) => {
    const profiles = await Profile.findOne({ accountId: req.user.accountId });
    if (!(await profileManager.validateProfile(req.query.profileId, profiles)))
      return Utils.createError(
        "errors.com.epicgames.modules.profiles.operation_forbidden",
        `Unable to find template configuration for profile ${req.query.profileId}`,
        [req.query.profileId],
        12813,
        undefined,
        403,
        res
      );
    if (req.query.profileId != "athena")
      return Utils.createError(
        "errors.com.epicgames.modules.profiles.invalid_command",
        `SetCosmeticLockerSlot is not valid on ${req.query.profileId} profile`,
        ["SetCosmeticLockerSlot", req.query.profileId],
        12801,
        undefined,
        400,
        res
      );
    let profile = profiles?.profiles[req.query.profileId];
    const memory = Utils.GetVersion(req);
    if (req.query.profileId == "athena")
      profile.stats.attributes.season_num = memory.season;
    let ApplyProfileChanges = [];
    let BaseRevision = profile.rvn;
    let ProfileRevisionCheck =
      memory.build >= 12.2 ? profile.commandRevision : profile.rvn;
    let QueryRevision = req.query.rvn || -1;
    let specialCosmetics = [
      "AthenaCharacter:cid_random",
      "AthenaBackpack:bid_random",
      "AthenaPickaxe:pickaxe_random",
      "AthenaGlider:glider_random",
      "AthenaSkyDiveContrail:trails_random",
      "AthenaItemWrap:wrap_random",
      "AthenaMusicPack:musicpack_random",
      "AthenaLoadingScreen:lsid_random",
    ];
    let missingFields = checkFields(["category", "lockerItem"], req.body);
    if (missingFields.fields.length > 0)
      return Utils.createError(
        "errors.com.epicgames.validation.validation_failed",
        `Validation Failed. [${missingFields.fields.join(
          ", "
        )}] field(s) is missing.`,
        [`[${missingFields.fields.join(", ")}]`],
        1040,
        undefined,
        400,
        res
      );
    if (typeof req.body.itemToSlot != "string")
      return ValidationError("itemToSlot", "a string", res);
    if (typeof req.body.slotIndex != "number")
      return ValidationError("slotIndex", "a number", res);
    if (typeof req.body.lockerItem != "string")
      return ValidationError("lockerItem", "a string", res);
    if (typeof req.body.category != "string")
      return ValidationError("category", "a string", res);
    if (!profile.items) profile.items = {};
    let itemToSlotID = "";
    if (req.body.itemToSlot) {
      for (let itemId in profile.items) {
        if (
          profile.items[itemId].templateId.toLowerCase() ==
          req.body.itemToSlot.toLowerCase()
        ) {
          itemToSlotID = itemId;
          break;
        }
      }
    }
    if (!profile.items[req.body.lockerItem])
      return Utils.createError(
        "errors.com.epicgames.fortnite.id_invalid",
        `Item (id: '${req.body.lockerItem}') not found`,
        [req.body.lockerItem],
        16027,
        undefined,
        400,
        res
      );
    if (
      profile.items[req.body.lockerItem].templateId.toLowerCase() !=
      "cosmeticlocker:cosmeticlocker_athena"
    )
      return Utils.createError(
        "errors.com.epicgames.fortnite.id_invalid",
        `lockerItem id is not a cosmeticlocker`,
        ["lockerItem"],
        16027,
        undefined,
        400,
        res
      );
    if (!profile.items[itemToSlotID] && req.body.itemToSlot) {
      let item = req.body.itemToSlot;
      if (!specialCosmetics.includes(item)) {
        return Utils.createError(
          "errors.com.epicgames.fortnite.id_invalid",
          `Item (id: '${req.body.itemToSlot}') not found`,
          [req.body.itemToSlot],
          16027,
          undefined,
          400,
          res
        );
      } else {
        if (!item.startsWith(`Athena${req.body.category}:`))
          return Utils.createError(
            "errors.com.epicgames.fortnite.id_invalid",
            `Cannot slot item of type ${
              item.split(":")[0]
            } in slot of category ${req.body.category}`,
            [item.split(":")[0], req.body.category],
            16027,
            undefined,
            400,
            res
          );
      }
    }
    if (profile.items[itemToSlotID]) {
      if (
        !profile.items[itemToSlotID].templateId.startsWith(
          `Athena${req.body.category}:`
        )
      )
        return Utils.createError(
          "errors.com.epicgames.fortnite.id_invalid",
          `Cannot slot item of type ${
            profile.items[itemToSlotID].templateId.split(":")[0]
          } in slot of category ${req.body.category}`,
          [
            profile.items[itemToSlotID].templateId.split(":")[0],
            req.body.category,
          ],
          16027,
          undefined,
          400,
          res
        );
      if(Utils.GetVersion(req).season===4)ensureSeason4Styles(profile);
      let Variants = req.body.variantUpdates;
      if (Array.isArray(Variants)) {
        for(const update of Variants)applyOwnedVariant(profile.items[itemToSlotID],update);
        ApplyProfileChanges.push({
          changeType: "itemAttrChanged",
          itemId: itemToSlotID,
          attributeName: "variants",
          attributeValue: profile.items[itemToSlotID].attributes.variants,
        });
      }
    }
    switch (req.body.category) {
      case "Dance":
        if (
          !profile.items[req.body.lockerItem].attributes.locker_slots_data
            .slots[req.body.category]
        )
          break;
        if (req.body.slotIndex >= 0 && req.body.slotIndex <= 5) {
          profile.items[
            req.body.lockerItem
          ].attributes.locker_slots_data.slots.Dance.items[req.body.slotIndex] =
            req.body.itemToSlot;
          profile.stats.attributes.favorite_dance[req.body.slotIndex] =
            itemToSlotID || req.body.itemToSlot;
          ApplyProfileChanges.push({
            changeType: "itemAttrChanged",
            itemId: req.body.lockerItem,
            attributeName: "locker_slots_data",
            attributeValue:
              profile.items[req.body.lockerItem].attributes.locker_slots_data,
          });
        }
        break;
      case "ItemWrap":
        if (
          !profile.items[req.body.lockerItem].attributes.locker_slots_data
            .slots[req.body.category]
        )
          break;
        switch (true) {
          case req.body.slotIndex >= 0 && req.body.slotIndex <= 7:
            profile.items[
              req.body.lockerItem
            ].attributes.locker_slots_data.slots.ItemWrap.items[
              req.body.slotIndex
            ] = req.body.itemToSlot;
            profile.stats.attributes.favorite_itemwraps[req.body.slotIndex] =
              itemToSlotID || req.body.itemToSlot;
            ApplyProfileChanges.push({
              changeType: "itemAttrChanged",
              itemId: req.body.lockerItem,
              attributeName: "locker_slots_data",
              attributeValue:
                profile.items[req.body.lockerItem].attributes.locker_slots_data,
            });
            break;
          case req.body.slotIndex == -1:
            for (let i = 0; i < 7; i++) {
              profile.items[
                req.body.lockerItem
              ].attributes.locker_slots_data.slots.ItemWrap.items[i] =
                req.body.itemToSlot;
              profile.stats.attributes.favorite_itemwraps[i] =
                itemToSlotID || req.body.itemToSlot;
            }
            ApplyProfileChanges.push({
              changeType: "itemAttrChanged",
              itemId: req.body.lockerItem,
              attributeName: "locker_slots_data",
              attributeValue:
                profile.items[req.body.lockerItem].attributes.locker_slots_data,
            });
            break;
        }
        break;
      default:
        if (
          !profile.items[req.body.lockerItem].attributes.locker_slots_data
            .slots[req.body.category]
        )
          break;
        if (req.body.category == "Pickaxe" || req.body.category == "Glider") {
          if (!req.body.itemToSlot)
            return Utils.createError(
              "errors.com.epicgames.fortnite.id_invalid",
              `${req.body.category} can not be empty.`,
              [req.body.category],
              16027,
              undefined,
              400,
              res
            );
        }
        profile.items[req.body.lockerItem].attributes.locker_slots_data.slots[
          req.body.category
        ].items = [req.body.itemToSlot];
        profile.stats.attributes[
          `favorite_${req.body.category}`.toLowerCase()
        ] = itemToSlotID || req.body.itemToSlot;
        ApplyProfileChanges.push({
          changeType: "itemAttrChanged",
          itemId: req.body.lockerItem,
          attributeName: "locker_slots_data",
          attributeValue:
            profile.items[req.body.lockerItem].attributes.locker_slots_data,
        });
        break;
    }
    if (ApplyProfileChanges.length > 0) {
      profile.rvn += 1;
      profile.commandRevision += 1;
      profile.updated = new Date().toISOString();
      //        await profiles?.updateOne({ $set: { [`profiles.${req.query.profileId}`]: profile } });
    }
    if (QueryRevision != ProfileRevisionCheck) {
      ApplyProfileChanges = [
        {
          changeType: "fullProfileUpdate",
          profile: profile,
        },
      ];
    }
    res.json({
      profileRevision: profile.rvn || 0,
      profileId: req.query.profileId,
      profileChangesBaseRevision: BaseRevision,
      profileChanges: ApplyProfileChanges,
      profileCommandRevision: profile.commandRevision || 0,
      serverTime: new Date().toISOString(),
      responseVersion: 1,
    });
    if (ApplyProfileChanges.length > 0)
      await profiles?.updateOne({
        $set: { [`profiles.${req.query.profileId}`]: profile },
      });
    // The match server's locker files are written after the reply, so "Saving Selection" doesn't wait for the disk.
    try{const build=clientBuild(req.headers['user-agent']);await publishCharacter(req.user.accountId,['1.11','2.5','1.8.2','1.7.2'].includes(build)?project111Profile(profile,build):profile);}
    catch(error){console.warn('[Syndicate] Could not save the locker for the match:',error.message);}
  }
);

app.post(
  "/fortnite/api/game/v2/profile/*/client/AthenaPinQuest",
  verifyToken,
  async (req, res) => {
    const profiles = await Profile.findOne({ accountId: req.user.accountId });
    if (!(await profileManager.validateProfile(req.query.profileId, profiles)))
      return Utils.createError(
        "errors.com.epicgames.modules.profiles.operation_forbidden",
        `Unable to find template configuration for profile ${req.query.profileId}`,
        [req.query.profileId],
        12813,
        undefined,
        403,
        res
      );
    let profile = profiles?.profiles[req.query.profileId];

    // do not change any of these or you will end up breaking it
    var ApplyProfileChanges = [];
    var BaseRevision = profile.rvn || 0;
    var QueryRevision = req.query.rvn || -1;
    var StatChanged = false;

    if (profile.stats.attributes.hasOwnProperty("pinned_quest")) {
      profile.stats.attributes.pinned_quest = req.body.pinnedQuest || "";
      StatChanged = true;
    }

    if (StatChanged == true) {
      profile.rvn += 1;
      profile.commandRevision += 1;

      ApplyProfileChanges.push({
        changeType: "statModified",
        name: "pinned_quest",
        value: profile.stats.attributes.pinned_quest,
      });

      //fs.writeFileSync(`./profiles/${req.query.profileId || "athena"}.json`, JSON.stringify(profile, null, 2));
    }

    // this doesn't work properly on version v12.20 and above but whatever
    if (QueryRevision != BaseRevision) {
      ApplyProfileChanges = [
        {
          changeType: "fullProfileUpdate",
          profile: profile,
        },
      ];
    }

    res.json({
      profileRevision: profile.rvn || 0,
      profileId: req.query.profileId || "athena",
      profileChangesBaseRevision: BaseRevision,
      profileChanges: ApplyProfileChanges,
      profileCommandRevision: profile.commandRevision || 0,
      serverTime: new Date().toISOString(),
      responseVersion: 1,
    });
    if (ApplyProfileChanges.length > 0)
      await profiles?.updateOne({
        $set: { [`profiles.${req.query.profileId}`]: profile },
      });
  }
);

// Set pinned STW quests
app.post(
  "/fortnite/api/game/v2/profile/*/client/SetPinnedQuests",
  verifyToken,
  async (req, res) => {
    const profiles = await Profile.findOne({ accountId: req.user.accountId });
    if (!(await profileManager.validateProfile(req.query.profileId, profiles)))
      return Utils.createError(
        "errors.com.epicgames.modules.profiles.operation_forbidden",
        `Unable to find template configuration for profile ${req.query.profileId}`,
        [req.query.profileId],
        12813,
        undefined,
        403,
        res
      );
    let profile = profiles?.profiles[req.query.profileId];
    const memory = Utils.GetVersion(req);

    // do not change any of these or you will end up breaking it
    var ApplyProfileChanges = [];
    var BaseRevision =
      memory.build >= 12.2 ? profile.commandRevision : profile.rvn;
    var QueryRevision = req.query.rvn || -1;
    var StatChanged = false;

    if (req.body.pinnedQuestIds) {
      profile.stats.attributes.client_settings.pinnedQuestInstances =
        req.body.pinnedQuestIds;
      StatChanged = true;
    }

    if (StatChanged == true) {
      profile.rvn += 1;
      profile.commandRevision += 1;

      ApplyProfileChanges.push({
        changeType: "statModified",
        name: "client_settings",
        value: profile.stats.attributes.client_settings,
      });

      //fs.writeFileSync(`./profiles/${req.query.profileId || "campaign"}.json`, JSON.stringify(profile, null, 2));
    }

    // this doesn't work properly on version v12.20 and above but whatever
    if (QueryRevision != BaseRevision) {
      ApplyProfileChanges = [
        {
          changeType: "fullProfileUpdate",
          profile: profile,
        },
      ];
    }

    res.json({
      profileRevision: profile.rvn || 0,
      profileId: req.query.profileId || "campaign",
      profileChangesBaseRevision: BaseRevision,
      profileChanges: ApplyProfileChanges,
      profileCommandRevision: profile.commandRevision || 0,
      serverTime: new Date().toISOString(),
      responseVersion: 1,
    });
    if (ApplyProfileChanges.length > 0)
      await profiles?.updateOne({
        $set: { [`profiles.${req.query.profileId}`]: profile },
      });
  }
);

// Mark New Quest Notification Sent
app.post(
  "/fortnite/api/game/v2/profile/*/client/MarkNewQuestNotificationSent",
  verifyToken,
  async (req, res) => {
    const profiles = await Profile.findOne({ accountId: req.user.accountId });
    if (!(await profileManager.validateProfile(req.query.profileId, profiles)))
      return Utils.createError(
        "errors.com.epicgames.modules.profiles.operation_forbidden",
        `Unable to find template configuration for profile ${req.query.profileId}`,
        [req.query.profileId],
        12813,
        undefined,
        403,
        res
      );
    let profile = profiles?.profiles[req.query.profileId];
    const memory = Utils.GetVersion(req);

    // do not change any of these or you will end up breaking it
    var ApplyProfileChanges = [];
    var BaseRevision =
      memory.build >= 12.2 ? profile.commandRevision : profile.rvn;
    var QueryRevision = req.query.rvn || -1;
    var StatChanged = false;

    if (req.body.itemIds) {
      for (var i in req.body.itemIds) {
        var id = req.body.itemIds[i];

        if (!profile.items[id]?.attributes) continue; // an item the old client still lists but the profile no longer has
        profile.items[id].attributes.sent_new_notification = true;

        ApplyProfileChanges.push({
          changeType: "itemAttrChanged",
          itemId: id,
          attributeName: "sent_new_notification",
          attributeValue: true,
        });
      }

      StatChanged = true;
    }

    if (StatChanged == true) {
      profile.rvn += 1;
      profile.commandRevision += 1;

      await Profile.findOneAndUpdate(
        { accountId: req.user.accountId },
        { $set: profiles },
        { upsert: true }
      );
    }

    // this doesn't work properly on version v12.20 and above but whatever
    if (QueryRevision != BaseRevision) {
      ApplyProfileChanges = [
        {
          changeType: "fullProfileUpdate",
          profile: profile,
        },
      ];
    }

    res.json({
      profileRevision: profile.rvn || 0,
      profileId: req.query.profileId || "athena",
      profileChangesBaseRevision: BaseRevision,
      profileChanges: ApplyProfileChanges,
      profileCommandRevision: profile.commandRevision || 0,
      serverTime: new Date().toISOString(),
      responseVersion: 1,
    });
    if (ApplyProfileChanges.length > 0)
      await profiles?.updateOne({
        $set: { [`profiles.${req.query.profileId}`]: profile },
      });
  }
);

app.post(
  "/fortnite/api/game/v2/profile/*/client/UpdateQuestClientObjectives",
  verifyToken,
  async (req, res) => {
    const profiles = await Profile.findOne({ accountId: req.user.accountId });
    if (!(await profileManager.validateProfile(req.query.profileId, profiles)))
      return Utils.createError(
        "errors.com.epicgames.modules.profiles.operation_forbidden",
        `Unable to find template configuration for profile ${req.query.profileId}`,
        [req.query.profileId],
        12813,
        undefined,
        403,
        res
      );
    let profile = profiles?.profiles[req.query.profileId];
    const memory = Utils.GetVersion(req);

    // Save the World (dev launcher): Save the World quests live in profile0 (Pre-season/Season 1) or campaign,
    // keyed by item id, with one completion_<stat> attribute per objective. The BR lookup below never found
    // them and never saved, so the 1.7.2 Homebase tutorial sent the same objective twice a second forever
    // ("Saving homebase data..."). Record each objective, complete a quest once all its objectives are done,
    // and save.
    if (stwDevEnabled() && req.query.profileId !== "athena" && profile?.items) {
      const base = profile.rvn || 0;
      let changed = false;
      for (const adv of req.body?.advance || []) {
        if (!adv?.statName) continue;
        const attr = `completion_${String(adv.statName).toLowerCase()}`;
        for (const [id, item] of Object.entries(profile.items)) {
          if (!String(item?.templateId || "").toLowerCase().startsWith("quest:") || !item.attributes) continue;
          const key = Object.keys(item.attributes).find((k) => k.toLowerCase() === attr);
          if (!key) continue;
          item.attributes[key] = Math.max(Number(item.attributes[key]) || 0, Number(adv.count) || 0);
          item.attributes.last_state_change_time = new Date().toISOString();
          const done = Object.entries(item.attributes).every(([k, v]) => !k.toLowerCase().startsWith("completion_") || Number(v) > 0);
          if (done && String(item.attributes.quest_state || "").toLowerCase() !== "claimed") item.attributes.quest_state = "Claimed";
          changed = true;
        }
      }
      if (changed) {
        profile.rvn = base + 1;
        profile.commandRevision = (profile.commandRevision || 0) + 1;
        profile.updated = new Date().toISOString();
        await profiles.updateOne({ $set: { [`profiles.${req.query.profileId}`]: profile } });
      }
      return res.json({
        profileRevision: profile.rvn || 0,
        profileId: req.query.profileId,
        profileChangesBaseRevision: base,
        profileChanges: [{ changeType: "fullProfileUpdate", profile }],
        profileCommandRevision: profile.commandRevision || 0,
        serverTime: new Date().toISOString(),
        responseVersion: 1,
      });
    }

    // do not change any of these or you will end up breaking it
    var ApplyProfileChanges = [];
    var BaseRevision =
      memory.build >= 12.2 ? profile.commandRevision : profile.rvn;
    var QueryRevision = req.query.rvn || -1;
    var StatChanged = false;

    for (var adv of req.body.advance || []) {
      var s = adv.statName.split("_");
      if (!Number.isNaN(parseInt(s[s.length - 1]))) s.pop();
      var q = `S${memory.season}-Quest:${s.join("_")}`;
      var n = Object.keys(profile.items).find(
        (f) => f.toLowerCase() == q.toLowerCase()
      );
      // Chapter 2 challenge quests (Season 11/12) are not keyed as S<season>-Quest:<name>, so this lookup can
      // miss. Real objective progress comes from the game server's trusted queue, not these client counts, so
      // skip an unmatched quest instead of crashing. The crash failed the whole request and the client showed
      // "Connection to the server failed" (mcpprofilegroup.connection_failure) every 30 s mid-match.
      if (!n || !profile.items[n] || !profile.items[n].attributes) continue;
      profile.items[n].attributes[`completion_${adv.statName}`] = adv.count;
      profile.items[n].attributes.last_state_change_time =
        new Date().toISOString();

      ApplyProfileChanges.push({
        changeType: "itemAttrChanged",
        itemId: n,
        attributeName: `completion_${adv.statName}`,
        attributeValue:
          profile.items[n].attributes[`completion_${adv.statName}`],
      });

      ApplyProfileChanges.push({
        changeType: "itemAttrChanged",
        itemId: n,
        attributeName: "last_state_change_time",
        attributeValue: profile.items[n].attributes.last_state_change_time,
      });

      StatChanged = true;
    }

    if (StatChanged == true) {
      profile.rvn += 1;
      profile.commandRevision += 1;
    }

    if (QueryRevision != BaseRevision) {
      ApplyProfileChanges = [
        {
          changeType: "fullProfileUpdate",
          profile: profile,
        },
      ];
    }

    res.json({
      profileRevision: profile.rvn || 0,
      profileId: req.query.profileId || "athena",
      profileChangesBaseRevision: BaseRevision,
      profileChanges: ApplyProfileChanges,
      profileCommandRevision: profile.commandRevision || 0,
      serverTime: new Date().toISOString(),
      responseVersion: 1,
    });

    if (ApplyProfileChanges.length > 0)
      await profiles?.updateOne({
        $set: { [`profiles.${req.query.profileId}`]: profile },
      });
  }
);

app.post(
  "/fortnite/api/game/v2/profile/*/client/ClientQuestLogin",
  verifyToken,
  async (req, res) => {
    const profiles = await Profile.findOne({ accountId: req.user.accountId });

    if (!(await profileManager.validateProfile(req.query.profileId, profiles)))
      return error.createError(
        "errors.com.epicgames.modules.profiles.operation_forbidden",
        `Unable to find template configuration for profile ${req.query.profileId}`,
        [req.query.profileId],
        12813,
        undefined,
        403,
        res
      );

    let profile = profiles.profiles[req.query.profileId];
    let athena = profiles.profiles["athena"];
    var AthenaQuestIDS = JSON.parse(
      JSON.stringify(require("./../local/Battlepass/Quest/Season12/Quest.json"))
    ); // will add more seasons later
    const memory = Utils.GetVersion(req);

    var ApplyProfileChanges = [];
    var Notifications = [];
    var BaseRevision = profile.rvn || 0;
    var QueryRevision = req.query.rvn || -1;
    var StatChanged = false;

    var QuestCount = 0;
    var ShouldGiveQuest = true;
    var DateFormat = new Date().toISOString().split("T")[0];
    var DailyQuestIDS;
    var SeasonQuestIDS;

    const SeasonPrefix =
      memory.season < 10 ? `0${memory.season}` : memory.season;

    try {
      if (req.query.profileId == "profile0") {
        for (var key in profile.items) {
          if (
            profile.items[key].templateId
              .toLowerCase()
              .startsWith("quest:daily")
          ) {
            QuestCount += 1;
          }
        }
      }

      if (req.query.profileId == "athena") {
        DailyQuestIDS = AthenaQuestIDS.Daily;

        if (AthenaQuestIDS.hasOwnProperty(`Season${SeasonPrefix}`)) {
          SeasonQuestIDS = AthenaQuestIDS[`Season${SeasonPrefix}`];
        }

        for (var key in profile.items) {
          if (
            profile.items[key].templateId
              .toLowerCase()
              .startsWith("quest:athenadaily")
          ) {
            QuestCount += 1;
          }
        }
      }

      if (profile.stats.attributes.hasOwnProperty("quest_manager")) {
        if (
          profile.stats.attributes.quest_manager.hasOwnProperty(
            "dailyLoginInterval"
          )
        ) {
          if (
            profile.stats.attributes.quest_manager.dailyLoginInterval.includes(
              "T"
            )
          ) {
            var DailyLoginDate =
              profile.stats.attributes.quest_manager.dailyLoginInterval.split(
                "T"
              )[0];

            if (DailyLoginDate == DateFormat) {
              ShouldGiveQuest = false;
            } else {
              ShouldGiveQuest = true;
              if (
                profile.stats.attributes.quest_manager.dailyQuestRerolls <= 0
              ) {
                profile.stats.attributes.quest_manager.dailyQuestRerolls += 1;
              }
            }
          }
        }
      }

      if (QuestCount < 3 && ShouldGiveQuest == true && Array.isArray(DailyQuestIDS) && DailyQuestIDS.length > 0) {
        const selectedQuests = [];
        while (selectedQuests.length < 3) {
          const randomIndex = Math.floor(Math.random() * DailyQuestIDS.length);
          const quest = DailyQuestIDS[randomIndex];

          if (
            !Object.values(profile.items).some(
              (item) =>
                item.templateId.toLowerCase() === quest.templateId.toLowerCase()
            ) &&
            !selectedQuests.includes(quest)
          ) {
            selectedQuests.push(quest);
          }
        }

        for (const quest of selectedQuests) {
          const NewQuestID = functions.MakeID();

          profile.items[NewQuestID] = {
            templateId: quest.templateId,
            attributes: {
              creation_time: new Date().toISOString(),
              level: -1,
              item_seen: false,
              sent_new_notification: false,
              xp_reward_scalar: 1,
              quest_state: "Active",
              last_state_change_time: new Date().toISOString(),
              max_level_bonus: 0,
              xp: 0,
              favorite: false,
            },
            quantity: 1,
          };

          for (var i in quest.objectives) {
            profile.items[NewQuestID].attributes[
              `completion_${quest.objectives[i].toLowerCase()}`
            ] = 0;
          }

          ApplyProfileChanges.push({
            changeType: "itemAdded",
            itemId: NewQuestID,
            item: profile.items[NewQuestID],
          });
        }

        profile.stats.attributes.quest_manager.dailyLoginInterval =
          new Date().toISOString();

        ApplyProfileChanges.push({
          changeType: "statModified",
          name: "quest_manager",
          value: profile.stats.attributes.quest_manager,
        });

        StatChanged = true;
      }
    } catch (err) {
      log.error(err);
    }

    for (var key in profile.items) {
      if (
        key.startsWith("QS") &&
        Number.isInteger(Number(key[2])) &&
        Number.isInteger(Number(key[3])) &&
        key[4] === "-"
      ) {
        if (!key.startsWith(`QS${SeasonPrefix}-`)) {
          delete profile.items[key];

          ApplyProfileChanges.push({
            changeType: "itemRemoved",
            itemId: key,
          });

          StatChanged = true;
        }
      }
    }

    // Chapter 2 (11.00/12.50) already carries the native challenge items. Rebuilding them on every
    // ClientQuestLogin with fresh timestamps reset their progress, so preserve the profile's items.
    const chapterTwoNative = memory.season >= 11;

    if (SeasonQuestIDS) {
      var QuestsToAdd = [];

      if (req.query.profileId == "athena") {
        for (var ChallengeBundleScheduleID in SeasonQuestIDS.ChallengeBundleSchedules) {
          var priorSchedule = profile.items[ChallengeBundleScheduleID];
          var priorScheduleAttrs = priorSchedule?.attributes || {};
          if (priorSchedule && !chapterTwoNative) {
            ApplyProfileChanges.push({
              changeType: "itemRemoved",
              itemId: ChallengeBundleScheduleID,
            });
          }

          var ChallengeBundleSchedule =
            SeasonQuestIDS.ChallengeBundleSchedules[ChallengeBundleScheduleID];

          profile.items[ChallengeBundleScheduleID] = {
            templateId: ChallengeBundleSchedule.templateId,
            attributes: {
              ...(chapterTwoNative ? priorScheduleAttrs : {}),
              unlock_epoch: chapterTwoNative
                ? (priorScheduleAttrs.unlock_epoch || new Date(Date.now() - 70 * 86400000).toISOString())
                : new Date().toISOString(),
              max_level_bonus: 0,
              level: 1,
              item_seen: true,
              xp: 0,
              favorite: false,
              granted_bundles: ChallengeBundleSchedule.granted_bundles,
            },
            quantity: 1,
          };

          ApplyProfileChanges.push({
            changeType: "itemAdded",
            itemId: ChallengeBundleScheduleID,
            item: profile.items[ChallengeBundleScheduleID],
          });

          StatChanged = true;
        }

        for (var ChallengeBundleID in SeasonQuestIDS.ChallengeBundles) {
          var priorBundle = profile.items[ChallengeBundleID];
          var priorBundleAttrs = priorBundle?.attributes || {};
          if (priorBundle && !chapterTwoNative) {
            ApplyProfileChanges.push({
              changeType: "itemRemoved",
              itemId: ChallengeBundleID,
            });
          }

          var ChallengeBundle =
            SeasonQuestIDS.ChallengeBundles[ChallengeBundleID];

          profile.items[ChallengeBundleID] = {
            templateId: ChallengeBundle.templateId,
            attributes: {
              ...(chapterTwoNative ? priorBundleAttrs : {}),
              has_unlock_by_completion: false,
              num_quests_completed: chapterTwoNative ? (Number(priorBundleAttrs.num_quests_completed) || 0) : 0,
              level: chapterTwoNative ? (Number(priorBundleAttrs.level) || 0) : 0,
              grantedquestinstanceids: ChallengeBundle.grantedquestinstanceids,
              item_seen: true,
              max_allowed_bundle_level: 0,
              num_granted_bundle_quests: 0,
              max_level_bonus: 0,
              challenge_bundle_schedule_id:
                ChallengeBundle.challenge_bundle_schedule_id,
              num_progress_quests_completed: chapterTwoNative ? (Number(priorBundleAttrs.num_progress_quests_completed) || 0) : 0,
              xp: 0,
              favorite: false,
            },
            quantity: 1,
          };

          QuestsToAdd = QuestsToAdd.concat(
            ChallengeBundle.grantedquestinstanceids
          );
          profile.items[
            ChallengeBundleID
          ].attributes.num_granted_bundle_quests =
            ChallengeBundle.grantedquestinstanceids.length;

          ApplyProfileChanges.push({
            changeType: "itemAdded",
            itemId: ChallengeBundleID,
            item: profile.items[ChallengeBundleID],
          });

          StatChanged = true;
        }
      }
    }

    function ParseQuest(QuestID) {
      var Quest = SeasonQuestIDS.Quests[QuestID];
      if (!Quest) {
        return;
      }

      // Chapter 2: keep the quest the profile already has (state, completion counters,
      // creation time). Re-adding it every login reset every challenge's progress.
      var existingQuest = profile.items[QuestID];
      if (existingQuest && chapterTwoNative) {
        var existingAttrs = existingQuest.attributes || (existingQuest.attributes = {});
        existingAttrs.quest_state ??= "Active";
        existingAttrs.creation_time ??= new Date().toISOString();
        existingAttrs.last_state_change_time ??= existingAttrs.creation_time;
        existingAttrs.challenge_bundle_id ||= Quest.challenge_bundle_id || "";
        existingAttrs.item_seen ??= true;
        for (var i in Quest.objectives) {
          existingAttrs[`completion_${i}`] ??= 0;
        }
        ApplyProfileChanges.push({
          changeType: "itemAdded",
          itemId: QuestID,
          item: existingQuest,
        });
        StatChanged = true;
        return;
      }

      if (profile.items.hasOwnProperty(QuestID)) {
        ApplyProfileChanges.push({
          changeType: "itemRemoved",
          itemId: QuestID,
        });
      }

      profile.items[QuestID] = {
        templateId: Quest.templateId,
        attributes: {
          creation_time: new Date().toISOString(),
          level: -1,
          item_seen: true,
          sent_new_notification: true,
          challenge_bundle_id: Quest.challenge_bundle_id || "",
          xp_reward_scalar: 1,
          quest_state: "Active",
          last_state_change_time: new Date().toISOString(),
          max_level_bonus: 0,
          xp: 0,
          favorite: false,
        },
        quantity: 1,
      };

      for (var i in Quest.objectives) {
        profile.items[QuestID].attributes[`completion_${i}`] = 0;
      }

      ApplyProfileChanges.push({
        changeType: "itemAdded",
        itemId: QuestID,
        item: profile.items[QuestID],
      });

      StatChanged = true;
    }

    for (var Quest in QuestsToAdd) {
      ParseQuest(QuestsToAdd[Quest]);
    }

    if (StatChanged == true) {
      profile.rvn += 1;
      profile.commandRevision += 1;
      profile.updated = new Date().toISOString();

      await profiles.updateOne({
        $set: { [`profiles.${req.query.profileId}`]: profile },
      });
    }

    if (QueryRevision != BaseRevision) {
      ApplyProfileChanges = [
        {
          changeType: "fullProfileUpdate",
          profile: profile,
        },
      ];
    }

    res.json({
      profileRevision: profile.rvn || 0,
      profileId: req.query.profileId,
      profileChangesBaseRevision: BaseRevision,
      profileChanges: ApplyProfileChanges,
      notifications: Notifications,
      profileCommandRevision: profile.commandRevision || 0,
      serverTime: new Date().toISOString(),
      responseVersion: 1,
    });
  }
);

app.post(
  "/fortnite/api/game/v2/profile/*/client/:operation",
  verifyToken,
  async (req, res) => {
    // A progression problem must never take the whole backend down; log it and keep serving the profile.
    if(req.query.profileId==='athena'){try{await awardAccount(req.user.accountId);}catch(error){console.error('[Syndicate XP] profile sync skipped:',error.message);}}
    let profiles = await Profile.findOne({ accountId: req.user.accountId });
    // Dev launcher only: give the account Save the World access and full Save the World profiles once.
    if(profiles&&(req.query.profileId==='common_core'||req.query.profileId==='profile0'||STW_PROFILES.includes(req.query.profileId))){
      try{const update=stwDevUpdate(profiles);if(update){await profiles.updateOne({$set:update});profiles=await Profile.findOne({accountId:req.user.accountId});console.log('[Syndicate STW] dev: Save the World profiles set up for',req.user.accountId);}}
      catch(error){console.error('[Syndicate STW] dev profile setup skipped:',error.message);}
    }
    if (!(await profileManager.validateProfile(req.query.profileId, profiles)))
      return Utils.createError(
        "errors.com.epicgames.modules.profiles.operation_forbidden",
        `Unable to find template configuration for profile ${req.query.profileId}`,
        [req.query.profileId],
        12813,
        undefined,
        403,
        res
      );
    let profile = profiles?.profiles[req.query.profileId];
    if (profile.rvn == profile.commandRevision) {
      profile.rvn += 1;
      if (req.query.profileId == "athena") {
        if (!profile.stats.attributes.last_applied_loadout)
          profile.stats.attributes.last_applied_loadout =
            profile.stats.attributes.loadouts[0];
      }
      await profiles?.updateOne({
        $set: { [`profiles.${req.query.profileId}`]: profile },
      });
    }
    const memory = Utils.GetVersion(req);
    if (req.query.profileId == "athena") {
      profile.stats.attributes.season_num = memory.season;
    }
    let MultiUpdate = [];
    if (
      req.query.profileId == "common_core" &&
      global.giftReceived[req.user.accountId]
    ) {
      global.giftReceived[req.user.accountId] = false;
      let athena = profiles?.profiles["athena"];
      MultiUpdate = [
        {
          profileRevision: athena.rvn || 0,
          profileId: "athena",
          profileChangesBaseRevision: athena.rvn || 0,
          profileChanges: [
            {
              changeType: "fullProfileUpdate",
              profile: athena,
            },
          ],
          profileCommandRevision: athena.commandRevision || 0,
        },
      ];
    }
    let ApplyProfileChanges = [];
    let BaseRevision = profile.rvn;
    let ProfileRevisionCheck =
      memory.build >= 12.2 ? profile.commandRevision : profile.rvn;
    let QueryRevision = req.query.rvn || -1;
    switch (req.params.operation) {
      case "QueryProfile":
        if(req.query.profileId==='athena')await publishCharacter(req.user.accountId,(['1.11','2.5','1.8.2','1.7.2'].includes(clientBuild(req.headers['user-agent']))?project111Profile(profile,clientBuild(req.headers['user-agent'])):profile));
        break;
      case "ClientQuestLogin":
        break;
      case "RefreshExpeditions":
        break;
      case "GetMcpTimeForLogin":
        break;
      case "IncrementNamedCounterStat":
        break;
      case "SetHardcoreModifier":
        break;
      case "SetMtxPlatform":
        break;
      // Chapter 2 entrance: 11.00 reports the intro match was played when its movie starts. A 404 here put an
      // error window over the cinematic and closed it.
      case "SetIntroGamePlayed":
        break;
      case "BulkEquipBattleRoyaleCustomization":
        break;
      case "ClaimMfaEnabled":
        if (profile.stats.attributes.mfa_enabled)
          return Utils.createError(
            "errors.com.epicgames.modules.profiles.operation_forbidden",
            "MFA already enabled",
            [],
            12813,
            undefined,
            403,
            res
          );
        break;
      case "CopyCosmeticLoadout":
        try {
          // log.debug(JSON.stringify(req.body));
          if (
            !(await profileManager.validateProfile(
              req.query.profileId,
              profiles
            ))
          )
            return Utils.createError(
              "errors.com.epicgames.modules.profiles.operation_forbidden",
              `Unable to find template configuration for profile ${req.query.profileId}`,
              [req.query.profileId],
              12813,
              undefined,
              403,
              res
            );
          const profileDocument = profiles;
          profile = profileDocument?.profiles[req.query.profileId];
          let item;
          if (req.body.sourceIndex == 0) {
            item = profile.items[`Helix${req.body.targetIndex - 1}-loadout`];
            if (!item) {
              profile.items[`Helix${req.body.targetIndex - 1}-loadout`] =
                profile.items["sandbox_loadout"];
            }
            var item2 =
              profile.items[`Helix${req.body.targetIndex - 1}-loadout`];
            if (req.body.optNewNameForTarget != "")
              profile.items[
                `Helix${req.body.targetIndex - 1}-loadout`
              ].attributes["locker_name"] = req.body.optNewNameForTarget;
            profile.stats.attributes.loadouts[req.body.targetIndex] = `Helix${
              req.body.targetIndex - 1
            }-loadout`;
            profile.stats.attributes["active_loadout_index"] =
              req.body.targetIndex;
            profile.stats.attributes["last_applied_loadout"] = `Helix${
              req.body.targetIndex - 1
            }-loadout`;
            profile.items["sandbox_loadout"].attributes["locker_slots_data"] =
              item2.attributes["locker_slots_data"];
          } else {
            item = profile.items[`Helix${req.body.sourceIndex - 1}-loadout`];
            if (!item)
              return Utils.createError(
                "errors.com.epicgames.modules.profiles.operation_forbidden",
                `Locker item {0} not found`,
                [req.query.profileId],
                12813,
                undefined,
                403,
                res
              );
            profile.stats.attributes["active_loadout_index"] =
              req.body.sourceIndex;
            profile.stats.attributes["last_applied_loadout"] = `Helix${
              req.body.sourceIndex - 1
            }-loadout`;
            profile.items["sandbox_loadout"].attributes["locker_slots_data"] =
              item.attributes["locker_slots_data"];
            let n = item.attributes["locker_name"];
            profile.items[`Helix${req.body.sourceIndex - 1}-loadout`] =
              profile.items["sandbox_loadout"];
            profile.items[
              `Helix${req.body.sourceIndex - 1}-loadout`
            ].attributes["locker_name"] = n;
          }
          if (profileDocument?.profiles[req.query.profileId]) {
            profileDocument.profiles[req.query.profileId].rvn += 1;
            profile.rvn = profileDocument.profiles[req.query.profileId].rvn;
          }
          profile.updated = new Date().toISOString();
          if (
            profileDocument &&
            profileDocument.profiles[req.query.profileId]
          ) {
            profileDocument.profiles[req.query.profileId].commandRevision =
              (profileDocument.profiles[req.query.profileId].commandRevision ||
                0) + 1;
          }
          await Profile.findOneAndUpdate(
            { accountId: req.user.accountId },
            { $set: profiles },
            { upsert: true }
          );
        } catch (err) {
          console.log(err);
        }
        break;
      case "DeleteCosmeticLoadout":
        try {
          if (
            !(await profileManager.validateProfile(
              req.query.profileId,
              profiles
            ))
          )
            return Utils.createError(
              "errors.com.epicgames.modules.profiles.operation_forbidden",
              `Unable to find template configuration for profile ${req.query.profileId}`,
              [req.query.profileId],
              12813,
              undefined,
              403,
              res
            );
          const profileDocument = profiles;
          profile = profileDocument?.profiles[req.query.profileId];
          // log.debug(JSON.stringify(req.body));
          if (req.body.fallbackLoadoutIndex != -1) {
            item = profile.items[`Helix${fallbackLoadoutIndex - 1}-loadout`];
            if (!item)
              return Utils.createError(
                "errors.com.epicgames.modules.profiles.operation_forbidden",
                `Locker item {0} not found`,
                [req.query.profileId],
                12813,
                undefined,
                403,
                res
              );
            profile.stats.attributes["active_loadout_index"] =
              fallbackLoadoutIndex;
            profile.stats.attributes["last_applied_loadout"] = `Helix${
              fallbackLoadoutIndex - 1
            }-loadout`;
            profile.items["sandbox_loadout"].attributes["locker_slots_data"] =
              item.attributes["locker_slots_data"];
            let n = item.attributes["locker_name"];
            profile.items[`Helix${fallbackLoadoutIndex - 1}-loadout`] =
              profile.items["sandbox_loadout"];
            profile.items[
              `Helix${fallbackLoadoutIndex - 1}-loadout`
            ].attributes["locker_name"] = n;
          }

          var it = Object.entries(profile.items);
          profile.items = Object.fromEntries(it);
          profile.stats.attributes.loadouts.splice(req.body.index);

          if (profileDocument?.profiles[req.query.profileId]) {
            profileDocument.profiles[req.query.profileId].rvn += 1;
            profile.rvn = profileDocument.profiles[req.query.profileId].rvn;
          }
          profile.updated = new Date().toISOString();
          if (
            profileDocument &&
            profileDocument.profiles[req.query.profileId]
          ) {
            profileDocument.profiles[req.query.profileId].commandRevision =
              (profileDocument.profiles[req.query.profileId].commandRevision ||
                0) + 1;
          }
          await Profile.findOneAndUpdate(
            { accountId: req.user.accountId },
            { $set: profiles },
            { upsert: true }
          );
        } catch (err) {
          log.error(err.toString());
        }
        break;
      case "SetCosmeticLockerName":
        try {
          if (
            !(await profileManager.validateProfile(
              req.query.profileId,
              profiles
            ))
          )
            return Utils.createError(
              "errors.com.epicgames.modules.profiles.operation_forbidden",
              `Unable to find template configuration for profile ${req.query.profileId}`,
              [req.query.profileId],
              12813,
              undefined,
              403,
              res
            );
          const profileDocument = profiles;
          profile = profileDocument?.profiles[req.query.profileId];
          const item2 = profile.items[req.body.lockerItem];
          if (!item2)
            return Utils.createError(
              "errors.com.epicgames.modules.profiles.operation_forbidden",
              `Locker item {0} not found`,
              [req.query.profileId],
              12813,
              undefined,
              403,
              res
            );
          if (
            typeof req.body.name === "string" &&
            item2.attributes.locker_name != req.body.name
          ) {
            let attrItem = profile.items[req.body.lockerItem];
            let attrName = "locker_name";
            if (!item2) {
              return;
            }
            if (!item2.attributes) {
              item2.attributes = {};
            }
            item2.attributes[attrName] = req.body.name;
            if (ApplyProfileChanges != null) {
              ApplyProfileChanges.push({
                changeType: "itemAttrChanged",
                itemId: req.body.lockerItem,
                itemName: item2.templateId,
                item: item2,
              });
            }
          }
          if (profileDocument?.profiles[req.query.profileId]) {
            profileDocument.profiles[req.query.profileId].rvn += 1;
            profile.rvn = profileDocument.profiles[req.query.profileId].rvn;
          }
          profile.updated = new Date().toISOString();
          if (
            profileDocument &&
            profileDocument.profiles[req.query.profileId]
          ) {
            profileDocument.profiles[req.query.profileId].commandRevision =
              (profileDocument.profiles[req.query.profileId].commandRevision ||
                0) + 1;
          }
          await Profile.findOneAndUpdate(
            { accountId: req.user.accountId },
            { $set: profiles },
            { upsert: true }
          );
        } catch (err) {
          log.error(err.toString());
        }
        break;
      // Save the World Homebase name and banner (first-time Homebase setup on Pre-season: without these the game
      // sat on "Saving homebase data..."). profile0 keeps them in the homebase stat, common_public separately.
      case "SetHomebaseName":
      case "SetHomebaseBanner": {
        const attrs = (profile.stats ??= {}).attributes ??= {};
        let changed = false;
        if (req.query.profileId === "profile0") {
          const home = (attrs.homebase ??= { townName: "", bannerIconId: "StandardBanner1", bannerColorId: "DefaultColor1", flagPattern: -1, flagColor: -1 });
          if (req.params.operation === "SetHomebaseName" && typeof req.body?.homebaseName === "string") { home.townName = req.body.homebaseName.slice(0, 64); changed = true; }
          if (req.params.operation === "SetHomebaseBanner" && req.body?.homebaseBannerIconId && req.body?.homebaseBannerColorId) {
            home.bannerIconId = String(req.body.homebaseBannerIconId); home.bannerColorId = String(req.body.homebaseBannerColorId); changed = true;
          }
        } else if (req.query.profileId === "common_public") {
          if (req.params.operation === "SetHomebaseName" && typeof req.body?.homebaseName === "string") { attrs.homebase_name = req.body.homebaseName.slice(0, 64); changed = true; }
          if (req.params.operation === "SetHomebaseBanner" && req.body?.homebaseBannerIconId && req.body?.homebaseBannerColorId) {
            attrs.banner_icon = String(req.body.homebaseBannerIconId); attrs.banner_color = String(req.body.homebaseBannerColorId); changed = true;
          }
        }
        if (changed) {
          profile.rvn = (profile.rvn || 0) + 1;
          profile.commandRevision = (profile.commandRevision || 0) + 1;
          profile.updated = new Date().toISOString();
          await profiles?.updateOne({ $set: { [`profiles.${req.query.profileId}`]: profile } });
        }
        break;
      }
      default:
        // Dev launcher: Save the World lobby operations with no handler yet answer with no changes, not 404.
        if (stwDevNoOp(req.query.profileId)) {
          console.log(`[Syndicate STW] dev: no handler for ${req.params.operation} (${req.query.profileId}); answered with no changes`);
          break;
        }
        Utils.createError(
          "errors.com.epicgames.fortnite.operation_not_found",
          `Operation ${req.params.operation} not valid`,
          [req.params.operation],
          16035,
          undefined,
          404,
          res
        );
        return;
    }
    if (QueryRevision != ProfileRevisionCheck) {
      ApplyProfileChanges = [
        {
          changeType: "fullProfileUpdate",
          profile: profile,
        },
      ];
    }
    ApplyProfileChanges = [
      {
        changeType: "fullProfileUpdate",
        profile: profile,
      },
    ];
    res.json({
      profileRevision: profile.rvn || 0,
      profileId: req.query.profileId,
      profileChangesBaseRevision: BaseRevision,
      profileChanges: ApplyProfileChanges,
      profileCommandRevision: profile.commandRevision || 0,
      serverTime: new Date().toISOString(),
      multiUpdate: MultiUpdate,
      responseVersion: 1,
    });
  }
);

app.post(
  "/fortnite/api/game/v3/profile/*/client/emptygift",
  async (req, res) => {
    const playerName = req.body.playerName;
    const user = await User.findOne({ username: playerName });

    if (!user) {
      return Utils.createError(
        "errors.com.epicgames.user.not_found",
        "User not found.",
        undefined,
        16027,
        undefined,
        404,
        res
      );
    }

    const senderAccountId = user.accountId.toString();
    const profiles = await Profile.findOne({ accountId: senderAccountId });

    if (!(await profileManager.validateProfile("common_core", profiles)))
      return Utils.createError(
        "errors.com.epicgames.modules.profiles.operation_forbidden",
        `Unable to find template configuration for profile ${"common_core"}`,
        ["common_core"],
        12813,
        undefined,
        403,
        res
      );

    let profile = profiles.profiles["common_core"];

    if ("common_core" != "common_core") {
      return Utils.createError(
        "errors.com.epicgames.modules.profiles.invalid_command",
        `GiftCatalogEntry is not valid on ${"common_core"} profile`,
        ["GiftCatalogEntry", "common_core"],
        12801,
        undefined,
        400,
        res
      );
    }

    const memory = Utils.GetVersion(req);

    let Notifications = [];
    let ApplyProfileChanges = [];
    let BaseRevision = profile.rvn;
    let ProfileRevisionCheck =
      memory.build >= 12.2 ? profile.commandRevision : profile.rvn;
    let QueryRevision = req.query.rvn || -1;
    let validGiftBoxes = [
      "GiftBox:gb_accountmergevbucks",
      "GiftBox:gb_accountmerge",
      "GiftBox:gb_battlepass",
      "GiftBox:gb_makegoodathena",
      "GiftBox:gb_makegood",
      "GiftBox:gb_seasonfirstwin",
    ];

    let missingFields = checkFields(
      ["offerId", "giftWrapTemplateId"],
      req.body
    );

    if (missingFields.fields.length > 0)
      return Utils.createError(
        "errors.com.epicgames.validation.validation_failed",
        `Validation Failed. [${missingFields.fields.join(
          ", "
        )}] field(s) is missing.`,
        [`[${missingFields.fields.join(", ")}]`],
        1040,
        undefined,
        400,
        res
      );

    if (typeof req.body.offerId != "string")
      return ValidationError("offerId", "a string", res);
    if (typeof req.body.giftWrapTemplateId != "string")
      return ValidationError("giftWrapTemplateId", "a string", res);
    if (typeof req.body.personalMessage != "string")
      return ValidationError("personalMessage", "a string", res);

    if (req.body.personalMessage.length > 100)
      return Utils.createError(
        "errors.com.epicgames.string.length_check",
        `The personalMessage you provided is longer than 100 characters, please make sure your personal message is less than 100 characters long and try again.`,
        undefined,
        16027,
        undefined,
        400,
        res
      );

    if (!validGiftBoxes.includes(req.body.giftWrapTemplateId))
      return Utils.createError(
        "errors.com.epicgames.giftbox.invalid",
        `The giftbox you provided is invalid, please provide a valid giftbox and try again.`,
        undefined,
        16027,
        undefined,
        400,
        res
      );

    let sender = await Friends.findOne({ accountId: senderAccountId }).lean();

    const receiverUser = await User.findOne({
      playerName: req.body.receiverPlayerName,
    });

    if (!receiverUser) {
      return Utils.createError(
        "errors.com.epicgames.user.not_found",
        "Receiver user not found.",
        undefined,
        16027,
        undefined,
        404,
        res
      );
    }

    const receiverAccountId = senderAccountId;

    let receiverProfile = await Profile.findOne({
      accountId: receiverAccountId,
    });

    if (!receiverProfile) {
      return Utils.createError(
        "errors.com.epicgames.profile.not_found",
        "Receiver profile not found.",
        undefined,
        16027,
        undefined,
        404,
        res
      );
    }

    let athena = receiverProfile.profiles["athena"];
    let common_core =
      receiverAccountId == senderAccountId
        ? profile
        : receiverProfile.profiles["common_core"];

    athena.rvn += 1;
    athena.commandRevision += 1;
    athena.updated = new Date().toISOString();

    common_core.rvn += 1;
    common_core.commandRevision += 1;
    common_core.updated = new Date().toISOString();

    await receiverProfile.updateOne({
      $set: {
        [`profiles.athena`]: athena,
        [`profiles.common_core`]: common_core,
      },
    });

    global.giftReceived[receiverAccountId] = true;

    Utils.SendXMPPMessage(
      {
        type: "com.epicgames.gift.received",
        payload: {},
        timestamp: new Date().toISOString(),
      },
      receiverAccountId
    );

    if (
      ApplyProfileChanges.length > 0 &&
      receiverAccountId !== senderAccountId
    ) {
      profile.rvn += 1;
      profile.commandRevision += 1;
      profile.updated = new Date().toISOString();

      await profiles.updateOne({
        $set: { [`profiles.${"common_core"}`]: profile },
      });
    }

    if (QueryRevision != ProfileRevisionCheck) {
      ApplyProfileChanges = [
        {
          changeType: "fullProfileUpdate",
          profile: profile,
        },
      ];
    }
    log.backend("EmptyGift sent to " + user.username + "!");
    res.json({
      profileRevision: profile.rvn || 0,
      profileId: "common_core",
      profileChangesBaseRevision: BaseRevision,
      profileChanges: ApplyProfileChanges,
      notifications: Notifications,
      profileCommandRevision: profile.commandRevision || 0,
      serverTime: new Date().toISOString(),
      responseVersion: 1,
    });
  }
);

app.post(
  "/fortnite/api/game/v2/profile/:accountId/dedicated_server/:operation",
  async (req, res) => {
    // log.debug("dedicated_server for " + req.params.accountId);
    const profiles = await Profile.findOne({
      accountId: req.params.accountId,
    }).lean();
    if (!profiles) return res.status(404).json({});
    if (!(await profileManager.validateProfile(req.query.profileId, profiles)))
      return Utils.createError(
        "errors.com.epicgames.modules.profiles.operation_forbidden",
        `Unable to find template configuration for profile ${req.query.profileId}`,
        [req.query.profileId],
        12813,
        undefined,
        403,
        res
      );
    let profile = profiles?.profiles[req.query.profileId];
    const memory = Utils.GetVersion(req);
    let ApplyProfileChanges = [];
    let BaseRevision = profile.rvn;
    let ProfileRevisionCheck =
      memory.build >= 12.2 ? profile.commandRevision : profile.rvn;
    let QueryRevision = req.query.rvn || -1;
    if (QueryRevision != ProfileRevisionCheck) {
      ApplyProfileChanges = [
        {
          changeType: "fullProfileUpdate",
          profile: profile,
        },
      ];
    }
    res.json({
      profileRevision: profile.rvn || 0,
      profileId: req.query.profileId,
      profileChangesBaseRevision: BaseRevision,
      profileChanges: ApplyProfileChanges,
      profileCommandRevision: profile.commandRevision || 0,
      serverTime: new Date().toISOString(),
      responseVersion: 1,
    });
  }
);
function checkFields(fields, body) {
  let missingFields = { fields: [] };
  fields.forEach((field) => {
    if (!body[field]) missingFields.fields.push(field);
  });
  return missingFields;
}
function ValidationError(field, type, res) {
  return Utils.createError(
    "errors.com.epicgames.validation.validation_failed",
    `Validation Failed. '${field}' is not ${type}.`,
    [field],
    1040,
    undefined,
    400,
    res
  );
}
function checkIfDuplicateExists(arr) {
  return new Set(arr).size !== arr.length;
}
export default app;
