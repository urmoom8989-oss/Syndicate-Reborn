// The launcher's hidden item shop (unlocked with a key code in the launcher): every item-shop cosmetic
// from every season, searchable, bought with V-Bucks. Purchases land in the locker with the reward pop-up.
import fs from 'node:fs';
import Profile from '../User/Mongodb/Schema/profiles.js';
import {withAccountLock} from './syndicateAccountLock.js';
import {allShopOffers} from './syndicateShopRotation.js';
import {debitShopWallet,ensureSharedWallet,walletId} from './syndicateShopWallet.js';
import {addRewardGift} from './syndicatePurchaseGift.js';
import {randomUUID} from 'node:crypto';
const names=JSON.parse(fs.readFileSync(new URL('../local/syndicate-cosmetic-names.json',import.meta.url),'utf8'));
const kinds={AthenaCharacter:'Outfit',AthenaBackpack:'Back bling',AthenaPickaxe:'Pickaxe',AthenaGlider:'Glider',AthenaDance:'Emote',AthenaSkyDiveContrail:'Contrail',AthenaItemWrap:'Wrap',AthenaLoadingScreen:'Loading screen'};
const order=Object.keys(kinds);
const nameOf=id=>{const raw=id.split(':')[1]||id;return names[raw.toLowerCase()]||raw.replace(/^(CID|BID|EID|Pickaxe_ID|Glider_ID|Trails_ID|Wrap|LSID)_\d*_?/i,'').replace(/_/g,' ');};
let cache=null,cacheAt=0;
function offers(){
 if(cache && Date.now()-cacheAt<60000)return cache;
 cache=allShopOffers().filter(o=>kinds[o.itemGrants[0].split(':')[0]]).map(o=>{const id=o.itemGrants[0],type=id.split(':')[0];
  return {id,name:nameOf(id),type:kinds[type],price:Number(o.price)||0,items:o.itemGrants.map(g=>({id:g,name:nameOf(g),type:kinds[g.split(':')[0]]||''}))};})
  .sort((a,b)=>order.indexOf(a.id.split(':')[0])-order.indexOf(b.id.split(':')[0])||a.name.localeCompare(b.name));
 cacheAt=Date.now();return cache;
}
const owned=athena=>new Set(Object.values(athena?.items||{}).map(i=>String(i.templateId||'').toLowerCase()));
const balanceOf=common=>{try{ensureSharedWallet(common);}catch{}return Number(common?.items?.[walletId]?.quantity)||0;};
export function catalog(doc){
 const have=owned(doc.profiles.athena);
 return {balance:balanceOf(doc.profiles.common_core),items:offers().map(o=>({...o,owned:have.has(o.id.toLowerCase())}))};
}
export async function purchase(accountId,templateId){
 const offer=offers().find(o=>o.id.toLowerCase()===String(templateId||'').toLowerCase());
 if(!offer)throw new Error('That item is not in the shop.');
 return withAccountLock(accountId,async()=>{
  for(let attempt=0;attempt<6;attempt++){
   const doc=await Profile.collection.findOne({accountId});if(!doc?.profiles?.athena||!doc.profiles.common_core)throw new Error('Account profile is missing.');
   const a=doc.profiles.athena,c=doc.profiles.common_core,ar=a.rvn,cr=c.rvn,have=owned(a);
   if(have.has(offer.id.toLowerCase()))throw new Error('You already own '+offer.name+'.');
   if(!debitShopWallet(c,offer.price).ok)throw new Error('Not enough V-Bucks.');
   const loot=[];
   for(const id of offer.items.map(i=>i.id)){
    if(have.has(id.toLowerCase()))continue;
    const guid=randomUUID();
    a.items[guid]={templateId:id,quantity:1,attributes:{item_seen:false,level:1,xp:0,variants:[],favorite:false}};
    loot.push({itemType:id,itemGuid:guid,itemProfile:'athena',quantity:1});
   }
   addRewardGift(c,loot,'ITEM SHOP',offer.name+' is in your locker.');
   for(const p of [a,c]){p.rvn=(p.rvn||0)+1;p.commandRevision=(p.commandRevision||0)+1;p.updated=new Date().toISOString();}
   const saved=await Profile.collection.updateOne({accountId,'profiles.athena.rvn':ar,'profiles.common_core.rvn':cr},{$set:{'profiles.athena':a,'profiles.common_core':c}});
   if(saved.modifiedCount){if(global.giftReceived)global.giftReceived[accountId]=true;return {...catalog(doc),message:offer.name+' bought. It is in your locker.'};}
  }
  throw new Error('Your balance changed. Try again.');
 });
}
