// Syndicate Rewards: money earned by playing, $5 for every elimination and $10 for every Victory Royale (every
// season, never while the Syndicate menu is on; syndicateProgression.js). Matches pay these instead of V-Bucks. They
// are spent on the V-Bucks packs in the game's STORE tab (syndicateVbucksStore.js, Routes/syndicateCheckout.js).
// Kept in cents, account-wide, on common_core next to the V-Bucks wallet.
export const REWARD_KEY='syndicate_reward_cents';
export const rewardCentsFor=Object.freeze({kill:500,win:1000});
export const formatDollars=cents=>'$'+(Math.max(0,Number(cents)||0)/100).toLocaleString('en-US',{minimumFractionDigits:2,maximumFractionDigits:2});
export function rewardBalance(core){
 const value=Number(core?.stats?.attributes?.[REWARD_KEY]);
 return Number.isSafeInteger(value)&&value>0?value:0;
}
function setBalance(core,cents){
 core.stats??={};core.stats.attributes??={};
 core.stats.attributes[REWARD_KEY]=cents;
 return cents;
}
// Adds earned cents; returns the new balance.
export function addReward(core,cents){
 if(!Number.isSafeInteger(cents)||cents<0)throw Error('Invalid reward');
 return setBalance(core,rewardBalance(core)+cents);
}
// Takes cents for a purchase. False (and nothing taken) when the balance is short.
export function spendReward(core,cents){
 if(!Number.isSafeInteger(cents)||cents<0)throw Error('Invalid price');
 const balance=rewardBalance(core);
 if(balance<cents)return false;
 setBalance(core,balance-cents);
 return true;
}
