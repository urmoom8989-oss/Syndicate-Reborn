import {syncXpBoosts} from './syndicateXpBoosts.js';
import {ensureSeason2Shields} from './syndicateSeason2Shields.js';
const fields=new Set(['level','xp','xp_overflow','book_level','book_xp','book_purchased','season_match_boost','season_friend_match_boost','season_update','rested_xp','rested_xp_overflow','rested_xp_mult','rested_xp_exchange','purchased_battle_pass_tier_offers','quest_manager','daily_rewards','party_assist_quest']);
const scoped=k=>fields.has(k)||/^tilted_season[0-9]+_/.test(k)||/^tilted_(challenge|quest)/.test(k);
const seasonalItem=i=>/^(Quest|ChallengeBundle|ChallengeBundleSchedule|Token):/i.test(i.templateId||'');
const clone=x=>JSON.parse(JSON.stringify(x));
export function activeSeason(p){const season=Number(p.stats.attributes.tilted_active_season);return [2,3,5,6,7,8,9,10,11,12].includes(season)?season:4;}
export function captureSeason(p){return {attributes:Object.fromEntries(Object.entries(p.stats.attributes).filter(([k])=>scoped(k)).map(([k,v])=>[k,clone(v)])),items:Object.fromEntries(Object.entries(p.items||{}).filter(([,v])=>seasonalItem(v)).map(([k,v])=>[k,clone(v)]))};}
// Replaces the active season's level, pass and challenge state with a captured one.
export function putSeason(p,state){
 const a=p.stats.attributes;p.items??={};
 for(const k of Object.keys(a))if(scoped(k))delete a[k];
 Object.assign(a,clone(state.attributes||{}));
 for(const [id,item] of Object.entries(p.items))if(seasonalItem(item))delete p.items[id];
 Object.assign(p.items,clone(state.items||{}));
}
export function activateSeason(p,season){
 if(![2,3,4,5,6,7,8,9,10,11,12].includes(season))throw Error('Unsupported season');
 const a=p.stats.attributes;let changed=false;
 if(!a.tilted_season_states){a.tilted_season_states={'4':captureSeason(p)};a.tilted_active_season=4;changed=true;}
 const old=activeSeason(p);
 if(old!==season){
  a.tilted_season_states[old]=captureSeason(p);
  putSeason(p,a.tilted_season_states[season]||{attributes:{level:0,xp:0,book_level:1,book_xp:0,book_purchased:false,season_match_boost:0,season_friend_match_boost:0,rested_xp:0,quest_manager:{},purchased_battle_pass_tier_offers:[]},items:{}});
  a.tilted_active_season=season;changed=true;
 }
 if(a.season_num!==season){a.season_num=season;changed=true;}
 const boostChanged=syncXpBoosts(p);
 return ensureSeason2Shields(p)||boostChanged||changed;
}
export function ensureSharedDefaults(p){
 p.items??={};
 // "Remove default outfits from locker" (launcher): while it is on, never re-add the defaults here.
 if(p?.stats?.attributes?.tilted_hide_defaults)return false;
 let changed=false;const owned=new Set(Object.values(p.items).map(i=>i.templateId?.toLowerCase()));
 const ensure=(key,templateId)=>{if(!owned.has(templateId.toLowerCase())){p.items[key]={templateId,quantity:1,attributes:{item_seen:true,level:1,xp:0}};owned.add(templateId.toLowerCase());changed=true;}};
 for(let n=1;n<=8;n++){const id='CID_'+String(n).padStart(3,'0')+'_Athena_Commando_'+(n<=4?'F':'M')+'_Default';ensure('tilted_default_'+id,'AthenaCharacter:'+id);}
 // Chapter 2 (Season 11/12) ships its own default recruits (CID_556..563 "RebirthDefault" A-D, F then M)
 // instead of CID_001..008. Grant them on Chapter 2 so the locker shows the Chapter 2 defaults and the empty
 // ("random default") slot resolves to one. Only on Chapter 2 builds, where the assets exist.
 const season=Number(p?.stats?.attributes?.season_num)||0;
 if(season>=11)for(let n=556;n<=563;n++){const sex=n<=559?'F':'M';const letter='ABCD'[(n-556)%4];const id='CID_'+n+'_Athena_Commando_'+sex+'_RebirthDefault'+letter;ensure('tilted_default_'+id,'AthenaCharacter:'+id);}
 // The default pickaxe and glider are shared across chapters (DefaultPickaxe/DefaultGlider); they ship in
 // athena.json so these are no-ops normally, but they let the toggle restore them after it removed them.
 ensure('tilted_default_pickaxe','AthenaPickaxe:DefaultPickaxe');
 ensure('tilted_default_glider','AthenaGlider:DefaultGlider');
 return changed;
}
// Any item test for the default outfits (CID_001..008, plus the Chapter 2 CID_556..563 RebirthDefaults), pickaxe
// and glider that come free with every account.
export const DEFAULT_ITEM=/^(athenacharacter:cid_\d{3}_athena_commando_[mf]_(default|rebirthdefault[a-d])|athenapickaxe:defaultpickaxe|athenaglider:defaultglider)$/i;
// "Remove default outfits from locker": while a.tilted_hide_defaults is on, strip the free default outfits,
// pickaxe and glider from the locker. A default that is equipped is first moved to an owned non-default of
// the same slot; if there is none, that one default is kept so the slot is never left empty. Reversible:
// turn the toggle off and ensureSharedDefaults puts them back. Returns true when it changed the profile.
export function applyHideDefaults(p){
 const a=p?.stats?.attributes;if(!a?.tilted_hide_defaults)return false;
 p.items??={};let changed=false;
 const slots=[['favorite_character','AthenaCharacter'],['favorite_pickaxe','AthenaPickaxe'],['favorite_glider','AthenaGlider']];
 for(const [attr,type] of slots){
  const cur=p.items[a[attr]];
  if(cur&&DEFAULT_ITEM.test(cur.templateId||'')){
   const alt=Object.entries(p.items).find(([,i])=>typeof i.templateId==='string'&&i.templateId.toLowerCase().startsWith(type.toLowerCase()+':')&&!DEFAULT_ITEM.test(i.templateId)&&(i.quantity??1)>0);
   if(alt){a[attr]=alt[0];changed=true;}
  }
 }
 for(const [key,item] of Object.entries(p.items)){
  if(DEFAULT_ITEM.test(item.templateId||'')&&!slots.some(([attr])=>a[attr]===key)){delete p.items[key];changed=true;}
 }
 return changed;
}
// Launcher pass bundles (syndicatePassShop.js, "SEASON N • COMPLETE PASS") grant every reward item. This also
// unlocks the season's Battle Pass itself: bought, every tier reached, and the reward trackers set to the
// last tier so nothing is granted twice. Called while that season is the active one (Fortnite.js), so
// it also covers passes bought before this existed. Returns true when something changed.
export function unlockSeasonPass(p,season){
 if(![2,3,4,5,6,7,8,9,10,11,12].includes(season)||activeSeason(p)!==season)return false;
 const a=p.stats.attributes,cap=season===2?70:100,key='tilted_season'+season+'_';let changed=false;
 const set=(k,v)=>{if(a[k]!==v){a[k]=v;changed=true;}};
 set('book_purchased',true);
 // Chapter 2's Battle Pass tier is its season level, so raise the level too. Chapter 1
 // keeps its real season level and only gets the tier.
 if(season>=11){set('level',100);set('xp',0);}
 if((Number(a.book_level)||0)<cap){set('book_level',cap);set('book_xp',0);}
 for(const track of ['free','paid'])if((Number(a[key+track+'_reward_tier'])||0)<cap)set(key+track+'_reward_tier',cap);
 if(season===4&&(Number(a.tilted_season4_reward_tier)||0)<cap)set('tilted_season4_reward_tier',cap);
 return changed;
}
// Launcher "Unlock all": the active season at its last level and Battle Pass tier, with XP that matches
// (none toward a next level). The reward trackers move to the last tier too, so no tier reward is ever
// claimed for it (turning Unlock all off puts the real trackers back). True when something changed.
export function holdSeasonMax(p,season){
 const a=p.stats.attributes,cap=season===2?70:100,key='tilted_season'+season+'_';let changed=false;
 const set=(k,v)=>{if(a[k]!==v){a[k]=v;changed=true;}};
 set('level',100);set('xp',0);set('book_level',cap);set('book_xp',0);set('book_purchased',true);
 for(const track of ['free','paid'])set(key+track+'_reward_tier',cap);
 if(season===4)set('tilted_season4_reward_tier',cap);
 return changed;
}
