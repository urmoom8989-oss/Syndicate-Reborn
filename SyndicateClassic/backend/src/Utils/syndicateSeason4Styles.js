// Unlocks from the installed 4.00 progressive quest objectives and variant tokens.
export const styleTracks=Object.freeze([
 {id:'athenacharacter:cid_115_athena_commando_m_carbideblue',levels:[10,20,30,40,65],pickaxe:'Pickaxe_ID_047_CarbideBlue'},
 {id:'athenacharacter:cid_116_athena_commando_m_carbideblack',levels:[25,35,45,55,80],pickaxe:'Pickaxe_ID_048_CarbideBlack'}
]);
export function ensureSeason4Styles(profile){
 if(profile?.stats?.attributes?.season_num!==4)return false;
 const level=Number(profile.stats.attributes.level)||1;let changed=false;
 for(const item of Object.values(profile.items||{})){
  const id=item.templateId?.toLowerCase()||'';
  if(/^athenacharacter:cid_(?:14[4-9]|15[01])_athena_commando_[mf]_soccer(?:dude|girl)[a-d]$/.test(id)){
   changed=ensureChannel(item,'Pattern',['Mat0'],'Mat0')||changed;
   changed=ensureChannel(item,'JerseyColor',["Color.Fortnite","Color.Argentina","Color.Australia","Color.Belgium","Color.Brazil","Color.Canada","Color.Colombia","Color.Denmark","Color.Egypt","Color.England","Color.France","Color.Germany","Color.Iceland","Color.Italy","Color.Ireland","Color.Japan","Color.Korea","Color.Mexico","Color.Netherlands","Color.NewZealand","Color.Nigeria","Color.Norway","Color.Poland","Color.Portugal","Color.Russia","Color.Saudi","Color.Spain","Color.Sweden","Color.Switzerland","Color.Terkey","Color.Uruguay","Color.USA"],'Color.Fortnite')||changed;
   changed=ensureChannel(item,'Numeric',Array.from({length:99},(_,i)=>'Numeric.'+i),'Numeric.14')||changed;
  }
  const track=styleTracks.find(t=>t.id===id);if(!track)continue;
  changed=ensureChannel(item,'Emissive',level>=track.levels[4]?['Emissive0','Emissive1','Emissive2','Emissive3']:['Emissive0'],'Emissive0')||changed;
  item.attributes??={};item.attributes.variants??=[];
  let v=item.attributes.variants.find(v=>v.channel==='Progressive');
  if(!v){v={channel:'Progressive',active:'Stage0',owned:['Stage0']};item.attributes.variants.push(v);changed=true;}
  if(!Array.isArray(v.owned)){v.owned=['Stage0'];changed=true;}
  const earned=['Stage0',...track.levels.flatMap((threshold,i)=>level>=threshold?['Stage'+(i+1)]:[])];
  for(const stage of earned)if(!v.owned.includes(stage)){v.owned.push(stage);changed=true;item.attributes.item_seen=false;}
  if(level>=track.levels[2]){
   const templateId='AthenaPickaxe:'+track.pickaxe;
   if(!Object.values(profile.items).some(i=>i.templateId?.toLowerCase()===templateId.toLowerCase())){
    let key='tilted_progressive_'+track.pickaxe.toLowerCase();while(profile.items[key])key+='_reward';
    profile.items[key]={templateId,quantity:1,attributes:{item_seen:false,level:1,xp:0,variants:[]}};changed=true;
   }
  }
  // Keep the player's chosen owned style; do not reset it every login.
  if(!v.owned.includes(v.active)){v.active='Stage0';changed=true;}
 }
 return changed;
}
export function applyOwnedVariant(item,update){
 if(!item||!update||typeof update.channel!=='string'||typeof update.active!=='string')return false;
 const v=item.attributes?.variants?.find(v=>v.channel===update.channel);
 if(!v||!Array.isArray(v.owned)||!v.owned.includes(update.active))return false;
 v.active=update.active;return true;
}

// Add asset-supported selections without resetting an already owned active style.
function ensureChannel(item,channel,owned,fallback){
 item.attributes??={};item.attributes.variants??=[];
 let v=item.attributes.variants.find(v=>v.channel===channel),changed=false;
 if(!v){v={channel,active:fallback,owned:[]};item.attributes.variants.push(v);changed=true;}
 if(!Array.isArray(v.owned)){v.owned=[];changed=true;}
 for(const tag of owned)if(!v.owned.includes(tag)){v.owned.push(tag);changed=true;}
 if(!v.owned.includes(v.active)){v.active=fallback;changed=true;}
 return changed;
}
