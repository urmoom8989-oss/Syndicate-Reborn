// Human-readable match report built from one match's events. Every statement points at the event
// ids it came from, so a developer (or an AI tool) can check it against the raw JSONL.
import { matchFacts } from './facts.js';
import { detectWeaknesses, summarize } from './weaknesses.js';
import { mean, round } from './stats.js';
import { summarizeBuildFights } from './buildfights.js';

export function matchReport(match) {
  const f = matchFacts(match);
  const players = [...f.players.values()];
  const humans = players.filter(p => p.kind === 'human'), bots = players.filter(p => p.kind === 'bot');
  const start = match.start, end = match.end;

  const overview = {
    match: match.id, startedAt: match.startedAt, endedAt: match.endedAt, durationSeconds: round(match.duration, 1),
    outcome: match.termination, complete: match.complete,
    fortniteVersion: match.fortniteVersion, mode: match.mode, playlist: start?.playlist ?? null, botVersion: match.botVersion, serverBuild: start?.serverBuild ?? null,
    players: start?.players ?? null, humans: start?.humans ?? humans.length, bots: start?.bots ?? bots.length,
    winner: end?.winnerKind ?? null,
    // Host server health every 10 s: a low tick rate or long frames cause lag for every player.
    serverPerformance: (() => { const p = match.events.filter(e => e.type === 'server_perf'); return p.length ? { samples: p.length, averageTicksPerSecond: mean(p.map(e => e.ticksPerSecond)), lowestTicksPerSecond: Math.min(...p.map(e => e.ticksPerSecond)), worstFrameMs: Math.max(...p.map(e => e.worstFrameMs)), averageBotMs: mean(p.map(e => e.botMs)), averageReplicationMs: mean(p.map(e => e.replicationMs)) } : null; })(),
    integrity: { events: match.events.length, missing: match.missing, duplicatesDropped: match.duplicates, malformed: match.malformed, outOfOrder: match.outOfOrder },
  };

  const humanSummaries = humans.map(p => ({
    player: p.id, landedAt: p.landPlace ?? null, kills: p.kills, knocks: p.downs || 0,
    survivedSeconds: p.landedAt != null ? round(((p.diedAt ?? p.leftAt ?? f.lastT) - p.landedAt), 1) : null,
    place: p.place, eliminatedBy: p.diedAt != null ? { kind: p.killedBy, botState: p.killerState, distanceM: p.deathDistance != null ? Math.round(p.deathDistance / 100) : null } : null,
    left: p.leftAt != null, evidence: [p.landEvent, p.deathEvent, ...p.killEvents].filter(Boolean).slice(0, 20),
  }));

  const botDead = bots.filter(p => p.diedAt != null);
  const shots = botDead.reduce((s, p) => s + (p.shots || 0), 0), hits = botDead.reduce((s, p) => s + (p.hits || 0), 0);
  const stateTotals = {};
  for (const p of bots) for (const [k, v] of Object.entries(p.stateTime)) stateTotals[k] = (stateTotals[k] || 0) + v;
  const allTime = Object.values(stateTotals).reduce((a, b) => a + b, 0);
  const transitions = {};
  for (const e of f.states) { const k = `${e.from}->${e.to}`; transitions[k] = (transitions[k] || 0) + 1; }
  const botSummary = {
    landed: bots.filter(p => p.landedAt != null).length, eliminated: botDead.length,
    eliminatedBy: { human: botDead.filter(p => p.killedBy === 'human').length, bot: botDead.filter(p => p.killedBy === 'bot').length, storm: botDead.filter(p => p.killedBy === 'storm').length, other: botDead.filter(p => !['human', 'bot', 'storm'].includes(p.killedBy)).length },
    averageSurvivalSeconds: mean(botDead.filter(p => p.landedAt != null).map(p => p.diedAt - p.landedAt)),
    accuracyOfEliminatedBots: shots ? round(hits / shots) : null,
    stuckEvents: f.stuck.length, killsByBots: bots.reduce((s, p) => s + p.kills, 0),
  };
  const decisions = {
    timeShare: Object.fromEntries(Object.entries(stateTotals).sort((a, b) => b[1] - a[1]).map(([k, v]) => [k, round(v / (allTime || 1))])),
    topTransitions: Object.entries(transitions).sort((a, b) => b[1] - a[1]).slice(0, 10).map(([k, v]) => ({ transition: k, count: v })),
    mostOscillating: bots.filter(p => p.flips > 0).sort((a, b) => b.flips - a.flips).slice(0, 5).map(p => ({ bot: p.id, quickFlips: p.flips })),
    deathsByState: countBy(botDead.filter(p => p.deathCause === 'weapon'), p => p.deathState || 'unknown'),
    note: 'States are the activity the bot AI was running (it does not score alternatives, so no candidate list is recorded).',
  };

  const important = new Set(['match_start', 'match_end', 'storm_phase', 'player_left']);
  const humanIds = new Set(humans.map(p => p.id));
  const timeline = match.events.filter(e => important.has(e.type) ||
    (e.type === 'landed' && humanIds.has(e.actor?.id)) ||
    ((e.type === 'elimination' || e.type === 'downed') && (humanIds.has(e.victim?.id) || humanIds.has(e.killer?.id))))
    .slice(0, 400).map(e => ({ id: e.id, t: e.t, type: e.type, summary: summarize(e) }));
  const encounters = f.eliminations.filter(e => humanIds.has(e.victim?.id) || humanIds.has(e.killer?.id)).map(e => ({
    id: e.id, t: e.t, victim: e.victim?.id, killer: e.killer?.id ?? e.cause, weapon: e.weapon ?? null,
    distanceM: e.distance != null ? Math.round(e.distance / 100) : null, botState: e.victimState ?? e.killerState ?? null, place: e.poi || e.grid,
  }));

  const mistakes = detectWeaknesses([match], { minCount: 2 }).map(w => ({ title: w.title, observed: w.observed, evidence: w.evidence.slice(0, 8), hypothesis: w.hypothesis.cause, nextStep: w.experiment }));
  const playstyle = humanSummaries.map(h => ({
    player: h.player, observed: [
      h.landedAt ? `landed at ${h.landedAt}` : 'landing not observed',
      `${h.kills} eliminations`, h.eliminatedBy ? `eliminated by a ${h.eliminatedBy.kind}${h.eliminatedBy.distanceM != null ? ` at ${h.eliminatedBy.distanceM} m` : ''}` : (h.left ? 'left the match' : 'not eliminated'),
    ], note: 'Single-match observation; playstyle profiles need several matches (see /players).',
  }));
  const steps = [];
  if (!match.complete) steps.push('The match has no end event: check the server log (ready.pid.bot.log) around the last event time for a crash or a closed host.');
  if (match.missing) steps.push(`${match.missing} events are missing (gaps in seq): the server may have crashed or the disk was full.`);
  for (const m of mistakes.slice(0, 3)) steps.push(`${m.title}: ${m.nextStep}`);

  const buildFights = summarizeBuildFights(match.events);
  return { overview, humans: humanSummaries, bots: botSummary, decisions, buildFights, timeline, encounters, issues: match.issues, suspectedMistakes: mistakes, playstyle, recommendedSteps: steps };
}

