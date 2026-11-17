// Recurring bot failures across matches. Each finding keeps observed facts (counts, rates, event
// ids) apart from the hypothesis (why it might happen) and says how to test a fix. A pattern is
// a correlation until a controlled benchmark (benchmark.js) shows a change helps.
import { matchFacts } from './facts.js';
import { wilson, confidence, round } from './stats.js';

const DEFAULTS = { minCount: 5, maxEvidence: 20 };

function severityOf(rate, n) {
  if (rate == null || n < 3) return 'info';
  if (rate >= 0.3 && n >= 10) return 'high';
  if (rate >= 0.15) return 'medium';
  return 'low';
}

// Events in the same match within `seconds` before the failure that involve the same bot/player.
function leadUp(match, event, actorId, seconds = 20) {
  return match.events.filter(e => e.t <= event.t && e.t >= event.t - seconds && e.seq <= event.seq &&
    (e.bot === actorId || e.actor?.id === actorId || e.victim?.id === actorId || e.killer?.id === actorId))
    .slice(-12).map(e => ({ id: e.id, t: e.t, type: e.type, summary: summarize(e) }));
}

export function summarize(e) {
  switch (e.type) {
    case 'landed': return `${e.actor?.id} landed at ${e.poi || e.grid}${e.strategy ? ` (${e.strategy})` : ''}`;
    case 'elimination': return `${e.victim?.id} eliminated by ${e.killer?.id ?? e.cause}${e.weapon ? ` with ${e.weapon}` : ''}${e.distance != null ? ` at ${Math.round(e.distance / 100)} m` : ''}${e.victimState ? ` while ${e.victimState}` : ''}`;
    case 'downed': return `${e.victim?.id} knocked by ${e.killer?.id ?? e.cause}`;
    case 'bot_state': return `${e.bot}: ${e.from} -> ${e.to}${e.enemyDist != null ? ` (enemy ${Math.round(e.enemyDist / 100)} m)` : ''}${e.flips ? `, ${e.flips} quick flips` : ''}`;
    case 'bot_stuck': return `${e.bot} stuck at ${e.poi || e.grid} (${e.recovery})`;
    case 'storm_phase': return `storm ${e.from} -> ${e.to}, ${e.alive} alive`;
    case 'player_left': return `${e.actor?.id} left`;
    case 'match_end': return `match end: ${e.reason}`;
    case 'match_start': return `match start: ${e.fortniteVersion} ${e.mode}, ${e.bots} bots, ${e.humans} humans`;
    default: return e.type;
  }
}

function finding({ kind, title, k, n, matches, evidence, example, hypothesis, alternatives, experiment, testCase, success, botVersions, extra = {} }, opts) {
  const rate = wilson(k, n);
  return {
    id: kind, kind, title,
    severity: severityOf(rate.rate, k), confidence: confidence(n, rate),
    botVersions: [...botVersions], encounters: k, sample: n, rate,
    observed: extra.observed ?? `${k} of ${n} (${rate.rate != null ? Math.round(rate.rate * 100) : '?'}%, 95% CI ${rate.low != null ? Math.round(rate.low * 100) : '?'}-${rate.high != null ? Math.round(rate.high * 100) : '?'}%)`,
    matches: [...matches].slice(0, opts.maxEvidence), evidence: evidence.slice(0, opts.maxEvidence),
    timeline: example ?? [],
    hypothesis: { cause: hypothesis, alternatives, label: 'hypothesis: not yet verified by a controlled test' },
    experiment, testCase, successCriterion: success, ...extra.more,
  };
}

