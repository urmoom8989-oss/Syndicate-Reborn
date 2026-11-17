// Syndicate: the launcher's Choose Player > Reset season (Routes/launcher.js /launcher/seasonreset/*).
//
// For one season (2 to X, Chapter 2 Seasons 1 and 2) the player can:
//  - reset its progress: level, XP, Battle Pass tier and challenges go back to a first visit (the same fresh state
//    syndicateSeasonState.js gives a season that was never played). V-Bucks the pass already paid out are not paid
//    again when those tiers are earned again (tilted_seasonN_<track>_currency_through).
//  - take that season's Battle Pass items out of the locker: all of them or the ones they pick. Without a progress
//    reset the removed items are remembered (stats.attributes.tilted_removed_pass_items[season]) so
//    grantMissingPassCosmetics and applyProgression never put them back; "Put back" clears that list. With a reset
//    nothing is remembered: the items are earned again tier by tier.
// Unlock All players must switch to Progression first (Unlock All puts every cosmetic back on every launch).
// Everything here works on the profile objects only; the route reads and saves them.
import fs from 'node:fs';
import {activeSeason,captureSeason,putSeason,freshSeasonState} from './syndicateSeasonState.js';
import {walletId,ensureSharedWallet} from './syndicateShopWallet.js';

export const RESET_SEASONS = Object.freeze([2,3,4,5,6,7,8,9,10,11,12]);
export const seasonLabel = s => s===10?'Season X':s===11?'Chapter 2 Season 1':s===12?'Chapter 2 Season 2':'Season '+s;
const tierCap = s => s===2?70:100;
const lower = v => String(v||'').toLowerCase();
const clone = x => JSON.parse(JSON.stringify(x));
// Same cosmetic types grantMissingPassCosmetics hands out.
export const PASS_COSMETIC = /^(Athena(Character|Backpack|Pickaxe|Glider|Dance|ItemWrap|SkyDiveContrail|LoadingScreen|MusicPack|Pet|PetCarrier|Spray|Emoji|Toy)|HomebaseBannerIcon):/i;
// Items that come with a pass outfit without being in the pass tables: removed together with the outfit.
const COMPANIONS = Object.freeze({
 // Season 2 knights: 1.11 draws the shield on the outfit, later builds need it as a back bling (syndicateSeason2Shields.js).
 'athenacharacter:cid_032_athena_commando_m_medieval':['athenabackpack:bid_001_bluesquire'],
 'athenacharacter:cid_033_athena_commando_f_medieval':['athenabackpack:bid_002_royaleknight'],
 'athenacharacter:cid_035_athena_commando_m_medieval':['athenabackpack:bid_004_blackknight'],
 // Season 4 Carbide and Omega: their progressive pickaxes (syndicateSeason4Styles.js).
 'athenacharacter:cid_115_athena_commando_m_carbideblue':['athenapickaxe:pickaxe_id_047_carbideblue'],
 'athenacharacter:cid_116_athena_commando_m_carbideblack':['athenapickaxe:pickaxe_id_048_carbideblack'],
});
const TYPE_ORDER = ['Outfit','Back bling','Pickaxe','Glider','Contrail','Emote','Wrap','Music','Loading screen','Pet','Toy','Spray','Emoticon','Banner'];
function typeLabel(templateId){
 const [type,id='']=lower(templateId).split(':');
 if(type==='athenadance')return id.startsWith('spid_')?'Spray':id.startsWith('emoji_')?'Emoticon':id.startsWith('toy_')?'Toy':'Emote';
 return {athenacharacter:'Outfit',athenabackpack:'Back bling',athenapickaxe:'Pickaxe',athenaglider:'Glider',athenaitemwrap:'Wrap',
  athenaskydivecontrail:'Contrail',athenaloadingscreen:'Loading screen',athenamusicpack:'Music',athenapet:'Pet',athenapetcarrier:'Pet',
  athenaspray:'Spray',athenaemoji:'Emoticon',athenatoy:'Toy',homebasebannericon:'Banner'}[type]||'Item';
}
let names=null;
function itemName(templateId){
 names??=(()=>{try{return JSON.parse(fs.readFileSync(new URL('../local/syndicate-cosmetic-names.json',import.meta.url),'utf8'));}catch{return {};}})();
 const id=lower(templateId).split(':')[1]||'';
 if(names[id])return names[id];
 const words=s=>s.split(/[_\s]+/).filter(Boolean).map(w=>w[0].toUpperCase()+w.slice(1)).join(' ');
 // Season banners: brseason02salt / brs8jollyroger / brs12_cowboy -> "Salt Banner".
 if(/^homebasebannericon:/i.test(templateId))return (words(id.replace(/^br(?:season|s)\d+_?/,''))||'Season')+' Banner';
 return words(id);
}

