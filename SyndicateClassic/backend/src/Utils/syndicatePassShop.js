import fs from 'node:fs';
import {randomUUID} from 'node:crypto';
import Profile from '../User/Mongodb/Schema/profiles.js';
import {withAccountLock} from './syndicateAccountLock.js';
import {activeSeason,unlockSeasonPass} from './syndicateSeasonState.js';
import {season1Offers} from './syndicateSeason1Shop.js';
import {ownedSnapshot,addedLoot,addRewardGift} from './syndicatePurchaseGift.js';
const read=p=>JSON.parse(fs.readFileSync(new URL(p,import.meta.url),'utf8'));
const names=read('../local/syndicate-cosmetic-names.json');
const categories={AthenaCharacter:'Outfits',AthenaBackpack:'Back blings',AthenaPickaxe:'Pickaxes',AthenaGlider:'Gliders',AthenaDance:'Emotes',AthenaSkyDiveContrail:'Contrails',AthenaLoadingScreen:'Loading screens',AthenaItemWrap:'Wraps',HomebaseBannerIcon:'Banners',Currency:'V-Bucks',Token:'Boosts'};
function rewards(season){
 const json=read(`../local/Battlepass/Jsons/Season${season}.json`), result=new Map();
 for(const row of [...json.paidRewards,...json.freeRewards])for(const [id,quantity] of Object.entries(row)){
  if(!id || !/^(Athena|HomebaseBanner|Currency:|Token:)/i.test(id) || !Number.isSafeInteger(quantity)||quantity<=0)continue;
  const key=id.toLowerCase(),stack=/^(Currency|Token):/i.test(id);
  if(result.has(key)){if(stack)result.get(key).quantity+=quantity;continue;}
  result.set(key,{templateId:id,quantity:stack?quantity:1});
 }
 return [...result.values()];
}
// The original Season 1 Season Shop (same items, levels and prices as syndicateSeason1Shop.js).
const season1=season1Offers.map(o=>({templateId:o.id,quantity:1,level:o.level}));
const season1Level=Math.max(...season1Offers.map(o=>o.level));
export const catalog=[
 {id:'season1',title:'SEASON 1 • SEASON SHOP',price:4400,minLevel:season1Level,description:`All four original Season Shop items: Aerial Assault One (Season Level 5), Aerial Assault Trooper (15), Renegade Raider (20) and Raider's Revenge (35). 500 + 1,200 + 1,200 + 1,500 V-Bucks. Unlocks at Season Level ${season1Level}.`,rewards:season1},
 {id:'season2',title:'SEASON 2 • COMPLETE PASS',price:11300,description:'70 tiers. Standard pass (950) + 69 tiers × 150 V-Bucks. Includes free and premium rewards.',rewards:rewards(2)},
 {id:'season3',title:'SEASON 3 • COMPLETE PASS',price:15800,description:'100 tiers. Standard pass (950) + 99 tiers × 150 V-Bucks. Includes free and premium rewards.',rewards:rewards(3)},
 {id:'chapter2-season1',title:'CHAPTER 2 SEASON 1 - COMPLETE PASS',price:15800,description:'100 tiers. Includes every free and premium Chapter 2 Season 1 reward.',rewards:rewards(11)},
 {id:'chapter2-season2',title:'CHAPTER 2 SEASON 2 - COMPLETE PASS',price:15800,description:'100 tiers. Includes every free and premium Chapter 2 Season 2 reward.',rewards:rewards(12)},
 ...[4,5,6,7,8,9,10].map(season=>({id:'season'+season,title:`SEASON ${season===10?'X':season} — COMPLETE PASS`,price:15800,description:'100 tiers. Includes all free and premium rewards.',rewards:rewards(season)})),
 ...[
 ['rogue-agent','ROGUE AGENT PACK','AthenaCharacter:CID_090_Athena_Commando_M_Tactical','AthenaBackpack:BID_030_TacticalRogue'],
 ['wingman','WINGMAN PACK','AthenaCharacter:CID_139_Athena_Commando_M_FighterPilot','AthenaBackpack:BID_056_FighterPilot'],
 ['blue-team-leader','BLUE TEAM LEADER PACK','AthenaCharacter:CID_052_Athena_Commando_F_PSBlue','AthenaGlider:Glider_ID_013_PSBlue'],
 ['blue-striker','BLUE STRIKER PACK','AthenaCharacter:CID_138_Athena_Commando_M_PSBurnout','AthenaBackpack:BID_055_PSBurnout','AthenaPickaxe:Pickaxe_ID_088_PSBurnout','AthenaGlider:Glider_ID_067_PSBurnout','AthenaSkyDiveContrail:Trails_ID_019_PSBurnout'],
 ['trailblazer','TRAILBLAZER PACK','AthenaCharacter:CID_114_Athena_Commando_F_TacticalWoodland','AthenaBackpack:BID_049_TacticalWoodland','AthenaPickaxe:Pickaxe_ID_044_TacticalUrbanHammer','AthenaDance:EID_HipHop01'],
 ['reflex','REFLEX PACK','AthenaCharacter:CID_259_Athena_Commando_M_StreetOps','AthenaBackpack:BID_140_StreetOpsMale','AthenaPickaxe:Pickaxe_ID_118_StreetOps','AthenaGlider:Glider_ID_092_StreetOps','AthenaCharacter:CID_386_Athena_Commando_M_StreetOpsStealth','AthenaBackpack:BID_249_StreetOpsStealth','AthenaPickaxe:Pickaxe_ID_189_StreetOpsStealth','AthenaGlider:Glider_ID_137_StreetOpsStealth'],
 ['frostbite','FROSTBITE PACK','AthenaCharacter:CID_261_Athena_Commando_M_RaptorArcticCamo','AthenaBackpack:BID_142_RaptorArcticCamo','AthenaPickaxe:Pickaxe_ID_097_RaptorArcticCamo','AthenaGlider:Glider_ID_074_RaptorArcticCamo'],
 ['red-strike','RED STRIKE PACK','AthenaCharacter:CID_384_Athena_Commando_M_StreetAssassin','AthenaBackpack:BID_247_StreetAssassin'],
 ['ace','ACE PACK','AthenaCharacter:CID_195_Athena_Commando_F_Bling','AthenaBackpack:BID_101_BlingFemale'],
 ['summit-striker','SUMMIT STRIKER PACK','AthenaCharacter:CID_253_Athena_Commando_M_MilitaryFashion2','AthenaBackpack:BID_134_MilitaryFashion'],
 ['cobalt','COBALT PACK','AthenaCharacter:CID_327_Athena_Commando_M_BlueMystery','AthenaBackpack:BID_203_BlueMystery'],
 ['fallen-love-ranger','FALLEN LOVE RANGER QUEST PACK','AthenaCharacter:CID_343_Athena_Commando_M_CupidDark','AthenaBackpack:BID_208_CupidDarkMale'],
 ['laguna','LAGUNA PACK','AthenaCharacter:CID_367_Athena_Commando_F_Tropical','AthenaBackpack:BID_231_TropicalFemale','AthenaItemWrap:Wrap_033_TropicalGirl'],
 ['inferno','INFERNO PACK','AthenaCharacter:CID_370_Athena_Commando_M_EvilSuit','AthenaPickaxe:Pickaxe_ID_177_EvilSuit','AthenaItemWrap:Wrap_037_EvilSuit2'],
 ['wilde','WILDE PACK','AthenaCharacter:CID_420_Athena_Commando_F_WhiteTiger','AthenaBackpack:BID_277_WhiteTiger'],
 ['dark-vertex','DARK VERTEX PACK','AthenaCharacter:CID_371_Athena_Commando_M_SpeedyMidnight','AthenaBackpack:BID_234_SpeedyMidnight','AthenaPickaxe:Pickaxe_ID_178_SpeedyMidnight','AthenaGlider:Glider_ID_131_SpeedyMidnight'],
 ['breakpoint','BREAKPOINT QUEST PACK','AthenaCharacter:CID_429_Athena_Commando_F_NeonLines','AthenaBackpack:BID_284_NeonLines'],
 ['p-1000','P-1000 QUEST PACK','AthenaCharacter:CID_534_Athena_Commando_M_PeelyMech','AthenaDance:EID_MechPeely'],
 ['rogue-spider-knight','ROGUE SPIDER KNIGHT PACK','AthenaCharacter:CID_516_Athena_Commando_M_BlackWidowRogue','AthenaBackpack:BID_346_BlackWidowRogue'],
 ].map(([id,title,...items])=>({id,title,kind:'pack',price:id==='dark-vertex'||id==='rogue-spider-knight'?2000:1000,description:'Includes every cosmetic in the pack. No V-Bucks are included.',rewards:items.map(templateId=>({templateId,quantity:1}))})),
 // Bundles: several outfits (or a set of pickaxes) in one pack, each at its own price.
 {id:'stw-pickaxes',title:'SAVE THE WORLD PICKAXE PACK',kind:'pack',price:1500,description:'Seven Save the World pickaxes: Ol\' Woody, Basic Basher, Tech Axe, Hydraulic Wrecker, Vindertech Elite, Laser Pick and Axehammer. Items only appear in seasons whose game files include them.',rewards:['AthenaPickaxe:Pickaxe_ID_STW001_Tier_1','AthenaPickaxe:Pickaxe_ID_STW007_Basic','AthenaPickaxe:Pickaxe_ID_STW002_Tier_3','AthenaPickaxe:Pickaxe_ID_STW003_Tier_4','AthenaPickaxe:Pickaxe_ID_STW004_Tier_5','AthenaPickaxe:Pickaxe_ID_STW005_Tier_6','AthenaPickaxe:Pickaxe_ID_STW006_Tier_7'].map(templateId=>({templateId,quantity:1}))},
 {id:'frozen-legends',title:'FROZEN LEGENDS PACK',kind:'pack',price:2500,description:'Frozen Red Knight, Frozen Love Ranger and Frozen Raven with their back blings. Items only appear in seasons whose game files include them.',rewards:['AthenaCharacter:CID_294_Athena_Commando_F_RedKnightWinter','AthenaCharacter:CID_295_Athena_Commando_M_CupidWinter','AthenaCharacter:CID_293_Athena_Commando_M_RavenWinter','AthenaBackpack:BID_167_RedKnightWinterFemale','AthenaBackpack:BID_168_CupidWinterMale','AthenaBackpack:BID_166_RavenWinterMale'].map(templateId=>({templateId,quantity:1}))},
 {id:'lava-legends',title:'LAVA LEGENDS PACK',kind:'pack',price:2500,description:'Molten Battle Hound and Molten Valkyrie with their back blings and the Lavawing glider. Items only appear in seasons whose game files include them.',rewards:['AthenaCharacter:CID_379_Athena_Commando_M_BattleHoundFire','AthenaCharacter:CID_380_Athena_Commando_F_DarkViking_Fire','AthenaBackpack:BID_243_BattleHoundFire','AthenaBackpack:BID_244_DarkVikingFire','AthenaGlider:Glider_ID_134_DarkVikingFire'].map(templateId=>({templateId,quantity:1}))},
 {id:'shadows-rising',title:'SHADOWS RISING PACK',kind:'pack',price:2500,description:'Shadow Skully, Shadowbird and Perfect Shadow with their back blings and the Array wrap. Items only appear in seasons whose game files include them.',rewards:['AthenaCharacter:CID_439_Athena_Commando_F_SkullBriteEclipse','AthenaCharacter:CID_437_Athena_Commando_F_AztecEclipse','AthenaCharacter:CID_438_Athena_Commando_M_WinterGhoulEclipse','AthenaBackpack:BID_285_SkullBriteEclipse','AthenaBackpack:BID_287_AztecFemaleEclipse','AthenaBackpack:BID_286_WinterGhoulMaleEclipse','AthenaItemWrap:Wrap_081_Blackout2'].map(templateId=>({templateId,quantity:1}))},
 {id:'dark-reflections',title:'DARK REFLECTIONS PACK',kind:'pack',price:2500,description:'Dark Jonesy, Dark Red Knight and Dark Wildcard with the Dark Axe and their back blings. Items only appear in seasons whose game files include them.',rewards:['AthenaCharacter:CID_513_Athena_Commando_M_CubePaintJonesy','AthenaCharacter:CID_512_Athena_Commando_F_CubePaintRedKnight','AthenaCharacter:CID_511_Athena_Commando_M_CubePaintWildCard','AthenaPickaxe:Pickaxe_ID_263_JonesyCube','AthenaBackpack:BID_343_CubeRedKnight','AthenaBackpack:BID_344_CubeWildCard'].map(templateId=>({templateId,quantity:1}))},
 {id:'batman-caped-crusader',title:'BATMAN CAPED CRUSADER PACK',kind:'pack',price:2500,description:'Batman Comic Book and The Dark Knight Movie outfits with their capes, the Batman Pickaxe and the Batwing glider. Items only appear in seasons whose game files include them.',rewards:['AthenaCharacter:CID_528_Athena_Commando_M_BlackMondayHouston_7DGBT','AthenaCharacter:CID_529_Athena_Commando_M_BlackMondayKansas_HWD90','AthenaBackpack:BID_362_BlackMondayHouston_2I53G','AthenaBackpack:BID_360_BlackMondayKansas_VCZ9M','AthenaPickaxe:Pickaxe_ID_277_BlackMondayMale_5TLSD','AthenaGlider:Glider_ID_178_BlackMondayMale_03M3E'].map(templateId=>({templateId,quantity:1}))},
 // Exclusives: bundle, console and promotional skins with their matching items.
 ...[
 ['x-galaxy','GALAXY','AthenaCharacter:CID_175_Athena_Commando_M_Celestial','AthenaBackpack:BID_138_Celestial','AthenaGlider:Glider_ID_090_Celestial','AthenaPickaxe:Pickaxe_ID_116_Celestial','AthenaSkyDiveContrail:Trails_ID_075_Celestial','AthenaDance:SPID_066_Llamalaxy'],
 ['x-ikonik','IKONIK','AthenaCharacter:CID_313_Athena_Commando_M_KpopFashion','AthenaDance:EID_KPopDance03'],
 ['x-eon','EON','AthenaCharacter:CID_174_Athena_Commando_F_CarbideWhite','AthenaGlider:Glider_ID_056_CarbideWhite','AthenaPickaxe:Pickaxe_ID_077_CarbideWhite'],
 ['x-double-helix','DOUBLE HELIX','AthenaCharacter:CID_183_Athena_Commando_M_ModernMilitaryRed','AthenaBackpack:BID_114_ModernMilitaryRed','AthenaGlider:Glider_ID_075_ModernMilitaryRed','AthenaPickaxe:Pickaxe_ID_099_ModernMilitaryRed'],
 ['x-royale-bomber','ROYALE BOMBER','AthenaCharacter:CID_113_Athena_Commando_M_BlueAce'],
 ['x-carbon-commando','CARBON COMMANDO','AthenaCharacter:CID_360_Athena_Commando_M_TechOpsBlue','AthenaBackpack:BID_224_TechOpsBlue','AthenaGlider:Glider_ID_150_TechOpsBlue','AthenaPickaxe:Pickaxe_ID_256_TechOpsBlue','AthenaGlider:Glider_ID_112_TechOps','AthenaSkyDiveContrail:Trails_ID_059_Sony2','AthenaLoadingScreen:LSID_141_PS06','AthenaItemWrap:Wrap_121_TechOpsBlue','AthenaDance:Emoji_TechOpsBlue'],
 ['x-neo-versa','NEO VERSA','AthenaCharacter:CID_441_Athena_Commando_F_CyberScavengerBlue','AthenaBackpack:BID_288_CyberScavengerFemaleBlue'],
 ['x-honor-guard','HONOR GUARD','AthenaCharacter:CID_342_Athena_Commando_M_StreetRacerMetallic'],
 ['x-glow','GLOW','AthenaCharacter:CID_479_Athena_Commando_F_Davinci','AthenaDance:EID_Davinci'],
 ['x-wonder','WONDER','AthenaCharacter:CID_434_Athena_Commando_F_StealthHonor'],
 ['x-sub-commander','SUB COMMANDER & HAVOC','AthenaCharacter:CID_085_Athena_Commando_M_Twitch','AthenaGlider:Glider_ID_018_Twitch','AthenaCharacter:CID_089_Athena_Commando_M_RetroGrey','AthenaBackpack:BID_029_RetroGrey','AthenaPickaxe:Pickaxe_ID_039_TacticalBlack'],
 ['x-warpaint','FOUNDERS • WARPAINT & ROSE TEAM LEADER','AthenaCharacter:CID_095_Athena_Commando_M_Founder','AthenaBackpack:BID_032_FounderMale','AthenaGlider:Glider_ID_117_Warpaint','AthenaPickaxe:Pickaxe_ID_237_Warpaint','AthenaCharacter:CID_096_Athena_Commando_F_Founder','AthenaBackpack:BID_033_FounderFemale','AthenaPickaxe:Pickaxe_ID_153_RoseLeader','AthenaGlider:Glider_ID_161_RoseLeader','AthenaGlider:FounderGlider'],
 ['x-prodigy','PRODIGY','AthenaCharacter:CID_296_Athena_Commando_M_Math','AthenaBackpack:BID_169_MathMale','AthenaDance:Emoji_Pizza'],
 ].map(([id,title,...items])=>({id,title,kind:'pack',exclusive:true,price:id==='x-prodigy'||id==='x-carbon-commando'?1000:2000,description:'Exclusive outfit with its matching items. Items only appear in seasons whose game files include them.',rewards:items.map(templateId=>({templateId,quantity:1}))})),
 // OG Halloween bundle: both outfits with every color style owned, wearing Purple (Skull) and Pink (Ghoul).
 // Styles show in seasons that have them (Season 6 and later); earlier seasons get the outfits.
 {id:'x-og-halloween',title:'PURPLE SKULL & PINK GHOUL',kind:'pack',exclusive:true,price:4000,
  description:'Purple Skull Trooper and Pink Ghoul Trooper in one bundle: both outfits with all their color styles, set to purple and pink. Already own one? You still get its styles.',
  rewards:[
   {templateId:'AthenaCharacter:CID_030_Athena_Commando_M_Halloween',quantity:1,variants:[{channel:'ClothingColor',active:'Mat1',owned:['Mat0','Mat1','Mat2']}]},
   {templateId:'AthenaCharacter:CID_029_Athena_Commando_F_Halloween',quantity:1,variants:[{channel:'ClothingColor',active:'Mat1',owned:['Mat0','Mat1','Mat2']}]}
  ]}
];
// Packs bought before items were added to them: the missing cosmetics are given on the next profile load.
// Returns true when something was added.
export function topUpPurchasedPacks(doc){
 const a=doc?.profiles?.athena;if(!a?.items||!doc.syndicatePassPurchases)return false;let added=false;
 const owned=new Set(Object.values(a.items).map(i=>String(i.templateId||'').toLowerCase()));
 for(const pack of catalog){
  if(pack.kind!=='pack'||!doc.syndicatePassPurchases[pack.id])continue;
  for(const reward of pack.rewards){
   const id=reward.templateId;if(!/^Athena/i.test(id)||owned.has(id.toLowerCase()))continue;
   const styles=Array.isArray(reward.variants)?reward.variants:[];
   a.items[randomUUID()]={templateId:id,quantity:1,attributes:{item_seen:false,level:1,xp:0,variants:styles.map(v=>({channel:v.channel,active:v.active,owned:[...v.owned]})),favorite:false}};
   owned.add(id.toLowerCase());added=true;
  }
 }
 return added;
}
function wallets(profiles){const c=profiles.common_core;const platform=(c.stats.attributes.current_mtx_platform||'EpicPC').toLowerCase();return Object.values(c.items).filter(i=>/^currency:mtx/i.test(i.templateId)&&Number.isSafeInteger(i.quantity)&&i.quantity>=0&&['shared',platform].includes((i.attributes?.platform||'Shared').toLowerCase()));}
export const balance=profiles=>wallets(profiles).reduce((n,i)=>n+Math.max(0,Number(i.quantity)||0),0);
function itemView(r){const [type,id]=r.templateId.split(':');return {...r,name:names[id.toLowerCase()]||({athenaseasonxpboost:'Personal XP boost',athenaseasonfriendxpboost:'Friend XP boost',athenanextseasontierboost:'Next-season tier boost'}[id.toLowerCase()])||({Currency:'V-Bucks',Token:'Season boost',HomebaseBannerIcon:'Season banner'}[type])||id.replace(/_/g,' '),category:categories[type]||'Cosmetics'};}
// The level the Season Shop checks in game (syndicateSeason1Purchase.js): the profile level Fortnite shows.
const playerLevel=profiles=>Math.max(0,Number(profiles?.athena?.stats?.attributes?.level)||0);
export function view(doc){const level=playerLevel(doc.profiles);return {balance:balance(doc.profiles),level,passes:catalog.map(p=>({...p,rewards:p.rewards.map(itemView),owned:!!doc.syndicatePassPurchases?.[p.id],locked:!!p.minLevel&&level<p.minLevel}))};}
export async function purchase(accountId,passId){
 const pass=catalog.find(p=>p.id===passId);if(!pass)throw new Error('Unknown battle pass.');
 return withAccountLock(accountId,async()=>{
 for(let attempt=0;attempt<8;attempt++){
  const doc=await Profile.collection.findOne({accountId});if(!doc)throw new Error('Account profile is missing.');
  if(doc.syndicatePassPurchases?.[passId])return {...view(doc),message:'Already purchased. No V-Bucks were charged.'};
  if(pass.minLevel&&playerLevel(doc.profiles)<pass.minLevel)throw new Error(`Reach Season Level ${pass.minLevel} first (you are level ${playerLevel(doc.profiles)}).`);
  if(balance(doc.profiles)<pass.price)throw new Error('Not enough V-Bucks.');
  const a=doc.profiles.athena,c=doc.profiles.common_core,ar=a.rvn,cr=c.rvn;
  let remaining=pass.price;for(const item of wallets(doc.profiles)){const take=Math.min(remaining,item.quantity);item.quantity-=take;remaining-=take;if(!remaining)break;}
  const before=ownedSnapshot(a),styled=[];
  for(const reward of pass.rewards){
   const id=reward.templateId;const target=/^(Currency:|HomebaseBanner)/i.test(id)?c:a;
   const existing=Object.values(target.items).find(i=>i.templateId?.toLowerCase()===id.toLowerCase());
   const stack=/^(Currency|Token):/i.test(id);
   const styles=Array.isArray(reward.variants)?reward.variants:[];
   if(existing){
    if(stack)existing.quantity+=reward.quantity;
    // Owned already: the bundle's styles are added to it (owned, and worn).
    for(const v of styles){existing.attributes??={};const list=existing.attributes.variants??=[];
     let entry=list.find(e=>String(e.channel).toLowerCase()===v.channel.toLowerCase());
     if(!entry)list.push(entry={channel:v.channel,active:v.active,owned:[]});
     for(const tag of v.owned)if(!entry.owned.includes(tag))entry.owned.push(tag);
     entry.active=v.active;styled.push(existing);}
    continue;
   }
   target.items[randomUUID()]={templateId:id,quantity:reward.quantity,attributes:{item_seen:false,level:1,xp:0,variants:styles.map(v=>({channel:v.channel,active:v.active,owned:[...v.owned]})),favorite:false,...(/^Currency:/i.test(id)?{platform:'Shared'}:{})}};
  }
  // Buying a pass while its season is active unlocks that season's Battle Pass now;
  // other seasons are unlocked when the player switches to them in Fortnite.js.
  const boughtSeason=pass.id==='chapter2-season1'?11:pass.id==='chapter2-season2'?12:/^season(\d+)$/i.test(pass.id)&&Number(pass.id.slice(6))!==1?Number(pass.id.slice(6)):0;
  if(boughtSeason && activeSeason(a)===boughtSeason)unlockSeasonPass(a,boughtSeason);
  const loot=addedLoot(a,before);
  for(const item of new Set(styled)){const guid=Object.keys(a.items).find(k=>a.items[k]===item);if(guid && !loot.some(l=>l.itemGuid===guid))loot.push({itemType:item.templateId,itemGuid:guid,itemProfile:'athena',quantity:1});}
  addRewardGift(c,loot,pass.title,pass.kind==='pack'?'Your pack is ready.':'Your battle pass rewards are ready.'); // reward pop-up in game
  for(const profile of [a,c]){profile.rvn++;profile.commandRevision++;profile.updated=new Date().toISOString();}
  const receipt={price:pass.price,purchasedAt:new Date().toISOString()};
  const saved=await Profile.collection.updateOne({accountId,'profiles.athena.rvn':ar,'profiles.common_core.rvn':cr,[`syndicatePassPurchases.${passId}`]:{$exists:false}},{$set:{'profiles.athena':a,'profiles.common_core':c,[`syndicatePassPurchases.${passId}`]:receipt}});
  if(saved.modifiedCount){doc.syndicatePassPurchases={...doc.syndicatePassPurchases,[passId]:receipt};if(global.giftReceived)global.giftReceived[accountId]=true;return {...view(doc),message:'Purchased. Your items are saved and will appear when you next load Fortnite.'};}
 }
 throw new Error('Your balance changed. Refresh and try again.');
 });
}
