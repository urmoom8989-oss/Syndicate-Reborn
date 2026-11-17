export const season1Offers=[
 {id:'AthenaGlider:Glider_ID_001',name:'Aerial Assault One',price:500,level:5},
 {id:'AthenaCharacter:CID_017_Athena_Commando_M',name:'Aerial Assault Trooper',price:1200,level:15},
 {id:'AthenaCharacter:CID_028_Athena_Commando_F',name:'Renegade Raider',price:1200,level:20},
 {id:'AthenaPickaxe:Pickaxe_Lockjaw',name:"Raider's Revenge",price:1500,level:35}
];
// Season Shop tab layout in 1.8.2: Panel 1 = big tile on the right, Panel 2 = big tile in the middle,
// Panel 3 = small tile bottom left, Panel 4 = small tile top left. The original 2017 shop had the glider
// and the trooper small on the left, Renegade Raider big in the middle and Raider's Revenge big on the
// right, so the panels are filled from the highest level down. The tile's second line (normally the item
// type) shows the offer's shortDescription, "Season Level N", as in the original shop.
export function season1Store(create){return {name:'BRSeasonStorefront',catalogEntries:[...season1Offers].reverse().map((item,i)=>{
 const entry=create('season1-'+i,{itemGrants:[item.id],price:item.price},'Panel '+(i+1),'Season','season1-'+i);
 entry.devName=item.name;entry.title=item.name;entry.shortDescription='Season Level '+item.level;
 entry.giftInfo.bIsEnabled=false;entry.meta={...entry.meta,MinLevel:String(item.level)};
 entry.metaInfo=[...(entry.metaInfo||[]),{Key:'MinLevel',Value:String(item.level)}];
 return entry;
})};}

// Item shop (daily + weekly) for the Pre-season (1.7.2) and Season 1 (1.8.2). These builds only
// contain Season 1 cosmetics, so the later-season rotation showed blank placeholder tiles. This
// rotation only uses cosmetics those builds have (checked against the 3.5 game export) and never
// includes the four Season Shop items above, which stay in their own Season tab.
const s1Outfits=[
 // Original 2017 outfits (CID_017 and CID_028 are Season Shop items and are left out).
 ...[9,10,11,12,13,14,15,16,18,19,20,21,22,23,24,25,26,27].map(n=>'AthenaCharacter:CID_0'+String(n).padStart(2,'0')+'_Athena_Commando_'+([13,14,15,16,21,22,23,24,27].includes(n)?'F':'M'))
];
const s1Halloween=['AthenaCharacter:CID_030_Athena_Commando_M_Halloween','AthenaCharacter:CID_029_Athena_Commando_F_Halloween']; // Fortnitemares (1.8)
const s1Pickaxes=[
 {id:'AthenaPickaxe:HalloweenScythe',price:1200,season1Only:true},   // Reaper (Fortnitemares)
 {id:'AthenaPickaxe:SickleBatPickaxe',price:800,season1Only:true},   // Batsickle (Fortnitemares)
 {id:'AthenaPickaxe:SpikyPickaxe',price:800},
 {id:'AthenaPickaxe:Pickaxe_Deathvalley',price:800},
 {id:'AthenaPickaxe:BoltOnPickaxe',price:500},
 {id:'AthenaPickaxe:Pickaxe_Flamingo',price:500},
 {id:'AthenaPickaxe:HappyPickaxe',price:500}
];
const s1Gliders=[
 {id:'AthenaGlider:Glider_Venom',price:1200},
 {id:'AthenaGlider:Glider_MiG',price:800},
 {id:'AthenaGlider:Glider_Military',price:500},
 {id:'AthenaGlider:Glider_Stealth',price:500},
 {id:'AthenaGlider:Glider_Voyager',price:1500},
 {id:'AthenaGlider:Glider_RoadTrip',price:1200}
];
export function season1Rotation(build,now=new Date()){
 const window=Math.floor(now.getTime()/21600000); // same 6-hour refresh as the other shops
 if(!Number.isFinite(window))throw new Error('Invalid shop date');
 const preSeason=build==='1.7.2';
 const take=(pool,count)=>Array.from({length:Math.min(count,pool.length)},(_,i)=>pool[((window*count+i)%pool.length+pool.length)%pool.length]);
 const offer=(id,price)=>({itemGrants:[id],price});
 const outfits=s1Outfits.map(id=>offer(id,1200));
 // Weekly: the Fortnitemares troopers in Season 1, rotating original outfits in the Pre-season.
 const featured=preSeason?take(outfits,3):[...s1Halloween.map(id=>offer(id,1500)),...take(outfits,1)];
 const pickaxes=s1Pickaxes.filter(p=>!(preSeason&&p.season1Only)).map(p=>offer(p.id,p.price));
 const gliders=s1Gliders.map(g=>offer(g.id,g.price));
 // Daily: the 1.7.2 / 1.8.2 shop shows four daily tiles (two columns of two); six overflowed off the
 // right edge. One outfit that isn't in the weekly row, one glider, one pickaxe, and a second glider or
 // pickaxe (alternating each refresh).
 const featuredIds=new Set(featured.map(f=>f.itemGrants[0]));
 const extra=window%2?take(pickaxes,2)[1]:take(gliders,2)[1];
 const daily=[...take(outfits.filter(o=>!featuredIds.has(o.itemGrants[0])),1),take(gliders,1)[0],take(pickaxes,1)[0],extra].filter(Boolean);
 const slots={};
 featured.forEach((f,i)=>slots['featured'+(i+1)]=f);
 daily.forEach((d,i)=>slots['daily'+(i+1)]=d);
 return slots;
}
