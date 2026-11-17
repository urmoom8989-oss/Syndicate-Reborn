# Reproduce the existing local backend hotfix-list and byte-length corrections.
# No installed Reboot file is modified. Input and output paths must differ.
from pathlib import Path
import hashlib,sys
source=Path(sys.argv[1]); target=Path(sys.argv[2])
assert source.resolve()!=target.resolve(), "Choose a separate output file"
data=source.read_bytes()
assert hashlib.sha256(data).hexdigest()=="5d23c13ef5bbfff82c6d104c78fc105a92c986fc869bfc5f5a737988dd8d17bc", "Unexpected backend version"
replacements=[
    (b"memory.build <= 10.40", b"memory.build <  10.40"),
    (b"const ParsedFile = fs.readFileSync(path.join(dir, name), 'utf-8');", b"const ParsedFile = fs.readFileSync(path.join(dir, name)         );")
]
for before,after in replacements:
    assert len(before)==len(after) and data.count(before)==1
    data=data.replace(before,after)
target.write_bytes(data)
print("Created separate repaired backend copy.")
