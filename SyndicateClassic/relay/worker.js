// Syndicate - Cloudflare Worker ("springs-relay").
//
//   POST /interactions   Discord bot: /code commands for Syndicate admins (make item or V-Bucks codes, check,
//                        revoke, list, stats).
//   POST /redeem         Syndicate backends redeem a code here. Each code works ONCE for everyone: the first
//                        redeem marks it used in the D1 database, every later try is refused.
//   POST /redeem/release Gives a code back if the player's backend could not add the item (15 min window).
//   GET  /setup?key=...  One time (and after changing commands): registers the bot's slash commands.
//   POST /error          Launcher error reports (unchanged).
//   GET  /tester/start   Tester launcher: sends the player to Discord to sign in (state = the launcher's session).
//   GET  /tester/callback Discord sends them back here; we check they have the Tester role in our server.
//   POST /tester/poll    The launcher picks up its tester token once the sign-in finished.
//   POST /tester/check   Every tester launch (and every 20 min while open): is this token still a Tester?
//                        Also says which tester build is newest, so testers update like devs do.
//   POST /tester/publish Release script: announce a new tester build (header x-publish-key: TESTER_PUBLISH_KEY).
//   POST /report         Bot-learning match feed: forwards consenting players' pseudonymous gameplay
//                        reports to the DISCORD_WEBHOOK secret (503 when it is not set).
//
// Settings (Cloudflare dashboard > the Worker > Settings > Variables and Secrets):
//   DISCORD_PUBLIC_KEY  text    the app's Public Key (General Information page)
//   DISCORD_APP_ID      text    the Application ID
//   DISCORD_TOKEN       secret  the bot token (never in code or chat)
//   SETUP_KEY           secret  any long random word, used once for /setup
//   ADMIN_IDS           text    optional, extra admin Discord user IDs, comma separated
//   DISCORD_ERRORS_WEBHOOK secret (error reports, as before)
//   DISCORD_WEBHOOK        secret (bot-learning match feed; without it /report answers 503)
//   DISCORD_CLIENT_SECRET  secret  OAuth2 > Client Secret (tester sign-in)
//   TESTER_GUILD_ID     text    the Syndicate Discord server ID
//   TESTER_ROLE_IDS     text    role IDs that count as Tester, comma separated (add the Dev role too if devs use it)
//   TESTER_PUBLISH_KEY  secret  any long random word; the tester release script sends it to /tester/publish
// The bot (DISCORD_TOKEN) must be a member of that server to read a member's roles.
// Binding: D1 database as DB (Settings > Bindings > D1 database, variable name DB).
//
// Codes are never stored: only their SHA-256 (same scheme as the Syndicate backend), plus the last 4
// characters so admins can tell codes apart in /code list.

const ADMINS = ['1234172660728987709', '815797413418893324']; // unknown, Sact
// Tester role(s). TESTER_ROLE_IDS (comma separated) overrides this if set in the Worker; otherwise this default is
// used, so the role need not be configured in Cloudflare. Role IDs are not secret.
const TESTER_ROLE_IDS_DEFAULT = '1561525871511081121';
const REWARDS = {
  'psycho-bandit':    'Psycho Bandit',
  'travis-scott':     'Travis Scott',
  'axe-of-champions': 'Axe of Champions',
  'leviathan-axe':    'Leviathan Axe',
  'polo-prodigy':     'Polo Prodigy',
  'kratos':           'Kratos',
  'minty-axe':        'Minty Axe',
};
// Psycho Bandit codes made before the bot (MAKE REDEEM CODES): added once by /setup, so they are one-time too.
const LEGACY_PSYCHO = ["b66e847384ae9aec05661be7a9a3f91083abdbbb9e519c710449c782798b7f6b", "29cf89ff3569ed17d22a6c99e1209ee75bc2d5e1ada46fc4db35e53269d55c58", "4b03747faa0dcb7e509cc9251b5a00b2fbf6eb92d534586e8175fc2b0dd185a1", "626b61844a64106f4d31309357e33cc823876952a328e0a8c459fa929e6587ff", "c005b5076b4e71dd625ceb1e7e5cfc7d80f829340a1173fe1606a04580e2babb", "f4fda282add94fa9e028d701092d9a818050a8c80c4eef987443c5a2a9935a51", "5cfc35bf78cd0783117c8056e30ed39be36fa771bff08faddbdb422d1093f6fa", "0086ecc44b0a7d5b030e179b6b3f9c4a4e9296af43ebd082afaa8771c7b2cea1", "a511cc898f103086c13580f78f9a445d8bd0ac0238d41be8741892f60e6bd5d7", "ce36037b48b00378efe9987a0793bbd299b94c1636b6c84e7e5b2b34079a9385", "df69e811cd83754b7e43f09264563691eb4ef197889415fd4584eee37fd826bd", "2eb08671f6e39b9f8c8fedec40f6fd6a8b1b92e8c027e7cbf84543878d56833b", "085cf4e3ea4e4039735c7be610c9994d6af4269f61ad77269d9673823f23dba2", "3f795e2c800a030b826605df8ebc67fdf90c0539378ad05018285acbb14be4f6", "943dfd7e56c0ec247194904227f6aa200faecf301693d236e9bf571941c71711", "46511a1401fb15e1c2edbe416ea7f128290cb92e3a97562766e844360a882b39", "90620f9d096ba1f7d69913bdbb4a4dbec41f6e614848f9bec939b49cf927c1e5", "3a3c1341cbe58a7f5d31b504e6bf76cb1edd439e6577cbf70ad538f244b377a4", "ae37404cc18d6dd05ed018dfd4dc79519153b3ec81005daa5afa33c01c06d09d", "dcd0debe3587f21f6e74aec33c4d0f13edf3a4e7881cc5a89d061027382e266b"];

