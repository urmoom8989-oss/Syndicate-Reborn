// Syndicate custom outfits: outfits that are not in Fortnite, made from parts of real ones. Season 3 only, while the
// launcher's Unlock all is on. The locker lists each one as its own outfit (template id "AthenaCharacter:<id>", which the
// Syndicate menu registers on the Season 3 client; added to the profile when it is sent, never saved). Equipping one
// saves tilted_custom_outfit; the match server gets the base outfit as the character plus custom=<name> in the
// .loadout file, and Erbium (RestoreSyndicateOutfit) swaps the parts.
export const customOutfits = [
 // The Reaper's suit (CID_084) with the default Spitfire head.
 {name: 'phantom', key: 'syndicate_custom_phantom', id: 'CID_Syndicate_Phantom', base: 'CID_084_Athena_Commando_M_Assassin', builds: ['3.5.1'], seasons: [3]},
];
const byKey = key => customOutfits.find(o => o.key === String(key || '').toLowerCase());
export const customOutfitForItemKey = key => byKey(key) || null;
// The custom outfit the player wears in this season's matches, or null (Unlock all off, other season, none picked).
export function selectedCustomOutfit(attributes) {
 if (!attributes?.tilted_unlock_all) return null;
 const outfit = customOutfits.find(o => o.name === attributes.tilted_custom_outfit);
 const season = Number(attributes.tilted_active_season);
 return outfit && (!Number.isFinite(season) || outfit.seasons.includes(season)) ? outfit : null;
}
export const customOutfitBuilds = [...new Set(customOutfits.flatMap(o => o.builds))];
// Adds the custom outfits this build has to the athena profile being sent, and shows the picked one as equipped.
export function withCustomOutfits(profile, build) {
 const attributes = profile?.stats?.attributes;
 if (!profile?.items || !attributes?.tilted_unlock_all) return profile;
 const here = customOutfits.filter(o => o.builds.includes(build));
 if (!here.length) return profile;
 const p = {...profile, items: {...profile.items}, stats: {...profile.stats, attributes: {...attributes}}};
 for (const o of here) p.items[o.key] = {templateId: 'AthenaCharacter:' + o.id, quantity: 1, attributes: {item_seen: true, level: 1, xp: 0, variants: [], favorite: false}};
 const picked = selectedCustomOutfit(p.stats.attributes);
 if (picked && picked.builds.includes(build)) p.stats.attributes.favorite_character = picked.key;
 return p;
}
