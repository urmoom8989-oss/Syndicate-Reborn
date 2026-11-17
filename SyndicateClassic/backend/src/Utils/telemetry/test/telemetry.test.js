// node --test src/Utils/telemetry/test   (from the backend folder)
// All inputs are synthetic fixtures with hand-checked expected numbers.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'fs';
import os from 'os';
import path from 'path';
import { parseMatch, loadMatches, filterMatches, applyRetention, deleteMatches, validateEvent } from '../store.js';
import { matchReport, matchSummaryLine } from '../report.js';
import { detectWeaknesses } from '../weaknesses.js';
import { landingIntelligence } from '../landing.js';
import { playstyleProfiles } from '../playstyle.js';
import { compareBotVersions } from '../benchmark.js';
import { buildDataset, validateDataset } from '../dataset.js';
import { createUploader, relayLines, writeSettings, readSettings } from '../upload.js';
import { wilson } from '../stats.js';
import { matchId, matchText, bot, human, start } from './fixtures.js';

const H = 'h0123456789ab';

// A normal won match: human lands at City_001 with bot 1, kills it, a bot dies to the storm,
// bot 2 is killed while looting, human wins.
function wonMatch(id = matchId(), extra = {}) {
  return parseMatch(matchText(id, [
    start(extra.start),
    ['landed', 30, { actor: human(H), pos: [100, 100, 0], poi: 'City_001', grid: '0,0' }],
    ['landed', 31, { actor: bot(1), pos: [300, 100, 0], poi: 'City_001', grid: '0,0', strategy: 'hot', targetError: 200 }],
    ['landed', 35, { actor: bot(2), pos: [90000, 0, 0], poi: 'Lake_004', grid: '9,0', strategy: 'assigned', targetError: 5000 }],
    ['landed', 36, { actor: bot(3), pos: [0, 90000, 0], poi: '', grid: '0,9', strategy: 'spread', targetError: 100 }],
    ['bot_state', 40, { bot: 'b1', from: 'looting', to: 'fighting', held: 9, flips: 0, enemyDist: 800 }],
    ['elimination', 50, { victim: bot(1), killer: human(H), cause: 'weapon', weapon: 'WID_Shotgun', distance: 600, pos: [300, 100, 0], poi: 'City_001', grid: '0,0', victimState: 'fighting', hadGun: true, shots: 12, hits: 1, place: 4 }],
    ['elimination', 200, { victim: bot(2), killer: bot(3), cause: 'weapon', distance: 2000, pos: [90000, 0, 0], poi: 'Lake_004', grid: '9,0', victimState: 'looting', hadGun: false, shots: 0, hits: 0, place: 3 }],
    ['elimination', 400, { victim: bot(3), killer: null, cause: 'storm', pos: [0, 90000, 0], poi: '', grid: '0,9', victimState: 'rotating', hadGun: true, shots: 5, hits: 2, place: 2 }],
    ['match_end', 401, { reason: 'winner', winnerKind: 'human', humansAlive: 1, botsAlive: 0, dropped: 0 }],
  ], extra.opts), `${id}.jsonl`);
}

test('normal match: report counts, outcome and evidence', () => {
  const m = wonMatch();
  assert.equal(m.complete, true);
  assert.equal(m.termination, 'winner');
  assert.equal(m.events.length, 10);
  const r = matchReport(m);
  assert.equal(r.overview.winner, 'human');
  assert.deepEqual(r.bots.eliminatedBy, { human: 1, bot: 1, storm: 1, other: 0 });
  assert.equal(r.humans[0].kills, 1);
  assert.equal(r.humans[0].landedAt, 'City_001');
  assert.ok(r.humans[0].evidence.every(id => id.startsWith(m.id + ':')));
  assert.equal(r.encounters.length, 1);
  assert.equal(r.encounters[0].distanceM, 6);
  assert.equal(r.bots.accuracyOfEliminatedBots, Math.round(3 / 17 * 10000) / 10000);
  assert.match(matchSummaryLine(m), /winner \(human won\)/);
});

test('death of a human, early departure and crash recovery', () => {
  const id = matchId();
  const m = parseMatch(matchText(id, [
    start(),
    ['landed', 30, { actor: human(H), pos: [0, 0, 0], poi: 'City_001' }],
    ['downed', 60, { victim: human(H), killer: bot(1), cause: 'weapon' }],
    ['elimination', 61, { victim: human(H), killer: bot(1), cause: 'weapon', killerState: 'pursuing', distance: 900 }],
    ['player_left', 70, { actor: human('h999999999999'), alive: 50 }],
    // no match_end: the server crashed
  ]), `${id}.jsonl`);
  assert.equal(m.complete, false);
  assert.equal(m.termination, 'terminated_unexpectedly');
  assert.ok(m.issues.some(i => i.kind === 'incomplete_match'));
  const r = matchReport(m);
  assert.equal(r.humans.find(h => h.player === H).eliminatedBy.kind, 'bot');
  assert.equal(r.humans.find(h => h.player === H).eliminatedBy.botState, 'pursuing');
  assert.equal(r.humans.find(h => h.player === 'h999999999999').left, true);
  assert.ok(r.recommendedSteps[0].includes('no end event'));
});