// Rewards are either an item slug (in REWARDS) or "vbucks:<amount>" for a V-Bucks code. This turns either into
// a human title for the Discord replies; the backend parses "vbucks:<amount>" itself to credit the wallet.
const commas = n => String(n).replace(/\B(?=(\d{3})+(?!\d))/g, ',');
const rewardTitle = reward => { const m = /^vbucks:(\d+)$/i.exec(reward || ''); return m ? commas(m[1]) + ' V-Bucks' : (REWARDS[reward] || reward); };

const ALPHABET = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789'; // no I, O, 0 or 1
const json = (data, status = 200) => new Response(JSON.stringify(data), { status, headers: { 'content-type': 'application/json' } });
const hex = buf => [...new Uint8Array(buf)].map(b => b.toString(16).padStart(2, '0')).join('');
const normalize = code => { let b = String(code || '').toUpperCase().replace(/[^A-Z0-9]/g, ''); if (b.startsWith('TILT')) b = b.slice(4); return b; };
const hashBody = async body => hex(await crypto.subtle.digest('SHA-256', new TextEncoder().encode('tilted-redeem-v1:' + body)));
const pretty = body => 'TILT-' + body.match(/.{4}/g).join('-');
const masked = hint => 'TILT-****-****-****-' + hint;
function newBody() { const r = crypto.getRandomValues(new Uint8Array(16)); return [...r].map(b => ALPHABET[b % 32]).join(''); }

let schemaReady = false;
async function schema(env) {
  if (schemaReady) return;
  await env.DB.batch([
    env.DB.prepare('CREATE TABLE IF NOT EXISTS codes (hash TEXT PRIMARY KEY, reward TEXT NOT NULL, hint TEXT, note TEXT, created_by TEXT, created_at INTEGER, redeemed_at INTEGER, redeemed_by TEXT, redeemed_name TEXT, claim TEXT, revoked INTEGER DEFAULT 0)'),
    env.DB.prepare('CREATE TABLE IF NOT EXISTS tries (ip TEXT PRIMARY KEY, win INTEGER, count INTEGER)'),
    env.DB.prepare('CREATE TABLE IF NOT EXISTS tester_sessions (session TEXT PRIMARY KEY, install TEXT NOT NULL, created INTEGER, status TEXT, token TEXT, user_id TEXT, name TEXT, reason TEXT)'),
    env.DB.prepare('CREATE TABLE IF NOT EXISTS tester_tokens (token_hash TEXT PRIMARY KEY, user_id TEXT NOT NULL, name TEXT, install TEXT NOT NULL, created INTEGER, last_check INTEGER)'),
    env.DB.prepare('CREATE TABLE IF NOT EXISTS tester_build (id INTEGER PRIMARY KEY, build INTEGER, version TEXT, url TEXT, sha256 TEXT, updated INTEGER)'),
  ]);
  schemaReady = true;
}

