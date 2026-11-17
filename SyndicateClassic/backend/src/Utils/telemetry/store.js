// Reads the game server's match files (Runtime/Server/telemetry/<match>.jsonl), validates every
// line against the schema, drops duplicates and reports gaps, out-of-order lines and unfinished
// matches. Nothing here trusts the file: a malformed line is reported, never thrown.
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

export const SCHEMA_VERSION = 1;
export const DEFAULT_DIR = fileURLToPath(new URL('../../../../Runtime/Server/telemetry/', import.meta.url));

// Required fields per event type (beyond the envelope). Unknown types are kept but flagged.
export const EVENT_TYPES = {
  match_start: ['fortniteVersion', 'mode', 'botVersion'],
  match_end: ['reason'],
  landed: ['actor', 'pos'],
  elimination: ['victim', 'cause'],
  downed: ['victim'],
  bot_state: ['bot', 'from', 'to'],
  bot_stuck: ['bot', 'pos'],
  storm_phase: ['from', 'to'],
  checkpoint: ['alive'],
  player_left: ['actor'],
  server_perf: ['ticksPerSecond'],
  build_fight: ['bot', 'outcome'],
};

const MATCH_ID = /^[0-9]{8}-[0-9]{6}-[0-9a-f]{8}$/;
const MAX_LINE = 16 * 1024;

export function validateEvent(e) {
  const problems = [];
  if (!e || typeof e !== 'object' || Array.isArray(e)) return ['not an object'];
  if (e.v !== SCHEMA_VERSION) problems.push(`schema v${e.v} (expected ${SCHEMA_VERSION})`);
  if (typeof e.match !== 'string' || !MATCH_ID.test(e.match)) problems.push('bad match id');
  if (!Number.isInteger(e.seq) || e.seq < 1) problems.push('bad seq');
  if (e.id !== `${e.match}:${e.seq}`) problems.push('id does not match match:seq');
  if (typeof e.t !== 'number' || !Number.isFinite(e.t)) problems.push('bad t');
  if (typeof e.type !== 'string') problems.push('missing type');
  const required = EVENT_TYPES[e.type];
  if (required) for (const field of required) if (e[field] === undefined) problems.push(`${e.type} without ${field}`);
  if (e.pos !== undefined && !(Array.isArray(e.pos) && e.pos.length === 3 && e.pos.every(Number.isFinite))) problems.push('bad pos');
  return problems;
}

// Upgrades older schema versions in place (none yet; the hook is here so v2 can map v1 lines).
export function upgradeEvent(e) {
  return e;
}

// Parses one match file's text. Returns the normalized match with integrity information.
export function parseMatch(text, source = '') {
  const issues = [];
  const byId = new Map();
  let lineNo = 0, lastSeq = 0, duplicates = 0, outOfOrder = 0, malformed = 0;
  for (const raw of text.split(/\r?\n/)) {
    lineNo++;
    if (!raw.trim()) continue;
    if (raw.length > MAX_LINE) { malformed++; issues.push({ severity: 'warning', kind: 'line_too_long', line: lineNo }); continue; }
    let e;
    try { e = upgradeEvent(JSON.parse(raw)); } catch { malformed++; issues.push({ severity: 'warning', kind: 'malformed_line', line: lineNo }); continue; }
    const problems = validateEvent(e);
    if (problems.length) { malformed++; issues.push({ severity: 'warning', kind: 'invalid_event', line: lineNo, detail: problems.join('; ') }); continue; }
    if (byId.has(e.id)) { duplicates++; continue; }
    if (e.seq < lastSeq) outOfOrder++;
    lastSeq = Math.max(lastSeq, e.seq);
    if (!EVENT_TYPES[e.type]) issues.push({ severity: 'info', kind: 'unknown_type', line: lineNo, detail: e.type, ref: e.id });
    byId.set(e.id, e);
  }
  const events = [...byId.values()].sort((a, b) => a.seq - b.seq);
  const matchIds = new Set(events.map(e => e.match));
  if (matchIds.size > 1) issues.push({ severity: 'error', kind: 'mixed_matches', detail: [...matchIds].join(', ') });
  // Gaps in seq mean lost lines (crash between queue and disk, or a truncated copy).
  const missing = [];
  for (let i = 1; i < events.length; i++) for (let s = events[i - 1].seq + 1; s < events[i].seq && missing.length < 1000; s++) missing.push(s);
  if (events.length && events[0].seq !== 1) for (let s = 1; s < events[0].seq && missing.length < 1000; s++) missing.push(s);
  if (missing.length) issues.push({ severity: 'warning', kind: 'missing_events', detail: `${missing.length} missing (first seq ${missing[0]})` });
  if (duplicates) issues.push({ severity: 'info', kind: 'duplicates_dropped', detail: String(duplicates) });
  if (outOfOrder) issues.push({ severity: 'info', kind: 'out_of_order', detail: String(outOfOrder) });

  const start = events.find(e => e.type === 'match_start') || null;
  const end = [...events].reverse().find(e => e.type === 'match_end') || null;
  if (!start && events.length) issues.push({ severity: 'warning', kind: 'no_match_start' });
  // A match without match_end was cut short: server crash, host closed, or still running.
  const termination = end ? end.reason : 'terminated_unexpectedly';
  if (!end && events.length) issues.push({ severity: 'warning', kind: 'incomplete_match', detail: 'no match_end: recovered up to the last saved event' });
  const id = events[0]?.match || path.basename(source, '.jsonl');
  return {
    id, source, events, issues, missing: missing.length, duplicates, malformed, outOfOrder,
    start, end, complete: !!end, termination,
    fortniteVersion: start?.fortniteVersion ?? null, mode: start?.mode ?? null, map: start?.map ?? null,
    botVersion: start?.botVersion ?? null, startedAt: start?.ts ?? events[0]?.ts ?? null,
    endedAt: (end ?? events[events.length - 1])?.ts ?? null,
    duration: events.length ? events[events.length - 1].t - (start?.t ?? events[0].t) : 0,
  };
}

