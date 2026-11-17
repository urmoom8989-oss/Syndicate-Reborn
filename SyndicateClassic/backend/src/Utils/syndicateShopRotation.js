import {available111} from './syndicateCosmetics111.js';
import {catalog as passCatalog} from './syndicatePassShop.js';
import {REDEEM_ONLY} from './syndicateRedeemItems.js';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
// Exclude free and premium battle pass rewards in every cosmetic category.
const rewardsDir=new URL('../local/Battlepass/Jsons/',import.meta.url);
const passRewards=new Set([
 'AthenaCharacter:CID_140_Athena_Commando_M_Visitor',
 'AthenaCharacter:CID_017_Athena_Commando_M',
 'AthenaCharacter:CID_028_Athena_Commando_F',
 'AthenaGlider:Glider_ID_001',
 'AthenaPickaxe:Pickaxe_Lockjaw'
].map(id=>id.toLowerCase())); // Season 1 Seasonal Shop stays out of the rotating shop.
for(const file of fs.readdirSync(rewardsDir).filter(name=>/^Season[0-9]+[.]json$/i.test(name))){
 const pass=JSON.parse(fs.readFileSync(new URL(file,rewardsDir),'utf8'));
 for(const row of [...(pass.paidRewards||[]),...(pass.freeRewards||[])])
  for(const id of Object.keys(row))passRewards.add(id.toLowerCase());
}
// Launcher shop packs (starter packs and exclusives, syndicatePassShop.js) are sold only there: none of their
// items show up in the item shop or the hidden shop.
for(const pack of passCatalog)if(pack.kind==='pack')for(const r of pack.rewards||[])if(r?.templateId)passRewards.add(String(r.templateId).toLowerCase());
// Redeem-code exclusives (syndicateRedeemItems.js) are never sold either.
for(const id of REDEEM_ONLY)passRewards.add(id);
const notForSale=/carbide|omega|_default\b|^AthenaCharacter:CID_0(0\d|1\d|2[0-8])_Athena_Commando_[MF]$|^AthenaCharacter:CID_(commando|outlander|ninja|constructor|vip)_|^AthenaBackpack:BID_(commando|outlander|ninja|constructor)_|^AthenaBackpack:BID_STWHero|honor|jumpstart|bluebadass|twitch|psplus|ps4|xbox|samsung|galaxy|ikonik|founder|starter|_tbd|npc|_test|dev_|placeholder|henchman|hero_|banner|prestige|remix|vtid|_random|cid_515_athena_commando_m_barbequelarry|glider_warthog|eid_dancemoves/i; // eid_dancemoves: the free default dance, never for sale. // barbequelarry: redeem-code only (syndicateRedeem.js). glider_warthog (Mako): Season 1 only, so it is kept out of the Season 2+ rotating shops (Season 1 has its own curated shop in syndicateSeason1Shop.js).
const isBRGrant=id=>!/^AthenaGlider:.*umbrella/i.test(id) && !/^AthenaDance:(?!EID_)/i.test(id) && /^Athena(Character|Backpack|Pickaxe|Glider|Dance|LoadingScreen|SkyDiveContrail):/i.test(id) && !/founder|campaign|stw_/i.test(id);
const pools=JSON.parse(fs.readFileSync(new URL('../local/Storefront/rotation_pool.json',import.meta.url),'utf8'));
for(const category of ['outfits','accessories'])
 pools[category]=pools[category].filter(offer=>offer.itemGrants.every(isBRGrant) && !offer.itemGrants.some(id=>passRewards.has(id.toLowerCase()) || notForSale.test(id)));