// ---- redeem (called by Syndicate backends) ---------------------------------------------------------------
async function tooManyTries(env, ip) {
  const win = Math.floor(Date.now() / 600000); // 10-minute windows, 15 tries each
  const row = await env.DB.prepare('INSERT INTO tries (ip, win, count) VALUES (?1, ?2, 1) ON CONFLICT(ip) DO UPDATE SET count = CASE WHEN win = ?2 THEN count + 1 ELSE 1 END, win = ?2 RETURNING count').bind(ip, win).first();
  return (row?.count || 0) > 15;
}
async function redeem(request, env) {
  let data; try { data = await request.json(); } catch { return json({ ok: false, error: 'bad_request' }, 400); }
  const ip = request.headers.get('CF-Connecting-IP') || 'unknown';
  if (await tooManyTries(env, ip)) return json({ ok: false, error: 'slow_down', message: 'Too many tries. Wait a few minutes.' }, 429);
  const body = normalize(data.code);
  if (body.length !== 16) return json({ ok: false, error: 'invalid', message: 'That code is not valid.' });
  const hash = await hashBody(body), claim = hex(crypto.getRandomValues(new Uint8Array(16)));
  const who = String(data.accountId || '').slice(0, 64), name = String(data.username || '').slice(0, 40);
  // One statement: only an unused, not revoked code changes, so two players can never both get it.
  const won = await env.DB.prepare('UPDATE codes SET redeemed_at = ?1, redeemed_by = ?2, redeemed_name = ?3, claim = ?4 WHERE hash = ?5 AND redeemed_at IS NULL AND revoked = 0 RETURNING reward')
    .bind(Date.now(), who, name, claim, hash).first();
  if (won) return json({ ok: true, reward: won.reward, title: rewardTitle(won.reward), claim });
  const row = await env.DB.prepare('SELECT revoked, redeemed_at FROM codes WHERE hash = ?1').bind(hash).first();
  if (!row) return json({ ok: false, error: 'invalid', message: 'That code is not valid.' });
  if (row.revoked) return json({ ok: false, error: 'revoked', message: 'That code has been cancelled.' });
  return json({ ok: false, error: 'used', message: 'That code has already been used.' });
}
async function release(request, env) {
  let data; try { data = await request.json(); } catch { return json({ ok: false }, 400); }
  const body = normalize(data.code); if (body.length !== 16 || !/^[0-9a-f]{32}$/.test(String(data.claim || ''))) return json({ ok: false }, 400);
  const res = await env.DB.prepare('UPDATE codes SET redeemed_at = NULL, redeemed_by = NULL, redeemed_name = NULL, claim = NULL WHERE hash = ?1 AND claim = ?2 AND redeemed_at > ?3')
    .bind(await hashBody(body), data.claim, Date.now() - 900000).run();
  return json({ ok: !!res.meta?.changes });
}

// ---- Discord bot ----------------------------------------------------------------------------------------
const fromHex = h => new Uint8Array(h.match(/.{2}/g).map(b => parseInt(b, 16)));
async function verified(request, env, body) {
  const sig = request.headers.get('X-Signature-Ed25519'), ts = request.headers.get('X-Signature-Timestamp');
  if (!sig || !ts || !env.DISCORD_PUBLIC_KEY) return false;
  try {
    const key = await crypto.subtle.importKey('raw', fromHex(env.DISCORD_PUBLIC_KEY), { name: 'Ed25519' }, false, ['verify']);
    return await crypto.subtle.verify('Ed25519', key, fromHex(sig), new TextEncoder().encode(ts + body));
  } catch {
    try { // older runtimes name it NODE-ED25519
      const key = await crypto.subtle.importKey('raw', fromHex(env.DISCORD_PUBLIC_KEY), { name: 'NODE-ED25519', namedCurve: 'NODE-ED25519' }, false, ['verify']);
      return await crypto.subtle.verify('NODE-ED25519', key, fromHex(sig), new TextEncoder().encode(ts + body));
    } catch { return false; }
  }
}
const reply = content => json({ type: 4, data: { content: content.slice(0, 1990), flags: 64, allowed_mentions: { parse: [] } } }); // only the admin sees it
const admins = env => new Set([...ADMINS, ...String(env.ADMIN_IDS || '').split(',').map(s => s.trim()).filter(Boolean)]);
const option = (opts, name) => (opts || []).find(o => o.name === name)?.value;

