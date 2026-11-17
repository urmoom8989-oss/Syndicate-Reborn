// Custom-map editor service (Dev Kit only, this PC only). The launcher opens the editor here; it reads the
// live prefab/prop/landmark list the game server exports (Runtime/Server/map-assets.json) and saves finished
// maps natively (Runtime/Server/custom-maps/<slug>.json) - no copy-paste, no CORS games, because the editor
// page is served same-origin from this backend.
//
// Live editing: while a Dev Kit match runs (TILTED_DEV_LIVE=1 on the game server), the editor's commands are
// appended to Runtime/Server/mapeditor-ops.txt; the game server applies them a few times a second and writes
// what it really spawned (and where you are) to mapeditor-live.json.
//
// Each season has its own active map (custom-map-active-<version>.txt, e.g. -4.50). A map only becomes
// active after it passes validateMap against the class list that season's match exported.
//
// Gating is its own (not telemetry's authorize, which blocks any browser Origin): Dev Kit build, a loopback
// client, and the developer token (same token file the telemetry dev tools use). A browser page served from
// here is same-origin, so its fetches are allowed as long as they carry the token.
import express from 'express';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { validateMap, manifestFor, seasonKey, opLine, classSet, ingameFile } from '../Utils/syndicateCustomMap.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const serverDir = path.resolve(here, '../../../Runtime/Server');
const tokenPath = path.join(serverDir, 'telemetry', 'dev-token');
const assetsPath = path.join(serverDir, 'map-assets.json');
const mapsDir = path.join(serverDir, 'custom-maps');
const activeIndex = path.join(mapsDir, 'active.json');
const opsPath = path.join(serverDir, 'mapeditor-ops.txt');
const livePath = path.join(serverDir, 'mapeditor-live.json');
const ingamePath = path.join(serverDir, 'mapeditor-ingame.txt');
const editorHtml = path.join(here, 'mapeditor-app.html');

const LOCAL = new Set(['127.0.0.1', '::1', '::ffff:127.0.0.1']);
const devkit = () => process.env.TILTED_DEVKIT === '1';
function readToken() { try { return fs.readFileSync(tokenPath, 'utf8').trim(); } catch { return ''; } }
function slugify(s) { return String(s || 'custom-map').toLowerCase().replace(/[^a-z0-9._-]+/g, '-').replace(/^-+|-+$/g, '').slice(0, 64) || 'custom-map'; }
function readJson(file) { try { return JSON.parse(fs.readFileSync(file, 'utf8')); } catch { return null; } }
// The class list a season's match exported (map-assets-<version>.json), or null if none ran on it yet.
function seasonClasses(key) { return key ? classSet(readJson(path.join(serverDir, `map-assets-${key}.json`))) : null; }
const activeFile = (key) => path.join(serverDir, `custom-map-active-${key}.txt`);

// Make a saved map the one matches on its season load. Refuses (returns the validation) when it has errors.
function activate(map, slug) {
  const v = validateMap(map, seasonClasses(seasonKey(map.baseSeason)));
  if (!v.ok) return { active: false, validation: v };
  fs.writeFileSync(activeFile(v.season), manifestFor(map));
  // The old single manifest loaded on every season; per-season files replace it.
  try { fs.unlinkSync(path.join(serverDir, 'custom-map-active.txt')); } catch {}
  const index = readJson(activeIndex) || {};
  index[v.season] = slug;
  fs.writeFileSync(activeIndex, JSON.stringify(index, null, 2));
  return { active: true, season: v.season, validation: v };
}

// Allow if: Dev Kit, loopback address, and the token matches (from ?token= on the page URL or the
// x-tilted-dev header on fetches). The editor page itself is allowed to load so it can then send the token.
function gate(req, res, next) {
  if (!devkit()) return res.status(404).json({ error: 'Developer tools are only available in Dev Kit builds.' });
  const addr = (req.socket.remoteAddress || '').replace('::ffff:', '::ffff:');
  if (!LOCAL.has(req.socket.remoteAddress) && !LOCAL.has(addr)) return res.status(403).json({ error: 'Map editor is available on this PC only.' });
  next();
}
function tokenOk(req) {
  const want = readToken();
  if (want.length < 32) return false;
  const got = String(req.headers['x-tilted-dev'] || req.query.token || '');
  return got === want;
}
function authed(req, res, next) {
  if (!tokenOk(req)) return res.status(401).json({ error: 'Developer token required.' });
  next();
}

