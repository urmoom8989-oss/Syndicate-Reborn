import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {MODES, enabledModes} from './syndicateSeasonModes.js';

// Launcher Settings > News & tournaments (Services/NewsSettings.cs) writes Runtime/Server/news-settings.json:
// {version:1,
//  news:[{id, title, body, image, builds:["8.50", ...] (empty = every season)}],
//  tournaments:[{id, title, subtitle, description, schedule, mode, begin, end, image, color, builds, postInNews}]}.
// News posts go in the game's News tab (contentpages battleroyalenews). Tournaments go in the Compete tab on the
// seasons that have one (8.50 to 12.50, Routes/events.js) and, with postInNews, in the News tab of every season.
// Pictures are served only from the launcher's own folder (%APPDATA%\SyndicateLauncher\NewsImages).
const file = fileURLToPath(new URL('../../../Runtime/Server/news-settings.json', import.meta.url));
export const NEWS_IMAGE_DIR = process.env.APPDATA ? path.join(process.env.APPDATA, 'SyndicateLauncher', 'NewsImages') : '';
let cache = {mtime: -1, data: {}};
function load(source) {
  if (source) return source;
  try {
    const mtime = fs.statSync(file).mtimeMs;
    if (mtime !== cache.mtime) cache = {mtime, data: JSON.parse(fs.readFileSync(file, 'utf8')) || {}};
  } catch { cache = {mtime: -1, data: {}}; }
  return cache.data;
}

const text = (value, max) => typeof value === 'string' ? value.replace(/[\u0000-\u0008\u000b\u000c\u000e-\u001f]/g, '').trim().slice(0, max) : '';
const safeId = value => String(value || '').replace(/[^A-Za-z0-9_]/g, '').slice(0, 40);
const forBuild = (entry, build) => !Array.isArray(entry.builds) || !entry.builds.length || entry.builds.map(String).includes(String(build));
const imageName = value => { const name = path.basename(String(value || '')); return /^[A-Za-z0-9_.\-]+\.(png|jpe?g|gif|bmp)$/i.test(name) ? name : ''; };
const date = value => { const t = Date.parse(value); return Number.isFinite(t) ? new Date(t) : null; };
const color = value => /^[0-9A-Fa-f]{6}$/.test(String(value || '').replace(/^#/, '')) ? String(value).replace(/^#/, '').toUpperCase() : '';

// A picture the launcher saved, as a file path this backend may send, or null.
export function newsImageFile(name) {
  const clean = imageName(name);
  if (!NEWS_IMAGE_DIR || !clean) return null;
  const full = path.join(NEWS_IMAGE_DIR, clean);
  try { return fs.statSync(full).isFile() ? full : null; } catch { return null; }
}

export function customNews(build, {source} = {}) {
  const list = Array.isArray(load(source).news) ? load(source).news : [];
  return list.filter(n => n && typeof n === 'object' && forBuild(n, build))
    .map(n => ({id: safeId(n.id) || 'news', title: text(n.title, 80), body: text(n.body, 1200), image: imageName(n.image)}))
    .filter(n => n.title || n.body).slice(0, 20);
}

// Tournaments for a build: only modes this build has turned on, so the Compete tab never offers one that can't start.
export function customTournaments(build, {source, modes} = {}) {
  const list = Array.isArray(load(source).tournaments) ? load(source).tournaments : [];
  const on = new Set(modes || enabledModes(build));
  const out = [];
  for (const t of list) {
    if (!t || typeof t !== 'object' || !forBuild(t, build)) continue;
    const id = safeId(t.id), mode = String(t.mode || 'solo').toLowerCase();
    if (!id || !MODES[mode]?.playlist || !on.has(mode)) continue;
    const begin = date(t.begin) || new Date('2020-01-01T00:00:00Z');
    let end = date(t.end) || new Date('9999-12-31T23:59:59Z');
    if (end <= begin) end = new Date(begin.getTime() + 3600000);
    out.push({
      id, mode, playlist: MODES[mode].playlist,
      displayId: 'SYN_CUP_' + id,
      title: text(t.title, 40) || 'Tournament', subtitle: text(t.subtitle, 40),
      description: text(t.description, 600), schedule: text(t.schedule, 80),
      begin, end, image: imageName(t.image), color: color(t.color) || 'F81B2D',
      postInNews: t.postInNews !== false,
    });
  }
  return out.slice(0, 12);
}
