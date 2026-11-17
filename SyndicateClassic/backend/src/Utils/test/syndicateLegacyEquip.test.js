import test from 'node:test';
import assert from 'node:assert/strict';
import {project111Response} from '../syndicateLegacy111Profile.js';

// The route answers before it saves: the saved copy still has the old outfit.
test('Season 1/2 locker answer carries the new outfit even before it is saved', async () => {
 const saved = {profileId: 'athena', rvn: 5, commandRevision: 5, items: {
  old: {templateId: 'AthenaCharacter:CID_001_Athena_Commando_F_Default', quantity: 1, attributes: {}},
  neu: {templateId: 'AthenaCharacter:CID_002_Athena_Commando_F_Default', quantity: 1, attributes: {}}},
  stats: {attributes: {favorite_character: 'old', season_num: 1}}};
 const body = {profileId: 'athena', profileRevision: 6, profileCommandRevision: 6, profileChangesBaseRevision: 5,
  profileChanges: [{changeType: 'statModified', name: 'favorite_character', value: 'neu'}]};
 const out = await project111Response(body, async () => saved, null, null, '1.8.2');
 const p = out.profileChanges[0].profile;
 assert.equal(out.profileChanges[0].changeType, 'fullProfileUpdate');
 assert.equal(p.stats.attributes.favorite_character, 'neu');
 assert.equal(out.profileRevision, 6);
 assert.equal(saved.stats.attributes.favorite_character, 'old', 'the saved copy is not changed');
});
