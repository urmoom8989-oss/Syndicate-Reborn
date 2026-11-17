import {ensureSeason5Bundles,season5QuestIds} from './syndicateSeason5Bundles.js';
import {ensureChallengeBundles,bundleQuestIds} from './syndicateChallengeBundles.js';
import fs from 'node:fs';
export const definitions=JSON.parse(fs.readFileSync(new URL('./syndicateChallenges.json',import.meta.url)));
export function ensureDefaultOutfits(profile){
 profile.items??={};
 const owned=new Set(Object.values(profile.items).map(item=>item.templateId?.toLowerCase()));
 let changed=false;
 for(let n=1;n<=8;n++){
  const cid='CID_'+String(n).padStart(3,'0')+'_Athena_Commando_'+(n<=4?'F':'M')+'_Default';
  const templateId='AthenaCharacter:'+cid;
  if(owned.has(templateId.toLowerCase()))continue;
  let id='tilted_default_'+cid;
  while(profile.items[id])id+='_grant';
  profile.items[id]={templateId,quantity:1,attributes:{item_seen:false,level:1,xp:0,variants:[]}};
  owned.add(templateId.toLowerCase());changed=true;
 }
 return changed;
}
export function ensureChallenges(profile,now=new Date()){
 profile.items??={};const a=profile.stats.attributes;const day=now.toISOString().slice(0,10);let changed=ensureDefaultOutfits(profile);
 // Remove incompatible quests previously granted by the Season 12-only login handler.
 for(const [id,item] of Object.entries(profile.items))if(/^Quest:/i.test(item.templateId)&&!bundleQuestIds.has(item.templateId.toLowerCase())&&!(a.season_num===5&&season5QuestIds.has(item.templateId.toLowerCase()))&&!definitions.some(d=>('Quest:'+d.id).toLowerCase()===item.templateId.toLowerCase())){delete profile.items[id];changed=true;}
 const active=Object.values(profile.items).filter(i=>i.attributes?.tilted_daily&&i.attributes.quest_state==='Active');
 const daily=definitions.filter(d=>d.daily);
 const offset=Math.floor(now.getTime()/86400000)%daily.length;
 const chosen=[];if(a.tilted_quest_login_day!==day){for(let i=0;i<daily.length&&chosen.length+active.length<3;i++){const d=daily[(offset+i)%daily.length];if(!active.some(v=>v.templateId==='Quest:'+d.id))chosen.push(d);}}
 for(const d of [...definitions.filter(d=>!d.daily),...chosen]){
  const key='tilted_quest_'+d.id.toLowerCase()+(d.daily?'_'+day:'');if(profile.items[key])continue;
  profile.items[key]={templateId:'Quest:'+d.id,quantity:1,attributes:{creation_time:now.toISOString(),last_state_change_time:now.toISOString(),level:-1,item_seen:false,sent_new_notification:false,xp_reward_scalar:1,quest_state:'Active',max_level_bonus:0,xp:0,favorite:false,tilted_daily:d.daily,['completion_'+d.objective]:0}};changed=true;
 }
 if(a.tilted_quest_login_day!==day){a.tilted_quest_login_day=day;changed=true;}
 a.quest_manager={...(a.quest_manager||{}),dailyLoginInterval:now.toISOString(),dailyQuestRerolls:1};
 return (a.season_num===5?ensureSeason5Bundles(profile):ensureChallengeBundles(profile))||changed;
}
export function progressChallenges(profiles,event){
 if(!event)return {xp:0,stars:0};let xp=0,stars=0;
 for(const item of Object.values(profiles.athena.items||{})){
  const d=definitions.find(d=>('Quest:'+d.id).toLowerCase()===item.templateId?.toLowerCase());if(!d||item.attributes.quest_state!=='Active')continue;
  const a=item.attributes,key='completion_'+d.objective,old=Number(a[key])||0;let next=old;
  if(!d.id.startsWith('AthenaDailyQuest_PlayerElimination')&&!d.id.startsWith('AthenaDaily_Outlive')&&!['Quest_BR_Build_Structures','Quest_BR_Damage','AthenaDailyQuest_InteractTreasureChest','AthenaDailyQuest_InteractAmmoCrate','AthenaDailyQuest_PlayerElimination','Quest_BR_Eliminate','AthenaDaily_PlayMatches','AthenaDaily_Solo_Top25','AthenaDaily_Solo_Top50','AthenaDaily_Squad_Top6','AthenaDaily_Squad_Top12'].includes(d.id)&&event.kind==='quest'&&event.quest?.toLowerCase()===d.id.toLowerCase()&&event.objective?.toLowerCase()===d.objective&&Number.isSafeInteger(event.count)&&event.count>=0)next=Math.max(old,event.count);
  if(event.kind==='quest_delta'&&event.quest?.toLowerCase()===d.id.toLowerCase()&&event.objective?.toLowerCase()===d.objective&&Number.isSafeInteger(event.count)&&event.count>0&&event.count<=100000)next=old+event.count;
  // These server events remain reliable even when a native quest manager hasn't populated yet.
  if(event.kind==='kill'&&['athena_daily_kill_players_v2','battlepass_killingblow_athena_players'].includes(d.objective))next=old+1;
  if(event.kind==='kill'){
   const weapon=(event.weapon||'').toLowerCase();const families={AthenaDailyQuest_PlayerEliminationSniperRifles:'sniper',AthenaDailyQuest_PlayerEliminationSMGs:'smg',AthenaDailyQuest_PlayerEliminationPistols:'pistol',AthenaDailyQuest_PlayerEliminationShotguns:'shotgun',AthenaDailyQuest_PlayerEliminationAssaultRifles:'assault'};
   const smg=/pistol_(scavenger|autoheavysuppressed)/.test(weapon);
   const family=smg?'smg':weapon.includes('assault_lmg')?'lmg':weapon.includes('assault')?'assault':weapon.includes('shotgun')?'shotgun':weapon.includes('sniper')?'sniper':weapon.includes('pistol')?'pistol':'';
   if(families[d.id]&&family===families[d.id])next=old+1;
  }
  if(event.kind==='match'){
   const place=Number(event.place)||100;
   const survived=Math.max(0,Math.min(100,(Number(event.players)||0)-place));
   if(d.id==='AthenaDaily_Outlive'||d.id==='AthenaDaily_Outlive_Solo'&&event.mode==='solo'||d.id==='AthenaDaily_Outlive_Squad'&&event.mode==='squad')next=old+survived;
   if(d.id==='AthenaDaily_PlayMatches')next=old+1;
   if(d.id==='AthenaDaily_Solo_Top25'&&event.mode==='solo'&&place<=25)next=old+1;
   if(d.id==='AthenaDaily_Solo_Top50'&&event.mode==='solo'&&place<=50)next=old+1;
   if(d.id==='AthenaDaily_Squad_Top6'&&event.mode==='squad'&&place<=6)next=old+1;
   if(d.id==='AthenaDaily_Squad_Top12'&&event.mode==='squad'&&place<=12)next=old+1;
  }
  next=Math.min(d.target,next);if(next===old)continue;a[key]=next;a.last_state_change_time=new Date().toISOString();
  if(next>=d.target){a.quest_state='Claimed';a.tilted_reward_claimed=true;xp+=d.daily?500:0;stars+=d.daily?5:5;}
 }
 if(stars)profiles.athena.stats.attributes.book_xp=(profiles.athena.stats.attributes.book_xp||0)+stars;
 return {xp,stars};
}