const additions5=JSON.parse(fs.readFileSync(new URL('../local/Storefront/rotation_pool_season5.json',import.meta.url),'utf8'));
const season5Pools=Object.fromEntries(['outfits','accessories'].map(category=>{
 const combined=new Map(pools[category].map(o=>[o.itemGrants[0].toLowerCase(),o]));
 for(const offer of additions5[category])if(offer.itemGrants.every(isBRGrant)&&!offer.itemGrants.some(id=>passRewards.has(id.toLowerCase())||notForSale.test(id))&&!combined.has(offer.itemGrants[0].toLowerCase()))combined.set(offer.itemGrants[0].toLowerCase(),offer);
 return [category,[...combined.values()]];
}));
// Seasons 5-X: every shop cosmetic the build has, read from what its game server exported to
// Runtime/Server/cosmetics/season-N.json (a build also has every older season's items, so lower
// seasons' exports count too). Battle pass rewards, Save the World, defaults, promo exclusives
// and challenge rewards (Carbide, Omega, ...) are never offered.
const exportDir=fileURLToPath(new URL('../../../Runtime/Server/cosmetics/',import.meta.url));
const challengeDir=fileURLToPath(new URL('../../../Runtime/Server/challenge-data/',import.meta.url));
const price={AthenaCharacter:1200,AthenaBackpack:400,AthenaPickaxe:800,AthenaGlider:800,AthenaDance:500};
const fileList=dir=>{try{return fs.readdirSync(dir).filter(n=>/^season-\d+\.json$/.test(n));}catch{return [];}};
const stampOf=dir=>fileList(dir).map(n=>{try{return n+fs.statSync(path.join(dir,n)).mtimeMs;}catch{return n;}}).join('|');
function challengeRewards(){
 const ids=new Set();
 for(const name of fileList(challengeDir)){
  try{for(const m of fs.readFileSync(path.join(challengeDir,name),'utf8').matchAll(/Athena[A-Za-z]+:[A-Za-z0-9_.-]+/g))ids.add(m[0].toLowerCase());}catch{}
 }
 return ids;
}
// An outfit comes with the pieces of its own set that the build has: the back bling, pickaxe and glider
// named after it (CID_261_Athena_Commando_M_RaptorArcticCamo -> BID_142_RaptorArcticCamo,
// Pickaxe_ID_097_RaptorArcticCamo, Glider_ID_074_RaptorArcticCamo). A known offer's own grants are kept.
export function outfitSet(id,items,knownOffer,sellable=()=>true){
 const grants=new Map((knownOffer?.itemGrants||[id]).map(g=>[g.toLowerCase(),g]));
 const m=/^AthenaCharacter:CID_[A-Za-z0-9]+_Athena_Commando_([MF])_([A-Za-z0-9]+)$/i.exec(id);
 if(m){
  const name=m[2].toLowerCase(),gender=m[1].toUpperCase()==='F'?'female':'male';
  const fits=rest=>{rest=rest.toLowerCase();return rest===name || rest===name+gender || rest===name+(gender==='female'?'f':'m');};
  for(const [lower,item] of items){
   const p=/^Athena(Backpack|Pickaxe|Glider):(?:BID|Pickaxe_ID|Glider_ID)_[0-9]+_([A-Za-z0-9]+)$/i.exec(item);
   if(!p || !fits(p[2]) || !sellable(lower,item))continue;
   if([...grants.keys()].some(g=>g.startsWith('athena'+p[1].toLowerCase()+':')))continue; // one per slot
   grants.set(lower,item);
  }
 }
 return [...grants.values()];
}
const seasonCache=new Map();
function seasonPools(season){
 const key=season+'|'+stampOf(exportDir)+'|'+stampOf(challengeDir);
 if(seasonCache.has(key))return seasonCache.get(key);
 const items=new Map();
 for(const name of fileList(exportDir)){
  if(Number(name.match(/\d+/)[0])>season)continue;
  try{for(const id of JSON.parse(fs.readFileSync(path.join(exportDir,name),'utf8')).items||[])if(typeof id==='string')items.set(id.toLowerCase(),id);}catch{}
 }
 if(!items.size){seasonCache.set(key,null);return null;}
 const known=new Map();
 for(const offer of [...pools.outfits,...pools.accessories,...additions5.outfits,...additions5.accessories])known.set(offer.itemGrants[0].toLowerCase(),offer);
 if(season>=5)for(const offer of [...additions5.outfits,...additions5.accessories])items.set(offer.itemGrants[0].toLowerCase(),offer.itemGrants[0]);
 const blocked=challengeRewards();
 const outfits=[],accessories=[];
 const sellable=(lower,id)=>!!price[id.split(':')[0]] && isBRGrant(id) && !passRewards.has(lower) && !blocked.has(lower) && !notForSale.test(id);
 for(const [lower,id] of items){
  const type=id.split(':')[0];
  if(!price[type] || !isBRGrant(id) || passRewards.has(lower) || blocked.has(lower) || notForSale.test(id))continue;
  const offer={itemGrants:type==='AthenaCharacter'?outfitSet(id,items,known.get(lower),sellable):[id],price:known.get(lower)?.price||price[type]};
  (type==='AthenaCharacter'?outfits:accessories).push(offer);
 }
 // A fixed shuffle, so each refresh mixes old and new seasons instead of walking the IDs in order.
 const order=o=>crypto.createHash('sha1').update(o.itemGrants[0].toLowerCase()).digest('hex');
 outfits.sort((a,b)=>order(a)<order(b)?-1:1);accessories.sort((a,b)=>order(a)<order(b)?-1:1);
 const result=outfits.length>=4 && accessories.length>=8?{outfits,accessories}:null;
 if(result){
  // Bots wear these too: the game server reads one outfit name per line.
  const file=path.join(exportDir,'shop-outfits-season-'+season+'.txt');
  const text=outfits.map(o=>o.itemGrants[0].split(':')[1]).join('\n')+'\n';
  try{if(!fs.existsSync(file)||fs.readFileSync(file,'utf8')!==text){fs.writeFileSync(file+'.tmp',text);fs.renameSync(file+'.tmp',file);}}catch{}
 }
 seasonCache.clear();seasonCache.set(key,result);return result;
}
export function shopPoolSize(season){const p=seasonPools(season);return p?{outfits:p.outfits.length,accessories:p.accessories.length}:null;}
// Consecutive windows cover the entire pool, even when its size and slot count share factors.
export function rotatingShop(now=new Date(),season=4){
 const selected=season>=5?(seasonPools(season)||season5Pools):pools;
 const day=Math.floor(now.getTime()/21600000);
 if(!Number.isFinite(day))throw new Error('Invalid shop date');
 const take=(pool,count)=>{
  if(!Array.isArray(pool)||pool.length<count)throw new Error('Shop pool is incomplete');
  return Array.from({length:count},(_,i)=>pool[((day*count+i)%pool.length+pool.length)%pool.length]);
 };
 const outfits=take(selected.outfits,season>=8?4:3);
 const category=type=>selected.accessories.filter(offer=>offer.itemGrants[0].toLowerCase().startsWith(type.toLowerCase()+':'));
 const pickaxes=take(category('AthenaPickaxe'),season>=5?1:2),gliders=take(category('AthenaGlider'),2),emotes=take(category('AthenaDance'),2);
 const backblings=season>=5?category('AthenaBackpack'):[];
 return {daily1:pickaxes[0],daily2:backblings.length?take(backblings,1)[0]:pickaxes[1],daily3:gliders[0],daily4:gliders[1],
 daily5:emotes[0],daily6:emotes[1],featured1:outfits[0],featured2:outfits[1],featured3:outfits[2],featured4:outfits[3]};
}
export function nextShopRefresh(now=new Date()){return new Date((Math.floor(now.getTime()/21600000)+1)*21600000).toISOString();}

