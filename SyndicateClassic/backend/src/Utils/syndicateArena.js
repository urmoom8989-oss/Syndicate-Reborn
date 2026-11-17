// Arena hype (points). Stored in profiles.athena.stats.attributes.arena_hype,
// which is not a season-scoped key, so it carries over between seasons.
export const arenaThresholds=Object.freeze([250,500,1000,1500,2500,4000,6000,12000,16000]);
const busFare=[0,0,0,2,3,4,5,10,10,10];
export function arenaDivision(hype){let d=1;for(const t of arenaThresholds)if((hype||0)>=t)d++;return d;}
export function arenaBusFare(division){return busFare[Math.min(Math.max(division,1),10)-1];}
export function arenaLeague(division){return division>=8?'Champion League':division>=5?'Contender League':'Open League';}
export function placementHype(place){
  if(!(place>0))return 0;
  let h=0;if(place<=25)h+=20;if(place<=15)h+=20;if(place<=5)h+=30;if(place===1)h+=60;return h;
}
export function applyArenaHype(attr,event){
  if(!event||event.arena!==true)return null;
  const before=Math.max(0,Number(attr.arena_hype)||0);
  const division=arenaDivision(before);
  const floor=division>1?arenaThresholds[division-2]:0;
  let delta=0;
  if(event.kind==='kill')delta=20;
  else if(event.kind==='win')delta=placementHype(1)-busFare[division-1];
  else if(event.kind==='match'){
    if(Number(event.place)===1)return {hype:before,delta:0};
    delta=placementHype(Number(event.place))-busFare[division-1];
  }else return null;
  const after=Math.max(floor,before+delta);
  attr.arena_hype=after;
  return {hype:after,delta:after-before};
}
