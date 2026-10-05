"""Fail if a vendored upstream input changes without an intentional pin update."""
import hashlib
from pathlib import Path
root=Path(__file__).resolve().parents[1]/'third_party/sameboy'
for line in (root/'SHA256SUMS').read_text().splitlines():
    digest,name=line.split('  ',1)
    assert hashlib.sha256((root/name).read_bytes()).hexdigest()==digest,name
print('PASS pinned SameBoy source hashes')
