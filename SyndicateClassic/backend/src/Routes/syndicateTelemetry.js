// Developer diagnostics API for match telemetry: reports, weaknesses, landing intelligence,
// playstyles, bot version comparison, dataset export, telemetry settings and the Discord relay test.
// Dev Kit builds only (TILTED_DEVKIT=1 from the Dev Kit launcher), this PC only (like /launcher), and
// every request needs the developer token from
// Runtime/Server/telemetry/dev-token (created on first start, read by the launcher's
// developer page). Deletions and exports are written to audit.log.
import express from 'express';
import fs from 'fs';
import path from 'path';
import { randomBytes } from 'node:crypto';
import { authorize } from '../Utils/telemetry/access.js';
import { DEFAULT_DIR, loadMatches, filterMatches, applyRetention, deleteMatches } from '../Utils/telemetry/store.js';
import { matchReport, matchSummaryLine } from '../Utils/telemetry/report.js';
import { detectWeaknesses } from '../Utils/telemetry/weaknesses.js';
import { landingIntelligence } from '../Utils/telemetry/landing.js';
import { playstyleProfiles } from '../Utils/telemetry/playstyle.js';
import { compareBotVersions, versionMetrics } from '../Utils/telemetry/benchmark.js';
import { buildDataset, validateDataset, writeDataset } from '../Utils/telemetry/dataset.js';
import { readSettings, writeSettings, createUploader } from '../Utils/telemetry/upload.js';
import { deliver, testLines, webhookConfig } from '../Utils/telemetry/discord.js';

const dir = process.env.TILTED_TELEMETRY_DIR || DEFAULT_DIR;
const router = express.Router();
fs.mkdirSync(dir, { recursive: true });

const tokenPath = path.join(dir, 'dev-token');
let token = '';
try { token = fs.readFileSync(tokenPath, 'utf8').trim(); } catch { }
if (token.length < 32) { token = randomBytes(24).toString('hex'); try { fs.writeFileSync(tokenPath, token); } catch { } }

const audit = (action, detail) => { try { fs.appendFileSync(path.join(dir, 'audit.log'), `${new Date().toISOString()} ${action} ${JSON.stringify(detail)}\n`); } catch { } };

router.use('/tilted/telemetry', (req, res, next) => {
  const access = authorize({ address: req.socket.remoteAddress, origin: req.headers.origin, header: req.headers['x-tilted-dev'], devkit: process.env.TILTED_DEVKIT === '1' }, token);
  if (!access.ok) { audit('denied', { path: req.path, status: access.status }); return res.status(access.status).json({ message: access.message }); }
  res.set('Cache-Control', 'no-store');
  next();
});

const filters = (q) => ({ match: q.match, from: q.from, to: q.to, version: q.version, mode: q.mode, map: q.map, botVersion: q.botVersion, outcome: q.outcome, severity: q.severity, errorType: q.errorType });
const matchesFor = (q) => filterMatches(loadMatches(dir), filters(q));
const safe = (handler) => (req, res) => { try { handler(req, res); } catch (error) { res.status(500).json({ message: 'Diagnostics failed: ' + error.message }); } };

router.get('/tilted/telemetry/matches', safe((req, res) => {
  const list = matchesFor(req.query);
  const category = req.query.category;
  res.json(list.slice(0, 500).filter(m => !category || m.events.some(e => e.type === category)).map(m => ({
    match: m.id, startedAt: m.startedAt, endedAt: m.endedAt, durationSeconds: Math.round(m.duration), outcome: m.termination, complete: m.complete,
    fortniteVersion: m.fortniteVersion, mode: m.mode, botVersion: m.botVersion, events: m.events.length,
    warnings: m.issues.filter(i => i.severity !== 'info').length, summary: matchSummaryLine(m),
  })));
}));

router.get('/tilted/telemetry/matches/:id', safe((req, res) => {
  const m = loadMatches(dir).find(x => x.id === req.params.id);
  if (!m) return res.status(404).json({ message: 'No such match.' });
  res.json(matchReport(m));
}));

router.get('/tilted/telemetry/matches/:id/events', safe((req, res) => {
  const m = loadMatches(dir).find(x => x.id === req.params.id);
  if (!m) return res.status(404).json({ message: 'No such match.' });
  const type = req.query.type;
  res.json(m.events.filter(e => !type || e.type === type));
}));