const router = express.Router();

// The editor page (served even without the token so it can read ?token= and use it for data calls).
router.get('/tilted/mapeditor', gate, (req, res) => {
  fs.readFile(editorHtml, (err, buf) => {
    if (err) return res.status(500).send('Map editor page is missing from this build.');
    res.type('html').send(buf);
  });
});

// Live asset vocabulary the game server exported (prefabs/props/landmarks and the spawnable actor classes).
// ?season=4.5 picks that season's list; otherwise the last match's. Empty until a Dev Kit match has run.
router.get('/tilted/mapeditor/assets', gate, authed, (req, res) => {
  const key = req.query.season ? seasonKey(req.query.season) : null;
  const file = key ? path.join(serverDir, `map-assets-${key}.json`) : assetsPath;
  fs.readFile(file, 'utf8', (err, text) => {
    const none = { ready: false, meshes: [], prefabs: [], locations: [], classes: {} };
    if (err) return res.json({ ...none, note: key ? `No asset list for season ${key} yet - launch a Dev Kit match on it once.` : 'No asset list yet - launch a Dev Kit match once so the game server can export it.' });
    try { res.json({ ready: true, ...JSON.parse(text) }); } catch { res.json({ ...none, note: 'Asset file is not valid JSON yet.' }); }
  });
});

// Check a map without saving it.
router.post('/tilted/mapeditor/validate', gate, authed, express.json({ limit: '4mb' }), (req, res) => {
  const map = req.body;
  res.json(validateMap(map, seasonClasses(seasonKey(map && map.baseSeason))));
});

// Save a custom-map.json natively (no download needed). The work is always kept; it becomes the active map for
// its season only when it passes validation.
router.post('/tilted/mapeditor/save', gate, authed, express.json({ limit: '4mb' }), (req, res) => {
  const map = req.body;
  if (!map || typeof map !== 'object' || map.schema !== 'tilted-custom-map-v1') return res.status(400).json({ error: 'Expected a tilted-custom-map-v1 object.' });
  const slug = slugify(map.name);
  try {
    fs.mkdirSync(mapsDir, { recursive: true });
    fs.writeFileSync(path.join(mapsDir, slug + '.json'), JSON.stringify(map, null, 2));
    const a = activate(map, slug);
    res.json({ ok: true, slug, file: path.join('Runtime', 'Server', 'custom-maps', slug + '.json'), ...a });
  } catch (e) { res.status(500).json({ error: 'Could not save: ' + e.message }); }
});

// List saved custom maps (name, slug, season, and whether it is the active map for that season).
router.get('/tilted/mapeditor/maps', gate, authed, (req, res) => {
  const index = readJson(activeIndex) || {};
  let out = [];
  try {
    for (const f of fs.readdirSync(mapsDir)) {
      if (!f.endsWith('.json') || f === 'active.json') continue;
      const m = readJson(path.join(mapsDir, f)); if (!m) continue;
      const slug = f.replace(/\.json$/, ''), key = seasonKey(m.baseSeason);
      out.push({ slug, name: m.name || f, baseSeason: m.baseSeason || null, season: key, active: !!key && index[key] === slug && fs.existsSync(activeFile(key)), actors: m.actors?.length || 0, markers: (m.pois?.length || 0) + (m.riftZones?.length || 0) + (m.landingZones?.length || 0) + (m.dropRegions?.length || 0) });
    }
  } catch {}
  res.json({ maps: out });
});

// One saved map, to open it in the editor again.
router.get('/tilted/mapeditor/map/:slug', gate, authed, (req, res) => {
  const m = readJson(path.join(mapsDir, slugify(req.params.slug) + '.json'));
  if (!m) return res.status(404).json({ error: 'No saved map with that name.' });
  res.json(m);
});

