// Syndicate: bring players from an older/separate MongoDB into this backend's account store.
// Insert-only: nothing is ever deleted or overwritten in either database. A player is skipped when
// its account ID, email or login ID already exists here, or when it was deleted in the launcher.
//
// Two ways in:
//   - LEGACY_MONGO_URI in .env: checked once at start-up (names that clash are skipped).
//   - POST /launcher/import-legacy (Routes/launcher.js): the launcher's Project Tilted account import.
//     It starts a MongoDB on a copy of %LOCALAPPDATA%\ProjectTilted\UserData\Database and calls this with
//     renameOnClash, so a Tilted player whose name is already used here comes in as "<name> Tilted".
import mongoose from "mongoose";
import User from "../User/Mongodb/Schema/user.js";
import Profile from "../User/Mongodb/Schema/profiles.js";
import Friends from "../User/Mongodb/Schema/friends.js";

const NAME_RULE = /^[a-zA-Z0-9_ -]{3,24}$/;

async function freeName(name) {
  if (!(await User.collection.findOne({ username_lower: name.toLowerCase() }))) return name;
  for (let i = 0; i < 50; i++) {
    const suffix = i === 0 ? " Tilted" : ` Tilted${i + 1}`;
    const candidate = (name.slice(0, 24 - suffix.length) + suffix).trim();
    if (!NAME_RULE.test(candidate)) continue;
    if (!(await User.collection.findOne({ username_lower: candidate.toLowerCase() }))) return candidate;
  }
  return null;
}

/** Copies the players in the database at <uri> that are not here yet. Returns [{ username, unlockAll }]. */
export async function importLegacyFrom(uri, { renameOnClash = false } = {}) {
  let connection;
  try {
    connection = await mongoose.createConnection(uri, { serverSelectionTimeoutMS: 5000 }).asPromise();
  } catch {
    return null; // the old database is not running; nothing to import
  }
  const imported = [];
  try {
    const source = connection.db;
    const users = await source.collection(User.collection.collectionName).find({}).toArray();
    // Players deleted in the launcher stay deleted (they used to come back on every launch).
    const deleted = new Set((await User.db.collection('tilted_deleted_players').find({}).toArray()).map((d) => d.accountId));
    for (const user of users) {
      if (!user.accountId || !user.username || user.travelGuest || deleted.has(user.accountId)) continue;
      const sameAccount = await User.collection.findOne({
        $or: [{ accountId: user.accountId }, { email: user.email }, { discordId: user.discordId }],
      });
      if (sameAccount) continue; // already here (imported before, or the same login)
      let username = user.username;
      if (await User.collection.findOne({ username_lower: user.username_lower ?? username.toLowerCase() })) {
        if (!renameOnClash) continue;
        username = await freeName(username);
        if (!username) continue;
      }
      const profile = await source.collection(Profile.collection.collectionName).findOne({ accountId: user.accountId });
      if (!profile) continue;
      const friends = await source.collection(Friends.collection.collectionName).findOne({ accountId: user.accountId });
      const { _id: _u, ...userDoc } = user;
      const { _id: _p, ...profileDoc } = profile;
      userDoc.username = username;
      userDoc.username_lower = username.toLowerCase();
      userDoc.isOnline = false;
      await Profile.collection.updateOne({ accountId: user.accountId }, { $setOnInsert: profileDoc }, { upsert: true });
      try {
        await User.collection.insertOne(userDoc);
      } catch (error) {
        if (error?.code === 11000) continue; // raced with a new player of the same name; try again next time
        throw error;
      }
      if (friends) {
        const { _id: _f, ...friendsDoc } = friends;
        await Friends.collection.updateOne({ accountId: user.accountId }, { $setOnInsert: friendsDoc }, { upsert: true });
      } else {
        await Friends.collection.updateOne({ accountId: user.accountId },
          { $setOnInsert: { created: new Date(), accountId: user.accountId } }, { upsert: true });
      }
      imported.push({ username, unlockAll: profileDoc?.profiles?.athena?.stats?.attributes?.tilted_unlock_all === true });
    }
    if (imported.length) console.log(`[Syndicate] Imported ${imported.length} player(s) from the previous database.`);
  } finally {
    await connection.close().catch(() => {});
  }
  return imported;
}

export async function importLegacyAccounts() {
  const uri = (process.env.LEGACY_MONGO_URI || "").trim();
  if (!uri || uri === (process.env.MONGO_URI || "").trim()) return 0;
  const imported = await importLegacyFrom(uri);
  return imported ? imported.length : 0;
}