export function detectWeaknesses(matches, options = {}) {
  const opts = { ...DEFAULTS, ...options };
  const all = matches.map(matchFacts);
  const findings = [];
  const botDeaths = [];
  for (const f of all) for (const p of f.players.values()) if (p.kind === 'bot' && p.diedAt != null) botDeaths.push({ f, p });
  const versions = new Set(all.map(f => f.match.botVersion).filter(Boolean));
  const ofMatch = (list) => new Set(list.map(x => x.f.match.id));
  const evidenceOf = (list, key = 'deathEvent') => list.map(x => x.p[key]).filter(Boolean);
  const exampleOf = (list) => { const x = list[0]; if (!x) return []; const ev = x.f.match.events.find(e => e.id === x.p.deathEvent); return ev ? leadUp(x.f.match, ev, x.p.id) : []; };

  const add = (spec) => { if (spec.k >= opts.minCount) findings.push(finding({ botVersions: versions, ...spec }, opts)); };

  // 1. Storm deaths
  const storm = botDeaths.filter(x => x.p.deathCause === 'storm');
  add({ kind: 'storm_deaths', title: 'Bots die to the storm', k: storm.length, n: botDeaths.length, matches: ofMatch(storm), evidence: evidenceOf(storm), example: exampleOf(storm),
    hypothesis: 'Rotation starts too late or the route to the safe zone is blocked (stuck / long detours).',
    alternatives: ['Bots spawned or landed far from the first circle', 'Bots stuck in buildings while the storm closed', 'Storm damage tuning differs on this build'],
    experiment: 'Start rotating earlier (more time margin per storm phase) in a candidate bot version and compare.',
    testCase: 'Replay matches with the circle far from the bus path; count bots outside the zone when each phase closes.',
    success: 'Storm share of bot deaths drops by half in a benchmark of at least 20 matches per version, with no rise in early fight deaths.' });

  // 2. Caught looting / harvesting
  const combat = botDeaths.filter(x => x.p.deathCause === 'weapon');
  const busy = combat.filter(x => ['looting', 'harvesting', 'farming'].includes(x.p.deathState));
  add({ kind: 'caught_unprepared', title: 'Bots are killed while looting, farming or harvesting', k: busy.length, n: combat.length, matches: ofMatch(busy), evidence: evidenceOf(busy), example: exampleOf(busy),
    hypothesis: 'Bots do not react to nearby threats while their loot/harvest routine runs (no interrupt on hearing or seeing an enemy).',
    alternatives: ['Opponents engage from outside the bots\' sight range', 'Early-game deaths at hot drops where everyone is looting'],
    experiment: 'Interrupt looting/harvesting when an enemy is within sight or deals damage; switch to fighting or cover.',
    testCase: 'Scenario: human approaches a looting bot from behind at 20-40 m. Measure reaction time and survival.',
    success: 'Share of combat deaths while looting/harvesting falls by 30% across 20+ matches per version.' });

  // 3. No gun in a fight
  const unarmed = combat.filter(x => x.p.hadGun === false);
  add({ kind: 'unarmed_deaths', title: 'Bots die without a gun', k: unarmed.length, n: combat.length, matches: ofMatch(unarmed), evidence: evidenceOf(unarmed), example: exampleOf(unarmed),
    hypothesis: 'Landing spot loot is contested or the loot target is too far, so bots fight with the pickaxe.',
    alternatives: ['Bots land at spots with few chests', 'Weapon pickup failures on this build'],
    experiment: 'Prefer landing spots with a chest within reach; avoid engaging before a gun is found.',
    testCase: 'Count bots without a gun 60 s after landing per POI.', success: 'Unarmed deaths fall below 10% of combat deaths.' });

  // 4. Pursuit deaths
  const chase = combat.filter(x => x.p.deathState === 'pursuing');
  add({ kind: 'pursuit_deaths', title: 'Bots die while chasing', k: chase.length, n: combat.length, matches: ofMatch(chase), evidence: evidenceOf(chase), example: exampleOf(chase),
    hypothesis: 'Bots keep pursuing when disengaging is safer (low health, third parties, opponent on high ground).',
    alternatives: ['Pursuit is correct but aim/build fights are lost', 'Humans bait pursuit into traps'],
    experiment: 'Stop pursuing below a health/shield threshold or when a second enemy is visible.',
    testCase: 'Scenario: bot at 50 HP chases a human who retreats toward a teammate.', success: 'Pursuit-state deaths drop without lowering bot kills per match.' });

  // 5. Low accuracy before dying in a fight
  const shooters = combat.filter(x => (x.p.shots ?? 0) >= 10);
  const missers = shooters.filter(x => (x.p.hits ?? 0) / x.p.shots < 0.15);
  add({ kind: 'low_accuracy', title: 'Bots that died had very low accuracy', k: missers.length, n: shooters.length, matches: ofMatch(missers), evidence: evidenceOf(missers), example: exampleOf(missers),
    hypothesis: 'Aim spread or target leading is off for moving/building opponents.',
    alternatives: ['Long-range fights with weapons that are inaccurate by design', 'Skill setting intentionally low'],
    experiment: 'Tune aim error per skill level; measure hits per shot against moving targets.',
    testCase: 'Scenario: human strafes at 15-30 m; record bot hit rate per weapon.', success: 'Median hit rate of bots in fights rises to 20%+ at the same skill.' });

  // 6. Stuck hotspots: the same 100 m cell in several matches
  const cells = new Map();
  for (const f of all) for (const e of f.stuck) { const key = `${f.match.fortniteVersion}|${e.poi || e.grid}`; if (!cells.has(key)) cells.set(key, { events: [], matches: new Set() }); const c = cells.get(key); c.events.push(e); c.matches.add(f.match.id); }
  const totalStuck = all.reduce((s, f) => s + f.stuck.length, 0);
  for (const [key, c] of cells) {
    if (c.events.length < Math.max(3, opts.minCount) || c.matches.size < 2) continue;
    const [version, where] = key.split('|');
    const first = c.events[0], m = all.find(f => f.match.id === first.match)?.match;
    findings.push(finding({ kind: `stuck_hotspot:${key}`, title: `Bots get stuck at ${where} (${version})`, k: c.events.length, n: totalStuck, matches: c.matches, evidence: c.events.map(e => e.id),
      example: m ? leadUp(m, first, first.bot) : [], botVersions: versions,
      hypothesis: 'Navigation fails at a specific piece of geometry (stairs, fences, doors, roof edges) at this spot.',
      alternatives: ['Many bots land here, so stuck events are frequent but not unusual per bot', 'Building pieces placed by players block the route'],
      experiment: 'Inspect the positions; add a detour/escape rule or mark the spot as costly in the bot experience map.',
      testCase: `Spawn a bot at ${first.pos?.join(', ')} heading to its next target and check it leaves within 6 s.`, success: 'No stuck events at this cell in the next 10 matches that reach it.',
      extra: { observed: `${c.events.length} stuck events in ${c.matches.size} matches at ${where}` } }, opts));
  }

  // 7. Decision oscillation (quick state flips)
  const flips = [];
  for (const f of all) for (const e of f.states) if ((e.flips || 0) >= 3) flips.push({ f, e });
  const stateChanges = all.reduce((s, f) => s + f.states.length, 0);
  if (flips.length >= opts.minCount) {
    const pairs = {};
    for (const { e } of flips) { const k = `${e.from}->${e.to}`; pairs[k] = (pairs[k] || 0) + 1; }
    const top = Object.entries(pairs).sort((a, b) => b[1] - a[1]).slice(0, 3).map(([k, v]) => `${k} (${v})`).join(', ');
    const x = flips[0];
    findings.push(finding({ kind: 'decision_oscillation', title: 'Bots flip between activities rapidly', k: flips.length, n: stateChanges, matches: new Set(flips.map(x => x.f.match.id)), evidence: flips.map(x => x.e.id),
      example: leadUp(x.f.match, x.e, x.e.bot), botVersions: versions,
      hypothesis: `Two behaviours keep overriding each other without hysteresis; most common: ${top}.`,
      alternatives: ['An enemy at the edge of sight range toggles visibility', 'Loot targets that become unreachable and reachable again'],
      experiment: 'Add a minimum time before switching back to the previous activity (hysteresis) for the top pairs.',
      testCase: 'Scenario: enemy standing at the bot\'s sight-range edge; count state changes per minute.', success: 'Quick flips per bot-minute fall by 50% with equal or better survival.',
      extra: { observed: `${flips.length} activity switches came after 3+ quick flips (top: ${top})` } }, opts));
  }

  // 8. Landing far from the intended spot
  const botLandings = [];
  for (const f of all) for (const p of f.players.values()) if (p.kind === 'bot' && p.landedAt != null && Number.isFinite(p.targetError)) botLandings.push({ f, p });
  const missed = botLandings.filter(x => x.p.targetError > 3000);
  add({ kind: 'landing_misses', title: 'Bots land far from their chosen spot', k: missed.length, n: botLandings.length, matches: ofMatch(missed), evidence: evidenceOf(missed, 'landEvent'), example: [],
    hypothesis: 'Jump timing or glide steering cannot reach the target from the bus path.',
    alternatives: ['Targets chosen too far from the bus line', 'Glider redeploy rules on this build'],
    experiment: 'Pick reachable targets (distance from bus path within glide range) or jump earlier.',
    testCase: 'For each bus path, check the share of bots within 30 m of their target.', success: '90% of bots land within 30 m of their target.' });

  // 9. Killed at range without fighting back
  const ranged = combat.filter(x => x.p.killedBy === 'human' && (x.p.deathDistance ?? 0) > 5000);
  const passive = ranged.filter(x => x.p.deathState && x.p.deathState !== 'fighting');
  add({ kind: 'no_response_at_range', title: 'Bots killed from range without engaging', k: passive.length, n: ranged.length, matches: ofMatch(passive), evidence: evidenceOf(passive), example: exampleOf(passive),
    hypothesis: 'Bots do not detect or respond to damage from opponents beyond their sight range.',
    alternatives: ['Snipers one-shot bots before any reaction is possible', 'Deaths during rotation in the open'],
    experiment: 'React to incoming damage (take cover, build, or turn and fight) even when the shooter is not visible.',
    testCase: 'Scenario: human shoots a roaming bot from 80 m; measure time until the bot builds or moves to cover.', success: 'Bots respond within 1.5 s in 80% of ranged ambushes.' });

  const order = { high: 0, medium: 1, low: 2, info: 3 };
  return findings.sort((a, b) => order[a.severity] - order[b.severity] || b.encounters - a.encounters);
}
