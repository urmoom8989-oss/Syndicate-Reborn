export const walletId='Currency:MtxPurchased';
// One account-wide V-Bucks balance. Season progression never owns currency.
export function ensureSharedWallet(profile){
 const items=profile.items||(profile.items={});
 const entries=Object.entries(items).filter(([id,i])=>/^currency:mtx/i.test(i?.templateId||id));
 let total=0;
 for(const [,i] of entries){if(!Number.isSafeInteger(i.quantity)||i.quantity<0)throw Error('Invalid saved V-Bucks balance; migration stopped');total+=i.quantity;}
 if(!Number.isSafeInteger(total))throw Error('V-Bucks balance overflow; migration stopped');
 const current=items[walletId];
 if(entries.length===1 && entries[0][0]===walletId && current.templateId===walletId && current.attributes?.platform==='Shared')return false;
 const attributes={...(current?.attributes||{}),platform:'Shared'};
 for(const [id] of entries)delete items[id];
 items[walletId]={templateId:walletId,quantity:total,attributes};return true;
}
export function debitShopWallet(profile,price){
 ensureSharedWallet(profile);const wallet=profile.items[walletId],balance=wallet.quantity;
 if(!Number.isSafeInteger(price)||price<0||balance<price)return {ok:false,balance,changes:[]};
 wallet.quantity-=price;
 return {ok:true,balance,changes:[{changeType:'itemQuantityChanged',itemId:walletId,quantity:wallet.quantity}]};
}
