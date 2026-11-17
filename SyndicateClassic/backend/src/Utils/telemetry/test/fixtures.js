// Synthetic match files for tests. These are made-up events in the real schema, used only to check
// the calculations; they are never shown as real match data.
let counter = 0;
export function matchId(n = ++counter) { return `20261027-1200${String(n % 100).padStart(2, '0')}-${(0xa0000000 + n).toString(16)}`; }

// events: [type, t, fields] tuples; the envelope (v, match, seq, id, ts) is filled in.
export function matchText(id, events, { startedAt = '2026-10-27T12:00:00.000Z' } = {}) {
  const base = Date.parse(startedAt);
  return events.map(([type, t, fields], i) => JSON.stringify({ v: 1, match: id, seq: i + 1, id: `${id}:${i + 1}`, t, ts: new Date(base + t * 1000).toISOString(), type, ...fields })).join('\n') + '\n';
}

export const bot = (n, team = 100 + n) => ({ kind: 'bot', id: `b${n}`, team });
export const human = (h, team = 1) => ({ kind: 'human', id: h, team });

export function start(extra = {}) { return ['match_start', 0, { fortniteVersion: '4.50', map: 'Athena', mode: 'solo', playlist: 'Playlist_DefaultSolo', botVersion: 'spectra-A', serverBuild: 'test', humans: 1, bots: 3, players: 4, schema: 1, ...extra }]; }
