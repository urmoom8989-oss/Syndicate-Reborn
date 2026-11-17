// Boost tokens are season-scoped; expose their awarded percentage to the client.
export function syncXpBoosts(profile) {
 const attrs=profile.stats.attributes; let changed=false;
 for(const [token,field] of [['athenaseasonxpboost','season_match_boost'],['athenaseasonfriendxpboost','season_friend_match_boost']]) {
  const amount=Object.values(profile.items||{}).reduce((total,item)=>{
   const q=Number(item.quantity);
   return item.templateId?.toLowerCase()==='token:'+token && Number.isSafeInteger(q) && q>0 ? total+q : total;
  },0);
  if(attrs[field]!==amount){attrs[field]=amount;changed=true;}
 }
 return changed;
}
