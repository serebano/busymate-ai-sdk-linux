"""Maintainer helper: recompute release-manifest.json hashes for a NEW version only.
Never run it to make a changed file pass under an already released version."""
from pathlib import Path
import hashlib,json
root=Path(__file__).resolve().parents[1]
path=root/'release-manifest.json'
m=json.loads(path.read_text())
m['files']={name:hashlib.sha256((root/name).read_bytes()).hexdigest() for name in m['files']}
path.write_text(json.dumps(m,indent=2)+'\n')
print(json.dumps(m['files'],indent=2))
