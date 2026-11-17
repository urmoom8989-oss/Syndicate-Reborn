import {catalog as secretShopCatalog,purchase as buySecretShop} from '../Utils/syndicateSecretShop.js';
import {openTravel,finishTravel,travelActive} from '../Utils/syndicateTravel.js';
import {view as passView,purchase as buyPass} from '../Utils/syndicatePassShop.js';
import express from 'express';
import {importLegacyFrom} from '../Utils/syndicateLegacyImport.js';
import rateLimit from 'express-rate-limit';
import bcrypt from 'bcrypt';
import { randomBytes, randomUUID, createHash } from 'node:crypto';
import User from '../User/Mongodb/Schema/user.js';
import Profile from '../User/Mongodb/Schema/profiles.js';
import Friends from '../User/Mongodb/Schema/friends.js';
import profileManager from '../User/profile.js';
import {applyUnlockAll,removeUnlockAll,cosmeticsFor} from '../Utils/syndicateUnlockAll.js';
import {applyHideDefaults,ensureSharedDefaults} from '../Utils/syndicateSeasonState.js';
import {applyHideWinUmbrellas,restoreWinUmbrellas} from '../Utils/syndicateVictoryRewards.js';
import {applyMaxProgress,restoreMaxProgress} from '../Utils/syndicateMaxProgress.js';
import {stwHeroes,selectedHero,stwSkinsOn} from '../Utils/syndicateStwHeroes.js';
import {publishCharacter} from '../Utils/syndicateLocker.js';
import {redeem as redeemCode,redeemedItems} from '../Utils/syndicateRedeem.js';
import {arenaThresholds,arenaDivision,arenaBusFare,arenaLeague,placementHype} from '../Utils/syndicateArena.js';
import {readFileSync,statSync} from 'node:fs';
import {achievementView,claimAchievement} from '../Utils/syndicateAchievements.js';
import {walletId as achWalletId,ensureSharedWallet as achEnsureWallet} from '../Utils/syndicateShopWallet.js';
import {withAccountLock as achAccountLock} from '../Utils/syndicateAccountLock.js';
import {fileURLToPath} from 'node:url';

const router = express.Router();
const sessions = new Map();
const lifetime = 12 * 60 * 60 * 1000;
router.use('/launcher', (req, res, next) => {
  const address = req.socket.remoteAddress;
  if (!['127.0.0.1', '::1', '::ffff:127.0.0.1'].includes(address) || req.headers.origin)
    return res.status(403).json({ message: 'Launcher accounts are available on this PC only.' });
  res.set('Cache-Control', 'no-store');
  next();
});
router.use('/launcher', rateLimit({ windowMs: 60000, max: 30, standardHeaders: true, legacyHeaders: false,
  message: { message: 'Too many attempts. Please wait a minute and try again.' } }));
const safe = handler => async (req, res) => {
  try { await handler(req, res); }
  catch { res.status(503).json({ message: 'Account service is unavailable. Check that MongoDB is running.' }); }
};
function session(user) {
  const token = randomBytes(32).toString('hex');
  sessions.set(token, { accountId: user.accountId, expires: Date.now() + lifetime });
  return { token, username: user.username, accountId: user.accountId };
}

const rememberLifetime = 30 * 24 * 60 * 60 * 1000;
const rememberHash = value => createHash('sha256').update(value).digest('hex');
const validRemember = value => typeof value === 'string' && /^[a-f0-9]{64}$/.test(value);
async function loginResult(user, remember) {
  const result = session(user);
  if (remember === true) {
    const token = randomBytes(32).toString('hex');
    await User.collection.updateOne({accountId:user.accountId}, {$set:{launcherRemember:{
      hash:rememberHash(token), expires:Date.now()+rememberLifetime,
      passwordHash:rememberHash(user.password)
    }}});
    result.rememberToken = token;
  }
  return result;
}
router.post('/launcher/resume', safe(async (req,res) => {
  const token=req.body?.rememberToken;
  if (!validRemember(token)) return res.status(401).json({message:'Please sign in again.'});
  const user=await User.collection.findOne({'launcherRemember.hash':rememberHash(token)});
  if (!user || user.launcherRemember.expires <= Date.now() ||
      user.launcherRemember.passwordHash !== rememberHash(user.password))
    return res.status(401).json({message:'Your remembered login expired. Please sign in again.'});
  if (user.banned) return res.status(403).json({message:'This account is disabled.'});
  // Renew only a still-valid token; sign-out/password changes continue to revoke it.
  await User.collection.updateOne({'launcherRemember.hash':rememberHash(token), 'launcherRemember.passwordHash':rememberHash(user.password)}, {$set:{'launcherRemember.expires':Date.now()+rememberLifetime}});
  res.json(session(user));
}));

