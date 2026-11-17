import {randomUUID} from 'node:crypto';
import {available111} from './syndicateCosmetics111.js';
import {debitShopWallet} from './syndicateShopWallet.js';
import Utils from './Utils.js';
// The 1.11 profile0 wallet is a projection. Never debit that saved legacy profile.
export function purchaseSeason2Item(profiles,entry,quantity){
 if(profiles.athena.stats.attributes.tilted_active_season!==2)throw Error('Season 2 profile required');
 if(quantity!==1)throw Error('Cosmetic purchase quantity must be one');
 const grants=Utils.offerGrants(entry);
 if(!grants.length||grants.some(g=>!available111(g.templateId)||!/^Athena(Character|Backpack|Pickaxe|Glider|Dance):/i.test(g.templateId)))throw Error('Offer is unavailable in Season 2');
 const price=entry.prices?.find(p=>p.currencyType?.toLowerCase()==='mtxcurrency')?.finalPrice;
 if(!Number.isSafeInteger(price)||price<0)throw Error('Invalid cosmetic price');
 const owned=new Set(Object.values(profiles.athena.items||{}).map(i=>i.templateId?.toLowerCase()));
 if(owned.has(grants[0].templateId.toLowerCase()))throw Error('You already own this item');
 const items=[];
 for(const grant of grants){const key=grant.templateId.toLowerCase();if(owned.has(key))continue;owned.add(key);items.push({id:randomUUID(),item:{templateId:grant.templateId,quantity:1,attributes:{item_seen:false,variants:[]}}});}
 if(!debitShopWallet(profiles.common_core,price).ok)throw Error('Not enough V-Bucks');
 profiles.athena.items??={};for(const {id,item} of items)profiles.athena.items[id]=item;
 for(const p of [profiles.athena,profiles.common_core]){p.rvn=(p.rvn||0)+1;p.commandRevision=(p.commandRevision||0)+1;p.updated=new Date().toISOString();}
 return items.map(({id,item})=>({itemType:item.templateId,itemGuid:id,itemProfile:'athena',quantity:1}));
}
