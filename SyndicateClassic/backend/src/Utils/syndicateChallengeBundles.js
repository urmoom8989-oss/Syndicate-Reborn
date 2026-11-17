import {ensureSeason4Styles} from './syndicateSeason4Styles.js';
import fs from 'node:fs';
export const bundleData=JSON.parse(fs.readFileSync(new URL('./syndicateChallengeBundles.json',import.meta.url)));
export const bundleQuestIds=new Set(bundleData.bundles.flatMap(b=>b.quests.map(q=>('Quest:'+q.id).toLowerCase())));
const epoch='2018-05-01T00:00:00.000Z';
export function bundleQuestComplete(definition,attributes){
 if(definition.id==='Quest_BR_Interact_FORTNITE')return definition.objectives.filter(o=>(Number(attributes['completion_'+o.name])||0)>=o.target).length>=8;
 return definition.objectives.length>0 && definition.objectives.every(o=>attributes['completion_'+o.name]>=o.target);
}
export function ensureChallengeBundles(profile){
 const before=JSON.stringify(profile.items),a=profile.stats.attributes,items=profile.items;
 if(a.season_num!==4||!a.book_purchased)return false;
 // Consolidate older UUID-based rows into the canonical bundle/schedule rows.
 // Preserve quest progress before removing duplicates; links are rebuilt below.
 const known=new Map([
  ...bundleData.schedules.map(s=>[('ChallengeBundleSchedule:'+s.id).toLowerCase(),'tilted_schedule_'+s.id.toLowerCase()]),
  ...bundleData.bundles.map(b=>[('ChallengeBundle:'+b.id).toLowerCase(),'tilted_bundle_'+b.id.toLowerCase()]),
  ...bundleData.bundles.flatMap(b=>b.quests.map(q=>[('Quest:'+q.id).toLowerCase(),'tilted_bundlequest_'+q.id.toLowerCase()]))
 ]);
 const stateRank={Active:0,Completed:1,Claimed:2};
 for(const [id,item] of Object.entries(items)){
  const target=known.get(item.templateId?.toLowerCase());if(!target||id===target)continue;
  if(!items[target])items[target]=structuredClone(item);
  else {
   const dest=items[target].attributes??={},source=item.attributes||{};
   for(const [key,value] of Object.entries(source))if(key.startsWith('completion_'))dest[key]=Math.max(Number(dest[key])||0,Number(value)||0);
   if((stateRank[source.quest_state]??-1)>(stateRank[dest.quest_state]??-1))dest.quest_state=source.quest_state;
  }
  delete items[id];
 }
 const tier=Math.max(1,Number(a.book_level)||1);
 for(const schedule of bundleData.schedules){
  if(schedule.id==='Season4_ProgressiveB_Schedule'&&tier<100)continue;
  const sid='tilted_schedule_'+schedule.id.toLowerCase(),granted=[];
  for(const entry of schedule.entries){
   if(entry.unlockType===1&&tier<entry.unlockValue)continue;
   const bundle=bundleData.bundles.find(b=>b.id===entry.bundle);if(!bundle)continue;
   const bid='tilted_bundle_'+bundle.id.toLowerCase(),quests=[];
   for(const definition of bundle.quests){
    let pair=Object.entries(items).find(([,i])=>i.templateId?.toLowerCase()===('Quest:'+definition.id).toLowerCase());
    const qid=pair?.[0]||'tilted_bundlequest_'+definition.id.toLowerCase();
    if(!pair)items[qid]={templateId:'Quest:'+definition.id,quantity:1,attributes:{quest_state:'Active',creation_time:epoch,last_state_change_time:epoch,item_seen:false,level:-1,xp:0,max_level_bonus:0,sent_new_notification:false,xp_reward_scalar:1}};
    const q=items[qid].attributes;q.challenge_bundle_id=bid;
    for(const objective of definition.objectives){
     const key='completion_'+objective.name;q[key]??=0;
     if(objective.name.startsWith('athena_season_levelup_'))q[key]=Math.max(q[key],Math.min(objective.target,Number(a.level)||1));
    }
    if(bundleQuestComplete(definition,q)&&q.quest_state==='Active')q.quest_state='Completed';
    quests.push(qid);
   }
   const completed=quests.filter(id=>['Completed','Claimed'].includes(items[id].attributes.quest_state)).length;
   items[bid]={templateId:'ChallengeBundle:'+bundle.id,quantity:1,attributes:{...(items[bid]?.attributes||{}),has_unlock_by_completion:false,num_quests_completed:completed,num_quests_completed_max:completed,num_granted_bundle_quests:quests.length,grantedquestinstanceids:quests,challenge_bundle_schedule_id:sid,item_seen:false,level:1,xp:0,max_level_bonus:0}};
   granted.push(bid);
  }
  items[sid]={templateId:'ChallengeBundleSchedule:'+schedule.id,quantity:1,attributes:{...(items[sid]?.attributes||{}),unlock_epoch:epoch,granted_bundles:granted,item_seen:false,level:1,xp:0,max_level_bonus:0}};
 }
 ensureSeason4Styles(profile);
 return before!==JSON.stringify(items);
}
export function progressBundleChallenges(profile,event){
 if(!event)return;
 for(const item of Object.values(profile.items||{})){
  if(!bundleQuestIds.has(item.templateId?.toLowerCase())||item.attributes.quest_state!=='Active')continue;
  const d=bundleData.bundles.flatMap(b=>b.quests).find(q=>('Quest:'+q.id).toLowerCase()===item.templateId.toLowerCase());
  for(const o of d.objectives){
   const key='completion_'+o.name,old=Number(item.attributes[key])||0;let next=old;
   if(event.kind==='quest'&&event.quest?.toLowerCase()===d.id.toLowerCase()&&event.objective?.toLowerCase()===o.name&&Number.isSafeInteger(event.count)&&event.count>=0)next=Math.max(old,event.count);
   if(event.kind==='match'){
    if(o.name==='battlepass_athena_play_matches')next++;
    if(o.name==='battlepass_athena_outlive_players')next+=Math.max(0,Math.min(100,(Number(event.players)||0)-(Number(event.place)||100)));
   }
   if(event.kind==='win'&&o.name==='battlepass_athena_win_match')next++;
   item.attributes[key]=Math.min(o.target,next);
  }
  if(bundleQuestComplete(d,item.attributes))item.attributes.quest_state='Completed';
 }
 ensureChallengeBundles(profile);
}
