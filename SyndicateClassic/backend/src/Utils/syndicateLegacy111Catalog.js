import {clientBuild} from './syndicateBuild.js';
import {available111} from './syndicateCosmetics111.js';
export function catalogForClient(catalog,userAgent){
 const build=clientBuild(userAgent);
 if(build==='3.5.1')return {...catalog,storefronts:(catalog.storefronts||[]).map(store=>store.name==='BRWeeklyStorefront'?{...store,catalogEntries:store.catalogEntries.slice(0,2)}:store)};
 if(build!=='1.11')return catalog;
 return {...catalog,storefronts:(catalog.storefronts||[]).map(store=>({...store,catalogEntries:store.catalogEntries.filter(offer=>offer.itemGrants?.length>0&&offer.itemGrants.every(grant=>available111(grant.templateId)))}))};
}