router.get('/tilted/telemetry/weaknesses', safe((req, res) => {
  let list = detectWeaknesses(matchesFor(req.query), { minCount: Number(req.query.minCount) || 5 });
  if (req.query.pattern) list = list.filter(f => f.kind.startsWith(req.query.pattern));
  if (req.query.findingSeverity) list = list.filter(f => f.severity === req.query.findingSeverity);
  res.json(list);
}));
router.get('/tilted/telemetry/landing', safe((req, res) => res.json(landingIntelligence(matchesFor(req.query), { minMatches: Number(req.query.minMatches) || 5 }))));
router.get('/tilted/telemetry/players', safe((req, res) => res.json(playstyleProfiles(matchesFor(req.query)))));
router.get('/tilted/telemetry/versions', safe((req, res) => {
  const all = matchesFor(req.query);
  const versions = [...new Set(all.map(m => m.botVersion).filter(Boolean))];
  res.json(versions.map(v => { const m = versionMetrics(all.filter(x => x.botVersion === v)); delete m._raw; return { botVersion: v, ...m }; }));
}));
router.get('/tilted/telemetry/compare', safe((req, res) => {
  if (!req.query.baseline || !req.query.candidate) return res.status(400).json({ message: 'baseline and candidate bot versions are required.' });
  res.json(compareBotVersions(matchesFor(req.query), String(req.query.baseline), String(req.query.candidate)));
}));

// Export: one match (raw events + report) or a batch of match reports, as JSON.
router.get('/tilted/telemetry/export', safe((req, res) => {
  const list = matchesFor(req.query);
  audit('export', { filters: filters(req.query), matches: list.length });
  res.set('Content-Disposition', `attachment; filename="syndicate-matches-${Date.now()}.json"`);
  res.json(list.map(m => ({ match: m.id, report: matchReport(m), events: req.query.raw === '1' ? m.events : undefined })));
}));

router.post('/tilted/telemetry/datasets', safe((req, res) => {
  const ds = buildDataset(matchesFor(req.body || {}), filters(req.body || {}));
  const errors = validateDataset(ds);
  if (errors.length) return res.status(422).json({ message: 'Dataset failed validation.', errors: errors.slice(0, 20) });
  const out = writeDataset(ds, dir);
  audit('dataset', { id: ds.manifest.id, counts: ds.manifest.counts });
  res.json({ ...ds.manifest, folder: out });
}));
router.get('/tilted/telemetry/datasets', safe((req, res) => {
  const base = path.join(dir, 'datasets');
  let ids = []; try { ids = fs.readdirSync(base); } catch { }
  res.json(ids.map(id => { try { return JSON.parse(fs.readFileSync(path.join(base, id, 'manifest.json'), 'utf8')); } catch { return { id, error: 'unreadable manifest' }; } }));
}));

router.get('/tilted/telemetry/status', safe((req, res) => {
  const all = loadMatches(dir);
  const bytes = (() => { try { return fs.readdirSync(dir).filter(n => n.endsWith('.jsonl')).reduce((s, n) => s + fs.statSync(path.join(dir, n)).size, 0); } catch { return 0; } })();
  res.json({ folder: dir, settings: readSettings(dir), matches: all.length, incomplete: all.filter(m => !m.complete).length, withWarnings: all.filter(m => m.issues.some(i => i.severity !== 'info')).length, bytes, upload: uploader.status() });
}));
// Developer Tools > Send test message. ok only when Discord accepted it; never returns the webhook.
let lastTest = 0;
router.post('/tilted/telemetry/discord-test', async (req, res) => {
  if (Date.now() - lastTest < 5000) return res.status(429).json({ ok: false, message: 'Wait a few seconds between tests.' });
  lastTest = Date.now();
  const result = await deliver(testLines());
  audit('discord_test', { ok: result.ok, via: result.via, status: result.status });
  res.status(result.ok ? 200 : 502).json({ ok: result.ok, via: result.via, status: result.status, configured: webhookConfig().kind, message: result.message });
});
router.post('/tilted/telemetry/settings', safe((req, res) => { const s = writeSettings(dir, req.body || {}); audit('settings', s); res.json(s); }));
router.delete('/tilted/telemetry/matches/:id', safe((req, res) => {
  if (!/^[0-9]{8}-[0-9]{6}-[0-9a-f]{8}$/.test(req.params.id)) return res.status(400).json({ message: 'Bad match id.' });
  const n = deleteMatches(dir, req.params.id); audit('delete', { match: req.params.id, removed: n }); res.json({ removed: n });
}));
router.delete('/tilted/telemetry/matches', safe((req, res) => { const n = deleteMatches(dir); audit('delete_all', { removed: n }); res.json({ removed: n }); }));

// Background: retention every hour, relay upload (if turned on) every 90 s.
const uploader = createUploader(dir);
setInterval(() => { const s = readSettings(dir); const removed = applyRetention(dir, { days: s.retentionDays, maxBytes: s.maxMegabytes * 1024 * 1024 }); if (removed.length) audit('retention', removed); }, 3600000).unref();
setInterval(() => { uploader.tick(loadMatches(dir)).catch(() => { }); }, 90000).unref();

export default router;