async function command(i, env) {
  const user = i.member?.user || i.user || {};
  if (!admins(env).has(String(user.id))) return reply('Only Syndicate admins can use this.');
  const sub = i.data.options?.[0]; const o = sub?.options || [];
  if (!sub) return reply('Use /code create, /code vbucks, /code check, /code revoke, /code list or /code stats.');
  if (sub.name === 'create') {
    const reward = option(o, 'reward'); if (!REWARDS[reward]) return reply('Unknown reward.');
    const amount = Math.max(1, Math.min(25, Number(option(o, 'amount')) || 1)); const note = String(option(o, 'note') || '').slice(0, 100);
    const made = [], now = Date.now();
    for (let n = 0; n < amount; n++) {
      const body = newBody();
      await env.DB.prepare('INSERT INTO codes (hash, reward, hint, note, created_by, created_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6)').bind(await hashBody(body), reward, body.slice(12), note, String(user.id), now).run();
      made.push(pretty(body));
    }
    return reply('**' + amount + ' ' + REWARDS[reward] + ' code' + (amount > 1 ? 's' : '') + '** (each works once, for one player):\n```\n' + made.join('\n') + '\n```\nCopy them now: they are not stored and cannot be shown again.' + (note ? '\nNote: ' + note : ''));
  }
  if (sub.name === 'vbucks') {
    const vbAmt = Math.floor(Number(option(o, 'vbucks')) || 0);
    if (!Number.isInteger(vbAmt) || vbAmt < 1 || vbAmt > 1000000) return reply('Pick a V-Bucks amount between 1 and 1,000,000.');
    const reward = 'vbucks:' + vbAmt; // the backend parses this and credits the player's wallet
    const amount = Math.max(1, Math.min(25, Number(option(o, 'amount')) || 1)); const note = String(option(o, 'note') || '').slice(0, 100);
    const made = [], now = Date.now();
    for (let n = 0; n < amount; n++) {
      const b = newBody();
      await env.DB.prepare('INSERT INTO codes (hash, reward, hint, note, created_by, created_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6)').bind(await hashBody(b), reward, b.slice(12), note, String(user.id), now).run();
      made.push(pretty(b));
    }
    return reply('**' + amount + ' ' + commas(vbAmt) + ' V-Bucks code' + (amount > 1 ? 's' : '') + '** (each works once, for one player):\n```\n' + made.join('\n') + '\n```\nCopy them now: they are not stored and cannot be shown again.' + (note ? '\nNote: ' + note : ''));
  }
  if (sub.name === 'check' || sub.name === 'revoke') {
    const body = normalize(option(o, 'code')); if (body.length !== 16) return reply('That is not a code (TILT-XXXX-XXXX-XXXX-XXXX).');
    const hash = await hashBody(body);
    const row = await env.DB.prepare('SELECT * FROM codes WHERE hash = ?1').bind(hash).first();
    if (!row) return reply('No such code.');
    if (sub.name === 'revoke') {
      if (row.redeemed_at) return reply('Already redeemed by ' + (row.redeemed_name || 'a player') + ' <t:' + Math.floor(row.redeemed_at / 1000) + ':R>; nothing to cancel.');
      await env.DB.prepare('UPDATE codes SET revoked = 1 WHERE hash = ?1').bind(hash).run();
      return reply('Cancelled ' + masked(row.hint) + ' (' + rewardTitle(row.reward) + '). It can no longer be redeemed.');
    }
    const state = row.revoked ? 'cancelled' : row.redeemed_at ? 'redeemed by **' + (row.redeemed_name || 'unknown') + '** <t:' + Math.floor(row.redeemed_at / 1000) + ':R>' : '**unused**';
    return reply(masked(row.hint) + ' · ' + rewardTitle(row.reward) + ' · ' + state + ' · made <t:' + Math.floor((row.created_at || 0) / 1000) + ':R>' + (row.note ? ' · ' + row.note : ''));
  }
  if (sub.name === 'list') {
    const reward = option(o, 'reward');
    const rows = (await (reward ? env.DB.prepare('SELECT * FROM codes WHERE reward = ?1 ORDER BY created_at DESC LIMIT 20').bind(reward) : env.DB.prepare('SELECT * FROM codes ORDER BY created_at DESC LIMIT 20')).all()).results || [];
    if (!rows.length) return reply('No codes yet.');
    return reply('Newest codes' + (reward ? ' for ' + rewardTitle(reward) : '') + ':\n' + rows.map(r => '`' + masked(r.hint || '????') + '` ' + rewardTitle(r.reward) + ' · ' + (r.revoked ? 'cancelled' : r.redeemed_at ? 'used by ' + (r.redeemed_name || '?') + ' <t:' + Math.floor(r.redeemed_at / 1000) + ':R>' : 'unused') + (r.note ? ' · ' + r.note : '')).join('\n'));
  }
  if (sub.name === 'stats') {
    const rows = (await env.DB.prepare('SELECT reward, COUNT(*) AS total, SUM(CASE WHEN redeemed_at IS NOT NULL THEN 1 ELSE 0 END) AS used, SUM(revoked) AS cancelled FROM codes GROUP BY reward').all()).results || [];
    if (!rows.length) return reply('No codes yet.');
    return reply(rows.map(r => '**' + rewardTitle(r.reward) + '**: ' + r.total + ' made, ' + r.used + ' used, ' + (r.total - r.used - r.cancelled) + ' unused' + (r.cancelled ? ', ' + r.cancelled + ' cancelled' : '')).join('\n'));
  }
  return reply('Unknown command.');
}
async function interactions(request, env) {
  const body = await request.text();
  if (!(await verified(request, env, body))) return new Response('Bad signature', { status: 401 });
  const i = JSON.parse(body);
  if (i.type === 1) return json({ type: 1 }); // Discord's PING
  if (i.type === 2 && i.data?.name === 'code') return command(i, env);
  return reply('Unknown command.');
}

