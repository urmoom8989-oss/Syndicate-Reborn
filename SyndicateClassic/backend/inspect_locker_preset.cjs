const { MongoClient } = require('mongodb');
(async()=>{
  const client = new MongoClient('mongodb://127.0.0.1:27028'); await client.connect();
  const db=client.db('project_tilted');
  const accountId=process.argv[2]; if(!accountId){ console.error('Usage: node inspect_locker_preset.cjs <accountId>'); await client.close(); process.exit(1); }
  const doc=await db.collection('profiles').findOne({accountId});
  const a=doc.profiles.athena; const at=a.stats.attributes;
  console.log('athena updated',a.updated,'rvn',a.rvn,'favorite_glider',at.favorite_glider,'season',at.season_num);
  const locker=a.items?.sandbox_loadout;
  console.log('sandbox_loadout key exists',!!locker);
  if(locker){ console.log(JSON.stringify(locker.attributes?.locker_slots_data?.slots,null,2)); }
  const g=a.items?.[at.favorite_glider];
  console.log('glider variants',JSON.stringify(g?.attributes?.variants));
  await client.close();
})().catch(e=>{console.error(e);process.exit(1)});
