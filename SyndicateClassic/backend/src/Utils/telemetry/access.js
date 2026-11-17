// Who may use the diagnostics API: only Dev Kit builds (the Dev Kit launcher starts the backend with
// TILTED_DEVKIT=1; public launchers clear it), only requests from this PC (no browser Origin header),
// and only with the developer token. Kept separate from the route so it can be tested without a server.
import { timingSafeEqual } from 'node:crypto';

const LOCAL = ['127.0.0.1', '::1', '::ffff:127.0.0.1'];

export function authorize({ address, origin, header, devkit }, token) {
  if (!devkit) return { ok: false, status: 404, message: 'Developer tools are only available in Dev Kit builds.' };
  if (!LOCAL.includes(address) || origin) return { ok: false, status: 403, message: 'Diagnostics are available on this PC only.' };
  const given = Buffer.from(String(header || ''));
  const want = Buffer.from(String(token || ''));
  if (want.length < 32 || given.length !== want.length || !timingSafeEqual(given, want)) return { ok: false, status: 401, message: 'Developer token required.' };
  return { ok: true };
}
