// Stored inside the Athena profile so account travel carries career totals.
export function recordCareer(attributes, event) {
  if (!event || !['kill', 'win', 'match'].includes(event.kind)) return;
  const mode = ['solo', 'duo', 'squad'].includes(event.mode) ? event.mode : 'unknown';
  const career = attributes.tilted_career ??= {};
  const row = career[mode] ??= {kills: 0, wins: 0};
  if(event.kind==='match'){
    if(Array.isArray(event.matchStats)&&event.matchStats.length===20){row.stats??=Array(20).fill(0);for(let i=0;i<20;i++)if(Number.isSafeInteger(event.matchStats[i])&&event.matchStats[i]>=0)row.stats[i]=(row.stats[i]||0)+event.matchStats[i];}
    row.score=(row.score||0)+Math.max(0,Math.floor(Number(event.score)||0));
    row.matches=Math.max(0,Number(row.matches)||0)+1;
    row.seconds=Math.max(0,Number(row.seconds)||0)+Math.max(0,Math.floor(Number(event.seconds)||0));
    const place=Math.max(1,Math.floor(Number(event.place)||100));
    if(place<=({solo:10,duo:5,squad:3}[mode]||1))row.high=(row.high||0)+1;
    if(place<=({solo:25,duo:12,squad:6}[mode]||1))row.low=(row.low||0)+1;
  } else {const key=event.kind==='kill'?'kills':'wins';row[key]=Math.max(0,Number(row[key])||0)+1;}
  attributes.lifetime_kills=Object.values(career).reduce((sum,r)=>sum+(Number(r.kills)||0),0);
  attributes.lifetime_wins=Object.values(career).reduce((sum,r)=>sum+(Number(r.wins)||0),0);
}

export function careerStats(attributes = {}) {
  const stats = {};
  for (const [mode, playlist] of [['solo', 'p2'], ['duo', 'p10'], ['squad', 'p9']]) {
    const row = attributes.tilted_career?.[mode] || {};
    for (const [field, name] of [['kills','kills'],['wins','placetop1'],['matches','matchesplayed'],['seconds','minutesplayed'],['score','score'],['high',({solo:'placetop10',duo:'placetop5',squad:'placetop3'})[mode]],['low',({solo:'placetop25',duo:'placetop12',squad:'placetop6'})[mode]]]) {
      const value = Math.max(0, Math.floor((Number(row[field]) || 0)/(field==='seconds'?60:1)));
      stats[`br_${name}_pc_m0_${playlist}`] = value;
    }
  }
  return stats;
}

export function legacyCareer(accountId, attributes) {
  return Object.entries(careerStats(attributes)).map(([name, value]) => ({
    name, value, window: 'alltime', ownerType: 1, accountId
  }));
}

// Fortnite 4.00 uses an MCP athena_stats profile in addition to the bulk stats API.
// Match stat slots follow the launch client's GameplayStat.Profile.Match table.
export function legacyAthenaStats(accountId,athena){
 const attrs=athena?.stats?.attributes||{},now=new Date().toISOString();
 const profile={created:athena?.created||now,updated:athena?.updated||now,rvn:athena?.rvn||0,commandRevision:athena?.commandRevision||0,wipeNumber:1,accountId,profileId:'athena_stats',version:'no_version',items:{},stats:{attributes:{recent_match_ids:[]}}};
 const total=Array(20).fill(0);
 for(const [mode,id] of [['solo',2],['duo',10],['squad',9]]){
  const r=attrs.tilted_career?.[mode]||{},s=Array.from({length:20},(_,i)=>r.stats?.[i]||0);
  s[0]=r.score||0;s[2]=r.seconds||0;s[3]=r.kills||0;s[16]=r.wins||0;s[17]=r.high||0;s[18]=r.low||0;s[19]=r.matches||0;
  for(let i=0;i<20;i++){s[i]=Math.max(0,Math.floor(Number(s[i])||0));total[i]+=s[i];}
  profile.items['lifetime_'+mode]={templateId:'ProfileStats:LifetimeStats',quantity:1,attributes:{bucket_name:'pc_m0_p'+id,stats:s}};
  profile.items['season4_'+mode]={templateId:'ProfileStats:SeasonStats',quantity:1,attributes:{bucket_name:'pc_m0_p'+id,season_number:4,stats:[...s]}};
 }
 profile.items.lifetime={templateId:'ProfileStats:LifetimeStats',quantity:1,attributes:{bucket_name:'',stats:total}};
 profile.items.season4={templateId:'ProfileStats:SeasonStats',quantity:1,attributes:{bucket_name:'',season_number:4,stats:[...total]}};
 return profile;
}
