// Delivery of match tracking messages to Discord, used by the Developer Tools test button and by
// match summary uploads. Two paths:
//   1. TILTED_DISCORD_WEBHOOK set (environment, or Syndicate-backend\.env on a Dev Kit PC):
//      post straight to that webhook with ?wait=true, so success means Discord stored the message.
//   2. Otherwise: the Syndicate Cloudflare relay, which keeps the webhook as a Worker secret and
//      answers 200 only when Discord accepted the message.
// The webhook is never in source, public builds (.env is excluded from the public pack), logs or
// API answers: every message that could contain it goes through redact().
import fs from 'fs';
import path from 'path';

// Optional: set TILTED_RELAY_URL to your own relay. Empty = no relay, match tracking is only sent to a webhook you set.
export const RELAY_URL = (process.env.TILTED_RELAY_URL || '').trim();
export const WEBHOOK_PATTERN = /^https:\/\/(?:(?:canary|ptb)\.)?discord(?:app)?\.com\/api\/webhooks\/\d{5,25}\/[A-Za-z0-9_-]{20,120}$/;
const ANY_WEBHOOK = /https:\/\/(?:(?:canary|ptb)\.)?discord(?:app)?\.com\/api\/webhooks\/[^\s"'<>]+/gi;

// Two Discord channels:
//   bot learning (match summaries): TILTED_DISCORD_WEBHOOK in .env, otherwise the Cloudflare relay;
//   mechanics learning (build-fight results): TILTED_DISCORD_MECHANICS_WEBHOOK, or the per-PC file
//   %LOCALAPPDATA%\ProjectTilted\UserData\discord-mechanics-webhook.txt (outside the install, so
//   never packed into a release, and outside the repo). Without one, build-fight results are not sent.
export function mechanicsWebhookFile(env = process.env) {
  return env.LOCALAPPDATA ? path.join(env.LOCALAPPDATA, 'ProjectTilted', 'UserData', 'discord-mechanics-webhook.txt') : null;
}
function configuredWebhook(env, channel = 'learning') {
  if (channel !== 'mechanics') return String(env.TILTED_DISCORD_WEBHOOK ?? '').trim();
  const fromEnv = String(env.TILTED_DISCORD_MECHANICS_WEBHOOK ?? '').trim();
  if (fromEnv) return fromEnv;
  const file = mechanicsWebhookFile(env);
  try { return file ? fs.readFileSync(file, 'utf8').trim() : ''; } catch { return ''; }
}

export function webhookConfig(env = process.env, channel = 'learning') {
  const raw = configuredWebhook(env, channel);
  if (!raw) return { kind: channel === 'mechanics' || !RELAY_URL ? 'none' : 'relay' };
  if (!WEBHOOK_PATTERN.test(raw)) return { kind: 'invalid' };
  return { kind: 'webhook', url: raw };
}

// Removes webhook URLs and the token part of the configured one from any text.
export function redact(text, env = process.env) {
  let out = String(text ?? '').replace(ANY_WEBHOOK, '[webhook redacted]');
  for (const channel of ['learning', 'mechanics']) {
    const token = configuredWebhook(env, channel).split('/').pop();
    if (token && token.length >= 20) out = out.split(token).join('[token redacted]');
  }
  return out;
}

// Same cleaning as the relay: plain text in a code block, no mentions, at most 1900 characters.
export function discordContent(lines) {
  const clean = lines.map(l => String(l).replace(/[`@]/g, "'").replace(/[\r\n]+/g, ' ').slice(0, 600)).join('\n');
  return '```\n' + clean.slice(0, 1900) + '\n```';
}

async function readJson(resp) { try { return await resp.json(); } catch { return null; } }

// Sends up to 10 lines. Returns { ok, via, status, message }; ok only when Discord accepted it.
export async function deliver(lines, { fetchImpl = globalThis.fetch, env = process.env, relayUrl = RELAY_URL, username = 'Syndicate match tracking', timeoutMs = 15000, channel = 'learning' } = {}) {
  const config = webhookConfig(env, channel);
  if (config.kind === 'none') return { ok: false, via: 'none', status: null, message: channel === 'mechanics' ? 'No mechanics webhook configured.' : 'No webhook or relay configured (set TILTED_DISCORD_WEBHOOK).' };
  if (config.kind === 'invalid') return { ok: false, via: 'webhook', status: null, message: 'TILTED_DISCORD_WEBHOOK is set but is not a Discord webhook URL (https://discord.com/api/webhooks/<id>/<token>). Fix it in Syndicate-backend\\.env.' };
  if (!fetchImpl) return { ok: false, via: config.kind, status: null, message: 'This Node version has no fetch; update Node.' };
  lines = lines.slice(0, 10);
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs);
  try {
    if (config.kind === 'webhook') {
      const resp = await fetchImpl(config.url + '?wait=true', {
        method: 'POST', headers: { 'content-type': 'application/json' }, signal: controller.signal,
        body: JSON.stringify({ username, content: discordContent(lines), allowed_mentions: { parse: [] } }),
      });
      const body = await readJson(resp);
      if (resp.ok && (resp.status === 204 || body?.id)) return { ok: true, via: 'webhook', status: resp.status, message: `Discord accepted the message${body?.id ? ` (message id ${body.id})` : ''}.` };
      if (resp.status === 429) return { ok: false, via: 'webhook', status: 429, message: `Discord rate limit: try again in ${Math.ceil(Number(body?.retry_after) || 5)} s.` };
      if ([401, 403, 404].includes(resp.status)) return { ok: false, via: 'webhook', status: resp.status, message: 'Discord rejected the webhook (deleted or regenerated). Put the current webhook in TILTED_DISCORD_WEBHOOK in Syndicate-backend\\.env (bot learning) or in %LOCALAPPDATA%\\ProjectTilted\\UserData\\discord-mechanics-webhook.txt (mechanics).' };
      if (resp.status >= 500) return { ok: false, via: 'webhook', status: resp.status, message: `Discord had a server error (HTTP ${resp.status}). Try again later.` };
      return { ok: false, via: 'webhook', status: resp.status, message: redact(`Discord refused the message (HTTP ${resp.status}${body?.message ? `: ${body.message}` : ''}).`, env) };
    }
    const resp = await fetchImpl(relayUrl, { method: 'POST', headers: { 'content-type': 'application/json' }, signal: controller.signal, body: JSON.stringify({ source: 'tilted', lines }) }); // the relay only accepts Syndicate reports
    const text = (await resp.text().catch(() => '')).slice(0, 200);
    if (resp.ok && text.trim() === 'ok') return { ok: true, via: 'relay', status: resp.status, message: 'Delivered through the Syndicate relay; Discord accepted the message.' };
    if (resp.status === 429) return { ok: false, via: 'relay', status: 429, message: 'Relay rate limit (one message per minute per PC): wait a minute and try again.' };
    if (resp.status === 410) return { ok: false, via: 'relay', status: 410, message: 'The Discord match feed through the relay is turned off.' };
    if (resp.status === 503) return { ok: false, via: 'relay', status: 503, message: 'The relay has no webhook configured. Set the DISCORD_WEBHOOK secret on the Cloudflare Worker.' };
    if (resp.status === 502) return { ok: false, via: 'relay', status: 502, message: 'The relay reached Discord but Discord refused it: the webhook saved in Cloudflare is probably outdated.' };
    return { ok: false, via: 'relay', status: resp.status, message: redact(`The relay answered HTTP ${resp.status}${text ? `: ${text}` : ''}.`, env) };
  } catch (error) {
    const reason = error?.name === 'AbortError' ? `no answer within ${timeoutMs / 1000} s` : (error?.cause?.code || error?.code || error?.message || 'network error');
    return { ok: false, via: config.kind, status: null, message: redact(`Couldn't reach ${config.kind === 'webhook' ? 'Discord' : 'the relay'}: ${reason}.`, env) };
  } finally { clearTimeout(timer); }
}

export function testLines(now = new Date()) {
  return [
    'TEST MESSAGE - Syndicate Dev Kit: match tracking relay check',
    `Sent from Settings > Match data > Developer Tools at ${now.toISOString()}.`,
    'If you can read this, finished-match summaries can reach this channel. No action needed.',
  ];
}