test('duplicates are dropped, gaps and malformed lines are reported, out-of-order is sorted', () => {
  const id = matchId();
  const lines = matchText(id, [start(), ['checkpoint', 30, { alive: 4 }], ['checkpoint', 60, { alive: 3 }], ['match_end', 90, { reason: 'endgame' }]]).trim().split('\n');
  const text = [lines[0], lines[2], lines[2], '{not json', JSON.stringify({ v: 1, match: id, seq: 9, id: 'wrong', t: 1, type: 'x' }), lines[3]].join('\n'); // seq 2 missing, seq 3 twice
  const m = parseMatch(text, `${id}.jsonl`);
  assert.deepEqual(m.events.map(e => e.seq), [1, 3, 4]);
  assert.equal(m.duplicates, 1);
  assert.equal(m.malformed, 2);
  assert.equal(m.missing, 1);
  assert.ok(m.issues.some(i => i.kind === 'missing_events'));
  const shuffled = parseMatch([lines[3], lines[0], lines[1], lines[2]].join('\n'), 'x');
  assert.deepEqual(shuffled.events.map(e => e.seq), [1, 2, 3, 4]);
  assert.equal(shuffled.outOfOrder, 3); // three lines arrived after a higher seq
});

test('schema validation and upgrades', () => {
  const id = matchId();
  const good = { v: 1, match: id, seq: 1, id: `${id}:1`, t: 0, type: 'landed', actor: bot(1), pos: [1, 2, 3] };
  assert.deepEqual(validateEvent(good), []);
  assert.ok(validateEvent({ ...good, v: 2 }).some(p => p.includes('schema')));
  assert.ok(validateEvent({ ...good, pos: [1, 2] }).includes('bad pos'));
  assert.ok(validateEvent({ ...good, actor: undefined }).some(p => p.includes('without actor')));
  assert.ok(validateEvent({ ...good, id: 'x' }).some(p => p.includes('id')));
});

test('wilson interval matches the hand-computed value', () => {
  const w = wilson(8, 10);
  assert.equal(w.rate, 0.8);
  assert.ok(Math.abs(w.low - 0.4902) < 0.001 && Math.abs(w.high - 0.9433) < 0.001);
  assert.equal(wilson(0, 0).rate, null);
});

test('weaknesses: storm, caught looting and unarmed deaths with evidence and hypotheses', () => {
  const matches = Array.from({ length: 6 }, () => wonMatch());
  const found = detectWeaknesses(matches, { minCount: 5 });
  const byKind = Object.fromEntries(found.map(f => [f.kind, f]));
  assert.equal(byKind.storm_deaths.encounters, 6);
  assert.equal(byKind.storm_deaths.sample, 18);
  assert.equal(byKind.caught_unprepared.encounters, 6); // the looting death in each match
  assert.equal(byKind.caught_unprepared.sample, 12);
  assert.equal(byKind.unarmed_deaths.encounters, 6);
  assert.equal(byKind.landing_misses.encounters, 6); // bot 2 lands 50 m from its target
  for (const f of found) {
    assert.ok(f.evidence.length > 0 && f.evidence.every(e => /:\d+$/.test(e)));
    assert.equal(f.hypothesis.label, 'hypothesis: not yet verified by a controlled test');
    assert.ok(f.experiment && f.successCriterion && f.testCase);
  }
  assert.equal(detectWeaknesses([wonMatch()], { minCount: 5 }).length, 0, 'one match is below the threshold');
});