const tables=new Map();
function passTables(season){
 if(!tables.has(season)){
  const read=name=>{try{return JSON.parse(fs.readFileSync(new URL(`../local/Battlepass/Data/Season${season}/${name}.json`,import.meta.url),'utf8'));}catch{return [];}};
  tables.set(season,{free:read('SeasonFreeBattlepass'),paid:read('SeasonPaidBattlepass')});
 }
 return tables.get(season);
}
// Every cosmetic in the season's pass, once, at the first tier it appears: {templateId, id, tier, track, variants}.
export function passItems(season){
 if(!RESET_SEASONS.includes(season))return [];
 const {free,paid}=passTables(season),found=new Map();
 for(const [track,table] of [['free',free],['paid',paid]])for(const row of table||[]){
  const tier=Number(row?.Level);if(!Number.isInteger(tier)||tier<1)continue;
  for(const reward of row.Rewards||[]){
   const templateId=reward?.templateId||reward?.TemplateId;if(!templateId||!PASS_COSMETIC.test(templateId))continue;
   const id=lower(templateId),known=found.get(id);
   if(!known||tier<known.tier||(tier===known.tier&&track==='free'))found.set(id,{templateId,id,tier,track,variants:Array.isArray(reward.variants)?reward.variants:[]});
  }
 }
 return [...found.values()].sort((a,b)=>a.tier-b.tier||(a.track===b.track?0:a.track==='free'?-1:1));
}

// The season's own attributes: the live ones while it is the active season, else its saved state (null if never played).
function seasonAttributes(athena,season){
 const a=athena?.stats?.attributes;if(!a)return null;
 if(activeSeason(athena)===season)return a;
 return a.tilted_season_states?.[season]?.attributes||null;
}
export function removedPassItems(attributes,season){
 const list=attributes?.tilted_removed_pass_items?.[season];
 return new Set(Array.isArray(list)?list.map(lower):[]);
}
function setRemoved(attributes,season,set){
 const all=attributes.tilted_removed_pass_items&&typeof attributes.tilted_removed_pass_items==='object'?attributes.tilted_removed_pass_items:{};
 if(set.size)all[season]=[...set].sort();else delete all[season];
 if(Object.keys(all).length)attributes.tilted_removed_pass_items=all;else delete attributes.tilted_removed_pass_items;
}
// Launcher pass bundles that unlock this season (syndicatePassShop.js ids, as Routes/Fortnite.js checks them).
export const passIdsFor = s => s===11?['chapter2-season1','season11']:s===12?['chapter2-season2','season12']:['season'+s];
function ownedIds(profiles){
 const ids=new Set();
 for(const profile of [profiles?.athena,profiles?.common_core])
  for(const item of Object.values(profile?.items||{}))if((item?.quantity??1)>0)ids.add(lower(item?.templateId));
 return ids;
}

