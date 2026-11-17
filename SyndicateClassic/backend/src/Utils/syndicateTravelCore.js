import crypto from 'node:crypto';
export function canonical(value){
 if(Array.isArray(value))return '['+value.map(canonical).join(',')+']';
 if(value&&typeof value==='object')return '{'+Object.keys(value).sort().map(k=>JSON.stringify(k)+':'+canonical(value[k])).join(',')+'}';
 return JSON.stringify(value);
}
export function digest(value){return crypto.createHash('sha256').update(canonical(value)).digest('hex');}
export function validateProfile(accountId,profiles){
 if(!/^[a-f0-9]{32}$/i.test(accountId)||!profiles||typeof profiles!=='object'||Array.isArray(profiles))throw Error('Invalid account profile.');
 if(!profiles.athena?.items||!profiles.common_core?.items)throw Error('Incomplete account profile.');
 if(Buffer.byteLength(JSON.stringify(profiles))>12*1024*1024)throw Error('Account profile exceeds transfer limit.');
 for(const p of Object.values(profiles))if(!p||typeof p!=='object'||(p.accountId&&p.accountId!==accountId))throw Error('Profile account ID does not match.');
}
export function signed(keys,body){
 const text=JSON.stringify({...body,at:Date.now(),nonce:crypto.randomBytes(16).toString('hex')});
 return {text,publicKey:keys.publicKey,signature:crypto.sign(null,Buffer.from(text),keys.privateKey).toString('base64')};
}
export function verified(envelope){
 if(typeof envelope?.text!=='string'||typeof envelope.publicKey!=='string'||envelope.publicKey.length>256)throw Error('Invalid signed request.');
 const key=crypto.createPublicKey(envelope.publicKey);
 if(key.asymmetricKeyType!=='ed25519'||!crypto.verify(null,Buffer.from(envelope.text),key,Buffer.from(envelope.signature||'','base64')))throw Error('Account signature does not match.');
 const data=JSON.parse(envelope.text);
 if(!Number.isFinite(data.at)||Math.abs(Date.now()-data.at)>120000||!/^[a-f0-9]{32}$/.test(data.nonce)||!/^[a-f0-9]{32}$/.test(data.accountId)||!/^[a-f0-9]{32}$/.test(data.sessionId))throw Error('Expired or invalid transfer request. Check both PCs clocks.');
 return data;
}
