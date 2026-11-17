import fs from 'node:fs';
export const bundleData=JSON.parse(fs.readFileSync(new URL('./syndicateSeason5Bundles.json',import.meta.url)));
export const season5QuestIds=new Set(bundleData.bundles.flatMap(b=>b.quests.map(q=>('Quest:'+q.id).toLowerCase())));
const epoch='2018-07-12T00:00:00.000Z';
export function bundleQuestComplete(definition,attributes){
 if(definition.id==='Quest_BR_Interact_FORTNITE')return definition.objectives.filter(o=>(Number(attributes['completion_'+o.name])||0)>=o.target).length>=8;
 return definition.objectives.length>0 && definition.objectives.every(o=>attributes['completion_'+o.name]>=o.target);
}
export function ensureSeason5Bundles(profile){
 const before=JSON.stringify(profile.items),a=profile.stats.attributes,items=profile.items;
 if(a.season_num!==5||!a.book_purchased)return false;
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
  if(schedule.id==='Season5_ProgressiveB_Schedule'&&tier<100)continue;
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
     // Drift's "Gain N XP" objectives are named athena_s5_season_gain_xp_progressive_*: they never matched 'seasonxp', so Drift stayed at 0.
     if(/seasonxp|season_gain_xp/.test(objective.name))q[key]=Math.max(q[key],Math.min(objective.target,Number(a.tilted_season5_xp_total)||0));
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

 ensureProgressiveStyles(profile);
 return before!==JSON.stringify(items);
}
// Drift (ProgressiveA) and Ragnarok (ProgressiveB): each completed "Gain N XP" stage unlocks the next Progressive style
// (Stage1-Stage5) on the outfit, like the Season 4 Carbide/Omega stages (syndicateSeason4Styles.js).
const progressiveOutfits=[
 {id:'athenacharacter:cid_161_athena_commando_m_drift',quest:/^quest:quest_br_s5_gain_seasonxp_progressivea_0(\d)$/i},
 {id:'athenacharacter:cid_165_athena_commando_m_darkviking',quest:/^quest:quest_br_s5_gain_seasonxp_progressiveb_0(\d)$/i}
];
function ensureProgressiveStyles(profile){
 const items=Object.values(profile.items||{});
 for(const track of progressiveOutfits){
  const outfit=items.find(i=>i.templateId?.toLowerCase()===track.id);if(!outfit)continue;
  const stages=items.filter(i=>['Completed','Claimed'].includes(i.attributes?.quest_state)).map(i=>track.quest.exec(i.templateId||'')?.[1]).filter(Boolean).map(n=>'Stage'+Number(n));
  const a=outfit.attributes??={};a.variants??=[];
  let v=a.variants.find(x=>x.channel==='Progressive');
  if(!v){v={channel:'Progressive',active:'Stage0',owned:['Stage0']};a.variants.push(v);}
  if(!Array.isArray(v.owned))v.owned=['Stage0'];
  for(const stage of ['Stage0',...stages])if(!v.owned.includes(stage)){v.owned.push(stage);a.item_seen=false;}
  if(!v.owned.includes(v.active))v.active='Stage0';
 }
}
export function progressSeason5Bundles(profile,event){
 if(!event)return;
 for(const item of Object.values(profile.items||{})){
  if(!season5QuestIds.has(item.templateId?.toLowerCase())||item.attributes.quest_state!=='Active')continue;
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
 ensureSeason5Bundles(profile);
}