// Syndicate: username-only play. No email or password: pick an existing player on this PC
// or type a new name. Accounts already created with email/password keep working and are
// listed here by their display name. These routes only answer requests from this PC.
router.post('/launcher/players', safe(async (req, res) => {
  const users = await User.collection.find({ travelGuest: { $exists: false }, banned: { $ne: true } },
    { projection: { username: 1, created: 1 } }).sort({ created: 1 }).limit(60).toArray();
  res.json({ players: users.map(u => ({ username: u.username })) });
}));
router.post('/launcher/quick', safe(async (req, res) => {
  const name = typeof req.body?.username === 'string' ? req.body.username.trim() : '';
  if (!/^[a-zA-Z0-9_ -]{3,24}$/.test(name))
    return res.status(400).json({ message: 'Use a name of 3-24 letters, numbers, spaces, underscores or hyphens.' });
  const existing = await User.findOne({ username_lower: name.toLowerCase() }).lean();
  if (existing) {
    if (existing.banned) return res.status(403).json({ message: 'This player is disabled.' });
    if (existing.travelGuest) return res.status(409).json({ message: 'That name belongs to a friend visiting from another PC. Pick a different name.' });
    return res.json(await loginResult(existing, true));
  }
  const accountId = randomUUID().replaceAll('-', '');
  const created = new Date();
  let user;
  try {
    const profiles = await profileManager.createProfiles(accountId);
    user = await User.create({ created, accountId, discordId: `local:${accountId}`, username: name,
      username_lower: name.toLowerCase(), email: `${accountId}@players.tilted.local`,
      password: await bcrypt.hash(randomBytes(24).toString('hex'), 10), banned: false });
    await Profile.create({ created, accountId, profiles });
    await Friends.create({ created, accountId });
  } catch (error) {
    await Promise.all([User.deleteOne({ accountId }), Profile.deleteOne({ accountId }), Friends.deleteOne({ accountId })]);
    if (error.code === 11000) {
      const raced = await User.findOne({ username_lower: name.toLowerCase() }).lean();
      if (raced && !raced.banned && !raced.travelGuest) return res.json(await loginResult(raced, true));
      return res.status(409).json({ message: 'That name is taken. Pick a different one.' });
    }
    throw error;
  }
  res.status(201).json(await loginResult(user, true));
}));

// Syndicate: rename a local player from the "Who's playing?" screen (right-click > Rename).
router.post('/launcher/players/rename', safe(async (req, res) => {
  const name = typeof req.body?.username === 'string' ? req.body.username.trim() : '';
  const next = typeof req.body?.newName === 'string' ? req.body.newName.trim() : '';
  if (!/^[a-zA-Z0-9_ -]{3,24}$/.test(next))
    return res.status(400).json({ message: 'Use a name of 3-24 letters, numbers, spaces, underscores or hyphens.' });
  const user = await User.findOne({ username_lower: name.toLowerCase() }).lean();
  if (!user || user.travelGuest) return res.status(404).json({ message: 'That player was not found on this PC.' });
  if (next.toLowerCase() !== user.username_lower && await User.exists({ username_lower: next.toLowerCase() }))
    return res.status(409).json({ message: 'That name is taken. Pick a different one.' });
  try {
    await User.updateOne({ accountId: user.accountId }, { $set: { username: next, username_lower: next.toLowerCase() } });
  } catch (error) {
    if (error.code === 11000) return res.status(409).json({ message: 'That name is taken. Pick a different one.' });
    throw error;
  }
  res.json({ username: next });
}));

// Syndicate: delete a local player from the "Who's playing?" screen (right-click > Delete).
// Removes the account, its profiles and friends list, and ends its launcher sessions.
router.post('/launcher/players/delete', safe(async (req, res) => {
  const name = typeof req.body?.username === 'string' ? req.body.username.trim() : '';
  if (!name) return res.status(400).json({ message: 'No player was selected.' });
  const user = await User.findOne({ username_lower: name.toLowerCase() }).lean();
  if (!user || user.travelGuest) return res.status(404).json({ message: 'That player was not found on this PC.' });
  // Remember the deletion first: the startup import from the older database would otherwise
  // copy this player back in on the next launch.
  await User.db.collection('tilted_deleted_players').updateOne({ accountId: user.accountId },
    { $set: { accountId: user.accountId, username_lower: user.username_lower, email: user.email, deleted: new Date() } }, { upsert: true });
  await Promise.all([User.deleteOne({ accountId: user.accountId }), Profile.deleteOne({ accountId: user.accountId }),
    Friends.deleteOne({ accountId: user.accountId })]);
  for (const [key, entry] of sessions) if (entry.accountId === user.accountId) sessions.delete(key);
  res.json({ deleted: user.username });
}));

// Syndicate: the launcher's Project Tilted account import. The launcher starts a MongoDB on a copy of the
// old Tilted database at 127.0.0.1:<port> and asks for the players that are not here yet.
router.post('/launcher/import-legacy', safe(async (req, res) => {
  const port = Number(req.body?.port);
  if (!Number.isInteger(port) || port < 1024 || port > 65535)
    return res.status(400).json({ message: 'No database port was given.' });
  const uri = `mongodb://127.0.0.1:${port}/project_tilted`;
  if (uri === (process.env.MONGO_URI || '').trim()) return res.status(400).json({ message: 'That is this backend\'s own database.' });
  const imported = await importLegacyFrom(uri, { renameOnClash: true });
  if (imported === null) return res.status(503).json({ message: 'The old account database did not answer.' });
  res.json({ imported });
}));

