from pathlib import Path
import json,os,shutil,socket,subprocess,tempfile,time,urllib.request
launcher=Path(__file__).resolve().parent
runtime=launcher/'SyndicateLauncher/Runtime/backend'
evidence=launcher.parents[1]/'ai1040/ResourceMovementFix'
root=Path(tempfile.mkdtemp(prefix='SyndicateNativeLocker-'))
checks=0;proc=None;log=None
try:
 shared=root/'Backend';shutil.copytree(runtime,shared)
 binary=bytearray((shared/'lawinserver.exe').read_bytes());payload=69268507
 marker=b'"./index.js"';x=binary.find(marker,68000000);a=binary.rfind(b'{',0,x);table,_=json.JSONDecoder().raw_decode(bytes(binary[a:a+300000]).decode())
 port=None
 for n in range(3781,3880):
  sock=socket.socket()
  try:sock.bind(('127.0.0.1',n));port=n;break
  except OSError:pass
  finally:sock.close()
 assert port
 off,length=table['./index.js'];at=payload+off
 block=bytes(binary[at:at+length]);assert block.count(b'const port = 3551;')==1
 block=block.replace(b'const port = 3551;',('const port = '+str(port)+';').encode())
 block=block.rstrip().replace(b'express.listen(port, () => {',b'express.listen(port,"127.0.0.1",()=>{')
 assert len(block)<=length;binary[at:at+length]=block+b' '*(length-len(block))
 off,length=table['./structure\\xmpp.js'];at=payload+off;block=bytes(binary[at:at+length]);assert block.count(b'const port = 80;')==1
 binary[at:at+length]=block.replace(b'const port = 80;',b'const port = 0 ;')
 (shared/'lawinserver.exe').write_bytes(binary)
 profiles={}
 for name in ('A','B'):
  cwd=root/name;cwd.mkdir();shutil.copytree(runtime/'profiles',cwd/'profiles');shutil.copyfile(runtime/'titanium-profiles.cjs',cwd/'titanium-profiles.cjs');profiles[name]=cwd
 original=json.loads((profiles['A']/'profiles/athena.json').read_text(encoding='utf-8-sig'))
 def item(kind):return next(k for k,v in original['items'].items() if v['templateId'].lower().startswith(kind.lower()+':'))
 character=item('AthenaCharacter');pickaxe=item('AthenaPickaxe');dance=item('AthenaDance');wrap=item('AthenaItemWrap')
 extra={name:item('Athena'+name) for name in ('Backpack','Glider','SkyDiveContrail','MusicPack','LoadingScreen')}
 def stop():
  global proc,log
  if proc:
   proc.terminate()
   try:proc.wait(10)
   except subprocess.TimeoutExpired:proc.kill();proc.wait()
   proc=None
  if log:log.close();log=None
 def start(name):
  global proc,log
  log=open(root/('backend-'+name+'.log'),'ab')
  env=os.environ.copy();env['LOCALAPPDATA']=str(root/'LocalAppData');env['APPDATA']=str(root/'AppData')
  Path(env['LOCALAPPDATA']).mkdir(exist_ok=True)
  proc=subprocess.Popen([str(shared/'lawinserver.exe')],cwd=profiles[name],stdout=log,stderr=log,env=env,creationflags=0x08000000)
  for _ in range(100):
   if proc.poll() is not None:raise RuntimeError((root/('backend-'+name+'.log')).read_text(errors='replace')[-3000:])
   try:
    urllib.request.urlopen(f'http://127.0.0.1:{port}/fortnite/api/version',timeout=.5).read();return
   except Exception:time.sleep(.1)
  raise RuntimeError('Private locker backend did not become ready.')
 def call(command,body):
  req=urllib.request.Request(f'http://127.0.0.1:{port}/fortnite/api/game/v2/profile/TitaniumLockerTest/client/{command}?profileId=athena&rvn=-1',data=json.dumps(body).encode(),headers={'Content-Type':'application/json','User-Agent':'Fortnite/++Fortnite+Release-10.40-CL-9380822 Windows/10.0.0.0.256.64bit'},method='POST')
  with urllib.request.urlopen(req,timeout=15) as res:return json.load(res)
 def query():return call('QueryProfile',{})['profileChanges'][0]['profile']
 def check(value,text):
  global checks
  assert value,text;checks+=1
 start('A')
 for slot,value in [('Character',character),('Pickaxe',pickaxe),('Dance',dance),('ItemWrap',wrap)]+list(extra.items()):
  call('EquipBattleRoyaleCustomization',{'slotName':slot,'itemToSlot':value,'indexWithinSlot':-1 if slot=='ItemWrap' else 0})
  key='favorite_'+slot.lower()+('s' if slot=='ItemWrap' else '')
  val=query()['stats']['attributes'][key]
  check((val==value) if isinstance(val,str) else value in val,'Older locker command '+slot)
 call('SetCosmeticLockerSlot',{'lockerItem':'lawin-loadout','category':'Character','itemToSlot':character,'slotIndex':0,'variantUpdates':[{'channel':'Material','active':'Mat2'}]})
 check(query()['items']['lawin-loadout']['attributes']['locker_slots_data']['slots']['Character']['items'][0]==character,'Locker slot command')
 call('SetCosmeticLockerSlot',{'lockerItem':'lawin-loadout','category':'Pickaxe','itemToSlot':pickaxe,'slotIndex':0})
 call('SetCosmeticLockerSlot',{'lockerItem':'lawin-loadout','category':'ItemWrap','itemToSlot':wrap,'slotIndex':-1})
 first=query();stop();start('A');again=query()
 check(again['stats']['attributes']['favorite_character']==character,'Favorite character survives full backend restart')
 check(again['stats']['attributes']['favorite_pickaxe']==pickaxe,'Pickaxe survives full backend restart')
 check(again['stats']['attributes']['favorite_dance'][0]==dance,'Emote survives full backend restart')
 check(all(v==wrap for v in again['stats']['attributes']['favorite_itemwraps']),'All-wrap selection survives restart')
 slots=again['items']['lawin-loadout']['attributes']['locker_slots_data']['slots']
 check(slots['Character']==first['items']['lawin-loadout']['attributes']['locker_slots_data']['slots']['Character'],'Slot item and style variants survive restart')
 check('Mat2' in json.dumps(slots['Character'].get('activeVariants')),'Selected style actually applied and persisted')
 for slot,value in extra.items():check(again['stats']['attributes']['favorite_'+slot.lower()]==value,'Restart restored '+slot)
 check(slots['Pickaxe']['items'][0]==pickaxe,'Locker-slot pickaxe survives restart')
 check(all(v==wrap for v in slots['ItemWrap']['items']),'Locker-slot all-wrap selection survives restart')
 stop();start('B');b=query()
 check(b['stats']['attributes']['favorite_character']==original['stats']['attributes']['favorite_character'],'Different launcher profile keeps its own default locker')
 call('EquipBattleRoyaleCustomization',{'slotName':'Character','itemToSlot':''});stop();start('A')
 check(query()['stats']['attributes']['favorite_character']==character,'Switching profiles never overwrites the first locker')
 check(not list(profiles['A'].glob('profiles/*.pending-*')),'No incomplete JSON publications')
 stop()
 (evidence/'native-locker-tests.log').write_text(f'PASS: {checks} native backend locker checks on an isolated loopback port; both locker command formats, style/slot state, full process restarts and separate profile folders. Fortnite was not launched.\n')
 print((evidence/'native-locker-tests.log').read_text())
except Exception:
 for file in root.glob('backend-*.log'):
  print(file.name,file.read_text(errors='replace')[-2500:])
 raise
finally:
 if proc:proc.terminate();proc.wait(10)
 if log:log.close()
 # The target is the exact directory returned by mkdtemp for this test only.
 assert root.name.startswith('SyndicateNativeLocker-') and root.parent==Path(tempfile.gettempdir())
 shutil.rmtree(root)