// ---- one-time setup: slash commands + old codes ----------------------------------------------------------
async function setup(url, env) {
  if (!env.SETUP_KEY || url.searchParams.get('key') !== env.SETUP_KEY) return new Response('Not found', { status: 404 });
  if (!env.DISCORD_TOKEN || !env.DISCORD_APP_ID) return new Response('Add DISCORD_TOKEN and DISCORD_APP_ID first.', { status: 500 });
  const choices = Object.entries(REWARDS).map(([value, name]) => ({ name, value }));
  const code = { type: 3, name: 'code', description: 'TILT-XXXX-XXXX-XXXX-XXXX', required: true };
  const commands = [{
    name: 'code', description: 'Syndicate exclusive codes (admins only)', type: 1, dm_permission: false, default_member_permissions: '8',
    options: [
      { type: 1, name: 'create', description: 'Make one-time codes for an exclusive item', options: [
        { type: 3, name: 'reward', description: 'Which item', required: true, choices },
        { type: 4, name: 'amount', description: 'How many codes (1-25, default 1)', min_value: 1, max_value: 25 },
        { type: 3, name: 'note', description: 'Optional note, e.g. who it is for', max_length: 100 }] },
      { type: 1, name: 'vbucks', description: 'Make one-time codes worth V-Bucks', options: [
        { type: 4, name: 'vbucks', description: 'V-Bucks each code gives (1-1,000,000)', required: true, min_value: 1, max_value: 1000000 },
        { type: 4, name: 'amount', description: 'How many codes (1-25, default 1)', min_value: 1, max_value: 25 },
        { type: 3, name: 'note', description: 'Optional note, e.g. who it is for', max_length: 100 }] },
      { type: 1, name: 'check', description: 'Is a code unused, redeemed (by whom) or cancelled?', options: [code] },
      { type: 1, name: 'revoke', description: 'Cancel an unused code', options: [code] },
      { type: 1, name: 'list', description: 'The 20 newest codes and their state', options: [{ type: 3, name: 'reward', description: 'Only this item', choices }] },
      { type: 1, name: 'stats', description: 'Made / used / unused per item' },
    ],
  }];
  const resp = await fetch('https://discord.com/api/v10/applications/' + env.DISCORD_APP_ID + '/commands', {
    method: 'PUT', headers: { authorization: 'Bot ' + env.DISCORD_TOKEN, 'content-type': 'application/json' }, body: JSON.stringify(commands) });
  let seeded = 0;
  for (const hash of LEGACY_PSYCHO) {
    const r = await env.DB.prepare("INSERT OR IGNORE INTO codes (hash, reward, hint, note, created_by, created_at) VALUES (?1, 'psycho-bandit', '????', 'made before the bot', 'legacy', ?2)").bind(hash, Date.now()).run();
    seeded += r.meta?.changes || 0;
  }
  return new Response((resp.ok ? 'Slash commands registered.' : 'Discord refused the commands: HTTP ' + resp.status + ' ' + (await resp.text()).slice(0, 300)) + '\nOlder Psycho Bandit codes added: ' + seeded, { status: resp.ok ? 200 : 502 });
}

