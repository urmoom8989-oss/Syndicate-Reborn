import crypto from 'node:crypto';
import fs from 'node:fs/promises';
import User from '../User/Mongodb/Schema/user.js';
import {selectedHero} from './syndicateStwHeroes.js';
import {selectedCustomOutfit} from './syndicateCustomOutfits.js';
import {ownCosmetics} from './syndicateUnlockAll.js';
import {project111Profile} from './syndicateLegacy111Profile.js';
// Never picked by "Random": the default outfits/pickaxe/glider and the STW hero outfits (SyndicateStw_, matches only).
const notRandom=/(^|:)(cid_\d{3}_athena_commando_[mf]_default|defaultpickaxe|defaultglider|syndicatestw_.*)$/i;
export function selectedLocker(profile, random=Math.random){
 const result={};
// contrail: Season 5+ (favorite_skydivecontrail); the match server puts it on the loadout's SkyDiveContrail.
 for(const [slot,type] of Object.entries({character:'AthenaCharacter',backpack:'AthenaBackpack',pickaxe:'AthenaPickaxe',glider:'AthenaGlider',contrail:'AthenaSkyDiveContrail'})){
  const selected=profile.stats?.attributes?.[slot==='contrail'?'favorite_skydivecontrail':`favorite_${slot}`];
  let template=profile.items?.[selected]?.templateId||selected||'';
  if(typeof template==='string' && /_random$/i.test(template)){
   // Only what this season's build has (its own export, when a match has run on it): an outfit from a later season
   // could not load in the match and the player got the default skin. Never a default item.
   const season=Number(profile.stats?.attributes?.season_num);
   const inBuild=new Set(ownCosmetics(Number.isInteger(season)?season:NaN).map(id=>id.toLowerCase()));
   const owned=Object.values(profile.items||{}).filter(item=>
    typeof item.templateId==='string' && item.templateId.toLowerCase().startsWith(type.toLowerCase()+':') &&
    !/_random$/i.test(item.templateId) && !notRandom.test(item.templateId) && (item.quantity??1)>0 &&
    (!inBuild.size || inBuild.has(item.templateId.toLowerCase())));
   const favorites=owned.filter(item=>item.attributes?.favorite===true);
   const pool=favorites.length?favorites:owned;
   template=pool.length?pool[Math.min(pool.length-1,Math.max(0,Math.floor(random()*pool.length)))].templateId:'';
  }
  const id=typeof template==='string'?template.replace(new RegExp(`^${type}:`,'i'),''):'';
  result[slot]=/^[a-z0-9_]+$/i.test(id)?id:'';
 }
 // Wraps (camo), Season 7 and later: favorite_itemwraps holds up to 7 slots (vehicles, then guns). Each
 // slot is written as wrap_<slot>=<WrapId> so the match server can put them on the player's loadout.
 const wraps=profile.stats?.attributes?.favorite_itemwraps;
 if(Array.isArray(wraps))wraps.slice(0,7).forEach((selected,slot)=>{
  const template=profile.items?.[selected]?.templateId||selected||'';
  const id=typeof template==='string'?template.replace(/^AthenaItemWrap:/i,''):'';
  if(/^[a-z0-9_]{1,80}$/i.test(id))result[`wrap_${slot}`]=id;
 });
 // Save the World hero picked in the launcher (Unlock all on): the match server puts it on instead of the outfit.
 const hero=selectedHero(profile.stats?.attributes);if(hero)result.hero=hero;
 // Syndicate custom outfit (syndicateCustomOutfits.js): the base outfit is the character; the match server swaps parts.
 const custom=selectedCustomOutfit(profile.stats?.attributes);
 if(custom&&!hero&&String(result.character).toLowerCase()===custom.base.toLowerCase())result.custom=custom.name;
 const outfit=Object.values(profile.items||{}).find(x=>String(x.templateId).toLowerCase()===(`AthenaCharacter:${result.character}`).toLowerCase());
 for(const variant of outfit?.attributes?.variants||[]){
  if(/^[a-z0-9_]{1,48}$/i.test(variant.channel||'') && /^[a-z0-9_]+(?:\.[a-z0-9_]+)*$/i.test(variant.active||'') && variant.active.length<=48 && (variant.owned||[]).includes(variant.active))
   result[`variant_${variant.channel}`]=variant.active;
 }
 return result;
}
export async function publishCharacter(accountId,profile){
 const user=await User.findOne({accountId}).select('username_lower').lean();if(!user)return;
 const selected=selectedLocker(profile);
 const folder=new URL('../../../Runtime/Server/locker/',import.meta.url);await fs.mkdir(folder,{recursive:true});
 const name=Buffer.from(user.username_lower,'utf8').toString('hex');
 // Always refresh the .txt the match server reads: a CID when an outfit is chosen, or "DEFAULT" for the empty/
 // random-default slot. Skipping the write left a stale CID, so picking the default slot kept the last-worn skin
 // (bug: a skin applied instead of a random default recruit).
 await fs.writeFile(new URL(`${name}.txt`,folder),/^CID_[a-z0-9_]+$/i.test(selected.character)?selected.character:'DEFAULT','utf8');
 const file=new URL(`${name}.loadout`,folder),temp=new URL(`${name}.${process.pid}.${crypto.randomUUID()}.loadout.tmp`,folder);
 await fs.writeFile(temp,Object.entries(selected).map(([k,v])=>`${k}=${v}`).join('\n'),'utf8');
 await fs.rename(temp,file);
}

// Pre-season (1.7.2) has no locker: the lobby shows a random outfit each time. Each match now does the same: one of
// the outfits this build has that the player owns (the default ones included), picked at random. Before, the match
// used whatever outfit was saved for another season, which 1.7.2 usually lacks, so everyone was the same default.
export function preseasonMatchProfile(athena, random = Math.random) {
 const p = project111Profile(athena, '1.7.2');
 const outfits = Object.entries(p.items || {}).filter(([, item]) => /^AthenaCharacter:CID_/i.test(item?.templateId || '') && !/syndicatestw_/i.test(item.templateId));
 if (outfits.length && !p.stats.attributes.tilted_stw_hero) p.stats.attributes.favorite_character = outfits[Math.min(outfits.length - 1, Math.floor(random() * outfits.length))][0];
 return p;
}