// What the launcher panel shows for one season.
export function seasonView(doc,season){
 const athena=doc.profiles.athena,a=athena.stats.attributes,active=activeSeason(athena),src=seasonAttributes(athena,season);
 const owned=ownedIds(doc.profiles),removed=removedPassItems(a,season);
 const items=passItems(season).map(i=>({templateId:i.templateId,name:itemName(i.templateId),type:typeLabel(i.templateId),tier:i.tier,track:i.track,
  owned:owned.has(i.id),removed:removed.has(i.id)&&!owned.has(i.id)}));
 const typeRank=t=>{const n=TYPE_ORDER.indexOf(t);return n<0?TYPE_ORDER.length:n;};
 items.sort((x,y)=>typeRank(x.type)-typeRank(y.type)||x.tier-y.tier||x.name.localeCompare(y.name));
 const purchase=passIdsFor(season).map(id=>doc.syndicatePassPurchases?.[id]).find(Boolean);
 return {season,label:seasonLabel(season),active:active===season,played:!!src,
  level:Math.max(0,Math.floor(Number(src?.level)||0)),tier:Math.max(0,Math.min(tierCap(season),Math.floor(Number(src?.book_level)||0))),
  completePass:purchase?{price:Math.max(0,Math.floor(Number(purchase.price)||0))}:null,
  owned:items.filter(i=>i.owned).length,removed:items.filter(i=>i.removed).length,total:items.length,items};
}
export function resetView(doc,season){
 const a=doc.profiles.athena.stats.attributes;
 const pick=RESET_SEASONS.includes(season)?season:activeSeason(doc.profiles.athena);
 return {unlockAll:!!a.tilted_unlock_all,seasons:RESET_SEASONS.map(s=>({season:s,label:seasonLabel(s)})),selected:seasonView(doc,pick)};
}

// Unequips removed keys/template ids everywhere the locker keeps a choice (same places removeUnlockAll clears).
function unequip(athena,gone){
 const a=athena.stats.attributes;
 for(const field of ['favorite_character','favorite_backpack','favorite_pickaxe','favorite_glider','favorite_skydivecontrail','favorite_loadingscreen','favorite_musicpack'])
  if(gone(a[field]))a[field]='';
 for(const field of ['favorite_dance','favorite_itemwraps'])if(Array.isArray(a[field]))a[field]=a[field].map(v=>gone(v)?'':v);
 for(const item of Object.values(athena.items||{})){
  const slots=item?.attributes?.locker_slots_data?.slots;if(!slots)continue;
  for(const slot of Object.values(slots))if(Array.isArray(slot?.items))slot.items=slot.items.map(v=>gone(v)?'':v);
 }
}
// Takes the season's pass items in `templateIds` (and their companions) out of the locker. With remember, they stay out
// until "Put back" or a progress reset. Returns how many of the picked pass items were removed.
export function removePassItems(profiles,season,templateIds,{remember=true}={}){
 const athena=profiles.athena,a=athena.stats.attributes,pass=new Map(passItems(season).map(i=>[i.id,i]));
 const picked=new Set((templateIds||[]).map(lower).filter(id=>pass.has(id)));
 if(!picked.size)return 0;
 const doomed=new Set(picked);for(const id of picked)for(const extra of COMPANIONS[id]||[])doomed.add(extra);
 const keys=new Set(),ids=new Set();
 for(const profile of [athena,profiles.common_core]){
  if(!profile?.items)continue;
  for(const [key,item] of Object.entries(profile.items))
   if(doomed.has(lower(item?.templateId))){keys.add(lower(key));ids.add(lower(item.templateId));delete profile.items[key];}
 }
 if(keys.size)unequip(athena,v=>typeof v==='string'&&(keys.has(lower(v))||ids.has(lower(v))));
 if(remember){const removed=removedPassItems(a,season);for(const id of picked)removed.add(id);setRemoved(a,season,removed);}
 return [...picked].filter(id=>ids.has(id)).length;
}