// ---- error reports (unchanged) --------------------------------------------------------------------------
const recentErrors = new Map();
const recentReports = new Map();
async function limited(map, ip, ms, key) {
  const now = Date.now();
  if (now - (map.get(ip) || 0) < ms) return true;
  const marker = new Request('https://relay.invalid/' + key + '/' + encodeURIComponent(ip));
  if (await caches.default.match(marker)) return true;
  map.set(ip, now);
  if (map.size > 5000) map.clear();
  await caches.default.put(marker, new Response('1', { headers: { 'Cache-Control': 'max-age=' + Math.ceil(ms / 1000) } }));
  return false;
}
async function errorReport(request, env) {
  if (!env.DISCORD_ERRORS_WEBHOOK) return new Response('Error reports not configured', { status: 503 });
  const body = await request.text();
  if (body.length > 262144) return new Response('Too large', { status: 413 });
  let data;
  try { data = JSON.parse(body); } catch { return new Response('Bad JSON', { status: 400 }); }
  if (data.source !== 'tilted') return new Response('Only Syndicate reports are accepted', { status: 403 });
  if (typeof data.summary !== 'string' || !data.summary.trim()) return new Response('Bad report', { status: 400 });
  const ip = request.headers.get('CF-Connecting-IP') || 'unknown';
  if (await limited(recentErrors, ip, 120000, 'error')) return new Response('Slow down', { status: 429 });
  const summary = data.summary.replace(/[`@]/g, "'").slice(0, 1800);
  const form = new FormData();
  form.append('payload_json', JSON.stringify({ username: 'Syndicate error reports', content: '```\n' + summary + '\n```', allowed_mentions: { parse: [] } }));
  if (typeof data.log === 'string' && data.log.length)
    form.append('files[0]', new Blob([data.log.slice(0, 250000)], { type: 'text/plain' }), 'error-report.txt');
  const resp = await fetch(env.DISCORD_ERRORS_WEBHOOK, { method: 'POST', body: form });
  return new Response(resp.ok ? 'ok' : 'discord error', { status: resp.ok ? 200 : 502 });
}
// Bot-learning match feed: consenting players' per-match gameplay reports (pseudonymous, no PII) from the
// whole playerbase, forwarded to the DISCORD_WEBHOOK secret (set on the Worker, never in the client) exactly
// like error reports use DISCORD_ERRORS_WEBHOOK. Returns 503 when no webhook is configured so the backend
// knows the feed is off. Body: { source:'tilted', lines:[...] }, same shape the backend already sends.
async function matchFeed(request, env) {
  if (!env.DISCORD_WEBHOOK) return new Response('Match feed not configured', { status: 503 });
  const body = await request.text();
  if (body.length > 65536) return new Response('Too large', { status: 413 });
  let data;
  try { data = JSON.parse(body); } catch { return new Response('Bad JSON', { status: 400 }); }
  if (data.source !== 'tilted') return new Response('Only Syndicate reports are accepted', { status: 403 });
  if (!Array.isArray(data.lines) || !data.lines.length) return new Response('Bad report', { status: 400 });
  const ip = request.headers.get('CF-Connecting-IP') || 'unknown';
  if (await limited(recentReports, ip, 60000, 'report')) return new Response('Slow down', { status: 429 });
  const text = data.lines.map(l => String(l).replace(/[`@]/g, "'").replace(/[\r\n]+/g, ' ')).slice(0, 15).join('\n').slice(0, 1800);
  const resp = await fetch(env.DISCORD_WEBHOOK, {
    method: 'POST', headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ username: 'Syndicate match tracking', content: '```\n' + text + '\n```', allowed_mentions: { parse: [] } }),
  });
  return new Response(resp.ok ? 'ok' : 'discord error', { status: resp.ok ? 200 : 502 });
}

// ---- Tester launcher ------------------------------------------------------------------------------------
// The tester EXE only runs for people with a Tester role in the Syndicate Discord. First start: the
// launcher opens /tester/start in the browser with a random session and its install ID (a hash of the PC),
// Discord signs them in (scope "identify" only), /tester/callback reads their roles with the bot and, if
// they are a Tester, makes a token tied to that PC. The launcher polls /tester/poll for it. Every start (and
// every 20 minutes while it is open) /tester/check asks Discord again, so removing the role locks them out
// and a new tester only needs the role. A token copied to another PC does not work (install ID mismatch).
const TESTER_DEVICES = 3;                 // PCs per Discord account
const SESSION_MS = 10 * 60 * 1000;        // a sign-in must finish within 10 minutes
const isHex = (v, n) => typeof v === 'string' && new RegExp('^[0-9a-f]{' + n + '}$').test(v);
const tokenHash = async t => hex(await crypto.subtle.digest('SHA-256', new TextEncoder().encode('tilted-tester-v1:' + t)));
const callbackUrl = url => url.origin + '/tester/callback';
const page = (title, text, ok) => new Response(`<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>${title}</title>
<body style="margin:0;min-height:100vh;display:grid;place-items:center;background:#06070c;color:#eef0fa;font-family:Segoe UI,system-ui,sans-serif">
<div style="max-width:520px;padding:32px 36px;border:1px solid #2a2f45;border-radius:18px 4px 18px 4px;background:#0c0f1a">
<div style="display:inline-block;padding:3px 12px;transform:skewX(-14deg);background:${ok ? '#ffd23f' : '#ff5a6a'};color:#1a1200;font-weight:700;font-size:12px">SYNDICATE · TESTERS</div>
<h1 style="font-size:34px;font-style:italic;margin:16px 0 8px">${title}</h1><p style="color:#a9b0cc;line-height:1.5">${text}</p></div></body>`, { headers: { 'content-type': 'text/html; charset=utf-8' } });

