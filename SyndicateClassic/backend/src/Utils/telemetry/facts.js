// Turns one parsed match into per-player facts (when and where each player landed, what killed
// them, what state a bot was in) that the reports and analyses share. Facts only: no judgement.
import { distance2d } from './stats.js';

export const place = (e) => (e.poi ? e.poi : e.grid ? `open:${e.grid}` : 'unknown');

export function matchFacts(match) {
  const players = new Map();
  const get = (actor) => {
    if (!actor || !actor.id) return null;
    if (!players.has(actor.id)) players.set(actor.id, { id: actor.id, kind: actor.kind, team: actor.team, kills: 0, killEvents: [], stuck: 0, flips: 0, stateTime: {} });
    return players.get(actor.id);
  };
  const botActor = (id) => ({ id, kind: 'bot' });
  const eliminations = [], downs = [], stuck = [], states = [], landings = [], left = [];
  for (const e of match.events) {
    switch (e.type) {
      case 'landed': {
        const p = get(e.actor); if (!p || p.landedAt != null) break;
        Object.assign(p, { landedAt: e.t, landPos: e.pos, landPlace: place(e), strategy: e.strategy ?? null, targetError: e.targetError ?? null, targetPoi: e.targetPoi ?? null, landEvent: e.id });
        landings.push({ player: p.id, kind: p.kind, t: e.t, place: place(e), pos: e.pos, grid: e.grid, strategy: e.strategy ?? null, event: e.id });
        break;
      }
      case 'elimination': {
        const v = get(e.victim); const k = e.killer ? get(e.killer) : null;
        if (v && v.diedAt == null) Object.assign(v, { diedAt: e.t, deathCause: e.cause, deathState: e.victimState ?? null, killedBy: k?.kind ?? (e.cause === 'storm' ? 'storm' : 'none'), killerState: e.killerState ?? null, deathEvent: e.id, deathPlace: place(e), deathDistance: e.distance ?? null, place: e.place ?? null, hadGun: e.hadGun, shots: e.shots, hits: e.hits });
        if (k && k !== v) { k.kills++; k.killEvents.push(e.id); }
        eliminations.push(e);
        break;
      }
      case 'downed': { const v = get(e.victim); if (v) { v.downs = (v.downs || 0) + 1; } downs.push(e); break; }
      case 'bot_stuck': { const p = get(botActor(e.bot)); if (p) p.stuck++; stuck.push(e); break; }
      case 'bot_state': {
        const p = get(botActor(e.bot));
        if (p) { p.flips += e.flips || 0; if (e.from && Number.isFinite(e.held)) p.stateTime[e.from] = (p.stateTime[e.from] || 0) + e.held; }
        states.push(e);
        break;
      }
      case 'player_left': { const p = get(e.actor); if (p && p.leftAt == null) p.leftAt = e.t; left.push(e); break; }
    }
  }
  const lastT = match.events.length ? match.events[match.events.length - 1].t : 0;
  return { match, players, eliminations, downs, stuck, states, landings, left, lastT };
}

// Seconds a landed player stayed alive after landing, and whether we saw long enough to know.
// Players still alive when the file ends (match over or cut short) are censored, not survivors.
export function survivedFor(p, facts, seconds) {
  if (p.landedAt == null) return null;
  if (p.diedAt != null) return p.diedAt - p.landedAt >= seconds;
  if (p.leftAt != null && p.leftAt - p.landedAt < seconds) return null; // left early: unknown, not a death
  if (facts.lastT - p.landedAt >= seconds) return true;
  return facts.match.complete ? true : null; // match won before the window ends / file cut short
}

// Other-team players who landed at the same place (or within `radius` game units, 90 m).
export function nearbyOpponents(landing, facts, radius = 9000) {
  const me = facts.players.get(landing.player);
  return facts.landings.filter(o => {
    if (o.player === landing.player) return false;
    const other = facts.players.get(o.player);
    if (me && other && me.team != null && me.team === other.team) return false;
    return o.place === landing.place || (distance2d(o.pos, landing.pos) ?? Infinity) < radius;
  }).length;
}
