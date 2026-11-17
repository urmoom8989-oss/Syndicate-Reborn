// Victory cosmetics verified against the installed 4.00 content.
// Match V-Bucks for legit players: 100 a kill, 1,500 a win (every season, Season X included). Nothing while the Syndicate menu is on (syndicateProgression.js).
export const vbucksAwards=Object.freeze({kill:100,win:1500,survival:0,match:0,quest:0,quest_delta:0,stats:0,xp:0});
// Each season's Victory Umbrella is earned by winning a match in that season.
export const seasonUmbrellas=Object.freeze({2:'Umbrella_Snowflake',3:'Umbrella_PaperParasol',4:'Umbrella_Season_04',5:'Umbrella_Season_05',6:'Umbrella_Season_06',7:'Umbrella_Season_07',8:'Umbrella_Season_08',9:'Umbrella_Season_09',10:'Umbrella_Season_10'});
export const victoryUmbrellas=Object.freeze(['FounderUmbrella']);
// Win-milestone umbrellas built from unreleased C1 assets (25 and 50 wins). The launcher's
// "Remove unreleased win umbrellas" toggle hides these.
export const unreleasedWinUmbrellas=Object.freeze(['Umbrella_Bronze','Umbrella_Gold']);
export const UNRELEASED_UMBRELLA=/^athenaglider:(umbrella_bronze|umbrella_gold)$/i;
export function grantVictoryUmbrellas(profile,event){
 if(event?.kind!=='win')return [];
 profile.items??={};const granted=[];
 const wins=Math.max(1,Number(profile.stats?.attributes?.lifetime_wins)||0);
 const earned=[...victoryUmbrellas,'Solo_Umbrella'];
 const seasonal=seasonUmbrellas[Number(event.season)];if(seasonal)earned.push(seasonal);
 // While the toggle is on, don't grant (or re-grant) the unreleased milestone umbrellas.
 const hideUnreleased=!!profile.stats?.attributes?.tilted_hide_win_umbrellas;
 if(!hideUnreleased&&wins>=25)earned.push('Umbrella_Bronze');
 if(!hideUnreleased&&wins>=50)earned.push('Umbrella_Gold');
 for(const id of earned){
  const templateId='AthenaGlider:'+id;
  if(Object.values(profile.items).some(item=>item.templateId?.toLowerCase()===templateId.toLowerCase()))continue;
  let key='tilted_victory_'+id.toLowerCase();while(profile.items[key])key+='_reward';
  profile.items[key]={templateId,quantity:1,attributes:{item_seen:false,level:1,xp:0,variants:[]}};
  granted.push(templateId);
 }
 return granted;
}
// "Remove unreleased win umbrellas": while a.tilted_hide_win_umbrellas is on, strip Umbrella_Bronze/Gold from
// the locker. An equipped one moves to another owned glider first (kept if there is none). Returns true on change.
export function applyHideWinUmbrellas(p){
 const a=p?.stats?.attributes;if(!a?.tilted_hide_win_umbrellas)return false;
 p.items??={};let changed=false;
 const cur=p.items[a.favorite_glider];
 if(cur&&UNRELEASED_UMBRELLA.test(cur.templateId||'')){
  const alt=Object.entries(p.items).find(([,i])=>typeof i.templateId==='string'&&/^athenaglider:/i.test(i.templateId)&&!UNRELEASED_UMBRELLA.test(i.templateId)&&(i.quantity??1)>0);
  if(alt){a.favorite_glider=alt[0];changed=true;}
 }
 for(const [key,item] of Object.entries(p.items)){
  if(UNRELEASED_UMBRELLA.test(item.templateId||'')&&a.favorite_glider!==key){delete p.items[key];changed=true;}
 }
 return changed;
}
// Turning the toggle off: re-grant the milestone umbrellas the player has earned by win count.
export function restoreWinUmbrellas(p){
 p.items??={};let changed=false;const wins=Math.max(0,Number(p?.stats?.attributes?.lifetime_wins)||0);
 const earned=[];if(wins>=25)earned.push('Umbrella_Bronze');if(wins>=50)earned.push('Umbrella_Gold');
 for(const id of earned){const templateId='AthenaGlider:'+id;
  if(Object.values(p.items).some(i=>i.templateId?.toLowerCase()===templateId.toLowerCase()))continue;
  let key='tilted_victory_'+id.toLowerCase();while(p.items[key])key+='_reward';
  p.items[key]={templateId,quantity:1,attributes:{item_seen:false,level:1,xp:0,variants:[]}};changed=true;}
 return changed;
}
