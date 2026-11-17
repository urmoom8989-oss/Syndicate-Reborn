import {randomUUID} from 'node:crypto';
// Reward pop-ups. Fortnite shows the "Item 1 of N / Collect All" screen for a gift box in common_core, so a
// purchase that grants several items (a bundle, a battle pass, tiers) leaves one with the items it gave.

// Template ids the athena profile owns right now (lower case), to tell afterwards what a purchase added.
export function ownedSnapshot(athena){
 return new Set(Object.values(athena?.items||{}).map(i=>String(i?.templateId||'').toLowerCase()));
}
// Athena items added since the snapshot, as a gift box loot list.
export function addedLoot(athena,before){
 const loot=[];
 for(const [guid,item] of Object.entries(athena?.items||{})){
  const id=String(item?.templateId||'');
  if(!/^Athena|^HomebaseBanner/i.test(id) || before.has(id.toLowerCase()))continue;
  loot.push({itemType:id,itemGuid:guid,itemProfile:'athena',quantity:Number(item.quantity)||1});
 }
 return loot;
}
// Puts the gift box in common_core and returns [id, item] (or null when there is nothing to show).
// Reward pop-ups come from Syndicate, not Epic Games. The game shows the sender by looking the account
// id up (Routes/user.js answers this id with the name "Project Tilted"); the old '[Epic Games]' sender showed
// Epic's logo and name.
export const TILTED_SENDER_ID='7117ed000000000000000000000000c0';
export const TILTED_SENDER_NAME='Project Tilted';
export function addRewardGift(common,loot,header,message,from=TILTED_SENDER_ID){
 if(!common?.items || !Array.isArray(loot) || !loot.length)return null;
 const id=randomUUID();
 common.items[id]={templateId:'GiftBox:GB_MakeGood',attributes:{fromAccountId:from,
  params:{DefaultHeaderText:header||TILTED_SENDER_NAME,userMessage:message||''},lootList:loot,giftedOn:new Date().toISOString()},quantity:1};
 return [id,common.items[id]];
}

// Items an older purchase should have included. Raptor came without his back bling (Season 3 shop) and
// Frostbite (shop and launcher pack) without his back bling, pickaxe and glider. Owners get what they miss.
export const SET_PIECE_FIXES=[
 {owns:['AthenaCharacter:CID_031_Athena_Commando_M_Retro'],give:['AthenaBackpack:BID_005_Raptor']},
 {owns:['AthenaCharacter:CID_261_Athena_Commando_M_RaptorArcticCamo'],
  give:['AthenaBackpack:BID_142_RaptorArcticCamo','AthenaPickaxe:Pickaxe_ID_097_RaptorArcticCamo','AthenaGlider:Glider_ID_074_RaptorArcticCamo']}
];
export const SET_PIECE_FIX_VERSION=1;
// Grants the missing pieces into athena and returns the loot list of what was added.
export function grantMissingSetPieces(athena){
 const owned=ownedSnapshot(athena),loot=[];
 for(const fix of SET_PIECE_FIXES){
  if(!fix.owns.some(id=>owned.has(id.toLowerCase())))continue;
  for(const id of fix.give){
   if(owned.has(id.toLowerCase()))continue;
   const guid=randomUUID();
   athena.items[guid]={templateId:id,attributes:{item_seen:false,level:1,xp:0,variants:[],favorite:false},quantity:1};
   owned.add(id.toLowerCase());
   loot.push({itemType:id,itemGuid:guid,itemProfile:'athena',quantity:1});
  }
 }
 return loot;
}
