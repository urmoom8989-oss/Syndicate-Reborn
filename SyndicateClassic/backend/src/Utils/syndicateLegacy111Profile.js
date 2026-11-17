import {isSeasonTwoBuild} from './syndicateBuild.js';
import {available111} from './syndicateCosmetics111.js';
import {stwHeroesForBuild,stwItemKey,stwTemplateName,selectedHero,stwSkinsOn} from './syndicateStwHeroes.js';
import fs from 'node:fs';
import {fileURLToPath} from 'node:url';
// Pre-season (1.7.2) and Season 1 (1.8.2) take the same old profile format as 1.11. Their cosmetics
// come from what their game server exported (cosmetics/season-0.json / season-1.json); until a
// match has run on that build, the 1.11 list is used (items a build lacks are skipped by the game).
const exportDir=fileURLToPath(new URL('../../../Runtime/Server/cosmetics/',import.meta.url));
const earlyCache={};
function availableFor(build){
 const season=build==='1.7.2'?0:build==='1.8.2'?1:build==='2.5'?2:null;
 if(season===null)return available111;
 const file=exportDir+'season-'+season+'.json';
 let stamp='';try{stamp=String(fs.statSync(file).mtimeMs);}catch{}
 if(earlyCache[season]?.stamp!==stamp){
  let ids=null;try{ids=new Set((JSON.parse(fs.readFileSync(file,'utf8')).items||[]).map(x=>String(x).toLowerCase()));}catch{}
  earlyCache[season]={stamp,test:ids&&ids.size?(id=>typeof id==='string'&&ids.has(id.toLowerCase())):available111};
 }
 return earlyCache[season].test;
}
const defaults={character:'AthenaCharacter:CID_008_Athena_Commando_M_Default',pickaxe:'AthenaPickaxe:DefaultPickaxe',glider:'AthenaGlider:DefaultGlider',dance:'AthenaDance:EID_DanceMoves'};
export function project111Profile(source,build='1.11'){
 const available=availableFor(build);
 const p=JSON.parse(JSON.stringify(source));
 for(const item of Object.values(p.items||{}))if(!Number.isFinite(item.quantity))item.quantity=1;
 if(p.profileId==='common_core'){
  // 1.11's wallet UI must receive the aggregate in its legacy purchased bucket.
  // This is a wire projection only; purchases debit the canonical shared wallet.
  // 1.11 accepts "Epic" or "Shared" here; "EpicPC" is ignored by its balance counter.
  const platform=(p.stats?.attributes?.current_mtx_platform||'EpicPC').toLowerCase();
  const entries=Object.entries(p.items||{}).filter(([,i])=>/^currency:mtx/i.test(i.templateId||'') && ['shared',platform].includes((i.attributes?.platform||'Shared').toLowerCase()));
  const total=entries.reduce((sum,[,i])=>sum+(Number.isSafeInteger(i.quantity)&&i.quantity>=0?i.quantity:0),0);
  const purchased=entries.find(([,i])=>/^currency:mtxpurchased$/i.test(i.templateId));
  const id=purchased?.[0]||'tilted_legacy_wallet';
  for(const [key] of entries)delete p.items[key];
  p.items[id]={templateId:'Currency:MtxPurchased',quantity:total,attributes:{...(purchased?.[1]?.attributes||{}),platform:'Shared'}};
  return p;
 }
 if(p.profileId!=='athena')return p;
 // Season 2 challenges (exported from this build by the game server) stay visible alongside 1.11 cosmetics.
 p.items=Object.fromEntries(Object.entries(p.items||{}).filter(([,item])=>available(item.templateId)||/^(Quest|ChallengeBundle|ChallengeBundleSchedule):/i.test(item.templateId||'')));
 // Save the World heroes as locker outfits while Unlock all and STW skins are on (the client registers them; see syndicateStwHeroes.js).
 // The picked hero shows as the equipped outfit.
 if(stwSkinsOn(p.stats?.attributes)){
  for(const hero of stwHeroesForBuild(build))p.items[stwItemKey(hero.id)]={templateId:'AthenaCharacter:'+stwTemplateName(hero.id),quantity:1,attributes:{item_seen:true,level:1,xp:0,variants:[],favorite:false}};
  const picked=selectedHero(p.stats.attributes);if(picked)p.stats.attributes.favorite_character=stwItemKey(picked);
 }
 const a=p.stats.attributes;
 // "Remove default outfits from locker" (launcher): on legacy builds the default outfits/pickaxe/glider are
 // re-added here each response, so honour the flag here too. The default dance stays so the emote slot works.
 const hideDefaults=!!a.tilted_hide_defaults;
 const keyFor=template=>Object.keys(p.items).find(k=>p.items[k].templateId.toLowerCase()===template.toLowerCase());
 if(!keyFor(defaults.dance))p.items[defaults.dance]={templateId:defaults.dance,quantity:1,attributes:{item_seen:true,level:1,xp:0}};
 if(!hideDefaults){
  for(const template of Object.values(defaults))if(!keyFor(template))p.items[template]={templateId:template,quantity:1,attributes:{item_seen:true,level:1,xp:0}};
  for(let n=1;n<=8;n++){
   const template='AthenaCharacter:CID_'+String(n).padStart(3,'0')+'_Athena_Commando_'+(n<=4?'F':'M')+'_Default';
   if(available(template)&&!keyFor(template))p.items[template]={templateId:template,quantity:1,attributes:{item_seen:true,level:1,xp:0}};
  }
 }
 const resolve=(value,slot)=>p.items[value]?.templateId?.toLowerCase().startsWith('athena'+slot+':')?value:typeof value==='string'&&value.toLowerCase().startsWith('athena'+slot+':')?(keyFor(value)||''):'';
 // Equip an owned item per slot. When defaults are hidden and a slot has nothing else owned, add back a single
 // default for that one slot so the legacy locker is never left with an empty character/pickaxe/glider.
 for(const slot of ['character','pickaxe','glider']){
  let equipped=resolve(a['favorite_'+slot],slot);
  if(!equipped){const dt=defaults[slot];if(hideDefaults&&!keyFor(dt))p.items[dt]={templateId:dt,quantity:1,attributes:{item_seen:true,level:1,xp:0}};equipped=keyFor(dt);}
  a['favorite_'+slot]=equipped||'';
 }
 a.favorite_dance=Array.from({length:6},(_,i)=>resolve(a.favorite_dance?.[i],'dance')||(i===0?keyFor(defaults.dance):''));
 for(const slot of ['backpack','skydivecontrail','loadingscreen','musicpack'])a['favorite_'+slot]=resolve(a['favorite_'+slot],slot);
 a.favorite_itemwraps=[];a.season_num=isSeasonTwoBuild(build)?2:1;
 delete a.loadouts;delete a.last_applied_loadout;delete a.active_loadout_index;delete a.tilted_season_states;
 // Keep the daily-challenge fields so the lobby's Daily Challenges panel shows them; drop any other
 // quest-manager state (later-season schedules the old client does not understand). Wiping it entirely
 // left Season 2 dailies invisible even though the quest items were in the profile.
 const qm=a.quest_manager||{};
 a.quest_manager={dailyLoginInterval:qm.dailyLoginInterval||'',dailyQuestRerolls:Number(qm.dailyQuestRerolls)||0};
 a.party_assist_quest='';
 return p;
}
export function project111WalletProfile(source,wallet,build='1.11'){
 const p=JSON.parse(JSON.stringify(source));const balance=project111Profile(wallet);
 p.items=p.items||{};
 for(const [id,item] of Object.entries(p.items))if(/^currency:mtx/i.test(item.templateId||id))delete p.items[id];
 for(const [id,item] of Object.entries(balance.items||{}))if(/^currency:mtx/i.test(item.templateId||id))p.items[id]=item;
 p.stats=p.stats||{attributes:{}};p.stats.attributes=p.stats.attributes||{};
 p.stats.attributes.current_mtx_platform=balance.stats?.attributes?.current_mtx_platform||'EpicPC';
 p.profileId='profile0';
 // Either source changing must advance the old client's combined profile revision.
 p.rvn=(source.rvn||0)+(wallet.rvn||0);p.commandRevision=(source.commandRevision||0)+(wallet.commandRevision||0);
 return p;
}
// Applies an MCP answer's changes (statModified, itemAdded, itemRemoved, itemAttrChanged, itemQuantityChanged) to a
// copy of the saved profile.
export function withChanges(saved,r){
 if(!saved)return saved;
 const p=JSON.parse(JSON.stringify(saved));p.items??={};p.stats??={};p.stats.attributes??={};
 for(const c of r?.profileChanges||[]){
  if(c.changeType==='statModified')p.stats.attributes[c.name]=c.value;
  else if(c.changeType==='itemAdded'&&c.itemId)p.items[c.itemId]=c.item;
  else if(c.changeType==='itemRemoved'&&c.itemId)delete p.items[c.itemId];
  else if(c.changeType==='itemAttrChanged'&&p.items[c.itemId]){p.items[c.itemId].attributes??={};p.items[c.itemId].attributes[c.attributeName]=c.attributeValue;}
  else if(c.changeType==='itemQuantityChanged'&&p.items[c.itemId])p.items[c.itemId].quantity=c.quantity;
 }
 return p;
}
export async function project111Response(body, readAthena, readWallet, readLegacyProfile, build='1.11') {
 if(!body||typeof body!=='object')return body;
 const result=JSON.parse(JSON.stringify(body));
 async function apply(r){
  if(!Array.isArray(r.profileChanges))return;
  const full=r.profileChanges.find(c=>c.changeType==='fullProfileUpdate');
  if(r.profileId==='profile0' && readWallet){
   const source=full?.profile || (readLegacyProfile && await readLegacyProfile());const wallet=await readWallet();
   if(source&&wallet){const p=project111WalletProfile(source,wallet);r.profileRevision=p.rvn;r.profileCommandRevision=p.commandRevision;r.profileChanges=[{changeType:'fullProfileUpdate',profile:p}];}
  }else if(r.profileId==='athena'){
   // Without a full profile in the answer, the saved copy is read and this answer's own changes are put on top of it:
   // most routes (equip, locker slot, favourite, banner...) save only after answering, so the saved copy could still
   // be the old one. The game then showed the old outfit ("Saving selection" and nothing changed until a second try).
   const source=full?.profile || withChanges(await readAthena(),r);
   if(source){const p=project111Profile(source,build);if(!full){p.rvn=r.profileRevision??p.rvn;p.commandRevision=r.profileCommandRevision??p.commandRevision;}r.profileRevision=p.rvn;r.profileCommandRevision=p.commandRevision;r.profileChanges=[{changeType:'fullProfileUpdate',profile:p}];}
  }else if(r.profileId==='common_core' && readWallet){
   const source=full?.profile || await readWallet();
   if(source){const p=project111Profile(source,build);r.profileRevision=p.rvn;r.profileCommandRevision=p.commandRevision;r.profileChanges=[{changeType:'fullProfileUpdate',profile:p}];}
  }else for(const c of r.profileChanges){if(c.profile)c.profile=project111Profile(c.profile,build);if(c.item&&!Number.isFinite(c.item.quantity))c.item.quantity=1;}
 }
 await apply(result);for(const update of result.multiUpdate||[])await apply(update);
 // 1.11 subscribes to profile0, not the later common_core wallet profile.
 if(result.profileId==='athena' && Array.isArray(result.profileChanges) && readWallet && readLegacyProfile){
  const wallet=await readWallet(),base=await readLegacyProfile();
  if(wallet&&base){const p=project111WalletProfile(base,wallet);result.multiUpdate=(result.multiUpdate||[]).filter(x=>!['common_core','profile0'].includes(x.profileId));
   result.multiUpdate.push({profileId:'profile0',profileRevision:p.rvn,profileCommandRevision:p.commandRevision,profileChangesBaseRevision:p.rvn,profileChanges:[{changeType:'fullProfileUpdate',profile:p}]});}
 }
 return result;
}
