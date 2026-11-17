const tails=new Map();
export async function acquireAccountLock(id){const previous=tails.get(id)||Promise.resolve();let release;const next=new Promise(r=>release=r);tails.set(id,next);await previous;return ()=>{release();if(tails.get(id)===next)tails.delete(id);};}
export async function withAccountLock(id,fn){const release=await acquireAccountLock(id);try{return await fn();}finally{release();}}
