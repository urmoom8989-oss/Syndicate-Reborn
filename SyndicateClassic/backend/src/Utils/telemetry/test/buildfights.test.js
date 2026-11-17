import test from 'node:test';
import assert from 'node:assert/strict';
import { summarizeBuildFights, buildFightLines } from '../buildfights.js';
import { discordContent } from '../discord.js';

const fight = (over) => ({ type: 'build_fight', bot: 'b1', skill: 3, enemy: 'human', trigger: 'open angle: block line of sight', states: 'shot-block,box-fight',
  outcome: 'enemy_down', seconds: 6.5, ok: { wall: 2, swap_shotgun: 1, peek: 1 }, failed: { wall: 1 }, sequence: 'WxWSP', heightBest: 0, ...over });

test('build fights are summarized per outcome, trigger and opening', () => {
  const s = summarizeBuildFights([fight(), fight({ outcome: 'bot_down', trigger: 'they climb', sequence: 'CCK', ok: { contest_ramp: 2 }, failed: {}, heightBest: 300, enemy: 'bot' }), { type: 'bot_state' }]);
  assert.equal(s.fights, 2);
  assert.equal(s.vsHumans, 1);
  assert.deepEqual(s.byOutcome, { enemy_down: 1, bot_down: 1 });
  assert.equal(s.actions.find(a => a.action === 'wall').failed, 1);
  assert.equal(s.openings.find(o => o.opening === 'WWSP').enemyDown, 1);
  assert.equal(s.tookHeight, 1);
});

test('no build fights: no lines; lines fit the Discord message', () => {
  assert.equal(summarizeBuildFights([{ type: 'bot_state' }]), null);
  assert.deepEqual(buildFightLines(null), []);
  const many = Array.from({ length: 300 }, (_, i) => fight({ trigger: 'trigger ' + (i % 7), sequence: 'WSPV'.repeat(10) }));
  const lines = buildFightLines(summarizeBuildFights(many));
  assert.equal(lines.length, 3);
  assert.ok(discordContent(lines).length <= 1910);
});

test('mechanics webhook from the per-PC file; bot-learning channel untouched', async () => {
  const fs = await import('fs'), os = await import('os'), path = await import('path');
  const { webhookConfig, redact } = await import('../discord.js');
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'syndicate-hook-'));
  const url = 'https://discord.com/api/webhooks/123456789012345678/' + 'Q'.repeat(68);
  fs.mkdirSync(path.join(root, 'ProjectTilted', 'UserData'), { recursive: true });
  fs.writeFileSync(path.join(root, 'ProjectTilted', 'UserData', 'discord-mechanics-webhook.txt'), url + '\r\n');
  assert.deepEqual(webhookConfig({ LOCALAPPDATA: root }, 'mechanics'), { kind: 'webhook', url });
  assert.equal(webhookConfig({ LOCALAPPDATA: root }).kind, 'relay'); // match summaries still go to the relay / .env webhook
  assert.equal(webhookConfig({}, 'mechanics').kind, 'none');
  assert.ok(!redact('x ' + 'Q'.repeat(68), { LOCALAPPDATA: root }).includes('Q'.repeat(68)));
  fs.rmSync(root, { recursive: true, force: true });
});

test('unlock all owns every exported outfit style and turning it off restores the old ones', async () => {
  const fs = await import('fs'), os = await import('os'), path = await import('path');
  const { stylesFor, grantAllStyles, removeUnlockAll } = await import('../../syndicateUnlockAll.js');
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'syndicate-styles-'));
  fs.writeFileSync(path.join(dir, 'styles-season-7.json'), JSON.stringify({ version: '7.40', season: 7, styles: {
    'AthenaCharacter:CID_175_Athena_Commando_M_Celestial': [{ channel: 'Material', options: ['Mat0', 'Mat1', 'Mat2'] }, { channel: 'bad channel!', options: ['x'] }] } }));
  const styles = stylesFor(7, dir);
  const channels = styles.get('athenacharacter:cid_175_athena_commando_m_celestial');
  assert.equal(channels.length, 1);
  const owned = { templateId: 'AthenaCharacter:CID_175_Athena_Commando_M_Celestial', attributes: { variants: [{ channel: 'Material', active: 'Mat0', owned: ['Mat0'] }] } };
  const added = { templateId: 'AthenaCharacter:CID_175_Athena_Commando_M_Celestial', attributes: { tilted_unlock_all: true, variants: [] } };
  assert.equal(grantAllStyles(owned, channels), true);
  assert.equal(grantAllStyles(owned, channels), false); // nothing new the second time
  assert.equal(grantAllStyles(added, channels), true);
  assert.deepEqual(owned.attributes.variants[0].owned, ['Mat0', 'Mat1', 'Mat2']);
  assert.equal(added.attributes.tilted_variants_before, undefined);
  owned.attributes.variants[0].active = 'Mat2';
  const profile = { stats: { attributes: {} }, items: { a: owned, b: added } };
  removeUnlockAll(profile);
  assert.deepEqual(profile.items.a.attributes.variants, [{ channel: 'Material', active: 'Mat0', owned: ['Mat0'] }]);
  assert.equal(profile.items.b, undefined);
  fs.rmSync(dir, { recursive: true, force: true });
});