test('landing intelligence: counts, percentages, contested rate, early survival, insufficient data', () => {
  const matches = Array.from({ length: 5 }, () => wonMatch());
  const out = landingIntelligence(matches, { minMatches: 5 }).versions['4.50'];
  assert.equal(out.matches, 5);
  assert.equal(out.landings, 20);
  const city = out.places.find(p => p.place === 'City_001');
  assert.equal(city.landings, 10);
  assert.equal(city.share.rate, 0.5);
  assert.equal(city.avgPerMatch, 2);
  assert.equal(city.contestedRate.rate, 1); // human + bot 1 (different teams) every match
  assert.equal(city.firstEngagementSeconds.mean, 20); // first landing 30 s, elimination 50 s
  assert.equal(city.earlyEliminationRate.rate, 0.5); // bot 1 dies 19 s after landing; the human survives
  assert.equal(city.humans, 5);
  assert.equal(city.sufficientData, true);
  const lake = out.places.find(p => p.place === 'Lake_004');
  assert.equal(lake.contestedRate.rate, 0);
  assert.equal(lake.survival.s180.rate, 0); // dies 165 s after landing
  const open = out.places.find(p => p.place === 'open:0,9');
  assert.equal(open.named, false);
  assert.equal(out.rankings.hottest[0], 'City_001');
  assert.ok(out.rankings.lowestContest.includes('Lake_004'));
  const strategy = Object.fromEntries(out.strategies.map(s => [s.strategy, s]));
  assert.equal(strategy.hot.earlyElimination.rate, 1);
  assert.equal(strategy.spread.survival180.rate, 1);
  const few = landingIntelligence(matches.slice(0, 2), { minMatches: 5 }).versions['4.50'];
  assert.equal(few.places.find(p => p.place === 'City_001').sufficientData, false);
  assert.match(few.places.find(p => p.place === 'City_001').explanation, /not enough data/);
  assert.deepEqual(landingIntelligence(matches, { minMatches: 5 }), landingIntelligence(matches, { minMatches: 5, now: Date.now() }), 'same input gives the same result');
});

test('landing trends separate recent and baseline windows', () => {
  const now = Date.parse('2026-10-27T12:00:00Z');
  const old = Array.from({ length: 5 }, (_, i) => wonMatch(matchId(), { opts: { startedAt: `2026-10-0${i + 1}T12:00:00.000Z` } }));
  const recent = Array.from({ length: 5 }, () => wonMatch());
  const t = landingIntelligence([...old, ...recent], { minMatches: 5, now }).versions['4.50'].trends;
  assert.equal(t.recentMatches, 5);
  assert.equal(t.baselineMatches, 5);
  assert.equal(t.places.find(p => p.place === 'City_001').label, 'stable');
});

test('playstyle needs enough matches before labelling', () => {
  const one = playstyleProfiles([wonMatch()]);
  assert.equal(one.profiles[0].sufficientData, false);
  assert.equal(one.profiles[0].labels.length, 0);
  const many = playstyleProfiles(Array.from({ length: 3 }, () => wonMatch()));
  assert.equal(many.profiles[0].matches, 3);
  assert.equal(many.profiles[0].observed.eliminations, 3);
  assert.ok(many.profiles[0].labels.some(l => l.label === 'close-range fighter'));
  assert.equal(many.howHumansBeatBots.byState.fighting, 3);
});

test('bot version comparison: sample gate and regression detection', () => {
  const a = Array.from({ length: 10 }, () => wonMatch(matchId(), { start: { botVersion: 'A' } }));
  const b = Array.from({ length: 10 }, () => wonMatch(matchId(), { start: { botVersion: 'B' } }));
  const same = compareBotVersions([...a, ...b], 'A', 'B');
  assert.equal(same.sampleOk, true);
  assert.equal(same.passed, true);
  assert.ok(same.checks.every(c => c.verdict === 'no significant change'));
  const small = compareBotVersions([...a, ...b.slice(0, 3)], 'A', 'B');
  assert.equal(small.passed, false);
  assert.match(small.decision, /Not enough matches/);
  // Candidate where every bot dies to the storm: regression on storm deaths.
  const bad = Array.from({ length: 10 }, () => {
    const id = matchId();
    return parseMatch(matchText(id, [start({ botVersion: 'C' }), ...[1, 2, 3].flatMap(n => [
      ['landed', 30 + n, { actor: bot(n), pos: [n, 0, 0], poi: 'City_001' }],
      ['elimination', 300 + n, { victim: bot(n), killer: null, cause: 'storm', pos: [n, 0, 0], poi: 'City_001' }]]),
      ['match_end', 400, { reason: 'endgame' }]]), id);
  });
  const reg = compareBotVersions([...a, ...bad], 'A', 'C');
  assert.equal(reg.passed, false);
  assert.equal(reg.checks.find(c => c.metric === 'stormDeathRate').verdict, 'regression');
});

