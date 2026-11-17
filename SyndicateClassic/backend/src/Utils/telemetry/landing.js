// Hot-drop and landing intelligence. Per named location (POI) and per 100 m cell: how many players
// land there, how often it is contested, how soon the first fight happens, and who survives.
//
// Definitions (also in SCHEMA.md):
//   landing            first time a player is on the ground after the bus (server observed)
//   place              nearest named location within 90 m, otherwise "open:<cell>"
//   contested match    a match where 2+ players from different teams landed at the place
//   hot                average landers per match >= hotLanders (default 4) or contested in >= 60% of matches
//   early window       the first `earlyWindow` seconds after landing (default 120)
//   early elimination  eliminated within the early window
//   survival N         alive N seconds after landing (players who left or whose file ended earlier are excluded)
//   first engagement   first knock or elimination at the place after the first landing there
// Map layouts differ per Fortnite version, so every number is computed per version.
import { matchFacts, survivedFor, nearbyOpponents } from './facts.js';
import { wilson, mean, median, confidence, compareRates, round } from './stats.js';

const DEFAULTS = { hotLanders: 4, hotContested: 0.6, earlyWindow: 120, minMatches: 5, recentDays: 7, now: Date.now() };

export function landingIntelligence(matches, options = {}) {
  const opts = { ...DEFAULTS, ...options };
  const byVersion = {};
  for (const m of matches) if (m.fortniteVersion) (byVersion[m.fortniteVersion] ||= []).push(m);
  const versions = {};
  for (const [version, list] of Object.entries(byVersion)) versions[version] = analyseVersion(list, opts);
  return { definitions: { ...opts, now: undefined }, versions };
}

