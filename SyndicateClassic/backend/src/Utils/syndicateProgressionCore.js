import {syncXpBoosts} from './syndicateXpBoosts.js';
import {ensureSeason2Shields} from './syndicateSeason2Shields.js';
import {activeSeason,holdSeasonMax} from './syndicateSeasonState.js';
import {ensureSeason4Styles} from './syndicateSeason4Styles.js';
import {seasonMaxed} from './syndicateMaxProgress.js';
import fs from 'node:fs';
const read = (season,name) => JSON.parse(fs.readFileSync(new URL(`../local/Battlepass/Data/Season${season}/${name}.json`, import.meta.url)));
const tables=Object.fromEntries([2,3,4,5,6,7,8,9,10,11,12].map(s=>[s,Object.fromEntries(['SeasonXP','SeasonBP','SeasonFreeBattlepass','SeasonPaidBattlepass'].map(n=>[n,
 // Chapter 2 has no Battle Stars: each level is one tier (10 stars per level here). Season 12 ships no
 // SeasonBP.json either, so it uses the same synthesis as Season 11.
 (s===11||s===12)&&n==='SeasonBP'?Array.from({length:101},(_,Level)=>({Level,BattleStars:Level>=2?10:0})):read(s,n)]))]));

export const awards = Object.freeze({kill:100, survival:50, win:500,match:0,quest:0,quest_delta:0,stats:0,xp:0});
// Chapter 2 profiles: the tier is exactly the season level (an older build could leave it ahead). True when changed.
export function syncChapter2Tier(athena){
  const s=athena?.stats?.attributes;if(!s||activeSeason(athena)<11)return false;
  const level=Math.max(1,Math.min(100,Number(s.level)||1)),xp=level>=100?0:Math.max(0,Number(s.xp)||0);
  if(s.book_level===level&&s.book_xp===xp)return false;
  s.book_level=level;s.book_xp=xp;return true;
}
// Every Battle Pass cosmetic of a tier the player has reached that is not in their locker (free track, and the paid
// track when the pass is owned). The reward trackers can be ahead of what was actually granted (tiers bought or
// reached through an older build), which left Season 8 lockers without some pass outfits. Only cosmetics: V-Bucks,
// XP boosts and tokens are never handed out again. Not while Unlock all holds the season at max (those tiers were not
// earned). Returns how many items were added.
export function grantMissingPassCosmetics(athena){
  const s=athena?.stats?.attributes;if(!s)return 0;
  const season=activeSeason(athena);if(!tables[season]||seasonMaxed(s,season))return 0;
  const reached=Math.max(0,Math.min(season===2?70:100,Number(s.book_level)||0));
  athena.items??={};const owned=new Set(Object.values(athena.items).map(i=>String(i?.templateId||'').toLowerCase()));let added=0;
  for(const [track,table,enabled] of [['free',tables[season].SeasonFreeBattlepass,true],['paid',tables[season].SeasonPaidBattlepass,!!s.book_purchased]]){
   if(!enabled)continue;
   for(const row of table)if(row.Level>=1&&row.Level<=reached)for(const reward of row.Rewards||[]){
    const id=reward.templateId||reward.TemplateId;if(!id||!/^(Athena(Character|Backpack|Pickaxe|Glider|Dance|ItemWrap|SkyDiveContrail|LoadingScreen|MusicPack|Pet|PetCarrier|Spray|Emoji|Toy)|HomebaseBannerIcon):/i.test(id)||owned.has(id.toLowerCase()))continue;
    athena.items['tilted_s'+season+'_'+track+'_'+row.Level+'_'+id]={templateId:id,quantity:1,attributes:{item_seen:false,level:1,xp:0,variants:reward.variants||[]}};
    owned.add(id.toLowerCase());added++;
   }
  }
  return added;
}
export function applyProgression(profiles, amount=0) {
  if (!Number.isSafeInteger(amount) || amount<0) throw new Error('Invalid XP');
  const a=profiles.athena, c=profiles.common_core, s=a.stats.attributes;
  const season=activeSeason(a),cap=season===2?70:100;
  if(!tables[season])throw new Error('No Battle Pass data for season '+season);
  // Launcher "Unlock all" holds this season at max (syndicateMaxProgress.js): match XP and stars must not move it.
  if(seasonMaxed(s,season)){
   holdSeasonMax(a,season);
   for(const profile of [a,c]){profile.rvn=(profile.rvn||0)+1;profile.commandRevision=(profile.commandRevision||0)+1;profile.updated=new Date().toISOString();}
   return {level:s.level,xp:s.xp,tier:s.book_level,stars:s.book_xp,maxed:true};
  }
  const {SeasonXP:xp,SeasonBP:stars,SeasonFreeBattlepass:free,SeasonPaidBattlepass:paid}=tables[season];
  const key='tilted_season'+season+'_';
  s.season_num=season;s.book_purchased=true;
  s.level=Math.max(season===2?0:1,Math.min(100,Number(s.level)||0));
  s.xp=Math.max(0,Number(s.xp)||0)+amount;
  s.book_level=Math.max(1,Math.min(cap,Number(s.book_level)||1));
  s.book_xp=Math.max(0,Number(s.book_xp)||0);
  while(s.level<100) {
    const cost=xp.find(row=>row.Level===Math.max(1,s.level))?.XpToNextLevel;
    if(!Number.isFinite(cost)||cost<=0)throw new Error('Missing season level cost');
    if(s.xp<cost)break;
    s.xp-=cost; s.level++;
    s.book_xp+=stars.find(row=>row.Level===s.level)?.BattleStars||0;
  }
  // Local gameplay stars keep the pass progressing after the season level cap.
  if(season<11){
  s[key+'star_xp']=(s[key+'star_xp']||0)+amount;
  s.book_xp+=Math.floor(s[key+'star_xp']/1000);
  s[key+'star_xp']%=1000;
  }
  if(season>=11){
   // Chapter 2: the season level is the Battle Pass tier, and the pass bar is the XP toward the next level.
   s.book_level=Math.max(1,Math.min(cap,s.level));
   s.book_xp=s.level>=100?0:s.xp;
  }else{
  while(s.book_xp>=10 && s.book_level<cap){s.book_xp-=10;s.book_level++;}
  if(s.book_level===cap)s.book_xp=0;
  }
  if(s.level===100)s.xp=0;
  s[key+'xp_total']=(s[key+'xp_total']||0)+amount;
  a.items??={};c.items??={};
  for(const [track,table,enabled] of [['free',free,true],['paid',paid,!!s.book_purchased]]){
   if(!enabled)continue;
   const last=s[key+track+'_reward_tier']??(season===4?s.tilted_season4_reward_tier||0:0);
   for(let tier=last+1;tier<=s.book_level;tier++)for(const reward of table.find(r=>r.Level===tier)?.Rewards||[]){
    const id=reward.templateId||reward.TemplateId;if(!id)continue;
    const target=/^Currency:/i.test(id)?c:a;
    const existing=Object.entries(target.items).find(([,v])=>v.templateId?.toLowerCase()===id.toLowerCase());
    const stack=/^(Currency|Token):/i.test(id);
    if(existing){if(stack)existing[1].quantity=(Number(existing[1].quantity)||0)+(reward.quantity||1);continue;}
    target.items['tilted_s'+season+'_'+track+'_'+tier+'_'+id]={templateId:id,quantity:reward.quantity||1,attributes:{item_seen:false,level:1,xp:0,variants:reward.variants||[],...(/^Currency:/i.test(id)?{platform:'Shared'}:{})}};
   }
   s[key+track+'_reward_tier']=s.book_level;
  }
  syncXpBoosts(a);
  ensureSeason2Shields(a);
  if(season===4){s.tilted_season4_reward_tier=s.book_level;ensureSeason4Styles(a);}
  for(const profile of [a,c]){profile.rvn=(profile.rvn||0)+1;profile.commandRevision=(profile.commandRevision||0)+1;profile.updated=new Date().toISOString();}
  return {level:s.level,xp:s.xp,tier:s.book_level,stars:s.book_xp};
}
