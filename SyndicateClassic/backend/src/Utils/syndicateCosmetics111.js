import fs from 'node:fs';
export const cosmetics111=JSON.parse(fs.readFileSync(new URL('../local/Storefront/cosmetics111.json',import.meta.url),'utf8'));
const ids=new Set(cosmetics111.map(x=>x.toLowerCase()));
export const available111=id=>typeof id==='string'&&ids.has(id.toLowerCase());