function analyseVersion(matches, opts) {
  const facts = matches.map(matchFacts).filter(f => f.landings.length);
  const totalMatches = facts.length;
  const places = new Map();
  const cells = new Map();
  const strategies = new Map();
  const landerRows = []; // one row per landing, for prediction evaluation and datasets
  let totalLandings = 0;

  for (const f of facts) {
    const perPlace = new Map();
    for (const l of f.landings) (perPlace.get(l.place) || perPlace.set(l.place, []).get(l.place)).push(l);
    for (const [name, list] of perPlace) {
      if (!places.has(name)) places.set(name, { name, landings: 0, humans: 0, bots: 0, perMatch: [], contestedMatches: 0, opponents: [], firstEngagement: [], earlyElims: [], s60: [0, 0], s90: [0, 0], s180: [0, 0], early: [0, 0], matches: new Set(), evidence: [] });
      const P = places.get(name);
      P.landings += list.length; totalLandings += list.length; P.perMatch.push(list.length); P.matches.add(f.match.id);
      const teams = new Set(list.map(l => f.players.get(l.player)?.team ?? l.player));
      if (teams.size >= 2) P.contestedMatches++;
      const firstLanding = Math.min(...list.map(l => l.t));
      const fights = [...f.downs, ...f.eliminations].filter(e => (e.poi ? e.poi : e.grid ? `open:${e.grid}` : '') === name && e.t >= firstLanding).sort((a, b) => a.t - b.t);
      if (fights.length) P.firstEngagement.push(fights[0].t - firstLanding);
      let earlyElims = 0;
      for (const l of list) {
        const p = f.players.get(l.player);
        if (p.kind === 'human') P.humans++; else P.bots++;
        P.opponents.push(nearbyOpponents(l, f));
        P.evidence.length < 10 && P.evidence.push(l.event);
        for (const [key, secs] of [['s60', 60], ['s90', 90], ['s180', 180]]) { const s = survivedFor(p, f, secs); if (s != null) { P[key][1]++; if (s) P[key][0]++; } }
        const early = survivedFor(p, f, opts.earlyWindow);
        if (early != null) { P.early[1]++; if (!early) { P.early[0]++; earlyElims++; } }
        const cell = l.grid || 'unknown';
        if (!cells.has(cell)) cells.set(cell, { cell, landings: 0, earlyDeaths: 0, survived: 0 });
        const C = cells.get(cell); C.landings++; if (early === false) C.earlyDeaths++; if (early === true) C.survived++;
        if (p.kind === 'bot' && l.strategy) {
          if (!strategies.has(l.strategy)) strategies.set(l.strategy, { strategy: l.strategy, landings: 0, s180: [0, 0], early: [0, 0], places: [], kills: 0 });
          const S = strategies.get(l.strategy); S.landings++; S.kills += p.kills;
          const s = survivedFor(p, f, 180); if (s != null) { S.s180[1]++; if (s) S.s180[0]++; }
          if (early != null) { S.early[1]++; if (!early) S.early[0]++; }
          if (p.place != null) S.places.push(p.place);
        }
        landerRows.push({ match: f.match.id, startedAt: f.match.startedAt, place: name, kind: p.kind, survived180: survivedFor(p, f, 180) });
      }
      P.earlyElims.push(earlyElims);
    }
  }

  const rows = [...places.values()].map(P => {
    const zeros = totalMatches - P.perMatch.length; // matches where nobody landed there
    const perMatchAll = [...P.perMatch, ...Array(Math.max(0, zeros)).fill(0)];
    const contested = wilson(P.contestedMatches, totalMatches);
    const row = {
      place: P.name, named: !P.name.startsWith('open:'),
      landings: P.landings, humans: P.humans, bots: P.bots,
      share: wilson(P.landings, totalLandings),
      avgPerMatch: mean(perMatchAll), medianPerMatch: median(perMatchAll),
      matchesObserved: P.matches.size, totalMatches,
      contestedRate: contested,
      avgOpponents: mean(P.opponents),
      firstEngagementSeconds: { mean: mean(P.firstEngagement), median: median(P.firstEngagement), n: P.firstEngagement.length },
      earlyEliminationsPerMatch: mean(P.earlyElims),
      earlyEliminationRate: wilson(P.early[0], P.early[1]),
      earlySurvivalRate: wilson(P.early[1] - P.early[0], P.early[1]),
      survival: { s60: wilson(...P.s60), s90: wilson(...P.s90), s180: wilson(...P.s180) },
      notMeasured: ['loot quality', 'distance to next safe zone at landing'],
      evidence: P.evidence,
    };
    row.sufficientData = P.matches.size >= opts.minMatches;
    row.confidence = confidence(P.matches.size, contested);
    row.hot = row.sufficientData && ((row.avgPerMatch ?? 0) >= opts.hotLanders || (contested.rate ?? 0) >= opts.hotContested);
    // Strategic value (0-1): survival 180 s, then how often it is uncontested, then fight opportunity.
    // Loot is not measured, so it does not appear here.
    const s180 = row.survival.s180.rate, unc = contested.rate != null ? 1 - contested.rate : null;
    row.strategicValue = s180 != null && unc != null ? round(0.6 * s180 + 0.25 * unc + 0.15 * Math.min(1, (row.avgOpponents ?? 0) / 4)) : null;
    // Plain-language explanation of the ranking, from the numbers above.
    row.explanation = row.sufficientData
      ? `${row.landings} landings in ${P.matches.size}/${totalMatches} matches; avg ${row.avgPerMatch} per match; contested in ${pct(contested.rate)} of matches; early elimination ${pct(row.earlyEliminationRate.rate)}; survives 180 s ${pct(s180)}.`
      : `Only ${P.matches.size} match(es) observed (need ${opts.minMatches}): not enough data to rank. This is a sample-size problem, not proof of low activity.`;
    return row;
  });

  const ranked = rows.filter(r => r.sufficientData && r.named);
  const by = (key, dir = -1) => [...ranked].filter(r => key(r) != null).sort((a, b) => dir * (key(a) - key(b)));
  const rankings = {
    hottest: by(r => r.avgPerMatch).slice(0, 10).map(r => r.place),
    lowestContest: by(r => r.contestedRate.rate, 1).slice(0, 10).map(r => r.place),
    deadliest: by(r => r.earlyEliminationRate.rate).slice(0, 10).map(r => r.place),
    safest: by(r => r.survival.s180.rate).slice(0, 10).map(r => r.place),
    bestStrategicValue: by(r => r.strategicValue).slice(0, 10).map(r => r.place),
    leastPopular: by(r => r.avgPerMatch, 1).slice(0, 10).map(r => r.place),
  };

  const strategyRows = [...strategies.values()].map(S => ({
    strategy: S.strategy, landings: S.landings, survival180: wilson(...S.s180), earlyElimination: wilson(...S.early),
    killsPerLanding: S.landings ? round(S.kills / S.landings) : null, averagePlace: mean(S.places),
  }));

  return {
    matches: totalMatches, landings: totalLandings,
    places: rows.sort((a, b) => (b.avgPerMatch ?? 0) - (a.avgPerMatch ?? 0)),
    rankings, heatmap: [...cells.values()].sort((a, b) => b.landings - a.landings),
    strategies: strategyRows, trends: trends(facts, opts), prediction: evaluatePrediction(landerRows),
  };
}

