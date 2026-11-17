from pathlib import Path
import hashlib,json,re,sys
source,target=map(Path,sys.argv[1:3]);original=source.read_bytes();data=bytearray(original)
start=original.find(b'"./index.js"',68000000);assert start>=0
at=original.rfind(b'{',0,start);table,_=json.JSONDecoder().raw_decode(original[at:at+300000].decode())
payload=69268507
assert original[payload+635:payload+660].startswith(b'const Express = require')
out=target.parent.parent.parent.parent.parent/'BackendSource'
# Optional explicit source-evidence folder.
if len(sys.argv)>3:out=Path(sys.argv[3])
out.mkdir(parents=True,exist_ok=True);changes=0
for name,pair in table.items():
 if not (name=='./index.js' or name.startswith('./structure\\') and name.endswith('.js')):continue
 pos=payload+pair[0];length=pair[1];text=original[pos:pos+length].decode('utf-8')
 def replace(m):
  global changes
  value=m.group(2);expr=re.fullmatch(r'\$\{(.*)\}(?:\.json)?',value)
  arg=expr.group(1) if expr else json.dumps(value.removesuffix('.json'))
  changes+=1;return 'global.tpf('+arg+')'
 text=re.sub(r'require\((["`])\./\.\./profiles/(.*?)\1\)',replace,text)
 if name=='./index.js':text='global.tpf=require(process.cwd()+"/titanium-profiles.cjs");\n'+text.replace('\r\n','\n')
 encoded=text.encode();assert len(encoded)<=length,(name,len(encoded),length)
 data[pos:pos+length]=encoded+b' '*(length-len(encoded))
 file=out/name.removeprefix('./').replace('\\','/');file.parent.mkdir(parents=True,exist_ok=True);file.write_text(text,encoding="utf-8")
assert changes==87,changes
(out/'patch-evidence.json').write_text(json.dumps({'base_sha256':hashlib.sha256(original).hexdigest(),'patched_sha256':hashlib.sha256(data).hexdigest(),'profile_imports_replaced':changes,'snapshot_lengths_unchanged':True},indent=2))
target.write_bytes(data)
print('Profile disk loader connected in 87 backend imports; resource offsets preserved.')

