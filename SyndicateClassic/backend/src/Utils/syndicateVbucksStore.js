// The V-Bucks packs in the game's own STORE tab, every season. They are real-money offers like Epic's
// (CurrencyStorefront, with prices from the catalog and price engine services below), but nothing is paid:
// buying one makes the game open its checkout browser, which SyndicateClient.dll points at Syndicate's checkout
// page (Routes/syndicateCheckout.js). That page has nothing to fill in; Confirm adds the V-Bucks for free.
// The ids are Syndicate's own, never Epic's, so a checkout that was not redirected can never show (or charge) a
// real Epic product.
import crypto from 'node:crypto';
import {ensureSharedWallet,walletId} from './syndicateShopWallet.js';
import {withAccountLock} from './syndicateAccountLock.js';

const hexId=key=>crypto.createHash('md5').update('syndicate-vbucks-'+key).digest('hex').toUpperCase();
// The Chapter 1 packs and their US prices.
export const VBUCKS_PACKS=Object.freeze([
 {quantity:1000,bonus:0,cents:999,devName:'Small Currency Pack',icon:'XSmall',sort:4},
 {quantity:2800,bonus:300,cents:2499,devName:'Medium Currency Pack',icon:'Small',sort:3,banner:'12PercentExtra'},
 {quantity:5000,bonus:1000,cents:3999,devName:'Intermediate Currency Pack',icon:'Medium',sort:2,banner:'25PercentExtra'},
 {quantity:13500,bonus:3500,cents:9999,devName:'Jumbo Currency Pack',icon:'XLarge',sort:1,banner:'35PercentExtra'},
].map(p=>Object.freeze({...p,appStoreId:'app-SyndicateVbucks'+p.quantity,offerId:hexId(p.quantity)})));
const byAppStoreId=new Map(VBUCKS_PACKS.map(p=>[p.appStoreId.toLowerCase(),p]));
export const packFor=id=>typeof id==='string'?byAppStoreId.get(id.toLowerCase())||VBUCKS_PACKS.find(p=>p.offerId===id.toUpperCase())||null:null;
export const formatVbucks=n=>Number(n).toLocaleString('en-US');
export const formatPrice=cents=>'$'+(cents/100).toFixed(2);
export const packTitle=p=>p.bonus?`${formatVbucks(p.quantity-p.bonus)} (+${formatVbucks(p.bonus)} Bonus!)`:`${formatVbucks(p.quantity)} V-Bucks`;

// The STORE tab's V-Bucks storefront. appStoreId[1] is the Epic purchasing service's id (EAppStore order), the
// one the game prices through bulk/offers and the price engine.
export function vbucksStorefront(){
 return {name:'CurrencyStorefront',catalogEntries:VBUCKS_PACKS.map(p=>{
  const meta=[['MtxQuantity',String(p.quantity)],['MtxBonus',String(p.bonus)],['IconSize',p.icon],...(p.banner?[['BannerOverride',p.banner]]:[]),['CurrencyAnalyticsName','MtxPack'+p.quantity]];
  return {
   offerId:p.offerId,devName:p.devName,offerType:'StaticPrice',
   prices:[{currencyType:'RealMoney',currencySubType:'',regularPrice:0,dynamicRegularPrice:-1,finalPrice:0,saleExpiration:'9999-12-31T23:59:59.999Z',basePrice:0}],
   categories:[],dailyLimit:-1,weeklyLimit:-1,monthlyLimit:-1,refundable:false,
   appStoreId:['',p.appStoreId,'','','','','','','','','',''],
   requirements:[],metaInfo:meta.map(([key,value])=>({key,value})),meta:Object.fromEntries(meta),
   giftInfo:{bIsEnabled:false,forcedGiftBoxTemplateId:'',purchaseRequirements:[],giftRecordIds:[]},
   catalogGroup:'',catalogGroupPriority:0,sortPriority:p.sort,
   title:packTitle(p),shortDescription:'',
   description:`Buy ${formatVbucks(p.quantity)} Fortnite V-Bucks that can be spent in Battle Royale and Save the World.`,
   displayAssetPath:`/Game/Catalog/DisplayAssets/DA_MtxPack${p.quantity}.DA_MtxPack${p.quantity}`,
   itemGrants:[],fulfillmentIds:[]
  };
 })};
}

// GET /catalog/api/shared/bulk/offers?id=...: the store entries the game looks up by appStoreId.
export function vbucksBulkOffers(ids){
 const out={},date='2017-01-01T00:00:00.000Z';
 for(const id of [].concat(ids||[])){
  const p=packFor(String(id));if(!p)continue;
  out[p.appStoreId]={
   id:p.appStoreId,title:packTitle(p),description:`${formatVbucks(p.quantity)} V-Bucks`,longDescription:`${formatVbucks(p.quantity)} V-Bucks`,technicalDetails:'',
   keyImages:[],categories:[{path:'addons'},{path:'addons/durable'}],namespace:'fn',status:'ACTIVE',
   creationDate:date,lastModifiedDate:date,internalName:packTitle(p),items:[{id:p.appStoreId,namespace:'fn'}],
   customAttributes:{},developer:'Syndicate',developerId:'o-syndicate',eulaIds:[],endOfSupport:false,mainGameItem:null,
   releaseInfo:[{id:p.appStoreId,appId:p.appStoreId,platform:['Windows'],dateAdded:date}],
   currencyCode:'USD',currentPrice:p.cents,price:p.cents,basePrice:p.cents,basePriceCurrencyCode:'USD',
   recurrence:'ONCE',freeDays:0,maxBillingCycles:0,seller:{id:'o-syndicate',name:'Syndicate'},
   viewableDate:date,effectiveDate:date,expiryDate:'9999-12-31T23:59:59.999Z',productSlug:'vbucks-'+p.quantity,
   purchaseLimit:-1,offerType:'CONSUMABLE',tags:[],unsearchable:false,roleNamesToGrant:[]
  };
 }
 return out;
}

