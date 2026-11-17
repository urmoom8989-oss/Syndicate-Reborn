// Build-fight summary for one match, from the game server's "build_fight" lines (one per fight a
// bot had: trigger, states, actions ok/failed in order, height and health before/after, outcome).
// Used by the match report and the Discord relay so build-fight decisions can be compared.
const ACTION_CODES = { W: 'wall', R: 'ramp cover', C: 'contest ramp', K: 'cone on enemy', T: 'top cover', S: 'swap shotgun', A: 'swap rifle', P: 'peek', V: 'recover' };

export function summarizeBuildFights(events) {
  const fights = events.filter(e => e.type === 'build_fight');
  if (!fights.length) return null;
  const count = (list, key) => { const out = {}; for (const x of list) { const k = key(x); if (k) out[k] = (out[k] || 0) + 1; } return out; };
  const sum = (field) => { const out = {}; for (const f of fights) for (const [k, v] of Object.entries(f[field] || {})) out[k] = (out[k] || 0) + (Number(v) || 0); return out; };
  const ok = sum('ok'), failed = sum('failed');
  const actions = Object.keys({ ...ok, ...failed }).map(a => ({ action: a, ok: ok[a] || 0, failed: failed[a] || 0 }))
    .sort((x, y) => (y.ok + y.failed) - (x.ok + x.failed));
  const byOutcome = count(fights, f => f.outcome);
  const byTrigger = count(fights, f => f.trigger);
  // Win rate per trigger: which first decisions tend to end with the opponent down.
  const triggerResults = Object.keys(byTrigger).map(t => {
    const list = fights.filter(f => f.trigger === t);
    return { trigger: t, fights: list.length, enemyDown: list.filter(f => f.outcome === 'enemy_down').length, botDown: list.filter(f => f.outcome === 'bot_down').length };
  }).sort((a, b) => b.fights - a.fights);
  // Most common openings (first 4 actions), with how they ended.
  const openings = {};
  for (const f of fights) {
    const key = String(f.sequence || '').replace(/x/g, '').slice(0, 4) || '-';
    const o = openings[key] ||= { opening: key, fights: 0, enemyDown: 0, botDown: 0 };
    o.fights++; if (f.outcome === 'enemy_down') o.enemyDown++; if (f.outcome === 'bot_down') o.botDown++;
  }
  const seconds = fights.map(f => Number(f.seconds) || 0);
  return {
    fights: fights.length,
    vsHumans: fights.filter(f => f.enemy === 'human').length,
    averageSeconds: Math.round(seconds.reduce((a, b) => a + b, 0) / fights.length * 10) / 10,
    byOutcome, byTrigger: triggerResults, actions,
    openings: Object.values(openings).sort((a, b) => b.fights - a.fights).slice(0, 5),
    tookHeight: fights.filter(f => Number(f.heightBest) >= 250).length,
    legend: ACTION_CODES,
  };
}

// Up to three short lines for the Discord relay.
export function buildFightLines(summary) {
  if (!summary) return [];
  const o = summary.byOutcome;
  const pct = (a, b) => b ? `${Math.round(100 * a / b)}%` : '-';
  const lines = [`build fights: ${summary.fights} (${summary.vsHumans} vs humans), avg ${summary.averageSeconds}s | won ${o.enemy_down || 0}, lost ${o.bot_down || 0}, traded ${o.traded || 0}, took height ${o.took_height || 0}, disengaged ${o.disengaged || 0}`];
  lines.push('build actions ok/failed: ' + summary.actions.slice(0, 8).map(a => `${a.action} ${a.ok}/${a.failed}`).join(', '));
  lines.push('triggers (fights, won, lost): ' + summary.byTrigger.slice(0, 4).map(t => `${t.trigger} ${t.fights}/${t.enemyDown}/${t.botDown}`).join('; ')
    + ' | openings ' + summary.openings.slice(0, 3).map(x => `${x.opening} won ${pct(x.enemyDown, x.fights)}`).join(', '));
  return lines;
}