// Reads every match in a folder. Files are re-parsed only when their size or time changes.
const cache = new Map();
export function loadMatches(dir = DEFAULT_DIR) {
  let names = [];
  try { names = fs.readdirSync(dir).filter(n => n.endsWith('.jsonl')); } catch { return []; }
  const matches = [];
  for (const name of names) {
    const file = path.join(dir, name);
    let stat; try { stat = fs.statSync(file); } catch { continue; }
    const key = `${stat.size}:${stat.mtimeMs}`;
    const hit = cache.get(file);
    if (hit && hit.key === key) { matches.push(hit.match); continue; }
    let text; try { text = fs.readFileSync(file, 'utf8'); } catch { continue; }
    const match = parseMatch(text, file);
    cache.set(file, { key, match });
    matches.push(match);
  }
  for (const file of cache.keys()) if (!names.includes(path.basename(file))) cache.delete(file);
  return matches.sort((a, b) => String(b.startedAt).localeCompare(String(a.startedAt)));
}

// Filters used by the dashboard and the API. All optional.
export function filterMatches(matches, f = {}) {
  return matches.filter(m =>
    (!f.match || m.id === f.match) &&
    (!f.from || (m.startedAt && m.startedAt >= f.from)) &&
    (!f.to || (m.startedAt && m.startedAt <= f.to)) &&
    (!f.version || m.fortniteVersion === f.version) &&
    (!f.mode || m.mode === f.mode) &&
    (!f.map || m.map === f.map) &&
    (!f.botVersion || m.botVersion === f.botVersion) &&
    (!f.outcome || m.termination === f.outcome) &&
    (!f.severity || m.issues.some(i => i.severity === f.severity)) &&
    (!f.errorType || m.issues.some(i => i.kind === f.errorType)));
}

// Retention: removes match files older than `days` and the oldest files beyond `maxBytes`.
// Returns what was deleted so the caller can log it (an audit trail for deletions).
export function applyRetention(dir = DEFAULT_DIR, { days = 30, maxBytes = 500 * 1024 * 1024, now = Date.now() } = {}) {
  let files = [];
  try { files = fs.readdirSync(dir).filter(n => n.endsWith('.jsonl')).map(n => { const p = path.join(dir, n); const s = fs.statSync(p); return { p, n, size: s.size, mtime: s.mtimeMs }; }); } catch { return []; }
  files.sort((a, b) => a.mtime - b.mtime);
  const removed = [];
  let total = files.reduce((s, f) => s + f.size, 0);
  for (const f of files) {
    const old = now - f.mtime > days * 86400000;
    if (!old && total <= maxBytes) continue;
    try { fs.unlinkSync(f.p); total -= f.size; removed.push({ file: f.n, reason: old ? `older than ${days} days` : 'storage limit' }); } catch { }
  }
  return removed;
}

// Deletes one match (developer action) or every match. Returns the number of files removed.
export function deleteMatches(dir = DEFAULT_DIR, matchId = null) {
  let n = 0;
  for (const name of (() => { try { return fs.readdirSync(dir); } catch { return []; } })()) {
    if (!name.endsWith('.jsonl')) continue;
    if (matchId && name !== `${matchId}.jsonl`) continue;
    try { fs.unlinkSync(path.join(dir, name)); n++; } catch { }
  }
  return n;
}