test('training dataset: complete matches only, deterministic split, no identifying fields', () => {
  const matches = [...Array.from({ length: 6 }, () => wonMatch()), parseMatch(matchText(matchId(), [start(), ['landed', 1, { actor: bot(1), pos: [0, 0, 0] }]]), 'cut')];
  const ds = buildDataset(matches);
  assert.equal(ds.manifest.matches, 6);
  assert.equal(ds.landing.length, 24);
  assert.equal(ds.decision.length, 6);
  assert.deepEqual(validateDataset(ds), []);
  assert.equal(buildDataset(matches).manifest.id, ds.manifest.id, 'same input, same dataset version');
  const lake = ds.landing.find(r => r.place === 'Lake_004');
  assert.equal(lake.outcome.alive180, false);
  assert.equal(lake.outcome.earlyElimination, false); // died at 165 s, after the 120 s window
  const tampered = { ...ds, landing: [...ds.landing, { ...ds.landing[0], username: 'Edwin' }] };
  assert.ok(validateDataset(tampered).some(e => e.includes('identifying')));
});

test('files: load, filter, retention, deletion and storage limit', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'syndicate-telemetry-'));
  const ids = [matchId(), matchId(), matchId()];
  ids.forEach((id, i) => fs.writeFileSync(path.join(dir, `${id}.jsonl`), matchText(id, [start({ mode: i ? 'solo' : 'duo' }), ['match_end', 10, { reason: 'endgame' }]])));
  assert.equal(loadMatches(dir).length, 3);
  assert.equal(filterMatches(loadMatches(dir), { mode: 'duo' }).length, 1);
  assert.equal(filterMatches(loadMatches(dir), { outcome: 'endgame' }).length, 3);
  const old = path.join(dir, `${ids[0]}.jsonl`);
  fs.utimesSync(old, new Date('2026-01-01'), new Date('2026-01-01'));
  const removed = applyRetention(dir, { days: 30, now: Date.parse('2026-10-27') });
  assert.deepEqual(removed.map(r => r.file), [`${ids[0]}.jsonl`]);
  const limited = applyRetention(dir, { days: 3650, maxBytes: 1, now: Date.parse('2026-10-27') });
  assert.equal(limited.length, 2);
  fs.writeFileSync(path.join(dir, `${ids[1]}.jsonl`), matchText(ids[1], [start()]));
  assert.equal(deleteMatches(dir, ids[1]), 1);
  assert.equal(loadMatches(dir).length, 0);
  fs.rmSync(dir, { recursive: true, force: true });
});

test('large match: 50k events parse quickly and stay bounded', () => {
  const id = matchId();
  const events = [start()];
  for (let i = 0; i < 50000; i++) events.push(['bot_state', i / 10, { bot: `b${i % 100}`, from: 'roaming', to: 'looting', held: 1, flips: 0 }]);
  const t0 = Date.now();
  const m = parseMatch(matchText(id, events), id);
  assert.equal(m.events.length, 50001);
  assert.ok(Date.now() - t0 < 5000);
  assert.ok(matchReport(m).decisions.timeShare.roaming === 1);
});

test('upload: on by default, can be turned off, sends once, retries with backoff, strips pseudonyms', async () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'syndicate-upload-'));
  const m = wonMatch();
  const calls = [];
  let fail = true, clock = 1_000_000;
  const fetchImpl = async (url, init) => { calls.push(JSON.parse(init.body)); if (fail) throw new Error('offline'); return { ok: true, status: 200, text: async () => 'ok' }; };
  const up = createUploader(dir, { fetchImpl, now: () => clock, env: {} });
  assert.equal(readSettings(dir).upload, true, 'shared unless the player turns it off');
  writeSettings(dir, { upload: false });
  assert.equal((await up.tick([m])).skipped, 'upload off');
  writeSettings(dir, { upload: true });
  const r1 = await up.tick([m]);
  assert.equal(r1.sent, false);
  assert.match(r1.error, /offline/);
  clock += 1000;
  assert.equal((await up.tick([m])).skipped, 'rate limit');
  clock += 200000; fail = false;
  const r2 = await up.tick([m]);
  assert.equal(r2.sent, true);
  clock += 200000;
  assert.equal((await up.tick([m])).skipped, 'nothing pending', 'never sent twice');
  assert.equal(calls.length, 2);
  for (const line of calls[1].lines) { assert.ok(line.length <= 600); assert.ok(!line.includes(H)); }
  assert.equal(calls[1].source, 'tilted', 'the relay only accepts reports marked as Syndicate');
  assert.ok(calls[1].lines.length <= 10);
  assert.ok(relayLines(m)[0].startsWith('[syndicate] match'));
  // Bad settings values are ignored.
  assert.equal(writeSettings(dir, { retentionDays: -5, upload: 'yes' }).retentionDays, 30);
  fs.rmSync(dir, { recursive: true, force: true });
});

