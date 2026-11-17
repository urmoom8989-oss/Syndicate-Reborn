// Optional upload of finished-match summaries to Discord (discord.js: the Dev Kit's local webhook
// secret if configured, otherwise the Cloudflare relay, which keeps the webhook as a Worker secret).
// On by default (settings.json "upload"; players can turn it off in Settings > Match data). Only counts are sent: no
// pseudonyms, names, positions or chat. Each match is sent once (uploaded.json keeps the ids);
// failures are retried with backoff and never touch the local files.
import fs from 'fs';
import path from 'path';
import { matchReport, matchSummaryLine, humanPlayLines } from './report.js';
import { deliver, webhookConfig, RELAY_URL } from './discord.js';
import { buildFightLines } from './buildfights.js';

export { RELAY_URL };

export function readSettings(dir) {
  // Sharing is on unless the player turns it off (Settings > Match data). Only counts are sent.
  const defaults = { collect: true, upload: true, retentionDays: 30, maxMegabytes: 500 };
  try { return { ...defaults, ...JSON.parse(fs.readFileSync(path.join(dir, 'settings.json'), 'utf8')) }; } catch { return defaults; }
}

export function writeSettings(dir, changes) {
  const next = { ...readSettings(dir) };
  if (typeof changes.collect === 'boolean') next.collect = changes.collect;
  if (typeof changes.upload === 'boolean') next.upload = changes.upload;
  if (Number.isInteger(changes.retentionDays) && changes.retentionDays >= 1 && changes.retentionDays <= 365) next.retentionDays = changes.retentionDays;
  if (Number.isInteger(changes.maxMegabytes) && changes.maxMegabytes >= 10 && changes.maxMegabytes <= 10000) next.maxMegabytes = changes.maxMegabytes;
  fs.mkdirSync(dir, { recursive: true });
  fs.writeFileSync(path.join(dir, 'settings.json'), JSON.stringify(next, null, 2));
  return next;
}

// The relay accepts at most 10 lines of 600 characters (6 KB) per request.
export function relayLines(match) {
  const r = matchReport(match);
  const lines = [`[syndicate] ${matchSummaryLine(match)}`];
  const ds = r.decisions.deathsByState;
  if (Object.keys(ds).length) lines.push(`bot combat deaths by activity: ${Object.entries(ds).map(([k, v]) => `${k} ${v}`).join(', ')}`);
  for (const m of r.suspectedMistakes.slice(0, 4)) lines.push(`finding: ${m.title}: ${m.observed}`);
  if (!match.complete) lines.push('match ended without an end event (crash or closed host); data recovered up to the last event');
  return lines.map(l => l.replace(/\bh[0-9a-f]{12}\b/g, 'player').slice(0, 590)).slice(0, 10);
}

// Build-fight results for the mechanics-learning channel (its own webhook; see discord.js).
export function mechanicsLines(match) {
  const fights = buildFightLines(matchReport(match).buildFights);
  if (!fights.length) return [];
  return [`[syndicate mechanics] match ${match.id} | ${match.fortniteVersion} ${match.mode}`, ...fights].map(l => l.slice(0, 590));
}

export function createUploader(dir, { fetchImpl = globalThis.fetch, now = () => Date.now(), url = RELAY_URL, env = process.env } = {}) {
  const ledgerPath = path.join(dir, 'uploaded.json');
  let ledger = {};
  try { ledger = JSON.parse(fs.readFileSync(ledgerPath, 'utf8')); } catch { }
  const save = () => { try { fs.writeFileSync(ledgerPath, JSON.stringify(ledger, null, 2)); } catch { } };
  let nextSend = 0, relayRetired = false;
  return {
    status: () => ({ ledger: Object.keys(ledger).length, sent: Object.values(ledger).filter(x => x.sent).length, failing: Object.entries(ledger).filter(([, x]) => !x.sent).map(([id, x]) => ({ match: id, attempts: x.attempts, lastError: x.lastError, nextTry: x.nextTry })) }),
    // Sends at most one pending match per call (the relay allows one report per minute).
    async tick(matches) {
      if (!readSettings(dir).upload || !fetchImpl) return { skipped: 'upload off' };
      const t = now();
      if (t < nextSend) return { skipped: 'rate limit' };
      const pending = matches.filter(m => m.complete && !(ledger[m.id]?.sent) && (ledger[m.id]?.nextTry ?? 0) <= t);
      const m = pending[0];
      if (!m) return { skipped: 'nothing pending' };
      const entry = ledger[m.id] ||= { attempts: 0, sent: false };
      entry.attempts++;
      // Same delivery as the Developer Tools test: the Dev Kit webhook if configured, else the relay.
      if (relayRetired && webhookConfig(env).kind !== 'webhook') return { skipped: 'match feed off' };
      // When a real player was in the match, send the per-human "how they played" report (pseudonymous,
      // PII-safe) so it reaches the bot-learning channel to paste back to Claude - over the host's own
      // webhook if configured, otherwise the relay, which forwards to the DISCORD_WEBHOOK secret. Bot-only
      // matches still send counts-only relayLines. The consent toggle (settings.upload) gates all of it.
      const human = humanPlayLines(m);
      const result = await deliver(human.length ? human : relayLines(m), { fetchImpl, env, relayUrl: url });
      // The relay answers 410 once the match feed is retired (relay/worker.js): stop sending for this run.
      if (result.status === 410 && result.via === 'relay') { relayRetired = true; return { skipped: 'match feed off' }; }
      if (result.ok) { entry.sent = true; entry.sentAt = new Date(t).toISOString(); entry.via = result.via; delete entry.lastError; delete entry.nextTry; }
      else { entry.lastError = result.message; entry.nextTry = t + Math.min(3600000, 60000 * 2 ** Math.min(6, entry.attempts)); }
      nextSend = t + 61000;
      // Mechanics channel: build-fight results of the same match, sent once when a webhook is set.
      if (entry.sent && !entry.mechanicsSent && webhookConfig(env, 'mechanics').kind === 'webhook') {
        const lines = mechanicsLines(m);
        if (!lines.length) entry.mechanicsSent = 'none';
        else { const r2 = await deliver(lines, { fetchImpl, env, channel: 'mechanics', username: 'Syndicate mechanics learning' }); if (r2.ok) entry.mechanicsSent = new Date(t).toISOString(); else entry.mechanicsError = r2.message; }
      }
      save();
      return { match: m.id, sent: entry.sent, error: entry.lastError ?? null };
    },
  };
}
