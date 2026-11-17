// Versioned training datasets from recorded matches. Two example kinds:
//   landing   context at landing (version, mode, place, strategy, other-team landers nearby) and the
//             early-game outcome (alive at 60/180 s, early elimination, kills, final place)
//   decision  one bot activity switch with what the bot could observe then (health, shield, enemy
//             distance/visibility, gun) and what happened next (eliminated within 15 s, kills)
// Both successes and failures are kept with their outcome, so tuning can weigh them; human actions
// are not labelled "correct". A deterministic 80/20 split by match id keeps holdout matches out of
// training. The id is a hash of the included matches, filters and schema, so the same input
// always produces the same dataset version.
import fs from 'fs';
import path from 'path';
import crypto from 'crypto';
import { matchFacts, survivedFor, nearbyOpponents } from './facts.js';
import { SCHEMA_VERSION } from './store.js';

export const DATASET_SCHEMA = 1;

const split = (matchId) => (parseInt(crypto.createHash('sha256').update(matchId).digest('hex').slice(0, 8), 16) % 100 < 80 ? 'train' : 'holdout');

export function buildDataset(matches, filters = {}) {
  const landing = [], decision = [];
  for (const m of matches.filter(m => m.complete)) {
    const f = matchFacts(m);
    for (const l of f.landings) {
      const p = f.players.get(l.player);
      landing.push({
        match: m.id, split: split(m.id), fortniteVersion: m.fortniteVersion, mode: m.mode, botVersion: m.botVersion,
        actorKind: p.kind, place: l.place, grid: l.grid ?? null, strategy: l.strategy, landedAtSeconds: l.t - (m.start?.t ?? 0),
        opponentsNearby: nearbyOpponents(l, f),
        outcome: { alive60: survivedFor(p, f, 60), alive180: survivedFor(p, f, 180), earlyElimination: survivedFor(p, f, 120) === false, kills: p.kills, finalPlace: p.place ?? null, eliminatedBy: p.killedBy ?? null },
        evidence: l.event,
      });
    }
    for (const e of f.states) {
      const next = m.events.find(x => x.type === 'elimination' && x.victim?.id === e.bot && x.t >= e.t && x.t <= e.t + 15);
      const kills = m.events.filter(x => x.type === 'elimination' && x.killer?.id === e.bot && x.t >= e.t && x.t <= e.t + 15).length;
      decision.push({
        match: m.id, split: split(m.id), fortniteVersion: m.fortniteVersion, botVersion: m.botVersion, bot: e.bot,
        observation: { previous: e.from, heldSeconds: e.held ?? null, health: e.health ?? null, shield: e.shield ?? null, enemyDistance: e.enemyDist ?? null, enemyVisible: e.enemyVisible ?? null, hasGun: e.hasGun ?? null, quickFlips: e.flips ?? 0 },
        selected: e.to,
        outcome: { eliminatedWithin15s: !!next, killsWithin15s: kills },
        evidence: e.id,
      });
    }
  }
  const included = [...new Set(landing.map(x => x.match).concat(decision.map(x => x.match)))].sort();
  const id = crypto.createHash('sha256').update(JSON.stringify({ included, filters, DATASET_SCHEMA, SCHEMA_VERSION })).digest('hex').slice(0, 12);
  return {
    manifest: { id, datasetSchema: DATASET_SCHEMA, telemetrySchema: SCHEMA_VERSION, createdAt: new Date().toISOString(), filters, matches: included.length,
      counts: { landing: landing.length, decision: decision.length }, split: 'sha256(match id) % 100 < 80 = train, else holdout',
      note: 'Only complete matches. Players are pseudonymous; no names, account ids or IPs.' },
    landing, decision,
  };
}

export function validateDataset(ds) {
  const errors = [];
  if (!ds?.manifest?.id) errors.push('missing manifest id');
  for (const [kind, rows] of [['landing', ds.landing], ['decision', ds.decision]]) {
    if (!Array.isArray(rows)) { errors.push(`${kind} is not a list`); continue; }
    rows.forEach((r, i) => {
      if (!r.match || !r.evidence) errors.push(`${kind}[${i}] without match/evidence`);
      if (!['train', 'holdout'].includes(r.split)) errors.push(`${kind}[${i}] bad split`);
      if (JSON.stringify(r).match(/"(name|username|accountId|ip)"/i)) errors.push(`${kind}[${i}] contains identifying fields`);
    });
    const trainMatches = new Set(rows.filter(r => r.split === 'train').map(r => r.match));
    if (rows.some(r => r.split === 'holdout' && trainMatches.has(r.match))) errors.push(`${kind}: a match is in both train and holdout`);
  }
  return errors;
}

export function writeDataset(ds, dir) {
  const out = path.join(dir, 'datasets', ds.manifest.id);
  fs.mkdirSync(out, { recursive: true });
  fs.writeFileSync(path.join(out, 'manifest.json'), JSON.stringify(ds.manifest, null, 2));
  fs.writeFileSync(path.join(out, 'landing.jsonl'), ds.landing.map(x => JSON.stringify(x)).join('\n') + '\n');
  fs.writeFileSync(path.join(out, 'decision.jsonl'), ds.decision.map(x => JSON.stringify(x)).join('\n') + '\n');
  return out;
}