test('access: only this PC, only with the developer token', async () => {
  const { authorize } = await import('../access.js');
  const token = 'a'.repeat(48);
  assert.equal(authorize({ devkit: true, address: '127.0.0.1', header: token }, token).ok, true);
  assert.equal(authorize({ devkit: true, address: '192.168.1.20', header: token }, token).status, 403);
  assert.equal(authorize({ devkit: true, address: '::1', origin: 'http://evil.example', header: token }, token).status, 403);
  assert.equal(authorize({ devkit: true, address: '::1', header: 'b'.repeat(48) }, token).status, 401);
  assert.equal(authorize({ devkit: true, address: '::1' }, token).status, 401);
  assert.equal(authorize({ devkit: true, address: '::1', header: '' }, '').status, 401, 'an empty token never matches');
  // Public builds: everything is unavailable, even from this PC with the right token.
  assert.equal(authorize({ devkit: false, address: '127.0.0.1', header: token }, token).status, 404);
  assert.equal(authorize({ address: '127.0.0.1', header: token }, token).ok, false);
});

test('discord delivery: webhook path, relay path, errors and secret redaction', async () => {
  const { deliver, redact, webhookConfig, discordContent, testLines } = await import('../discord.js');
  // A made-up webhook in the right shape (never a real one in tests).
  const fake = 'https://discord.com/api/webhooks/123456789012345678/' + 'T'.repeat(68);
  const env = { TILTED_DISCORD_WEBHOOK: fake };
  const reply = (status, body, text) => async () => ({ ok: status >= 200 && status < 300, status, json: async () => body, text: async () => text ?? JSON.stringify(body ?? '') });
  let seen = null;
  const ok = await deliver(testLines(), { env, fetchImpl: async (url, init) => { seen = { url, body: JSON.parse(init.body) }; return reply(200, { id: '999' })(); } });
  assert.equal(ok.ok, true); assert.equal(ok.via, 'webhook'); assert.match(ok.message, /accepted.*999/);
  assert.ok(seen.url.endsWith('?wait=true'), 'waits for Discord to confirm');
  assert.deepEqual(seen.body.allowed_mentions, { parse: [] });
  assert.match(seen.body.content, /TEST MESSAGE/);
  const limited = await deliver(['x'], { env, fetchImpl: reply(429, { retry_after: 2.4 }) });
  assert.equal(limited.ok, false); assert.match(limited.message, /rate limit.*3 s/);
  const gone = await deliver(['x'], { env, fetchImpl: reply(404, { message: 'Unknown Webhook' }) });
  assert.equal(gone.ok, false); assert.match(gone.message, /rejected the webhook/);
  const down = await deliver(['x'], { env, fetchImpl: reply(503, {}) });
  assert.match(down.message, /server error/);
  // Errors that echo the URL never leak it.
  const leaky = await deliver(['x'], { env, fetchImpl: async () => { throw new Error('connect ETIMEDOUT ' + fake); } });
  assert.equal(leaky.ok, false); assert.ok(!leaky.message.includes('T'.repeat(68)) && !leaky.message.includes('/api/webhooks/123'));
  for (const r of [ok, limited, gone, down, leaky]) assert.ok(!JSON.stringify(r).includes('T'.repeat(68)));
  assert.equal(webhookConfig({ TILTED_DISCORD_WEBHOOK: 'https://evil.example/hook' }).kind, 'invalid');
  const bad = await deliver(['x'], { env: { TILTED_DISCORD_WEBHOOK: 'not a url' }, fetchImpl: async () => { throw new Error('must not be called'); } });
  assert.equal(bad.ok, false); assert.match(bad.message, /not a Discord webhook URL/);
  // No webhook configured: the Cloudflare relay, success only on its "ok".
  const relay = await deliver(['x'], { env: {}, fetchImpl: async (url) => { assert.match(url, /workers\.dev\/report$/); return reply(200, null, 'ok')(); } });
  assert.equal(relay.ok, true); assert.equal(relay.via, 'relay');
  assert.match((await deliver(['x'], { env: {}, fetchImpl: reply(429, null, 'Slow down') })).message, /one message per minute/);
  assert.match((await deliver(['x'], { env: {}, fetchImpl: reply(503, null, 'Relay not configured') })).message, /no webhook configured/);
  assert.match((await deliver(['x'], { env: {}, fetchImpl: reply(502, null, 'discord error') })).message, /outdated/);
  assert.equal((await deliver(['x'], { env: {}, fetchImpl: reply(200, null, 'something else') })).ok, false, '200 without ok is not success');
  assert.equal(redact('see ' + fake, env), 'see [webhook redacted]');
  assert.ok(!discordContent(['@everyone `x`']).includes('@'));
});
