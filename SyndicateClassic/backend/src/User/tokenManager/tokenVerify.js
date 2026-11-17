import jwt from "jsonwebtoken";
import User from "../../User/Mongodb/Schema/user.js";
import Utils from "../../Utils/Utils.js";

// Syndicate fixes:
//  * A token signed by this backend and not expired stays valid across backend
//    restarts, unless the same account has since signed in again (that newer
//    login replaces it, exactly like before).
//  * Database hiccups return 503 and never revoke a valid token. Previously any
//    exception (including a slow MongoDB) deleted the token and every later
//    request failed with "Authorization failed".
//  * One profile request runs verifyToken up to five times; account lookups are
//    cached for a few seconds so each request costs one MongoDB read, not five.

const USER_CACHE_MS = 5000;
const userCache = new Map();

async function loadUser(accountId) {
  const hit = userCache.get(accountId);
  if (hit && hit.expires > Date.now()) return hit.user;
  const user = await User.findOne({ accountId }).lean();
  userCache.set(accountId, { user, expires: Date.now() + USER_CACHE_MS });
  if (userCache.size > 500) {
    const now = Date.now();
    for (const [key, entry] of userCache) if (entry.expires <= now) userCache.delete(key);
  }
  return user;
}

export function forgetCachedUser(accountId) {
  if (accountId) userCache.delete(accountId);
}

function expired(decoded) {
  if (!decoded || !decoded.creation_date || decoded.hours_expire == null) return true;
  return DateAddHours(new Date(decoded.creation_date), decoded.hours_expire).getTime() <= Date.now();
}

// Returns the decoded payload when the raw token is genuinely ours and live.
function checkSignature(raw) {
  try {
    return jwt.verify(raw, global.JWT_SECRET);
  } catch {
    return null;
  }
}

function removeToken(list, token) {
  const index = list.findIndex((i) => i.token == token);
  if (index != -1) {
    list.splice(index, 1);
    Utils.UpdateTokens();
  }
}

// Accept a signed, unexpired access token that is missing from memory (for
// example after a restart) unless a newer token for the account exists.
function adoptAccessToken(raw, decoded) {
  const full = `eg1~${raw}`;
  if (global.accessTokens.find((i) => i.token == full)) return true;
  if (!decoded.sub) return false;
  const newer = global.accessTokens.some((i) => {
    if (i.accountId != decoded.sub) return false;
    const other = jwt.decode(i.token.replace("eg1~", ""));
    return other && new Date(other.creation_date).getTime() > new Date(decoded.creation_date).getTime();
  });
  if (newer) return false;
  global.accessTokens.push({ accountId: decoded.sub, token: full });
  Utils.UpdateTokens();
  return true;
}

function authFailure(req, res) {
  return Utils.createError(
    "errors.com.epicgames.common.authorization.authorization_failed",
    `Authorization failed for ${req.originalUrl}`,
    [req.originalUrl],
    1032,
    undefined,
    401,
    res
  );
}

function serviceBusy(res) {
  return Utils.createError(
    "errors.com.syndicate.service_busy",
    "The local account service is busy. Please try again.",
    [],
    1041,
    undefined,
    503,
    res
  );
}

function bannedResponse(res) {
  return Utils.createError(
    "errors.com.epicgames.account.account_not_active",
    "You have been permanently banned from Syndicate.",
    [],
    -1,
    undefined,
    400,
    res
  );
}

function bearer(req) {
  const header = req.headers["authorization"];
  if (!header || !header.startsWith("bearer eg1~")) return null;
  return header.replace("bearer eg1~", "");
}

async function verifyToken(req, res, next) {
  let token = bearer(req);
  if (!token) return authFailure(req, res);

  let decoded = checkSignature(token);
  if (!decoded || expired(decoded)) {
    removeToken(global.accessTokens, `eg1~${token}`);
    return authFailure(req, res);
  }

  if (decoded.am == "client_credentials") {
    if (!global.clientTokens.find((i) => i.token == `eg1~${token}`)) return authFailure(req, res);
    const linked = global.accessTokens.find((i) => {
      const other = jwt.decode(i.token.replace("eg1~", ""));
      return other && other.clid == decoded.clid;
    });
    if (!linked) return authFailure(req, res);
    token = linked.token.replace("eg1~", "");
    decoded = checkSignature(token);
    if (!decoded || expired(decoded)) return authFailure(req, res);
  }

  if (!adoptAccessToken(token, decoded)) return authFailure(req, res);

  let user;
  try {
    user = await loadUser(decoded.sub);
  } catch {
    return serviceBusy(res);
  }
  if (!user || (user.travelGuest && user.travelGuest.status !== "active")) return authFailure(req, res);
  if (user.banned) return bannedResponse(res);
  req.user = user;
  next();
}

async function verifyClient(req, res, next) {
  const token = bearer(req);
  if (!token) return authFailure(req, res);
  const full = `eg1~${token}`;

  const decoded = checkSignature(token);
  if (!decoded || expired(decoded)) {
    removeToken(global.accessTokens, full);
    removeToken(global.clientTokens, full);
    return authFailure(req, res);
  }

  const isClient = global.clientTokens.find((i) => i.token == full);
  const isAccess = !isClient && decoded.sub && adoptAccessToken(token, decoded);
  if (!isClient && !isAccess) return authFailure(req, res);

  if (isAccess) {
    let user;
    try {
      user = await loadUser(decoded.sub);
    } catch {
      return serviceBusy(res);
    }
    if (!user || (user.travelGuest && user.travelGuest.status !== "active")) return authFailure(req, res);
    if (user.banned) return bannedResponse(res);
    req.user = user;
  }
  next();
}

function DateAddHours(pdate, number) {
  let date = pdate;
  date.setHours(date.getHours() + number);
  return date;
}
export { verifyToken, verifyClient, DateAddHours };