// 'ok' | 'no-role' | 'not-member' | 'unavailable'
async function testerRole(env, userId) {
  if (!env.DISCORD_TOKEN || !env.TESTER_GUILD_ID) return 'unavailable';
  const r = await fetch(`https://discord.com/api/v10/guilds/${env.TESTER_GUILD_ID}/members/${userId}`, { headers: { authorization: 'Bot ' + env.DISCORD_TOKEN } });
  if (r.status === 404) return 'not-member';
  if (!r.ok) return 'unavailable';
  const member = await r.json();
  const wanted = String(env.TESTER_ROLE_IDS || TESTER_ROLE_IDS_DEFAULT).split(',').map(s => s.trim()).filter(Boolean);
  return (member.roles || []).some(role => wanted.includes(role)) ? 'ok' : 'no-role';
}
const roleReason = state => state === 'not-member' ? 'You are not in the Syndicate Discord server.'
  : state === 'no-role' ? 'Your Discord account does not have the Tester role.' : 'Discord could not be reached. Try again in a minute.';

async function testerStart(url, env) {
  const session = url.searchParams.get('session'), install = url.searchParams.get('install');
  if (!isHex(session, 32) || !isHex(install, 32)) return page('Invalid link', 'Open this from the Syndicate Tester launcher.', false);
  if (!env.DISCORD_APP_ID || !env.DISCORD_CLIENT_SECRET) return page('Not set up yet', 'Tester sign-in is not configured on the server yet. Ask a dev.', false);
  await env.DB.prepare('DELETE FROM tester_sessions WHERE created < ?1').bind(Date.now() - SESSION_MS).run();
  await env.DB.prepare("INSERT OR REPLACE INTO tester_sessions (session, install, created, status) VALUES (?1, ?2, ?3, 'pending')").bind(session, install, Date.now()).run();
  const auth = new URL('https://discord.com/oauth2/authorize');
  auth.search = new URLSearchParams({ client_id: env.DISCORD_APP_ID, response_type: 'code', scope: 'identify', redirect_uri: callbackUrl(url), state: session, prompt: 'consent' });
  return Response.redirect(auth.toString(), 302);
}

