const { MongoClient } = require('mongodb');
(async()=>{
  const client = new MongoClient('mongodb://127.0.0.1:27028');
  await client.connect();
  const db = client.db('project_tilted');
  const accountId = process.argv[2];
  if (!accountId) { console.error('Usage: node inspect_profile.cjs <accountId>'); await client.close(); process.exit(1); }
  const doc = await db.collection('profiles').findOne({accountId});
  if(!doc){ console.log('PROFILE NOT FOUND'); await client.close(); return; }
  const a = doc.profiles?.athena; if(!a){ console.log('NO ATHENA'); await client.close(); return; }
  const attrs = a.stats?.attributes||{};
  console.log('rvn',a.rvn,'season',attrs.season_num,'unlock_all',attrs.tilted_unlock_all,'favorite_glider',attrs.favorite_glider);
  const gliderKey = attrs.favorite_glider;
  const glider = a.items?.[gliderKey] || Object.entries(a.items||{}).find(([k,v])=>String(v.templateId).toLowerCase()==='athenaglider:glider_id_001');
  console.log('glider key',Array.isArray(glider)?glider[0]:gliderKey);
  console.log('glider item',JSON.stringify(Array.isArray(glider)?glider[1]:glider,null,2));
  const outfit = a.items?.[attrs.favorite_character];
  console.log('outfit variants',JSON.stringify(outfit?.attributes?.variants,null,2));
  console.log('char item count',Object.keys(a.items||{}).length);
  await client.close();
})().catch(e=>{console.error(e);process.exit(1)});
