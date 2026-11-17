// Exclusive rewards that only a redeem code gives (launcher Settings > Redeem code, syndicateRedeem.js). The
// ids match the Discord bot's choices in relay/worker.js. Kept apart from syndicateRedeem.js so Unlock all
// and the shops can read the list without the database code.
// An item only shows in builds whose game files have it (Travis Scott is 12.41, Kratos, the Leviathan Axe
// and Polo Prodigy are later still); the code still marks it redeemed for this player.
export const REWARDS = {
  'psycho-bandit':    {title: 'Psycho Bandit',    items: ['AthenaCharacter:CID_515_Athena_Commando_M_BarbequeLarry']},
  'travis-scott':     {title: 'Travis Scott',     items: ['AthenaCharacter:CID_703_Athena_Commando_M_Cyclone']},
  'axe-of-champions': {title: 'Axe of Champions', items: ['AthenaPickaxe:Pickaxe_ID_376_FNCS']},
  'leviathan-axe':    {title: 'Leviathan Axe',    items: ['AthenaPickaxe:Pickaxe_ID_508_HistorianMale_6BQSW']},
  'polo-prodigy':     {title: 'Polo Prodigy',     items: ['AthenaCharacter:Character_StallionAviator']},
  'kratos':           {title: 'Kratos',           items: ['AthenaCharacter:CID_964_Athena_Commando_M_Historian_869BC']},
  'minty-axe':        {title: 'Minty Axe',        items: ['AthenaPickaxe:Pickaxe_ID_294_CandyCane']},
};
// Unlock all, the item shop and the hidden shop leave these out.
export const REDEEM_ONLY = new Set(Object.values(REWARDS).flatMap(r => r.items.map(i => i.toLowerCase())));
// Lower-case items this player redeemed (profile document field syndicateRedeemed: {'psycho-bandit': true}).
export function redeemedItems(doc) {
  const out = new Set();
  for (const [id, on] of Object.entries(doc?.syndicateRedeemed || {})) if (on && REWARDS[id]) for (const item of REWARDS[id].items) out.add(item.toLowerCase());
  return out;
}