// Puts the season back to a first visit. The live season is replaced in place; another season's saved state is
// replaced. Removed-item memory for the season is cleared so the items can be earned again.
export function resetSeasonProgress(athena,season){
 if(!RESET_SEASONS.includes(season))throw Error('That season cannot be reset.');
 const a=athena.stats.attributes,key='tilted_season'+season+'_';
 // Same first-run setup activateSeason does, so a later season switch keeps working.
 if(!a.tilted_season_states){a.tilted_season_states={'4':captureSeason(athena)};a.tilted_active_season=4;}
 const active=activeSeason(athena),old=(season===active?a:a.tilted_season_states[season]?.attributes)||{};
 const fresh=freshSeasonState();
 for(const track of ['free','paid']){
  const paidOut=Number(old[key+track+'_reward_tier']??(season===4?old.tilted_season4_reward_tier:0))||0;
  const through=Math.max(Number(old[key+track+'_currency_through'])||0,paidOut);
  if(through>0)fresh.attributes[key+track+'_currency_through']=Math.min(tierCap(season),through);
 }
 if(season===active)putSeason(athena,fresh);
 a.tilted_season_states[season]=clone(fresh);
 // Unlock All's saved "before" copy of this season would bring the old progress back if it is ever turned off.
 if(a.tilted_maxed_before&&typeof a.tilted_maxed_before==='object')delete a.tilted_maxed_before[season];
 setRemoved(a,season,new Set());
 return true;
}

// Gives back V-Bucks paid for the season's launcher Complete Pass, so the pass can be earned (or bought) again.
// Returns {ids, refund}: the purchase ids to unset on the document and the V-Bucks credited.
function refundCompletePass(doc,season){
 const ids=passIdsFor(season).filter(id=>doc.syndicatePassPurchases?.[id]);
 let refund=0;for(const id of ids)refund+=Math.max(0,Math.floor(Number(doc.syndicatePassPurchases[id]?.price)||0));
 if(refund>0){
  const c=doc.profiles.common_core;ensureSharedWallet(c);
  const total=(Number(c.items[walletId].quantity)||0)+refund;
  if(!Number.isSafeInteger(total))throw Error('V-Bucks balance overflow.');
  c.items[walletId].quantity=total;
 }
 return {ids,refund};
}

// One launcher request. Mutates doc.profiles; returns what the route must save and tell the player.
export function applySeasonReset(doc,season,{resetProgress=false,remove=[]}={}){
 if(!RESET_SEASONS.includes(season))throw Error('That season cannot be reset.');
 const a=doc.profiles.athena.stats.attributes;
 if(a.tilted_unlock_all)throw Error('Switch this player to Progression first: Unlock All puts every cosmetic back each time you play.');
 const removed=removePassItems(doc.profiles,season,remove,{remember:!resetProgress});
 let purchases={ids:[],refund:0};
 if(resetProgress){resetSeasonProgress(doc.profiles.athena,season);purchases=refundCompletePass(doc,season);}
 if(!resetProgress&&!removed)throw Error('Nothing to change: pick items that are in the locker, or turn on the progress reset.');
 const label=seasonLabel(season),parts=[];
 if(resetProgress)parts.push(label+' starts over: level, Battle Pass tier and challenges are reset');
 if(removed)parts.push(removed+' Battle Pass item'+(removed===1?'':'s')+' removed from the locker'+(resetProgress?'':' (they stay out until you put them back)'));
 if(purchases.refund)parts.push(purchases.refund.toLocaleString('en-US')+' V-Bucks refunded for the Complete Pass');
 return {removed,reset:resetProgress,refund:purchases.refund,unsetPurchases:purchases.ids,message:parts.join('; ')+'.'};
}

// "Put back": the season's removed items return for every tier already reached; the rest come back as they are earned.
export function restoreRemovedPassItems(profiles,season){
 const athena=profiles.athena,a=athena.stats.attributes,list=removedPassItems(a,season);
 if(!list.size)return 0;
 setRemoved(a,season,new Set());
 const src=seasonAttributes(athena,season)||{},reached=Math.max(0,Math.min(tierCap(season),Number(src.book_level)||0)),premium=!!src.book_purchased;
 const owned=ownedIds(profiles);athena.items??={};let added=0;
 for(const item of passItems(season)){
  if(!list.has(item.id)||owned.has(item.id)||item.tier>reached||(item.track==='paid'&&!premium))continue;
  athena.items['tilted_s'+season+'_'+item.track+'_'+item.tier+'_'+item.templateId]={templateId:item.templateId,quantity:1,
   attributes:{item_seen:false,level:1,xp:0,variants:clone(item.variants)}};
  owned.add(item.id);added++;
 }
 return added;
}
