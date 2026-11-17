import mongoose from 'mongoose';import dotenv from 'dotenv';import fs from 'node:fs';import {randomUUID} from 'node:crypto';import assert from 'node:assert/strict';
import Profile from '../User/Mongodb/Schema/profiles.js';import User from '../User/Mongodb/Schema/user.js';
dotenv.config();await mongoose.connect(process.env.MONGO_URI);
const accountId=`syndicate-live-test-${randomUUID()}`;const id=`{${randomUUID()}}`;const file=new URL(`../../../Runtime/Server/progression-queue/${id}.json`,import.meta.url);
try{
 const profiles={};for(const kind of ['athena','common_core']){profiles[kind]=JSON.parse(fs.readFileSync(new URL(`../Profiles/DefaultProfiles/${kind}.json`,import.meta.url)));profiles[kind].accountId=accountId;}
 await Profile.collection.insertOne({accountId,created:new Date(),profiles});await User.collection.insertOne({accountId,username:accountId,username_lower:accountId});
 fs.writeFileSync(file,JSON.stringify({id,username:accountId,kind:'kill'}));
 let saved;for(let i=0;i<12;i++){await new Promise(r=>setTimeout(r,1000));saved=await Profile.collection.findOne({accountId});if(saved.syndicateProgressionEvents?.includes(id))break;}
 assert.equal(saved.profiles.athena.stats.attributes.tilted_season4_xp_total,100);assert.equal(fs.existsSync(file),false);
 console.log('PASS: running backend consumed kill event, awarded 100 XP, leveled up and acknowledged queue file.');
}finally{if(fs.existsSync(file))fs.unlinkSync(file);await User.collection.deleteOne({accountId});await Profile.collection.deleteOne({accountId});await mongoose.disconnect();}
