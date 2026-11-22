import fs from 'node:fs';
import {fileURLToPath} from 'node:url';

// Launcher Settings > Item shop (Services/ShopSettings.cs) writes Runtime/Server/shop-settings.json:
// {version:1, hours:6, custom:{"<season number>":{featured:[templateId], daily:[templateId]}}}.
// hours is how long each item shop lasts. A season with a custom shop shows those items in its featured and daily
// slots; slots it leaves empty, and every season without one, stay automatic.
const file = fileURLToPath(new URL('../../../Runtime/Server/shop-settings.json', import.meta.url));
let cache = {mtime: -1, data: {}};
function load() {
  try {
    const mtime = fs.statSync(file).mtimeMs;
    if (mtime !== cache.mtime) cache = {mtime, data: JSON.parse(fs.readFileSync(file, 'utf8')) || {}};
  } catch { cache = {mtime: -1, data: {}}; }
  return cache.data;
}
export function shopWindowMs() {
  const hours = Number(load().hours);
  return (Number.isFinite(hours) && hours >= 1 && hours <= 168 ? Math.round(hours) : 6) * 3600000;
}
export function customShop(season) {
  const entry = load().custom?.[String(season)];
  if (!entry || typeof entry !== 'object') return null;
  const ids = list => Array.isArray(list) ? list.filter(id => typeof id === 'string' && /^Athena[A-Za-z]+:[A-Za-z0-9_.\-]+$/.test(id)).slice(0, 12) : [];
  const shop = {featured: ids(entry.featured), daily: ids(entry.daily)};
  return shop.featured.length || shop.daily.length ? shop : null;
}
