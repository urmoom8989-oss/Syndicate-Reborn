from pathlib import Path
import shutil,json,hashlib,zipfile,os,base64
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
w=Path(__file__).parent.resolve();p=w/'payload'
for f in (w/'launcher-publish').iterdir():
 if f.is_file() and f.suffix!='.pdb':shutil.copy2(f,p/'Launcher'/f.name)
for rel in ['Launcher/components','Launcher/repair.pending','Launcher/components.applied','Launcher/repair-payload.json','Launcher/updater.json','testing-manifest.json','tilted-testing.channel']:
 f=p/rel;assert f.resolve().is_relative_to(p.resolve())
 if f.is_dir():shutil.rmtree(f)
 elif f.exists():f.unlink()
for f in p.rglob('*.pdb'):f.unlink()
private={'.env','jwt.secret','tokens.json','launcher.config.json','remember-login.dat','runtime','database','userdata','source','accountbackups','clientsettings','updater.json'}
files=[f for f in p.rglob('*') if f.is_file()]
for f in files:
 parts=f.relative_to(p).parts
 assert not (private-({'source'} if 'node_modules' in parts else set())).intersection(x.lower() for x in parts),f
 assert not any(x in f.name for x in ['.before-','.previous-']),f
 if 'node_modules' not in parts:assert f.suffix.lower() not in ['.cs','.cpp','.h','.csproj','.vcxproj'],f
backend='Syndicate-backend' if (p/'Syndicate-backend').is_dir() else 'Helix-backend-main'
required=['Launcher/SyndicateLauncher.exe','Launcher/SyndicateLauncher.dll','Launcher/clrjit.dll','Launcher/coreclr.dll','Launcher/hostfxr.dll','Launcher/hostpolicy.dll','Dependencies/Node/node.exe',backend+'/src/local/Storefront/rotation_pool_season5.json']
for rel in required:assert (p/rel).is_file(),rel
rows=[{'path':f.relative_to(p).as_posix(),'sha256':hashlib.sha256(f.read_bytes()).hexdigest()} for f in files]
(w/'component-manifest.json').write_text(json.dumps({'version':'0.3.40','files':rows},indent=2))
with zipfile.ZipFile(w/'payload.zip','w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
 for f in sorted(files):z.write(f,f.relative_to(p))
with zipfile.ZipFile(w/'payload.zip') as z:assert z.testzip() is None
key=AESGCM.generate_key(bit_length=256);nonce=os.urandom(12);data=AESGCM(key).encrypt(nonce,(w/'payload.zip').read_bytes(),None)
(w/'payload.enc').write_bytes(nonce+data[-16:]+data[:-16])
(w/'PayloadKey.cs').write_text('internal static class PayloadKey { internal const string Value="'+base64.b64encode(key).decode()+'"; }')
print('PASS full payload, required files, privacy scan, ZIP CRC; AES-256-GCM encrypted '+str(len(rows))+' files')