async function testerCallback(url, env) {
  const session = url.searchParams.get('state'), code = url.searchParams.get('code');
  if (!isHex(session, 32)) return page('Invalid sign-in', 'Start again from the Tester launcher.', false);
  const row = await env.DB.prepare('SELECT * FROM tester_sessions WHERE session = ?1').bind(session).first();
  if (!row || row.created < Date.now() - SESSION_MS) return page('Sign-in expired', 'Close this tab and press Sign in with Discord again in the launcher.', false);
  const fail = async (reason) => { await env.DB.prepare("UPDATE tester_sessions SET status = 'denied', reason = ?1 WHERE session = ?2").bind(reason, session).run(); return page('No access', reason + ' The Tester launcher will close.', false); };
  if (!code) return fail('Discord sign-in was cancelled.');
  const tokenResp = await fetch('https://discord.com/api/v10/oauth2/token', { method: 'POST', headers: { 'content-type': 'application/x-www-form-urlencoded' },
    body: new URLSearchParams({ client_id: env.DISCORD_APP_ID, client_secret: env.DISCORD_CLIENT_SECRET, grant_type: 'authorization_code', code, redirect_uri: callbackUrl(url) }) });
  if (!tokenResp.ok) return page('Sign-in failed', 'Discord did not accept the sign-in. Try again from the launcher.', false);
  const { access_token } = await tokenResp.json();
  const meResp = await fetch('https://discord.com/api/v10/users/@me', { headers: { authorization: 'Bearer ' + access_token } });
  if (!meResp.ok) return page('Sign-in failed', 'Could not read your Discord account. Try again.', false);
  const me = await meResp.json();
  const name = String(me.global_name || me.username || 'Tester').slice(0, 40);
  const state = await testerRole(env, me.id);
  if (state === 'unavailable') return page('Try again', roleReason(state), false);
  if (state !== 'ok') return fail(roleReason(state));
  const token = hex(crypto.getRandomValues(new Uint8Array(32)));
  const now = Date.now();
  await env.DB.batch([
    env.DB.prepare('DELETE FROM tester_tokens WHERE user_id = ?1 AND install = ?2').bind(me.id, row.install),
    env.DB.prepare('INSERT INTO tester_tokens (token_hash, user_id, name, install, created, last_check) VALUES (?1, ?2, ?3, ?4, ?5, ?5)').bind(await tokenHash(token), me.id, name, row.install, now),
    env.DB.prepare('DELETE FROM tester_tokens WHERE user_id = ?1 AND token_hash NOT IN (SELECT token_hash FROM tester_tokens WHERE user_id = ?1 ORDER BY created DESC LIMIT ?2)').bind(me.id, TESTER_DEVICES),
    env.DB.prepare("UPDATE tester_sessions SET status = 'ok', token = ?1, user_id = ?2, name = ?3 WHERE session = ?4").bind(token, me.id, name, session),
  ]);
  return page('You\'re in, ' + name.replace(/[<>&"]/g, ''), 'Your Tester role is confirmed. Go back to the Syndicate Tester launcher: it opens by itself. You can close this tab.', true);
}

async function testerPoll(request, env) {
  const { session, install } = await request.json().catch(() => ({}));
  if (!isHex(session, 32) || !isHex(install, 32)) return json({ status: 'denied', reason: 'Bad request.' }, 400);
  const row = await env.DB.prepare('SELECT * FROM tester_sessions WHERE session = ?1').bind(session).first();
  if (!row || row.install !== install || row.created < Date.now() - SESSION_MS) return json({ status: 'expired' });
  if (row.status === 'pending') return json({ status: 'pending' });
  await env.DB.prepare('DELETE FROM tester_sessions WHERE session = ?1').bind(session).run();   // the token is handed out once
  return row.status === 'ok' ? json({ status: 'ok', token: row.token, name: row.name }) : json({ status: 'denied', reason: row.reason || 'No Tester role.' });
}

async function testerCheck(request, env) {
  const { token, install } = await request.json().catch(() => ({}));
  if (!isHex(token, 64) || !isHex(install, 32)) return json({ ok: false, reason: 'Sign in again.' }, 401);
  const row = await env.DB.prepare('SELECT * FROM tester_tokens WHERE token_hash = ?1').bind(await tokenHash(token)).first();
  if (!row || row.install !== install) return json({ ok: false, reason: 'This Tester launcher is not activated on this PC. Sign in with Discord.' }, 401);
  const state = await testerRole(env, row.user_id);
  if (state === 'unavailable') return json({ ok: false, reason: roleReason(state) }, 503);
  if (state !== 'ok') {
    await env.DB.prepare('DELETE FROM tester_tokens WHERE user_id = ?1').bind(row.user_id).run();
    return json({ ok: false, denied: true, reason: roleReason(state) }, 403);
  }
  await env.DB.prepare('UPDATE tester_tokens SET last_check = ?1 WHERE token_hash = ?2').bind(Date.now(), row.token_hash).run();
  const latest = await env.DB.prepare('SELECT build, version, url, sha256 FROM tester_build WHERE id = 1').first();
  return json({ ok: true, name: row.name, latest: latest || null });
}

async function testerPublish(request, env) {
  if (!env.TESTER_PUBLISH_KEY || request.headers.get('x-publish-key') !== env.TESTER_PUBLISH_KEY) return new Response('Not found', { status: 404 });
  const { build, version, url, sha256 } = await request.json().catch(() => ({}));
  if (!Number.isInteger(build) || build < 1 || typeof url !== 'string' || !/^https:\/\/github\.com\//.test(url) || !/^[0-9A-Fa-f]{64}$/.test(sha256 || '')) return json({ ok: false, error: 'bad build' }, 400);
  await env.DB.prepare('INSERT OR REPLACE INTO tester_build (id, build, version, url, sha256, updated) VALUES (1, ?1, ?2, ?3, ?4, ?5)').bind(build, String(version || ''), url, sha256.toUpperCase(), Date.now()).run();
  return json({ ok: true, build });
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    try {
      if (request.method === 'POST' && url.pathname === '/error') return errorReport(request, env);
      if (request.method === 'POST' && url.pathname === '/report') return matchFeed(request, env);
      if (!env.DB) return new Response('Add the D1 binding DB first.', { status: 500 });
      await schema(env);
      if (request.method === 'POST' && url.pathname === '/interactions') return interactions(request, env);
      if (request.method === 'POST' && url.pathname === '/redeem') return redeem(request, env);
      if (request.method === 'POST' && url.pathname === '/redeem/release') return release(request, env);
      if (request.method === 'GET' && url.pathname === '/setup') return setup(url, env);
      if (request.method === 'GET' && url.pathname === '/tester/start') return testerStart(url, env);
      if (request.method === 'GET' && url.pathname === '/tester/callback') return testerCallback(url, env);
      if (request.method === 'POST' && url.pathname === '/tester/poll') return testerPoll(request, env);
      if (request.method === 'POST' && url.pathname === '/tester/check') return testerCheck(request, env);
      if (request.method === 'POST' && url.pathname === '/tester/publish') return testerPublish(request, env);
      return new Response('Not found', { status: 404 });
    } catch (e) {
      return json({ ok: false, error: 'server', message: 'Code server error. Try again.' }, 500);
    }
  },
};
