import test from 'node:test';
import assert from 'node:assert/strict';
import {withoutToggleSprint,settingsForSeason} from '../syndicateToggleSprint.js';

// The bytes Fortnite 1.8.2 writes for "bToggleSprint = true", between two other properties.
function save(value){
 const prop=(n,t,v=1)=>{const a=Buffer.alloc(4);a.writeInt32LE(n.length+1);const b=Buffer.alloc(4);b.writeInt32LE(t.length+1);
  return Buffer.concat([a,Buffer.from(n+'\0','latin1'),b,Buffer.from(t+'\0','latin1'),Buffer.alloc(8),Buffer.from([v,0])]);};
 return Buffer.concat([Buffer.from('header'),prop('bToggleSprint','BoolProperty',value),prop('bForceFeedbackEnabled','BoolProperty')]);
}
test('Toggle Sprint is switched off and nothing else changes',()=>{
 const on=save(1),off=withoutToggleSprint(on);
 assert.deepEqual(off,save(0));
 assert.equal(on.length,off.length);
 assert.deepEqual(on,save(1),'the input buffer is not modified');
});
test('only Pre-season to Season 4, only while Sprint by default is on',()=>{
 assert.deepEqual(settingsForSeason(save(1),4,true),save(0));
 assert.deepEqual(settingsForSeason(save(1),0,true),save(0));
 assert.deepEqual(settingsForSeason(save(1),5,true),save(1));
 assert.deepEqual(settingsForSeason(save(1),2,false),save(1));
 assert.equal(settingsForSeason(null,1,true),null);
});
