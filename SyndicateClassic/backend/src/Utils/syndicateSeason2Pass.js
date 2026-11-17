import fs from 'node:fs';
import {available111} from './syndicateCosmetics111.js';
import {debitShopWallet} from './syndicateShopWallet.js';
import {applyProgression} from './syndicateProgressionCore.js';
const pass=JSON.parse(fs.readFileSync(new URL('../local/Battlepass/Jsons/Season2.json',import.meta.url),'utf8'));
export const season2PassOffer=pass.battlePassOfferId;
export const season2TierOffer=pass.tierOfferId;
export function season2PassCatalog(makeEntry){
 const first='AthenaCharacter:CID_032_Athena_Commando_M_Medieval';
 if(!available111(first))return {name:'BRSeason2',catalogEntries:[]};
 const entry=makeEntry('season2_pass',{itemGrants:[first],price:0},'Panel 1');
 entry.offerId=season2PassOffer;entry.devName='Season2.BattlePass';entry.requirements=[];entry.giftInfo.bIsEnabled=false;entry.refundable=false;
 // The 1.11 client requires a resolvable primary definition even for tier offers.
 const tier=makeEntry('season2_tier',{itemGrants:[first],price:150},'Panel 2');
 tier.offerId=season2TierOffer;tier.devName='Season2.SingleTier';tier.requirements=[];tier.giftInfo.bIsEnabled=false;tier.refundable=false;
 return {name:'BRSeason2',catalogEntries:[entry,tier]};
}
export function purchaseSeason2(profiles,offerId,quantity){
 const a=profiles.athena.stats.attributes;
 if(a.tilted_active_season!==2)throw Error('This offer belongs to Season 2.');
 if(!Number.isSafeInteger(quantity)||quantity<1)throw Error('Invalid quantity');
 let cost;
 if(offerId===season2PassOffer){if(quantity!==1)throw Error('Invalid quantity');cost=0;}
 else if(offerId===season2TierOffer){if(!a.book_purchased)throw Error('Own the Season 2 Battle Pass first');if(quantity>69||a.book_level+quantity>70)throw Error('Season 2 ends at tier 70');cost=150*quantity;}
 else throw Error('Unknown Season 2 offer');
 if(!debitShopWallet(profiles.common_core,cost).ok)throw Error('Not enough V-Bucks');
 if(offerId===season2PassOffer)a.book_purchased=true;else a.book_level+=quantity;
 return applyProgression(profiles,0);
}
