import {progressSeason5Bundles} from './syndicateSeason5Bundles.js';
import {ensureSharedWallet,walletId} from './syndicateShopWallet.js';
import {activeSeason,activateSeason} from './syndicateSeasonState.js';
import {progressBundleChallenges} from './syndicateChallengeBundles.js';
import {progressChallenges} from './syndicateChallenges.js';
import {recordCareer} from './syndicateCareerCore.js';
import {recordAchievements} from './syndicateAchievements.js';
import {travelActive} from './syndicateTravel.js';
import {routeGuestReward,pullRemoteRewards} from './syndicateLan.js';
import {withAccountLock} from './syndicateAccountLock.js';
import {applyArenaHype} from './syndicateArena.js';
import {challengeSeasons,progressSeasonChallenges} from './syndicateSeasonChallenges.js';
import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import Profile from '../User/Mongodb/Schema/profiles.js';
import User from '../User/Mongodb/Schema/user.js';
import {applyProgression,awards} from './syndicateProgressionCore.js';
const queue=fileURLToPath(new URL('../../../Runtime/Server/progression-queue/',import.meta.url));
import {vbucksAwards,grantVictoryUmbrellas} from './syndicateVictoryRewards.js';
export {vbucksAwards};
export async function awardAccount(accountId,event=null) {
  if(travelActive(accountId))throw new Error("Account is hosted remotely; reward retained for retry");
  const visitor=await User.collection.findOne({accountId},{projection:{travelGuest:1}});
  if(visitor?.travelGuest&&visitor.travelGuest.status!=="active")throw new Error("Hosted session closed; reward retained");
  if(event && !Object.hasOwn(vbucksAwards,event.kind))throw new Error("Invalid gameplay reward");
  for(let attempt=0;attempt<8;attempt++){
    const doc=await Profile.collection.findOne({accountId});if(!doc)throw new Error('Missing progression profile');
    if(event && doc.syndicateProgressionEvents?.includes(event.id))return {duplicate:true};
    const originalSeason=activeSeason(doc.profiles.athena);
    // Season 1 (1.7.2 / 1.8.2) keeps no season of its own: its lobby shows the season that is active on the account
    // (the one played last). Its match XP used to go to Season 4 instead, so after playing another season, kills and
    // wins in Season 1 never moved the level it shows. Season 1 events now go to that same active season.
    if(event && event.season!==1)activateSeason(doc.profiles.athena,[2,3,5,6,7,8,9,10,11,12].includes(event.season)?event.season:4);
    const attr=doc.profiles.athena.stats.attributes;
    const season=activeSeason(doc.profiles.athena);
    if(!event && season===4 && attr.tilted_season4_reward_tier>=attr.book_level && attr.book_purchased)return {initialized:true};
    const old=doc.profiles.athena.rvn,common=doc.profiles.common_core.rvn;
    const challenge=season===4?progressChallenges(doc.profiles,event):{xp:0};
    // Seasons 2, 3 and 6-X: Battle Pass challenges from the game server's exported data and stat events.
    const seasonal=challengeSeasons.has(season)&&event?progressSeasonChallenges(doc.profiles.athena,event,season):{xp:0,stars:0};
    // Seasons whose weekly challenges have their own module (5): their daily challenges still progress here.
    const daily=!challengeSeasons.has(season)&&season!==4&&event?progressSeasonChallenges(doc.profiles.athena,event,season,true):{xp:0,stars:0};
    // Chapter 2 levels cost 10,000-80,000 XP instead of a few hundred, so match XP is scaled to that curve.
    const matchXpScale=season>=11?40:1;
    // "xp": XP the game itself gave in the match (Chapter 2's XP component: chests, survival, medals...), as counted by the server.
    const nativeXp=event?.kind==='xp'&&Number.isSafeInteger(event.count)?Math.max(0,Math.min(250000,event.count)):0;
    const result=applyProgression(doc.profiles,(event?awards[event.kind]*matchXpScale:0)+nativeXp+challenge.xp+seasonal.xp+daily.xp);
    result.seasonChallenges=seasonal;
    result.challengeRewards=challenge;
    // Only legit players earn match V-Bucks: the launcher sets syndicate_menu_active at every launch
    // (POST /launcher/menu), and while the Syndicate menu is on, kills and wins pay nothing.
    const menuOn=attr.syndicate_menu_active===true;
    const vbucks=event&&!menuOn?vbucksAwards[event.kind]:0;
    ensureSharedWallet(doc.profiles.common_core);
    if(vbucks)doc.profiles.common_core.items[walletId].quantity+=vbucks;
    if(season===4)progressBundleChallenges(doc.profiles.athena,event);
    if(season===5)progressSeason5Bundles(doc.profiles.athena,event);
    recordCareer(attr,event);
    // Syndicate Achievements (launcher): any season, never while the Syndicate menu is on.
    recordAchievements(attr,event);
    result.arena=applyArenaHype(attr,event);
    result.vbucksAwarded=vbucks;
    result.umbrellasGranted=grantVictoryUmbrellas(doc.profiles.athena,event);
    if(event&&originalSeason!==season)activateSeason(doc.profiles.athena,originalSeason);
    const update={$set:{'profiles.athena':doc.profiles.athena,'profiles.common_core':doc.profiles.common_core}};
    if(event)update.$addToSet={syndicateProgressionEvents:event.id};
    const saved=await Profile.collection.updateOne({accountId,'profiles.athena.rvn':old,'profiles.common_core.rvn':common},update);
    if(saved.modifiedCount){if(global.giftReceived)global.giftReceived[accountId]=true;return result;}
  }
  throw new Error('Profile busy; event retained for retry');
}
let busy=false;
export async function consumeProgression(){
  if(busy)return;busy=true;
  try{
    await pullRemoteRewards((id,event)=>withAccountLock(id,()=>awardAccount(id,event)));
    await fs.mkdir(queue,{recursive:true});
    for(const name of (await fs.readdir(queue)).filter(n=>/^\{[0-9a-f-]{36}\}\.json$/i.test(n)).slice(0,100)){
      const file=path.join(queue,name);
      try{
        const event=JSON.parse(await fs.readFile(file,'utf8'));
        if(`${event.id}.json`!==name || !Object.hasOwn(awards,event.kind) || typeof event.username!=='string')throw new Error('Invalid gameplay event');
        const user=await User.findOne({username_lower:event.username.toLowerCase()}).lean();
        if(!user){if(routeGuestReward(event)){await fs.unlink(file);continue;}throw new Error('Gameplay account not found');}
        const result=await withAccountLock(user.accountId,()=>awardAccount(user.accountId,event));
        await fs.unlink(file);
        console.log('[Syndicate XP]',event.kind,JSON.stringify(result));
      }catch(error){console.error('[Syndicate XP] event retained:',error.message);}
    }
  }finally{busy=false;}
}
setInterval(()=>consumeProgression().catch(e=>console.error('[Syndicate XP]',e.message)),2000).unref();