const money=amount=>({currencyCode:'USD',currencySymbolPlacement:'LEFT',originalPrice:amount,discountPrice:amount,discount:0,
 basePayoutCurrencyCode:'USD',basePayoutPrice:amount,payoutCurrencyExchangeRate:1,vat:0,voucherDiscount:0});
// POST /priceengine/api/shared/offers/price: the price the STORE tab shows on each pack. Other offers are priced at
// otherCents when it is set (the dev placeholder) and left out otherwise. Null when nothing was priced.
export function vbucksPrices(body,otherCents=0){
 const lines=Array.isArray(body?.lineOffers)?body.lineOffers:[];
 const priced=lines.map(o=>({o,p:packFor(String(o?.offerId||''))})).filter(x=>x.p||otherCents>0);
 if(!priced.length)return null;
 let total=0;
 const lineOffers=priced.map(({o,p})=>{const quantity=Math.max(1,Number(o.quantity)||1),amount=(p?p.cents:otherCents)*quantity;total+=amount;return {offerId:p?p.appStoreId:String(o?.offerId||''),quantity,price:money(amount),appliedRules:[]};});
 return {totalPrice:money(total),totalPaymentPrice:{paymentCurrencyCode:'USD',paymentCurrencySymbol:'$',paymentCurrencyAmount:total,paymentCurrencyExchangeRate:1,paymentCurrencySymbolPlacement:'LEFT'},lineOffers};
}

// Who is playing on this PC: the account that last made a game request from this address. The checkout page is
// opened by the game's own browser, which has no Fortnite sign-in, so this is how Confirm knows whose V-Bucks.
const seen=new Map();
export const isLoopback=ip=>['127.0.0.1','::1','::ffff:127.0.0.1'].includes(String(ip||''));
export function noteCheckoutAccount(ip,accountId){if(isLoopback(ip)&&typeof accountId==='string'&&accountId)seen.set('local',{accountId,at:Date.now()});}
export function localAccount(){const s=seen.get('local');return s&&Date.now()-s.at<6*3600e3?s.accountId:null;}

// Receipts of packs bought on the checkout page. The game redeems a purchase by asking for its receipts
// (/fortnite/api/receipts) and then VerifyRealMoneyPurchase; the V-Bucks were already added at Confirm, so those
// only report the purchase. Kept in memory: a receipt older than 30 minutes is no longer offered.
const receipts=new Map();
export function pendingReceipts(accountId){
 const now=Date.now();
 return (receipts.get(accountId)||[]).filter(r=>!r.redeemed&&now-r.at<30*60e3);
}
export function redeemReceipt(accountId,{receiptId,appStoreId}={}){
 const list=pendingReceipts(accountId);
 const hit=list.find(r=>receiptId&&r.receiptId===receiptId)||list.find(r=>appStoreId&&r.appStoreId.toLowerCase()===String(appStoreId).toLowerCase())||(!receiptId&&!appStoreId?list[0]:null);
 if(hit)hit.redeemed=true;
 return hit||null;
}
const usedNonces=new Map();
// Adds the pack's V-Bucks to the account's shared wallet. One Confirm per checkout page (nonce).
export async function grantVbucksPack(accountId,appStoreId,nonce){
 const p=packFor(appStoreId);
 if(!p)throw Error('Unknown V-Bucks pack.');
 if(!accountId)throw Error('Start Fortnite from the Syndicate launcher and sign in first.');
 if(typeof nonce!=='string'||nonce.length<8||nonce.length>80)throw Error('This checkout expired. Open it again from the STORE tab.');
 for(const [k,at] of usedNonces)if(Date.now()-at>24*3600e3)usedNonces.delete(k);
 if(usedNonces.has(nonce))throw Error('This purchase was already completed.');
 const {default:Profile}=await import('../User/Mongodb/Schema/profiles.js'); // loaded here so the price helpers above need no database
 return withAccountLock(accountId,async()=>{
  const doc=await Profile.collection.findOne({accountId});
  const core=doc?.profiles?.common_core;if(!core)throw Error('No Fortnite profile for this account yet.');
  ensureSharedWallet(core);
  core.items[walletId].quantity+=p.quantity;
  core.rvn=(Number(core.rvn)||0)+1;core.commandRevision=(Number(core.commandRevision)||0)+1;core.updated=new Date().toISOString();
  const saved=await Profile.collection.updateOne({accountId},{$set:{'profiles.common_core':core}});
  if(!saved.modifiedCount)throw Error('Your profile is busy. Try again.');
  usedNonces.set(nonce,Date.now());
  const receipt={receiptId:crypto.randomUUID().replace(/-/g,''),receiptInfo:'syndicate-'+crypto.randomUUID(),appStoreId:p.appStoreId,quantity:p.quantity,at:Date.now(),redeemed:false};
  receipts.set(accountId,[...pendingReceipts(accountId),receipt]);
  if(global.giftReceived)global.giftReceived[accountId]=true;
  return {pack:p,balance:core.items[walletId].quantity,receipt};
 });
}