// Items a build has, from its game server's export (Runtime/Server/cosmetics/season-N.json), else none.
function exported(season){
 const ids=new Set();
 try{const d=JSON.parse(fs.readFileSync(path.join(exportDir,'season-'+season+'.json'),'utf8'));for(const id of d.items||[])if(typeof id==='string')ids.add(id.toLowerCase());}catch{}
 return ids;
}
// 1.11 and 3.5.1. Season 3 also keeps what its own build exported, so an outfit's back bling (Raptor ->
// BID_005_Raptor) is no longer stripped by the 1.11 list, which has no back blings.
export function rotatingShop2(now=new Date(),season=2){
 const window=Math.floor(now.getTime()/21600000);if(!Number.isFinite(window))throw Error('Invalid shop date');
 const own=season>=3?exported(season):new Set();
 const has=id=>available111(id) || own.has(String(id).toLowerCase());
 const safe=pool=>pool.filter(o=>available111(o.itemGrants[0])).map(o=>({...o,itemGrants:o.itemGrants.filter(has)}));
 const outfits=safe(pools.outfits),accessories=safe(pools.accessories);
 const choose=(pool,count)=>Array.from({length:Math.min(count,pool.length)},(_,i)=>pool[((window*count+i)%pool.length+pool.length)%pool.length]);
 // 1.11 has two featured panels; the three-panel 4.5 layout overflows.
 const selected={};choose(outfits,2).forEach((offer,i)=>selected['featured'+(i+1)]=offer);
 const daily=[...choose(accessories.filter(o=>/^AthenaPickaxe:/i.test(o.itemGrants[0])),2),...choose(accessories.filter(o=>/^AthenaGlider:/i.test(o.itemGrants[0])),2),...choose(accessories.filter(o=>/^AthenaDance:/i.test(o.itemGrants[0])),2)];
 daily.forEach((offer,i)=>selected['daily'+(i+1)]=offer);
 // Season 3: pin the Rogue Agent (Season 3 Starter Pack exclusive) as a permanent featured offer so the skin is
 // always in the shop and obtainable in-game. Its Catalyst back bling rides along on the purchase. Replaces one
 // of the two rotating featured outfits so the two-panel layout 3.5.1 expects is unchanged (a third overflows).
 // alwaysShow in dev builds (TILTED_DEVKIT=1) so it is visible even on an Unlock-All account that already owns it.
 if(season===3)selected.featured1={itemGrants:['AthenaCharacter:CID_090_Athena_Commando_M_Tactical','AthenaBackpack:BID_030_TacticalRogue'],price:1200,alwaysShow:process.env.TILTED_DEVKIT==='1'};
 return selected;
}