router.post('/launcher/register', safe(async (req, res) => {
  const { username, email, password } = req.body;
  if (typeof username !== 'string' || !/^[a-zA-Z0-9_ -]{3,24}$/.test(username.trim()))
    return res.status(400).json({ message: 'Use a display name of 3–24 letters, numbers, spaces, underscores or hyphens.' });
  if (typeof email !== 'string' || email.length > 254 || !/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email.trim()))
    return res.status(400).json({ message: 'Enter a valid email address.' });
  if (typeof password !== 'string' || password.length < 8 || Buffer.byteLength(password, 'utf8') > 72)
    return res.status(400).json({ message: 'Use a password of at least 8 characters and no more than 72 UTF-8 bytes.' });
  const normalizedEmail = email.trim().toLowerCase();
  const name = username.trim();
  if (await User.exists({ $or: [{ email: normalizedEmail }, { username_lower: name.toLowerCase() }] }))
    return res.status(409).json({ message: 'That email or display name is already registered.' });
  const accountId = randomUUID().replaceAll('-', '');
  const created = new Date();
  let user;
  try {
    const profiles = await profileManager.createProfiles(accountId);
    user = await User.create({ created, accountId, discordId: `local:${accountId}`, username: name,
      username_lower: name.toLowerCase(), email: normalizedEmail, password: await bcrypt.hash(password, 10), banned: false });
    await Profile.create({ created, accountId, profiles });
    await Friends.create({ created, accountId });
  } catch (error) {
    await Promise.all([User.deleteOne({ accountId }), Profile.deleteOne({ accountId }), Friends.deleteOne({ accountId })]);
    if (error.code === 11000) return res.status(409).json({ message: 'That email or display name is already registered.' });
    throw error;
  }
  res.status(201).json(await loginResult(user, req.body.remember));
}));
router.post('/launcher/login', safe(async (req, res) => {
  const { email, password } = req.body;
  if (typeof email !== 'string' || typeof password !== 'string' || email.length > 254 || Buffer.byteLength(password) > 72)
    return res.status(400).json({ message: 'Enter your email and password.' });
  const user = await User.findOne({ email: email.trim().toLowerCase() }).lean();
  // Log only a fixed reason and database identity; never credentials, hashes or account identifiers.
  let loginReason = !user ? 'account_not_found' : 'password_mismatch';
  let passwordMatches = false;
  if (user) {
    try { passwordMatches = await bcrypt.compare(password, user.password); }
    catch { loginReason = 'credential_verifier_error'; }
  }
  if (!passwordMatches) {
    console.warn('[Syndicate login]', JSON.stringify({reason:loginReason, database:User.db.name, port:User.db.port, pid:process.pid}));
    return res.status(401).json({ message: 'Email or password is incorrect.' });
  }
  if (user.banned) return res.status(403).json({ message: 'This account is disabled.' });
  res.json(await loginResult(user, req.body.remember));
}));
router.post('/launcher/reset-password', safe(async (req, res) => {
  const {email, password, confirmPassword} = req.body || {};
  if(typeof email !== 'string' || email.length > 254 || !email.trim())
    return res.status(400).json({message:'Enter your account email.'});
  if(typeof password !== 'string' || password.length < 8 || Buffer.byteLength(password,'utf8') > 72)
    return res.status(400).json({message:'Use a password of at least 8 characters and no more than 72 UTF-8 bytes.'});
  if(password !== confirmPassword) return res.status(400).json({message:'Passwords do not match.'});
  const user = await User.findOne({email:email.trim().toLowerCase()}).lean();
  if(!user || user.travelGuest) return res.status(404).json({message:'No local account was found with that email.'});
  if(user.banned) return res.status(403).json({message:'This account is disabled.'});
  const result = await User.collection.updateOne({accountId:user.accountId,password:user.password},
    {$set:{password:await bcrypt.hash(password,10)},$unset:{launcherRemember:''}});
  if(result.modifiedCount !== 1) return res.status(409).json({message:'Account changed during reset. Please try again.'});
  for(const [key,entry] of sessions) if(entry.accountId===user.accountId) sessions.delete(key);
  res.json({ok:true,message:'Password reset. Sign in with your new password.'});
}));
router.post('/launcher/logout', safe(async (req, res) => {
  sessions.delete((req.headers.authorization || '').replace(/^Bearer /, ''));
  const token=req.body?.rememberToken;
  if(validRemember(token)) await User.collection.updateOne(
    {'launcherRemember.hash':rememberHash(token)}, {$unset:{launcherRemember:''}});
  res.json({ ok: true });
}));
router.post('/launcher/exchange', safe(async (req, res) => {
  const key = (req.headers.authorization || '').replace(/^Bearer /, '');
  const entry = sessions.get(key);
  if (!entry || entry.expires <= Date.now()) {
    sessions.delete(key);
    return res.status(401).json({ message: 'Your session expired. Please sign in again.' });
  }
  const user = await User.findOne({ accountId: entry.accountId }).lean();
  if (!user || user.banned) return res.status(403).json({ message: 'This account is unavailable.' });
  try {const remote=await openTravel(user.accountId);if(remote)return res.json({code:remote.code});}
  catch(error){return res.status(409).json({message:error.message});}
  const code = randomBytes(24).toString('hex');
  // Valid for 10 minutes and reusable until then, so slow first loads and login retries in
  // Fortnite never fall back to the in-game login screen.
  global.exchangeCodes.push({ exchange_code: code, accountId: user.accountId, reusable: true, expires: Date.now() + 600000 });
  global.syndicateLaunchAccount = { accountId: user.accountId, at: Date.now() }; // who is starting Fortnite (syndicateApolloIntro.js)
  setTimeout(() => {
    const index = global.exchangeCodes.findIndex(e => e.exchange_code === code);
    if (index >= 0) global.exchangeCodes.splice(index, 1);
  }, 600000).unref();
  res.json({ code });
}));
router.post('/launcher/travel/finish',safe(async(req,res)=>{
 const entry=sessions.get((req.headers.authorization||'').replace(/^Bearer /,''));
 if(!entry||entry.expires<=Date.now())return res.status(401).json({message:'Sign in to finish account sync.'});
 try{const outcome=await finishTravel(entry.accountId);res.json({ok:true,...outcome});}catch(error){res.status(409).json({message:'Progress is preserved, but sync is pending: '+error.message});}
}));
for(const operation of ['catalog','purchase'])router.post(`/launcher/passes/${operation}`,safe(async(req,res)=>{
 const entry=sessions.get((req.headers.authorization||'').replace(/^Bearer /,''));
 if(!entry || entry.expires<=Date.now())return res.status(401).json({message:'Please sign in again.'});
 const user=await User.findOne({accountId:entry.accountId}).lean();
 if(!user || user.banned)return res.status(403).json({message:'Account unavailable.'});
 try{
  if(operation==='purchase'&&travelActive(entry.accountId))return res.status(409).json({message:'Use the in-game shop while connected to your friend, or Quit to sync your account first.'});
  if(operation==='purchase')return res.json(await buyPass(entry.accountId,req.body.passId));
  const doc=await Profile.collection.findOne({accountId:entry.accountId});if(!doc)throw new Error('Profile missing.');
  res.json(passView(doc));
 }catch(error){res.status(400).json({message:error.message});}
}));
// Syndicate: the launcher's hidden item shop (every item-shop cosmetic, unlocked by a key code in the launcher).
for(const operation of ['catalog','purchase'])router.post(`/launcher/shop/${operation}`,safe(async(req,res)=>{
 const entry=sessions.get((req.headers.authorization||'').replace(/^Bearer /,''));
 if(!entry || entry.expires<=Date.now())return res.status(401).json({message:'Please sign in again.'});
 const user=await User.findOne({accountId:entry.accountId}).lean();
 if(!user || user.banned)return res.status(403).json({message:'Account unavailable.'});
 try{
  if(operation==='purchase'){
   if(travelActive(entry.accountId))return res.status(409).json({message:'Quit your friend\'s match to sync your account first.'});
   return res.json(await buySecretShop(entry.accountId,req.body?.templateId));
  }
  const doc=await Profile.collection.findOne({accountId:entry.accountId});if(!doc)throw new Error('Profile missing.');
  res.json(secretShopCatalog(doc));
 }catch(error){res.status(400).json({message:error.message});}
}));
// Syndicate: Settings > Redeem code. A one-time code gives an item that is not in Unlock all or the shops
// (syndicateRedeem.js). Few tries per minute, so codes cannot be guessed.
const redeemLimit=rateLimit({windowMs:10*60000,max:10,standardHeaders:true,legacyHeaders:false,message:{message:'Too many tries. Wait a few minutes and try again.'}});
router.post('/launcher/redeem',redeemLimit,safe(async(req,res)=>{
 const entry=sessions.get((req.headers.authorization||'').replace(/^Bearer /,''));
 if(!entry || entry.expires<=Date.now())return res.status(401).json({message:'Please sign in again.'});
 const user=await User.findOne({accountId:entry.accountId}).lean();
 if(!user || user.banned)return res.status(403).json({message:'Account unavailable.'});
 if(travelActive(entry.accountId))return res.status(409).json({message:'Quit your friend\'s match to sync your account first.'});
 try{res.json(await redeemCode(entry.accountId,req.body?.code));}
 catch(error){res.status(400).json({message:error.message});}
}));
// Syndicate: the launcher's Stats tab. Career totals the backend records after every match
// (syndicateCareerCore.js, kept in the Athena profile): solo, duo and squad, plus a total.
router.post('/launcher/stats',safe(async(req,res)=>{
 const entry=sessions.get((req.headers.authorization||'').replace(/^Bearer /,''));
 if(!entry || entry.expires<=Date.now())return res.status(401).json({message:'Please sign in again.'});
 const doc=await Profile.collection.findOne({accountId:entry.accountId},{projection:{'profiles.athena.stats.attributes.tilted_career':1}});
 const career=doc?.profiles?.athena?.stats?.attributes?.tilted_career||{};
 const n=v=>Math.max(0,Math.floor(Number(v)||0));
 const modes={};const total={matches:0,wins:0,kills:0,seconds:0,score:0};
 for(const mode of ['solo','duo','squad']){
  const r=career[mode]||{};
  const row={matches:n(r.matches),wins:n(r.wins),kills:n(r.kills),seconds:n(r.seconds),score:n(r.score),top:n(r.high),topWide:n(r.low)};
  modes[mode]=row;for(const k of Object.keys(total))total[k]+=row[k];
 }
 res.json({username:(await User.findOne({accountId:entry.accountId}).lean())?.username||'',modes,total});
}));
// Syndicate Achievements (Utils/syndicateAchievements.js): the launcher's Home card and Achievements page.
// Returns every achievement with the player's progress, plus the account's V-Bucks balance for the Home wallet.
const achAccount=req=>{
 const entry=sessions.get((req.headers.authorization||'').replace(/^Bearer /,''));
 return entry && entry.expires>Date.now()?entry.accountId:null;
};
const achBalance=common=>{
 let total=0;for(const [id,i] of Object.entries(common?.items||{}))if(/^currency:mtx/i.test(i?.templateId||id)&&Number.isSafeInteger(i.quantity))total+=i.quantity;
 return total;
};
router.post('/launcher/achievements',safe(async(req,res)=>{
 const accountId=achAccount(req);if(!accountId)return res.status(401).json({message:'Please sign in again.'});
 const doc=await Profile.collection.findOne({accountId},{projection:{'profiles.athena.stats.attributes':1,'profiles.common_core.items':1}});
 res.json({...achievementView(doc?.profiles?.athena?.stats?.attributes||{}),vbucks:achBalance(doc?.profiles?.common_core)});
}));
// Claims one unlocked achievement: its reward goes into the shared V-Bucks wallet (common_core). Same save pattern as
// the shops: under the account lock, only if neither profile changed since it was read, with both revisions bumped.
router.post('/launcher/achievements/claim',safe(async(req,res)=>{
 const accountId=achAccount(req);if(!accountId)return res.status(401).json({message:'Please sign in again.'});
 if(travelActive(accountId))return res.status(409).json({message:'Quit your friend\'s match to sync your account first.'});
 const id=String(req.body?.id||'');
 try{
  const result=await achAccountLock(accountId,async()=>{
   for(let attempt=0;attempt<6;attempt++){
    const doc=await Profile.collection.findOne({accountId});
    if(!doc?.profiles?.athena||!doc.profiles.common_core)throw new Error('Play a match first.');
    const a=doc.profiles.athena,c=doc.profiles.common_core,ar=a.rvn,cr=c.rvn;
    const claimed=claimAchievement(doc.profiles,id,achWalletId,achEnsureWallet);
    for(const p of [a,c]){p.rvn=(p.rvn||0)+1;p.commandRevision=(p.commandRevision||0)+1;p.updated=new Date().toISOString();}
    const saved=await Profile.collection.updateOne({accountId,'profiles.athena.rvn':ar,'profiles.common_core.rvn':cr},{$set:{'profiles.athena':a,'profiles.common_core':c}});
    if(saved.modifiedCount){if(global.giftReceived)global.giftReceived[accountId]=true;return {...achievementView(a.stats.attributes),vbucks:achBalance(c),claimedReward:claimed.reward};}
   }
   throw new Error('Your account is busy. Try again.');
  });
  res.json(result);
 }catch(error){res.status(400).json({message:error.message});}
}));
// The launcher tells the backend at every launch whether the Syndicate menu is on. While it is, no match counts toward
// achievements (recordAchievements). Set at launch only, never at exit, so late events from a menu match never count.
router.post('/launcher/menu',safe(async(req,res)=>{
 const accountId=achAccount(req);if(!accountId)return res.status(401).json({message:'Please sign in again.'});
 const active=!!req.body?.active;
 // Under the account lock, like the match-event consumer, so the two never overwrite each other.
 await achAccountLock(accountId,()=>Profile.collection.updateOne({accountId},{$set:{'profiles.athena.stats.attributes.syndicate_menu_active':active}}));
 res.json({menuActive:active});
}));
// The signed-in player's season level and the XP toward the next level, for the launcher home card.
router.post('/launcher/level',safe(async(req,res)=>{
 const entry=sessions.get((req.headers.authorization||'').replace(/^Bearer /,''));
 if(!entry || entry.expires<=Date.now())return res.status(401).json({message:'Please sign in again.'});
 const doc=await Profile.collection.findOne({accountId:entry.accountId},{projection:{'profiles.athena.stats.attributes':1}});
 const a=doc?.profiles?.athena?.stats?.attributes||{};
 const active=Number(a.tilted_active_season)||Number(a.season_num)||4;
 // The launcher passes the season it's showing. Seasons 0 (pre-season) and 1 have no progress of their own, so
 // they show the active season. Every other season's level/XP is either live (if active) or in its saved state.
 const want=Number(req.body?.season);
 const season=[2,3,4,5,6,7,8,9,10,11,12].includes(want)?want:active;
 const src=season===active?a:(a.tilted_season_states?.[String(season)]?.attributes||{});
 const level=Math.max(1,Math.min(100,Math.floor(Number(src.level)||1)));
 const xp=Math.max(0,Math.floor(Number(src.xp)||0));
 let xpToNext=0;
 try{const table=JSON.parse(readFileSync(new URL('../local/Battlepass/Data/Season'+season+'/SeasonXP.json',import.meta.url)));xpToNext=Math.max(0,Math.floor(Number(table.find(r=>r.Level===level)?.XpToNextLevel)||0));}catch{}
 res.json({season,level,xp,xpToNext,tier:Math.max(0,Math.floor(Number(a.book_level)||0))});
}));
// Syndicate: "Unlock all cosmetics" toggle. On: every cosmetic exported from the player's Fortnite
// builds is added (marked). Off: only the marked ones are removed; owned cosmetics stay.
// It also maxes the season (level, Battle Pass tier, challenges); off puts the real progress back.
for(const operation of ['status','set'])router.post(`/launcher/unlockall/${operation}`,safe(async(req,res)=>{
 const entry=sessions.get((req.headers.authorization||'').replace(/^Bearer /,''));
 if(!entry || entry.expires<=Date.now())return res.status(401).json({message:'Please sign in again.'});
 if(operation==='set'&&travelActive(entry.accountId))return res.status(409).json({message:'Quit your friend\'s match to sync your account first.'});
 for(let attempt=0;attempt<4;attempt++){
  const doc=await Profile.collection.findOne({accountId:entry.accountId});if(!doc?.profiles?.athena)return res.status(404).json({message:'Profile missing.'});
  const p=doc.profiles.athena,a=p.stats.attributes,base=p.rvn,season=Number(a.tilted_active_season)||4;
  if(operation==='status')return res.json({enabled:!!a.tilted_unlock_all,available:cosmeticsFor(season).length});
  const enabled=req.body?.enabled===true;a.tilted_unlock_all=enabled;if(!enabled){delete a.tilted_stw_hero;delete a.tilted_stw_skins;}
  const changed=enabled?applyUnlockAll(p,season,doc.syndicatePassPurchases||{},redeemedItems(doc))+(applyMaxProgress(p,season)?1:0):removeUnlockAll(p)+restoreMaxProgress(p);
  p.rvn=(p.rvn||0)+1;p.commandRevision=(p.commandRevision||0)+1;p.updated=new Date().toISOString();
  const saved=await Profile.collection.updateOne({accountId:entry.accountId,'profiles.athena.rvn':base},{$set:{'profiles.athena':p}});
  if(saved.matchedCount)return res.json({enabled,changed,available:cosmeticsFor(season).length});
 }
 res.status(409).json({message:'Your locker is busy. Try again in a moment.'});
}));
// Syndicate: "Remove default outfits from locker" toggle. On: the free default outfits, pickaxe and glider are
// stripped from the locker (an equipped default moves to an owned non-default first; the last option in a slot
// is kept). Off: ensureSharedDefaults puts them back. Reversible and independent of Unlock all.
for(const operation of ['status','set'])router.post(`/launcher/hidedefaults/${operation}`,safe(async(req,res)=>{
 const entry=sessions.get((req.headers.authorization||'').replace(/^Bearer /,''));
 if(!entry || entry.expires<=Date.now())return res.status(401).json({message:'Please sign in again.'});
 if(operation==='set'&&travelActive(entry.accountId))return res.status(409).json({message:'Quit your friend\'s match to sync your account first.'});
 for(let attempt=0;attempt<4;attempt++){
  const doc=await Profile.collection.findOne({accountId:entry.accountId});if(!doc?.profiles?.athena)return res.status(404).json({message:'Profile missing.'});
  const p=doc.profiles.athena,a=p.stats.attributes,base=p.rvn;
  if(operation==='status')return res.json({enabled:!!a.tilted_hide_defaults});
  const enabled=req.body?.enabled===true;a.tilted_hide_defaults=enabled;
  const changed=enabled?applyHideDefaults(p):(ensureSharedDefaults(p)?1:0);
  p.rvn=(p.rvn||0)+1;p.commandRevision=(p.commandRevision||0)+1;p.updated=new Date().toISOString();
  const saved=await Profile.collection.updateOne({accountId:entry.accountId,'profiles.athena.rvn':base},{$set:{'profiles.athena':p}});
  if(saved.matchedCount){
   // Hiding a default may have moved an equipped slot to another cosmetic; refresh the match-server loadout.
   try{await publishCharacter(entry.accountId,p);}catch(error){console.warn('[Syndicate] Could not refresh the loadout:',error.message);}
   return res.json({enabled,changed:+changed});
  }
 }
 res.status(409).json({message:'Your locker is busy. Try again in a moment.'});
}));
// Syndicate: "Remove unreleased win umbrellas" toggle. On: the Bronze/Gold win-milestone umbrellas are stripped
// from the locker and not re-granted. Off: they come back if the player has earned them by win count.
for(const operation of ['status','set'])router.post(`/launcher/hidewinumbrellas/${operation}`,safe(async(req,res)=>{
 const entry=sessions.get((req.headers.authorization||'').replace(/^Bearer /,''));
 if(!entry || entry.expires<=Date.now())return res.status(401).json({message:'Please sign in again.'});
 if(operation==='set'&&travelActive(entry.accountId))return res.status(409).json({message:'Quit your friend\'s match to sync your account first.'});
 for(let attempt=0;attempt<4;attempt++){
  const doc=await Profile.collection.findOne({accountId:entry.accountId});if(!doc?.profiles?.athena)return res.status(404).json({message:'Profile missing.'});
  const p=doc.profiles.athena,a=p.stats.attributes,base=p.rvn;
  if(operation==='status')return res.json({enabled:!!a.tilted_hide_win_umbrellas});
  const enabled=req.body?.enabled===true;a.tilted_hide_win_umbrellas=enabled;
  const changed=enabled?applyHideWinUmbrellas(p):restoreWinUmbrellas(p);
  p.rvn=(p.rvn||0)+1;p.commandRevision=(p.commandRevision||0)+1;p.updated=new Date().toISOString();
  const saved=await Profile.collection.updateOne({accountId:entry.accountId,'profiles.athena.rvn':base},{$set:{'profiles.athena':p}});
  if(saved.matchedCount){
   try{await publishCharacter(entry.accountId,p);}catch(error){console.warn('[Syndicate] Could not refresh the loadout:',error.message);}
   return res.json({enabled,changed:+changed});
  }
 }
 res.status(409).json({message:'Your locker is busy. Try again in a moment.'});
}));
// Syndicate: "Save the World skins" switch, under Unlock all (which must be on). On: every Save the World hero is an
// outfit in the locker (Pre-season to Chapter 2 Season 1) and can be picked below. Off: the heroes leave the locker
// and the hero pick is cleared.
for(const operation of ['status','set'])router.post(`/launcher/stwskins/${operation}`,safe(async(req,res)=>{
 const entry=sessions.get((req.headers.authorization||'').replace(/^Bearer /,''));
 if(!entry || entry.expires<=Date.now())return res.status(401).json({message:'Please sign in again.'});
 if(operation==='set'&&travelActive(entry.accountId))return res.status(409).json({message:'Quit your friend\'s match to sync your account first.'});
 for(let attempt=0;attempt<4;attempt++){
  const doc=await Profile.collection.findOne({accountId:entry.accountId});if(!doc?.profiles?.athena)return res.status(404).json({message:'Profile missing.'});
  const p=doc.profiles.athena,a=p.stats.attributes,base=p.rvn;
  if(operation==='status')return res.json({unlockAll:!!a.tilted_unlock_all,enabled:stwSkinsOn(a)});
  const enabled=req.body?.enabled===true;
  if(enabled&&!a.tilted_unlock_all)return res.status(409).json({message:'Turn on Unlock all first.'});
  if(enabled)a.tilted_stw_skins=true;else{delete a.tilted_stw_skins;delete a.tilted_stw_hero;}
  // The STW backpacks, loading screens and music follow the switch too (syndicateUnlockAll.js STW_ITEM).
  if(a.tilted_unlock_all)applyUnlockAll(p,Number(a.tilted_active_season)||4,doc.syndicatePassPurchases||{},redeemedItems(doc));
  p.rvn=(p.rvn||0)+1;p.commandRevision=(p.commandRevision||0)+1;p.updated=new Date().toISOString();
  const saved=await Profile.collection.updateOne({accountId:entry.accountId,'profiles.athena.rvn':base},{$set:{'profiles.athena':p}});
  if(!saved.matchedCount)continue;
  try{await publishCharacter(entry.accountId,p);}catch(error){console.warn('[Syndicate] Could not refresh the loadout:',error.message);}
  return res.json({unlockAll:true,enabled:stwSkinsOn(a)});
 }
 res.status(409).json({message:'Your locker is busy. Try again in a moment.'});
}));
// Syndicate: Save the World hero in Battle Royale (Unlock all must be on). The pick is written to the match
// server's locker file right away; the match server puts the hero on the player (syndicateStwHeroes.js).
for(const operation of ['status','set'])router.post(`/launcher/stwhero/${operation}`,safe(async(req,res)=>{
 const entry=sessions.get((req.headers.authorization||'').replace(/^Bearer /,''));
 if(!entry || entry.expires<=Date.now())return res.status(401).json({message:'Please sign in again.'});
 if(operation==='set'&&travelActive(entry.accountId))return res.status(409).json({message:'Quit your friend\'s match to sync your account first.'});
 const heroes=stwHeroes();
 for(let attempt=0;attempt<4;attempt++){
  const doc=await Profile.collection.findOne({accountId:entry.accountId});if(!doc?.profiles?.athena)return res.status(404).json({message:'Profile missing.'});
  const p=doc.profiles.athena,a=p.stats.attributes,base=p.rvn;
  if(operation==='status')return res.json({unlockAll:!!a.tilted_unlock_all,stwSkins:stwSkinsOn(a),hero:selectedHero(a),heroes});
  if(!a.tilted_unlock_all)return res.status(409).json({message:'Turn on Unlock all first.'});
  if(!stwSkinsOn(a))return res.status(409).json({message:'Turn on Save the World skins first.'});
  const hero=typeof req.body?.hero==='string'?req.body.hero:'';
  if(hero&&!heroes.some(h=>h.id===hero))return res.status(400).json({message:'That hero is not available.'});
  if(hero)a.tilted_stw_hero=hero;else delete a.tilted_stw_hero;
  p.rvn=(p.rvn||0)+1;p.commandRevision=(p.commandRevision||0)+1;p.updated=new Date().toISOString();
  const saved=await Profile.collection.updateOne({accountId:entry.accountId,'profiles.athena.rvn':base},{$set:{'profiles.athena':p}});
  if(!saved.matchedCount)continue;
  try{await publishCharacter(entry.accountId,p);}catch(error){console.warn('[Syndicate] Could not save the hero for the match:',error.message);}
  return res.json({unlockAll:true,stwSkins:true,hero:selectedHero(a),heroes});
 }
 res.status(409).json({message:'Your locker is busy. Try again in a moment.'});
}));
// Syndicate Arena HUD: the launcher polls this once a second while Fortnite runs. The game server
// writes Runtime/Server/arena-live.json during Arena matches (kills, placement, players alive);
// Hype is shown as the value at the start of the match plus what this match has earned so far,
// using the same rules the backend applies when the match ends (syndicateArena.js).
const arenaLiveFile=fileURLToPath(new URL('../../../Runtime/Server/arena-live.json',import.meta.url));
let arenaLiveCache={mtime:0,data:null};
const arenaBaselines=new Map();
function readArenaLive(){
 try{
  const mtime=statSync(arenaLiveFile).mtimeMs;
  if(mtime!==arenaLiveCache.mtime)arenaLiveCache={mtime,data:JSON.parse(readFileSync(arenaLiveFile,'utf8'))};
  return Date.now()-mtime<6000?arenaLiveCache.data:null;
 }catch{return null;}
}
// Network HUD (launcher, Pre-season): the host's game server numbers. Read only; no account data.
// Guests on Radmin/LAN read it from the host's backend, so it needs no launcher session.
const netLiveFile=new URL('../../../Runtime/Server/net-live.json',import.meta.url);let netLiveCache={mtime:0,data:null};
router.get('/tilted/net/live',(req,res)=>{
 try{
  const mtime=statSync(netLiveFile).mtimeMs;
  if(mtime!==netLiveCache.mtime)netLiveCache={mtime,data:JSON.parse(readFileSync(netLiveFile,'utf8'))};
  const d=netLiveCache.data;
  if(!d || Date.now()-mtime>8000)return res.json({active:false});
  const players=Array.isArray(d.players)?d.players.slice(0,8).map(p=>({name:String(p?.name||'').slice(0,32),ping:Number.isFinite(p?.ping)?p.ping:-1})):[];
  res.json({active:true,tick:Number(d.tick)||0,worstFrameMs:Number(d.worstFrameMs)||0,humans:Number(d.humans)||0,bots:Number(d.bots)||0,version:Number(d.version)||0,players});
 }catch{res.json({active:false});}
});
// Syndicate level HUD: the local game server asks for the account's current season level
// (profiles.athena.stats.attributes.level) so the top-left badge does not stay at 1.
router.get('/tilted/level/:username',safe(async(req,res)=>{
 const name=String(req.params.username||'').toLowerCase();
 const user=await User.findOne({username_lower:name}).lean();
 if(!user)return res.json({level:1,bookLevel:0});
 const doc=await Profile.findOne({accountId:user.accountId},{'profiles.athena.stats.attributes.level':1,'profiles.athena.stats.attributes.book_level':1}).lean();
 const attrs=doc?.profiles?.athena?.stats?.attributes||{};
 const level=Math.max(1,Number(attrs.level)||1);
 const bookLevel=Math.max(0,Number(attrs.book_level)||0);
 res.json({level,bookLevel});
}));
router.get('/tilted/arena/live',safe(async(req,res)=>{
 const entry=sessions.get((req.headers.authorization||'').replace(/^Bearer /,''));
 if(!entry || entry.expires<=Date.now())return res.status(401).json({message:'Please sign in again.'});
 const live=readArenaLive();
 if(!live || !Array.isArray(live.players))return res.json({active:false});
 const user=await User.findOne({accountId:entry.accountId}).lean();
 const name=String(user?.username||'').toLowerCase();
 const me=live.players.find(p=>String(p.name||'').toLowerCase()===name);
 if(!me)return res.json({active:false});
 const kills=Math.max(0,Number(me.kills)||0),place=Math.max(0,Number(me.place)||0),alive=Math.max(0,Number(live.alive)||0);
 const key=`${live.match}|${entry.accountId}`;
 if(!arenaBaselines.has(key)){
  // First look at this match (or the backend/launcher restarted mid-match): the profile may already
  // hold this match's kill Hype, so take it back out. Placement is only saved after the match.
  if(place>0)return res.json({active:false});
  const doc=await Profile.collection.findOne({accountId:entry.accountId},{projection:{'profiles.athena.stats.attributes.arena_hype':1}});
  const current=Math.max(0,Number(doc?.profiles?.athena?.stats?.attributes?.arena_hype)||0);
  arenaBaselines.set(key,{hype:Math.max(0,current-kills*20),at:Date.now()});
  for(const [k,v] of arenaBaselines)if(Date.now()-v.at>6*60*60*1000)arenaBaselines.delete(k);
 }
 const baseline=arenaBaselines.get(key).hype;
 // Same rules as applyArenaHype: each elimination +20 as it happens, then placement minus the bus
 // fare of the division reached after those eliminations, never below that division's floor.
 const afterKills=baseline+kills*20;
 const division=arenaDivision(afterKills);
 const floor=division>1?arenaThresholds[division-2]:0;
 // Placement Hype: final once eliminated (or won); while alive, the milestones already survived.
 const placement=place>0?placementHype(place):(alive>0?placementHype(alive>25?0:alive<=5?5:alive<=15?15:25):0);
 const fare=arenaBusFare(division);
 const hype=Math.max(floor,afterKills+placement-fare);
 const now=arenaDivision(hype);
 res.json({active:true,season:live.season,phase:live.phase,alive,started:live.started,kills,place,
  killPoints:kills*20,placementPoints:placement,busFare:fare,earned:hype-baseline,baseline,hype,
  division:now,league:arenaLeague(now),divisionFloor:now>1?arenaThresholds[now-2]:0,nextThreshold:now<=arenaThresholds.length?arenaThresholds[now-1]:null});
}));
setInterval(() => {
  for (const [key, entry] of sessions) if (entry.expires <= Date.now()) sessions.delete(key);
}, 60000).unref();
export default router;
