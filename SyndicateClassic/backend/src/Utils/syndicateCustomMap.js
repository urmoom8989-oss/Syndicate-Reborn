// Custom maps for the Dev Kit map editor: the season key a map belongs to, the checks a map must pass before a
// match may load it, the flat manifest the game server reads, and the live-edit commands the editor sends to a
// running Dev Kit match. Pure functions (no files) so they are easy to test; the route does the disk work.

// Launcher build -> the version the game server reports (VersionInfo.FortniteVersion, printed "%.2f").
const BUILD_VERSIONS = { '1.7.2': 1.72, '1.8.2': 1.82, '1.8': 1.82, '3.5.1': 3.5 };
export const BUILDS = ['1.7.2', '1.8.2', '1.11', '3.5.1', '4.5', '5.10', '6.20', '7.40', '8.50', '9.40', '10.31', '10.40', '11.00'];
export function seasonKey(build) {
  const s = String(build == null ? '' : build).trim();
  const v = s in BUILD_VERSIONS ? BUILD_VERSIONS[s] : Number(s);
  return Number.isFinite(v) && v > 0 && v < 100 ? v.toFixed(2) : null;
}

// What the game server can spawn: real class names only (letters, digits, underscore). This also keeps tabs and
// newlines out of the manifest and the live command file.
const CLASS_RE = /^[A-Za-z0-9_]{1,128}$/;
export const LIMITS = { actors: 1500, props: 500, markers: 400, coord: 400000, zMin: -60000, zMax: 250000 };
// Build-piece shorthands the game server turns into the wood piece (PBWA_W1_<shape>_C).
export const BUILD_SHAPES = ['Floor', 'Solid', 'StairW', 'RoofC'];

const finite = (v) => typeof v === 'number' && Number.isFinite(v);
const num = (v) => (typeof v === 'string' && v.trim() !== '' ? Number(v) : v);

// classes: a Set of class names this season exported (map-assets-<season>.json), or null when no Dev Kit match
// has run on that season yet. A missing list is a warning (cannot check), a class not in the list is an error.
export function validateMap(map, classes = null) {
  const errors = [], warnings = [];
  if (!map || typeof map !== 'object' || Array.isArray(map)) return { ok: false, errors: ['Expected a custom-map object.'], warnings, season: null, counts: {} };
  if (map.schema !== 'tilted-custom-map-v1') errors.push('schema must be "tilted-custom-map-v1".');
  if (typeof map.name !== 'string' || !map.name.trim()) warnings.push('The map has no name; it will save as "custom-map".');
  const season = seasonKey(map.baseSeason);
  if (!season) errors.push('Pick a base build (season) for this map.');
  else if (!BUILDS.some((b) => seasonKey(b) === season)) warnings.push(`Base build ${map.baseSeason} is not one the launcher offers.`);
  const list = (k) => (map[k] == null ? [] : Array.isArray(map[k]) ? map[k] : (errors.push(`${k} must be a list.`), []));
  const actors = list('actors'), props = list('props'), pois = list('pois'), rifts = list('riftZones'), landing = list('landingZones'), drops = list('dropRegions');
  if (actors.length > LIMITS.actors) errors.push(`${actors.length} actors is more than the ${LIMITS.actors} a match can take.`);
  if (props.length > LIMITS.props) errors.push(`${props.length} props is more than ${LIMITS.props}.`);
  if (pois.length + rifts.length + landing.length + drops.length > LIMITS.markers) errors.push(`More than ${LIMITS.markers} markers.`);
  const xy = (p, what) => {
    const x = num(p && p.x), y = num(p && p.y);
    if (!finite(x) || !finite(y)) { errors.push(`${what}: x and y must be numbers.`); return false; }
    if (Math.abs(x) > LIMITS.coord || Math.abs(y) > LIMITS.coord) { errors.push(`${what}: (${Math.round(x)}, ${Math.round(y)}) is outside the world.`); return false; }
    return true;
  };
  const unknown = new Set(), seen = new Map();
  actors.slice(0, LIMITS.actors).forEach((a, i) => {
    const what = `Actor ${i + 1}`;
    const cls = a && a.class;
    if (typeof cls !== 'string' || !CLASS_RE.test(cls)) { errors.push(`${what}: "${String(cls).slice(0, 60)}" is not a class name.`); return; }
    if (!xy(a, `${what} (${cls})`)) return;
    const z = num(a.z);
    if (a.z != null && a.z !== '' && (!finite(z) || z < LIMITS.zMin || z > LIMITS.zMax)) errors.push(`${what} (${cls}): height ${a.z} is out of range.`);
    const yaw = num(a.yaw);
    if (a.yaw != null && !finite(yaw)) errors.push(`${what} (${cls}): yaw must be a number.`);
    if (classes && !BUILD_SHAPES.includes(cls) && !classes.has(cls)) unknown.add(cls);
    const key = `${cls}@${Math.round(num(a.x))},${Math.round(num(a.y))},${a.z == null ? 'g' : Math.round(z)}`;
    if (seen.has(key)) warnings.push(`${what} sits exactly on actor ${seen.get(key) + 1} (${cls}).`); else seen.set(key, i);
  });
  for (const c of unknown) errors.push(`${c} does not exist in season ${season} (not in the class list that season's match exported).`);
  if (!classes && actors.length) warnings.push(`No class list for season ${season || '?'} yet: launch one Dev Kit match on it so class names can be checked.`);
  props.forEach((p, i) => { if (typeof (p && p.asset) !== 'string' || !p.asset.trim()) errors.push(`Prop ${i + 1} has no asset.`); else xy(p, `Prop ${i + 1}`); });
  pois.forEach((p, i) => xy(p, `POI ${i + 1}`));
  landing.forEach((p, i) => xy(p, `Landing ${i + 1}`));
  drops.forEach((p, i) => xy(p, `Bot drop ${i + 1}`));
  rifts.forEach((r, i) => {
    const v = ['minX', 'maxX', 'minY', 'maxY'].map((k) => num(r && r[k]));
    if (!v.every(finite)) errors.push(`Rift ${i + 1}: bounds must be numbers.`);
    else if (v[0] >= v[1] || v[2] >= v[3]) errors.push(`Rift ${i + 1}: min must be below max.`);
  });
  if (!actors.length && !props.length && !pois.length && !rifts.length && !landing.length && !drops.length) warnings.push('The map is empty.');
  return { ok: errors.length === 0, errors, warnings, season, counts: { actors: actors.length, props: props.length, markers: pois.length + rifts.length + landing.length + drops.length } };
}

