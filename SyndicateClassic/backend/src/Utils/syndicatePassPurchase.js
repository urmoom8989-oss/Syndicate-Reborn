// Buying a season's Battle Pass in the game (Seasons 3 to 12; Season 2 is syndicateSeason2Pass.js). The pass is
// never free: a season starts without it and Unlock all does not give it (syndicateSeasonState.js
// syncPassOwnership). The Battle Pass costs 950 V-Bucks; the Battle Bundle is the pass and 25 tiers for 2,800.
import {debitShopWallet} from './syndicateShopWallet.js';
import {applyProgression} from './syndicateProgressionCore.js';
import {activeSeason,markPassBought,passBought} from './syndicateSeasonState.js';
import {seasonMaxed} from './syndicateMaxProgress.js';
import {passOfferKind} from './syndicateTierPurchase.js';

export const PASS_PRICE=950,BUNDLE_PRICE=2800,BUNDLE_TIERS=25;
const seasonName=season=>season===10?'X':season===11?'Chapter 2 Season 1':season===12?'Chapter 2 Season 2':String(season);

// Buys the offer for the active season. Returns 'pass' or 'bundle'; throws with a message the game can show.
export function purchasePass(profiles,season,offerId,quantity=1){
 const kind=passOfferKind(season,offerId);
 if(!kind)throw Error('This is not a Battle Pass offer.');
 const a=profiles.athena,s=a.stats.attributes;
 if(activeSeason(a)!==season)throw Error(`Open Season ${seasonName(season)} before buying its Battle Pass.`);
 if(quantity!==undefined&&quantity!==null&&Number(quantity)!==1)throw Error('Buy one Battle Pass at a time.');
 if(passBought(a,season))throw Error('You already own this Battle Pass.');
 if(!debitShopWallet(profiles.common_core,kind==='bundle'?BUNDLE_PRICE:PASS_PRICE).ok)throw Error('Not enough V-Bucks.');
 markPassBought(a,season);s.book_purchased=true;
 // Unlock all holds the season at its last tier: the bundle's tiers have nowhere to go.
 if(kind==='bundle'&&!seasonMaxed(s,season)){
  const cap=season===2?70:100;
  // Chapter 2: the Battle Pass tier is the season level (applyProgression copies it back), so raise the level.
  if(season>=11)s.level=Math.max(1,Math.min(cap,(Number(s.level)||1)+BUNDLE_TIERS));
  else s.book_level=Math.min(cap,Math.max(1,Number(s.book_level)||1)+BUNDLE_TIERS);
 }
 // Hands out the paid rewards of every tier reached so far.
 applyProgression(profiles,0);
 return kind;
}
