import test from 'node:test';
import assert from 'node:assert/strict';
import {seasonKey,validateMap,manifestFor,opLine,classSet,ingameFile} from '../syndicateCustomMap.js';
test('in-game placing file',()=>{
 assert.equal(ingameFile({on:true,class:'Floor'}),'on=1\nclass=Floor\nsnap=1\nplace=120\nturn=121\nremove=46\n');
 assert.equal(ingameFile({on:false}),'on=0\nclass=\nsnap=1\nplace=120\nturn=121\nremove=46\n');
 assert.match(ingameFile({on:true,class:'Tiered_Chest_Athena_C',snap:false,keys:{place:4}}),/snap=0\nplace=4\n/);
 assert.ok(ingameFile({on:true}).error);
 assert.ok(ingameFile({on:true,class:'a\nb'}).error);
 assert.ok(ingameFile({on:true,class:'Floor',keys:{place:121}}).error,'keys must differ');
 assert.ok(ingameFile({on:true,class:'Floor',keys:{turn:999}}).error);
});

const map=(extra={})=>({schema:'tilted-custom-map-v1',name:'Test',baseSeason:'4.5',actors:[],...extra});
test('season keys match what the game server prints',()=>{
 assert.equal(seasonKey('4.5'),'4.50');
 assert.equal(seasonKey('1.8.2'),'1.82');
 assert.equal(seasonKey('1.7.2'),'1.72');
 assert.equal(seasonKey('3.5.1'),'3.50');
 assert.equal(seasonKey('10.31'),'10.31');
  assert.equal(seasonKey('10.40'),'10.40');
 assert.equal(seasonKey(''),null);
 assert.equal(seasonKey('abc'),null);
});
test('a good map passes, with a warning while the season has no class list',()=>{
 const v=validateMap(map({actors:[{class:'Tiered_Chest_Athena_C',x:100,y:200,z:null,yaw:90}]}));
 assert.equal(v.ok,true);assert.equal(v.season,'4.50');
 assert.ok(v.warnings.some(w=>w.includes('No class list')));
});
test('classes the season does not have are errors; build shorthands always pass',()=>{
 const classes=classSet({classes:{containers:['Tiered_Chest_Athena_C'],build:['PBWA_W1_Floor_C']}});
 const v=validateMap(map({actors:[{class:'Tiered_Chest_Athena_C',x:0,y:0},{class:'Floor',x:0,y:600},{class:'Nope_C',x:5,y:5}]}),classes);
 assert.equal(v.ok,false);
 assert.equal(v.errors.length,1);assert.match(v.errors[0],/Nope_C/);
});
test('bad input is caught',()=>{
 assert.equal(validateMap(null).ok,false);
 assert.equal(validateMap(map({schema:'x'})).ok,false);
 assert.equal(validateMap(map({baseSeason:''})).ok,false);
 assert.equal(validateMap(map({actors:[{class:'A\tB',x:0,y:0}]})).ok,false,'tabs never reach the manifest');
 assert.equal(validateMap(map({actors:[{class:'A_C',x:9e9,y:0}]})).ok,false);
 assert.equal(validateMap(map({actors:[{class:'A_C',x:0,y:0,z:1e9}]})).ok,false);
 assert.equal(validateMap(map({riftZones:[{minX:5,maxX:1,minY:0,maxY:1}]})).ok,false);
 assert.equal(validateMap(map({actors:Array.from({length:1501},()=>({class:'A_C',x:0,y:0}))})).ok,false);
});
test('duplicates on the same spot are warned about',()=>{
 const v=validateMap(map({actors:[{class:'A_C',x:1,y:1},{class:'A_C',x:1,y:1}]}));
 assert.equal(v.ok,true);assert.ok(v.warnings.some(w=>w.includes('sits exactly')));
});
test('manifest carries the version and actor lines (g = on the ground)',()=>{
 const m=manifestFor(map({actors:[{class:'Floor',x:1.4,y:-2,z:null,yaw:0},{class:'Tiered_Chest_Athena_C',x:10,y:20,z:300,yaw:45}]}));
 assert.ok(m.includes('#\tversion\t4.50\n'));
 assert.ok(m.includes('actor\tFloor\t1\t-2\tg\t0\n'));
 assert.ok(m.includes('actor\tTiered_Chest_Athena_C\t10\t20\t300\t45\n'));
});
test('live commands become safe single lines',()=>{
 assert.equal(opLine({op:'spawn',id:3,class:'Floor',x:10,y:20,z:'g',yaw:0}),'spawn\t3\tFloor\t10\t20\tg\t0');
 assert.equal(opLine({op:'move',id:3,x:10,y:20,z:55.6,yaw:90}),'move\t3\t10\t20\t56\t90');
 assert.equal(opLine({op:'here',id:4,class:'Tiered_Chest_Athena_C'}),'here\t4\tTiered_Chest_Athena_C\t400\t0');
 assert.equal(opLine({op:'spawn',id:5,class:'PBWA_BG_Solid_C',x:1,y:2,z:3,yaw:37.456,pitch:0,roll:-90,sx:1,sy:1.25,sz:1,alt:2,mir:1,mesh:'/Game/Building/Meshes/Wall.Wall',tex:['/Game/Building/Tex/Brick.Brick','','-']}),
   'spawn\t5\tPBWA_BG_Solid_C\t1\t2\t3\t37.46\t0\t-90\t1\t1.25\t1\t2\t1\t/Game/Building/Meshes/Wall.Wall\t/Game/Building/Tex/Brick.Brick\t-\t-\t-');
 assert.equal(opLine({op:'spawn',id:5,class:'Floor',x:1,y:2,z:3,yaw:0,sx:2}),'spawn\t5\tFloor\t1\t2\t3\t0\t0\t0\t2\t1\t1\t-1\t-1\t-\t-\t-\t-\t-');
 assert.ok(opLine({op:'spawn',id:5,class:'Floor',x:1,y:2,z:3,mesh:'/Game/a\tb'}).error);
 assert.ok(opLine({op:'spawn',id:5,class:'Floor',x:1,y:2,z:3,sx:0}).error);
 assert.ok(opLine({op:'spawn',id:5,class:'Floor',x:1,y:2,z:3,tex:['/a','/b','/c','/d','/e']}).error);
 assert.equal(opLine({op:'delete',id:3}),'delete\t3');
 assert.equal(opLine({op:'clear'}),'clear');
 assert.ok(opLine({op:'spawn',id:3,class:'Bad\nclass',x:0,y:0}).error);
 assert.ok(opLine({op:'spawn',id:0,class:'Floor',x:0,y:0}).error);
 assert.ok(opLine({op:'move',id:3,x:'nope',y:0}).error);
 assert.ok(opLine({op:'rm -rf'}).error);
});