// A flat, tab-separated manifest the game-server loader reads (it has no JSON parser). Tabs and newlines are
// stripped from free text so every directive stays on one line with stable fields. Coordinates are rounded
// integers in Fortnite world units, matching what the editor stores. An actor with no height is placed on the
// ground under it ("g").
export function manifestFor(map) {
  const clean = (v) => String(v == null ? '' : v).replace(/[\t\r\n]+/g, ' ').trim();
  const n = (v) => Math.round(Number(v) || 0);
  const lines = [];
  lines.push(`#\tname\t${clean(map.name) || 'Untitled custom map'}`);
  lines.push(`#\tseason\t${clean(map.baseSeason)}`);
  const key = seasonKey(map.baseSeason);
  if (key) lines.push(`#\tversion\t${key}`);
  for (const a of (map.actors || [])) if (CLASS_RE.test(String(a.class))) lines.push(`actor\t${a.class}\t${n(a.x)}\t${n(a.y)}\t${a.z == null || a.z === '' ? 'g' : n(a.z)}\t${n(a.yaw)}`);
  for (const p of (map.props || [])) { const a = clean(p.asset); if (a) lines.push(`prop\t${a}\t${n(p.x)}\t${n(p.y)}`); }
  for (const p of (map.pois || [])) lines.push(`poi\t${n(p.x)}\t${n(p.y)}\t${clean(p.name) || 'POI'}`);
  for (const p of (map.landingZones || [])) lines.push(`landing\t${n(p.x)}\t${n(p.y)}\t${clean(p.name) || 'Landing'}`);
  for (const p of (map.dropRegions || [])) lines.push(`drop\t${n(p.x)}\t${n(p.y)}\t${clean(p.name) || 'Drop'}`);
  for (const r of (map.riftZones || [])) lines.push(`rift\t${n(r.minX)}\t${n(r.maxX)}\t${n(r.minY)}\t${n(r.maxY)}\t${clean(r.effect) || 'no_build'}\t${clean(r.name) || 'Rift'}`);
  return lines.join('\n') + '\n';
}

// Live editing: the editor sends commands, the game server applies them a few times a second and reports what
// it spawned in mapeditor-live.json. One command per line, tab separated:
//   spawn  <id> <class> <x> <y> <z|g> <yaw>     place a new actor (g = on the ground under x,y)
//          [<pitch> <roll> <sx> <sy> <sz> <alt> <mirror> <mesh> <tex0> <tex1> <tex2> <tex3>]
//                                               optional look of a copied piece (Studio House Plots): the rest of its
//                                               rotation, its scale, the alternate mesh index and mirroring (-1 =
//                                               leave as is), and asset paths of its mesh and texture sets (- = none)
//   here   <id> <class> <distance> <yaw>         place it on the ground that far in front of you
//   move   <id> <x> <y> <z|g> <yaw>              move it (the server re-spawns it there so every client sees it)
//   delete <id>                                  remove it
//   clear                                        remove everything the editor placed this match
// Returns the line, or { error }.
export function opLine(op) {
  if (!op || typeof op !== 'object') return { error: 'Expected a command object.' };
  const id = Number(op.id);
  const idOk = Number.isInteger(id) && id > 0 && id < 1e9;
  const coord = (v, lim) => { const x = num(v); return finite(x) && Math.abs(x) <= lim ? String(Math.round(x)) : null; };
  const height = (v) => (v == null || v === '' || v === 'g' ? 'g' : (() => { const z = num(v); return finite(z) && z >= LIMITS.zMin && z <= LIMITS.zMax ? String(Math.round(z)) : null; })());
  const angle = (v) => { const x = num(v); return finite(x) && Math.abs(x) <= 100000 ? String(Math.round(x * 100) / 100) : null; };
  const yaw = angle(op.yaw == null ? 0 : op.yaw);
  switch (op.op) {
    case 'clear': return 'clear';
    case 'delete': return idOk ? `delete\t${id}` : { error: 'delete needs an id.' };
    case 'spawn': case 'move': {
      if (!idOk) return { error: `${op.op} needs an id.` };
      if (op.op === 'spawn' && !CLASS_RE.test(String(op.class))) return { error: 'spawn needs a class name.' };
      const x = coord(op.x, LIMITS.coord), y = coord(op.y, LIMITS.coord), z = height(op.z);
      if (x == null || y == null || z == null || yaw == null) return { error: `${op.op}: position out of range.` };
      if (op.op === 'move') return `move\t${id}\t${x}\t${y}\t${z}\t${yaw}`;
      const line = `spawn\t${id}\t${op.class}\t${x}\t${y}\t${z}\t${yaw}`;
      const look = lookFields(op);
      return look && look.error ? look : look ? `${line}\t${look}` : line;
    }
    case 'here': {
      if (!idOk) return { error: 'here needs an id.' };
      if (!CLASS_RE.test(String(op.class))) return { error: 'here needs a class name.' };
      const d = coord(op.distance == null ? 400 : op.distance, 5000);
      if (d == null || yaw == null) return { error: 'here: distance out of range.' };
      return `here\t${id}\t${op.class}\t${d}\t${yaw}`;
    }
    default: return { error: `Unknown command "${String(op.op).slice(0, 20)}".` };
  }
}

