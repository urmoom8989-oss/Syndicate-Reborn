// Player playstyle profiles across matches, by pseudonym. A label is only given once there are
// enough matches; each label says whether it is observed, inferred, or verified across matches.
// Profiles are for understanding how people beat our bots; they never feed live bot decisions
// with information a bot could not see in the match.
import { matchFacts } from './facts.js';
import { mean, wilson, confidence, round } from './stats.js';

const DEFAULTS = { minMatches: 3, hotLanders: 4, longRange: 5000, earlyWindow: 120 };

export function playstyleProfiles(matches, options = {}) {
  const opts = { ...DEFAULTS, ...options };
  const people = new Map();
  const botDeathsToHumans = { byState: {}, byDistance: { '<10m': 0, '10-30m': 0, '30-50m': 0, '>50m': 0 } };
  for (const m of matches) {
    const f = matchFacts(m);
    const landersAt = new Map();
    for (const l of f.landings) landersAt.set(l.place, (landersAt.get(l.place) || 0) + 1);
    for (const p of f.players.values()) {
      if (p.kind !== 'human') continue;
      if (!people.has(p.id)) people.set(p.id, { player: p.id, matches: 0, kills: 0, earlyKills: 0, hotDrops: 0, landings: 0, killDistances: [], survival: [], places: {}, deaths: { bot: 0, human: 0, storm: 0, other: 0 }, leftEarly: 0, evidence: [] });
      const P = people.get(p.id);
      P.matches++; P.kills += p.kills;
      if (p.landedAt != null) { P.landings++; P.places[p.landPlace] = (P.places[p.landPlace] || 0) + 1; if ((landersAt.get(p.landPlace) || 0) >= opts.hotLanders) P.hotDrops++; }
      if (p.diedAt != null) { P.deaths[['bot', 'human', 'storm'].includes(p.killedBy) ? p.killedBy : 'other']++; }
      if (p.leftAt != null && p.diedAt == null) P.leftEarly++;
      if (p.landedAt != null) P.survival.push((p.diedAt ?? p.leftAt ?? f.lastT) - p.landedAt);
      for (const id of p.killEvents) {
        const e = m.events.find(x => x.id === id); if (!e) continue;
        if (e.distance != null) P.killDistances.push(e.distance);
        if (p.landedAt != null && e.t - p.landedAt <= opts.earlyWindow) P.earlyKills++;
        if (e.victim?.kind === 'bot') {
          const s = e.victimState || 'unknown'; botDeathsToHumans.byState[s] = (botDeathsToHumans.byState[s] || 0) + 1;
          const d = (e.distance ?? 0) / 100; botDeathsToHumans.byDistance[d < 10 ? '<10m' : d < 30 ? '10-30m' : d < 50 ? '30-50m' : '>50m']++;
        }
        P.evidence.length < 15 && P.evidence.push(id);
      }
    }
  }
  const profiles = [...people.values()].map(P => {
    const enough = P.matches >= opts.minMatches;
    const hot = wilson(P.hotDrops, P.landings), avgKill = mean(P.killDistances), kpm = P.matches ? P.kills / P.matches : 0;
    const labels = [];
    const add = (label, ok, basis) => ok && labels.push({ label, basis, status: enough ? 'verified across matches' : 'inferred (few matches)', confidence: confidence(P.matches) });
    add('hot dropper', (hot.rate ?? 0) >= 0.6, `${P.hotDrops}/${P.landings} landings at places with ${opts.hotLanders}+ landers`);
    add('low-contest lander', P.landings >= 2 && (hot.rate ?? 1) <= 0.2, `${P.hotDrops}/${P.landings} hot landings`);
    add('aggressive', kpm >= 3 || (P.kills && P.earlyKills / P.kills >= 0.5), `${round(kpm, 2)} eliminations per match, ${P.earlyKills} in the first ${opts.earlyWindow} s after landing`);
    add('long-range fighter', avgKill != null && avgKill >= opts.longRange, `average elimination distance ${Math.round((avgKill ?? 0) / 100)} m`);
    add('close-range fighter', avgKill != null && avgKill < 1500 && P.killDistances.length >= 3, `average elimination distance ${Math.round((avgKill ?? 0) / 100)} m`);
    add('survival focused', kpm < 1 && (mean(P.survival) ?? 0) >= 600, `${round(kpm, 2)} eliminations per match, ${Math.round(mean(P.survival) ?? 0)} s average survival`);
    const favourite = Object.entries(P.places).sort((a, b) => b[1] - a[1])[0];
    return {
      player: P.player, matches: P.matches, sufficientData: enough,
      observed: { eliminations: P.kills, eliminationsPerMatch: round(kpm, 2), earlyEliminations: P.earlyKills, landings: P.landings, hotDropRate: hot, averageEliminationDistanceM: avgKill != null ? Math.round(avgKill / 100) : null, averageSurvivalSeconds: mean(P.survival), deathsBy: P.deaths, leftEarly: P.leftEarly, favouritePlace: favourite ? { place: favourite[0], landings: favourite[1] } : null },
      labels: enough ? labels : [], tentativeLabels: enough ? [] : labels, evidence: P.evidence,
    };
  });
  return { profiles: profiles.sort((a, b) => b.matches - a.matches), howHumansBeatBots: botDeathsToHumans, minMatches: opts.minMatches };
}