function countBy(list, key) { const out = {}; for (const x of list) { const k = key(x); out[k] = (out[k] || 0) + 1; } return out; }

// Per-human "how they played" lines for the host's private learning webhook: readable enough to paste
// back to Claude for bot tuning, PII-safe (pseudonyms become stable Player N labels within the match; no
// names, raw ids, coordinates or chat). At most 10 lines of <=590 chars, matching the Discord limit.
export function humanPlayLines(match) {
  const r = matchReport(match);
  const o = r.overview;
  if (!r.humans.length) return [];
  const label = new Map(r.humans.map((h, i) => [h.player, `Player${i + 1}`]));
  const lines = [`[syndicate human play] match ${o.match} | ${o.fortniteVersion} ${o.mode} | ${Math.round((o.durationSeconds || 0) / 60)} min | ${o.humans} human(s)`];
  for (const h of r.humans) {
    const bits = [h.landedAt ? `landed ${h.landedAt}` : 'landing n/a'];
    if (h.survivedSeconds != null) bits.push(`survived ${Math.round(h.survivedSeconds)}s`);
    if (h.place != null) bits.push(`place ${h.place}`);
    bits.push(`${h.kills} elim${h.kills === 1 ? '' : 's'}`);
    if (h.knocks) bits.push(`${h.knocks} knock${h.knocks === 1 ? '' : 's'}`);
    if (h.eliminatedBy) bits.push(`died to ${h.eliminatedBy.kind}${h.eliminatedBy.botState ? ` (${h.eliminatedBy.botState})` : ''}${h.eliminatedBy.distanceM != null ? ` @${h.eliminatedBy.distanceM}m` : ''}`);
    else if (h.left) bits.push('left match');
    lines.push(`${label.get(h.player)}: ${bits.join(' | ')}`);
  }
  // A few human encounters (weapon / distance / location / the bot activity they fought) - the texture
  // used to tune bot combat. No ids: just what happened.
  for (const e of r.encounters.slice(0, 4)) {
    lines.push(`fight: ${e.weapon || '?'} @${e.distanceM != null ? e.distanceM + 'm' : '?'}${e.place ? ` ${e.place}` : ''}${e.botState ? ` vs ${e.botState}` : ''}`);
  }
  // Strip any stray pseudonym that slipped through, then clamp to the Discord shape.
  return lines.map(l => l.replace(/\bh[0-9a-f]{12}\b/g, 'player').slice(0, 590)).slice(0, 10);
}

// One-line summary for the match list and the Discord relay (no pseudonyms, only counts).
export function matchSummaryLine(match) {
  const r = matchReport(match);
  const o = r.overview, b = r.bots;
  return `match ${o.match} | ${o.fortniteVersion} ${o.mode} | ${o.outcome}${o.winner ? ` (${o.winner} won)` : ''} | ${Math.round((o.durationSeconds || 0) / 60)} min | humans ${o.humans} bots ${o.bots} | bot deaths: human ${b.eliminatedBy.human}, bot ${b.eliminatedBy.bot}, storm ${b.eliminatedBy.storm} | stuck ${b.stuckEvents} | bot ${o.botVersion}`;
}