// Asset paths of a copied piece's mesh and texture sets, e.g. /Game/Building/Meshes/Foo.Foo
const ASSET_RE = /^\/[A-Za-z0-9_\-\/.]{1,250}$/;
const LOOK_KEYS = ['pitch', 'roll', 'sx', 'sy', 'sz', 'alt', 'mir', 'mesh', 'tex'];
// The optional look fields of a spawn: null when there are none, the tab-separated text, or { error }.
function lookFields(op) {
  if (!LOOK_KEYS.some((k) => op[k] != null)) return null;
  const real = (v, def, lo, hi, places) => { const x = num(v == null ? def : v); if (!finite(x) || x < lo || x > hi) return null; const f = 10 ** places; return String(Math.round(x * f) / f); };
  const whole = (v, lo, hi) => { const x = num(v == null ? -1 : v); return Number.isInteger(x) && x >= lo && x <= hi ? String(x) : null; };
  const asset = (v) => (v == null || v === '' || v === '-' ? '-' : ASSET_RE.test(String(v)) ? String(v) : null);
  const f = [real(op.pitch, 0, -360, 360, 2), real(op.roll, 0, -360, 360, 2),
    real(op.sx, 1, 0.01, 100, 4), real(op.sy, 1, 0.01, 100, 4), real(op.sz, 1, 0.01, 100, 4),
    whole(op.alt, -1, 255), whole(op.mir, -1, 1), asset(op.mesh)];
  const tex = op.tex == null ? [] : Array.isArray(op.tex) ? op.tex : null;
  if (!tex || tex.length > 4) return { error: 'spawn: tex must be a list of up to 4 asset paths.' };
  for (let i = 0; i < 4; i++) f.push(asset(tex[i]));
  return f.some((v) => v == null) ? { error: 'spawn: bad pitch, roll, scale, alt, mir, mesh or tex.' } : f.join('\t');
}

// In-game placing switch for the match server (Runtime/Server/mapeditor-ingame.txt): on, the class the place key
// puts down, grid snapping for build pieces, and the place / turn / remove keys as Windows key codes. Returns
// the file text, or { error }.
export const INGAME_KEYS = { place: 0x78, turn: 0x79, remove: 0x2e }; // F9, F10, Delete
export function ingameFile(body) {
  const b = body && typeof body === 'object' ? body : {};
  const on = !!b.on, cls = b.class == null ? '' : String(b.class);
  if (cls && !CLASS_RE.test(cls)) return { error: 'Not a class name.' };
  if (on && !cls) return { error: 'Pick a class to place first.' };
  const keys = { ...INGAME_KEYS };
  for (const k of Object.keys(INGAME_KEYS)) {
    if (b.keys && b.keys[k] != null) { const v = Number(b.keys[k]); if (!Number.isInteger(v) || v < 1 || v > 254) return { error: `Bad ${k} key.` }; keys[k] = v; }
  }
  if (new Set(Object.values(keys)).size !== 3) return { error: 'Use three different keys.' };
  return `on=${on ? 1 : 0}\nclass=${cls}\nsnap=${b.snap === false ? 0 : 1}\nplace=${keys.place}\nturn=${keys.turn}\nremove=${keys.remove}\n`;
}

// The class names a season's map-assets file lists (every category), or null when the file has none.
export function classSet(assets) {
  if (!assets || typeof assets !== 'object' || !assets.classes || typeof assets.classes !== 'object') return null;
  const out = new Set();
  for (const v of Object.values(assets.classes)) if (Array.isArray(v)) for (const c of v) if (typeof c === 'string') out.add(c);
  return out.size ? out : null;
}