const pct = (r) => (r == null ? '?' : `${Math.round(r * 100)}%`);

// Recent window vs the longer baseline: which places gained or lost share of landings.
function trends(facts, opts) {
  const cutoff = new Date(opts.now - opts.recentDays * 86400000).toISOString();
  const recent = facts.filter(f => (f.match.startedAt || '') >= cutoff), base = facts.filter(f => (f.match.startedAt || '') < cutoff);
  const count = (list) => { const c = new Map(); let n = 0; for (const f of list) for (const l of f.landings) { c.set(l.place, (c.get(l.place) || 0) + 1); n++; } return { c, n }; };
  const R = count(recent), B = count(base);
  const out = [];
  for (const place of new Set([...R.c.keys(), ...B.c.keys()])) {
    const kr = R.c.get(place) || 0, kb = B.c.get(place) || 0;
    const test = compareRates(kb, B.n, kr, R.n);
    out.push({ place, recentShare: wilson(kr, R.n), baselineShare: wilson(kb, B.n), change: R.n && B.n ? round(kr / R.n - kb / B.n) : null, p: test.p,
      label: recent.length < opts.minMatches || base.length < opts.minMatches ? 'insufficient data' : test.p != null && test.p < 0.05 ? (kr / R.n > kb / B.n ? 'rising' : 'falling') : 'stable' });
  }
  return { recentMatches: recent.length, baselineMatches: base.length, places: out.sort((a, b) => Math.abs(b.change ?? 0) - Math.abs(a.change ?? 0)).slice(0, 20) };
}

// Holdout check of the simplest predictor (a place's past 180 s survival rate): train on the older
// 70% of matches, score on the newest 30%, against a baseline that ignores the place.
function evaluatePrediction(rows) {
  const known = rows.filter(r => r.survived180 != null);
  const matchOrder = [...new Set(known.map(r => r.match + '|' + r.startedAt))].sort((a, b) => a.split('|')[1].localeCompare(b.split('|')[1])).map(x => x.split('|')[0]);
  if (matchOrder.length < 10) return { status: 'insufficient data', matches: matchOrder.length, needed: 10 };
  const cut = Math.floor(matchOrder.length * 0.7), train = new Set(matchOrder.slice(0, cut));
  const tr = known.filter(r => train.has(r.match)), te = known.filter(r => !train.has(r.match));
  const global = tr.filter(r => r.survived180).length / tr.length;
  const perPlace = new Map();
  for (const r of tr) { const c = perPlace.get(r.place) || [0, 0]; c[1]++; if (r.survived180) c[0]++; perPlace.set(r.place, c); }
  const predict = (place) => { const c = perPlace.get(place); return c && c[1] >= 5 ? (c[0] + 1) / (c[1] + 2) : global; };
  const brier = te.reduce((s, r) => s + (predict(r.place) - (r.survived180 ? 1 : 0)) ** 2, 0) / te.length;
  const base = te.reduce((s, r) => s + (global - (r.survived180 ? 1 : 0)) ** 2, 0) / te.length;
  return { status: 'ok', trainMatches: cut, testMatches: matchOrder.length - cut, testLandings: te.length, brier: round(brier), baselineBrier: round(base), better: brier < base };
}
