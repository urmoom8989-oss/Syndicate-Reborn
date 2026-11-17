// Bot version comparison and the release gate. A candidate bot version is compared with the
// current one on recorded matches of the same Fortnite version and mode; it passes only when every
// guarded metric is not significantly worse and both sides have enough matches.
// Limitation: Syndicate has no headless match simulator yet, so "same scenario, same seed" runs are
// not possible; comparisons use real recorded matches, which differ in players and circles.
import { matchFacts } from './facts.js';
import { wilson, mean, compareRates, round } from './stats.js';

export const GATE = { minMatches: 10, alpha: 0.05 };

export function versionMetrics(matches) {
  let botLandings = 0, botDeaths = 0, storm = 0, busyDeaths = 0, combatDeaths = 0, stuck = 0, botSeconds = 0, shots = 0, hits = 0, botKills = 0, flips = 0, humanWins = 0, ended = 0;
  const survival = [], places = [], stateSet = new Map();
  for (const m of matches) {
    const f = matchFacts(m);
    if (m.end?.winnerKind) { ended++; if (m.end.winnerKind === 'human' || m.end.winnerKind === 'mixed') humanWins++; }
    stuck += f.stuck.length;
    for (const e of f.states) stateSet.set(e.to, (stateSet.get(e.to) || 0) + 1);
    for (const p of f.players.values()) {
      if (p.kind !== 'bot') continue;
      flips += p.flips; botKills += p.kills;
      if (p.landedAt == null) continue;
      botLandings++;
      const endT = p.diedAt ?? f.lastT; botSeconds += Math.max(0, endT - p.landedAt);
      if (p.diedAt != null) {
        botDeaths++; survival.push(p.diedAt - p.landedAt); if (p.place != null) places.push(p.place);
        if (p.deathCause === 'storm') storm++;
        if (p.deathCause === 'weapon') { combatDeaths++; if (['looting', 'harvesting', 'farming'].includes(p.deathState)) busyDeaths++; }
        shots += p.shots || 0; hits += p.hits || 0;
      }
    }
  }
  const total = [...stateSet.values()].reduce((a, b) => a + b, 0);
  const entropy = total ? -[...stateSet.values()].reduce((s, c) => s + (c / total) * Math.log2(c / total), 0) : null;
  const minutes = botSeconds / 60;
  return {
    matches: matches.length, botLandings, botDeaths,
    stormDeathRate: wilson(storm, botDeaths), caughtLootingRate: wilson(busyDeaths, combatDeaths),
    humanWinRate: wilson(humanWins, ended), accuracy: wilson(hits, shots),
    averageBotSurvivalSeconds: mean(survival), averageBotPlace: mean(places),
    stuckPerBotMinute: minutes ? round(stuck / minutes) : null, quickFlipsPerBotMinute: minutes ? round(flips / minutes) : null,
    killsPerBot: botLandings ? round(botKills / botLandings) : null,
    behaviourDiversityBits: round(entropy), // entropy of activity switches: higher = more varied behaviour
    _raw: { storm, botDeaths, busyDeaths, combatDeaths, humanWins, ended, hits, shots },
  };
}

// Compares baseline vs candidate bot versions. Lower is better for storm deaths, caught looting and
// human win rate (bots should win more); higher is better for accuracy.
export function compareBotVersions(matches, baseline, candidate, gate = GATE) {
  const A = matches.filter(m => m.botVersion === baseline), B = matches.filter(m => m.botVersion === candidate);
  const a = versionMetrics(A), b = versionMetrics(B);
  const checks = [
    ['stormDeathRate', 'storm', 'botDeaths', 'lower'],
    ['caughtLootingRate', 'busyDeaths', 'combatDeaths', 'lower'],
    ['humanWinRate', 'humanWins', 'ended', 'lower'],
    ['accuracy', 'hits', 'shots', 'higher'],
  ].map(([metric, k, n, better]) => {
    const t = compareRates(a._raw[k], a._raw[n], b._raw[k], b._raw[n]);
    const diff = a[metric].rate != null && b[metric].rate != null ? round(b[metric].rate - a[metric].rate) : null;
    const worse = t.p != null && t.p < gate.alpha && diff != null && (better === 'lower' ? diff > 0 : diff < 0);
    const improved = t.p != null && t.p < gate.alpha && diff != null && (better === 'lower' ? diff < 0 : diff > 0);
    return { metric, baseline: a[metric], candidate: b[metric], difference: diff, p: t.p, better, verdict: worse ? 'regression' : improved ? 'improvement' : 'no significant change' };
  });
  const enough = A.length >= gate.minMatches && B.length >= gate.minMatches;
  const regressions = checks.filter(c => c.verdict === 'regression');
  delete a._raw; delete b._raw;
  return {
    baseline, candidate, gate, sampleOk: enough, baselineMetrics: a, candidateMetrics: b, checks,
    passed: enough && regressions.length === 0,
    decision: !enough ? `Not enough matches (need ${gate.minMatches} per version, have ${A.length} and ${B.length}).`
      : regressions.length ? `Blocked: ${regressions.map(r => r.metric).join(', ')} got significantly worse.`
      : 'Passes the gate. Approval by a developer is still required before release.',
  };
}