// Every cosmetic the item shop can sell, across all seasons (the launcher's hidden shop): the
// Chapter 1 rotation pools plus each exported season's pool, one offer per first item, with the
// outfit's set pieces and its shop price. Battle pass, challenge, promo and Save the World items
// are already left out by the pools.
export function allShopOffers(){
 const all=new Map();
 const add=list=>{for(const offer of list||[]){const key=String(offer.itemGrants?.[0]||'').toLowerCase();if(key&&!all.has(key))all.set(key,{itemGrants:[...offer.itemGrants],price:offer.price});}};
 for(let season=10;season>=5;season--){const p=seasonPools(season);if(p){add(p.outfits);add(p.accessories);}}
 add(season5Pools.outfits);add(season5Pools.accessories);add(pools.outfits);add(pools.accessories);
 return [...all.values()];
}
// An outfit bought as a bundle comes with its pieces (back bling, pickaxe, glider). Purchases made before the pieces
// were granted (or while a build's list stripped them, e.g. Raptor's back bling in Season 3) left them out: an owned
// outfit from a known shop bundle gets the missing pieces. Only the shop's own bundles count. Returns how many.
const bundleOf=new Map([...pools.outfits,...additions5.outfits].filter(o=>o.itemGrants.length>1).map(o=>[o.itemGrants[0].toLowerCase(),o.itemGrants.slice(1)]));
export function grantOwnedBundlePieces(athena){
 if(!athena?.items)return 0;
 const owned=new Set(Object.values(athena.items).map(i=>String(i?.templateId||'').toLowerCase()));let added=0;
 for(const id of [...owned]){
  const pieces=bundleOf.get(id);if(!pieces)continue;
  for(const piece of pieces){const key=piece.toLowerCase();if(owned.has(key))continue;
   athena.items['tilted_bundle_'+key]={templateId:piece,quantity:1,attributes:{item_seen:false,level:1,xp:0,variants:[]}};owned.add(key);added++;}
 }
 return added;
}