// Make a saved map active for its season (validated first), or turn a season's custom map off.
router.post('/tilted/mapeditor/activate', gate, authed, express.json(), (req, res) => {
  const slug = slugify(req.body && req.body.slug);
  const m = readJson(path.join(mapsDir, slug + '.json'));
  if (!m) return res.status(404).json({ error: 'No saved map with that name.' });
  try { const a = activate(m, slug); res.status(a.active ? 200 : 422).json({ slug, ...a }); } catch (e) { res.status(500).json({ error: e.message }); }
});
router.post('/tilted/mapeditor/deactivate', gate, authed, express.json(), (req, res) => {
  const key = seasonKey(req.body && req.body.season);
  if (!key) return res.status(400).json({ error: 'Which season?' });
  try { fs.unlinkSync(activeFile(key)); } catch {}
  const index = readJson(activeIndex) || {}; delete index[key];
  try { fs.mkdirSync(mapsDir, { recursive: true }); fs.writeFileSync(activeIndex, JSON.stringify(index, null, 2)); } catch {}
  res.json({ ok: true, season: key });
});

// What the running Dev Kit match reports: its season, where you are, and every actor the editor placed. The
// game server rewrites the file twice a second, so an old file means no live match.
router.get('/tilted/mapeditor/live', gate, authed, (req, res) => {
  // The editor polls this while it is open: keep its in-game placing switch fresh (the match server ignores a
  // switch file older than 15 s, so closing the editor turns in-game placing off).
  try { const now = new Date(); fs.utimesSync(ingamePath, now, now); } catch {}
  let st = null; try { st = fs.statSync(livePath); } catch {}
  const age = st ? Date.now() - st.mtimeMs : Infinity;
  const data = age < 4000 ? readJson(livePath) : null;
  if (!data) return res.json({ live: false, note: 'No Dev Kit match is running (start one from the Dev Kit launcher).' });
  res.json({ live: true, ...data });
});

// Send live commands (see opLine in Utils/syndicateCustomMap.js). Each is checked; bad ones are reported, good ones
// are queued for the game server.
router.post('/tilted/mapeditor/live', gate, authed, express.json({ limit: '1mb' }), (req, res) => {
  const ops = Array.isArray(req.body && req.body.ops) ? req.body.ops : [];
  if (!ops.length || ops.length > 1500) return res.status(400).json({ error: 'Send 1 to 1500 commands.' });
  const lines = [], rejected = [];
  ops.forEach((op, i) => { const l = opLine(op); if (typeof l === 'string') lines.push(l); else rejected.push({ index: i, error: l.error }); });
  try { if (lines.length) fs.appendFileSync(opsPath, lines.join('\n') + '\n'); } catch (e) { return res.status(500).json({ error: 'Could not queue: ' + e.message }); }
  res.json({ ok: rejected.length === 0, queued: lines.length, rejected });
});

// In-game placing: which class the place key puts down, grid snapping, and the three keys (see ingameFile).
router.post('/tilted/mapeditor/ingame', gate, authed, express.json(), (req, res) => {
  const text = ingameFile(req.body);
  if (typeof text !== 'string') return res.status(400).json(text);
  try { fs.mkdirSync(serverDir, { recursive: true }); fs.writeFileSync(ingamePath, text); res.json({ ok: true }); } catch (e) { res.status(500).json({ error: e.message }); }
});

// A real BR island minimap that you drop in yourself (your own game image), served to the editor so it can
// show the ACTUAL map instead of the built-in schematic. Put it at Runtime/Server/island/chapter1.(png|jpg|webp)
// for Chapter 1 seasons, chapter2.* for 11.00. Nothing ships one - this is your file, on this PC only. The
// editor calls this when you press "BR island"; a 404 just means it falls back to the drawn reference.
const islandDir = path.join(serverDir, 'island');
const ISLAND_TYPES = { '.png': 'image/png', '.jpg': 'image/jpeg', '.jpeg': 'image/jpeg', '.webp': 'image/webp' };
router.get('/tilted/mapeditor/island', gate, authed, (req, res) => {
  const ch = String(req.query.chapter) === '2' ? 'chapter2' : 'chapter1'; // fixed names, not user paths
  for (const ext of Object.keys(ISLAND_TYPES)) {
    const f = path.join(islandDir, ch + ext);
    if (fs.existsSync(f)) { res.type(ISLAND_TYPES[ext]); return fs.createReadStream(f).pipe(res); }
  }
  res.status(404).json({ error: `No minimap at Runtime/Server/island/${ch}.png - using the built-in reference.` });
});

export default router;
