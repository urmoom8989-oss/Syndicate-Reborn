import fs from 'node:fs';
import {debitShopWallet} from './syndicateShopWallet.js';
import {applyProgression} from './syndicateProgressionCore.js';
import {activeSeason} from './syndicateSeasonState.js';
const read=n=>JSON.parse(fs.readFileSync(new URL(`../local/Battlepass/Jsons/Season${n}.json`,import.meta.url),'utf8'));
const passes=Object.fromEntries([3,4,5,6,7,8,9,10,11,12].map(n=>[n,read(n)]));
export const tierOfferId=passes[4].tierOfferId;
export const season5TierOfferId=passes[5].tierOfferId;
export const tierOfferIds=Object.freeze(Object.fromEntries(Object.entries(passes).map(([n,p])=>[n,p.tierOfferId])));
// Client build -> its Battle Pass season (builds without a detected release default to Season 4).
export function tierSeason(build){return ({'3.5.1':3,'4.5':4,'5.10':5,'6.20':6,'7.40':7,'8.50':8,'9.40':9,'10.31':10,'10.40':10,'11.00':11,'12.50':12})[build]||4;}
const passTexts=JSON.parse(fs.readFileSync(new URL('../local/Battlepass/PassOffers.json',import.meta.url),'utf8'));
// The season's Battle Pass storefront: the Battle Pass (950 V-Bucks), the Battle Bundle (the pass and 25 tiers,
// 2,800 V-Bucks) and single tiers (150). Bought in the game (Routes/Fortnite.js, syndicatePassPurchase.js).
export function tierCatalog(season=4){
 const selected=passes[season]||passes[4],n=passes[season]?season:4,texts=passTexts[n]||{};
 const entry=(offerId,devName,price,regular,extra)=>({
  offerId,devName,offerType:'StaticPrice',
  prices:[{currencyType:'MtxCurrency',currencySubType:'',regularPrice:regular,finalPrice:price,basePrice:price,
   ...(regular>price?{saleType:'PercentOff'}:{}),saleExpiration:'9999-12-31T23:59:59.999Z'}],
  itemGrants:[],requirements:[],categories:[],metaInfo:[],dailyLimit:-1,weeklyLimit:-1,monthlyLimit:-1,
  refundable:false,giftInfo:{bIsEnabled:false},appStoreId:[],fulfillmentIds:[],catalogGroup:'',catalogGroupPriority:0,sortPriority:0,...extra
 });
 const text=t=>({title:t.title,shortDescription:t.shortDescription||'',description:t.description||'',displayAssetPath:t.displayAssetPath||''});
 const entries=[];
 if(selected.battleBundleOfferId&&texts.bundle)entries.push(entry(selected.battleBundleOfferId,`BR.Season${n}.BattleBundle.01`,2800,4700,text(texts.bundle)));
 if(selected.battlePassOfferId&&texts.pass)entries.push(entry(selected.battlePassOfferId,`BR.Season${n}.BattlePass.01`,950,950,{...text(texts.pass),sortPriority:1}));
 entries.push(entry(selected.tierOfferId,`Season${n}.SingleTier.01`,150,150,{title:'Battle Pass Tier',shortDescription:'',description:'Get great rewards now!',displayAssetPath:''}));
 return {name:`BRSeason${n}`,catalogEntries:entries};
}
// 'pass' or 'bundle' when the offer is this season's Battle Pass or Battle Bundle, otherwise null.
export function passOfferKind(season,offerId){
 const p=passes[season];if(!p||typeof offerId!=='string')return null;
 return offerId===p.battlePassOfferId?'pass':offerId===p.battleBundleOfferId?'bundle':null;
}
export function purchaseTiers(profiles,quantity){
 const a=profiles.athena,c=profiles.common_core,s=a.stats.attributes;
 if(!Number.isSafeInteger(quantity)||quantity<1||quantity>99)throw Error('Choose a whole number of tiers from 1 to 99.');
 if(!s.book_purchased)throw Error('Buy the Battle Pass first: tiers can only be bought with the Battle Pass.');
 const season=activeSeason(a),cap=season===2?70:100;
 const level=Math.max(1,Number(s.book_level)||1);
 if(level+quantity>cap)throw Error('This purchase exceeds tier '+cap+'.');
 const payment=debitShopWallet(c,150*quantity);
 if(!payment.ok)throw Error('Not enough V-Bucks.');
 // Chapter 2: the Battle Pass tier IS the season level, and applyProgression resets book_level back to it,
 // so raising book_level alone is wiped (tiers bought, no rewards). Raise the season level instead.
 if(season>=11)s.level=Math.max(1,Math.min(cap,(Number(s.level)||1)+quantity));
 else s.book_level=level+quantity;
 applyProgression(profiles,0);
 return profiles;
}
