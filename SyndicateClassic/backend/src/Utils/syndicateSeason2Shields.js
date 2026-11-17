// 1.11 attaches shields to outfits; later clients need separate backpack ownership.
const pairs = [
 ['cid_032_athena_commando_m_medieval', 'bid_001_bluesquire'],
 ['cid_033_athena_commando_f_medieval', 'bid_002_royaleknight'],
 ['cid_035_athena_commando_m_medieval', 'bid_004_blackknight']
];
export function ensureSeason2Shields(profile) {
 profile.items ??= {};
 const owned = new Set(Object.values(profile.items).filter(i => (i.quantity ?? 1) > 0).map(i => String(i.templateId).toLowerCase()));
 let changed = false;
 for (const [outfit, shield] of pairs) {
  const templateId = 'AthenaBackpack:' + shield;
  if (!owned.has('athenacharacter:' + outfit) || owned.has(templateId.toLowerCase())) continue;
  profile.items['tilted_s2_shield_' + shield] = {templateId, quantity:1, attributes:{item_seen:false,level:1,xp:0,variants:[]}};
  changed = true;
 }
 return changed;
}
