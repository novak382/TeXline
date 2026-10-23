#!/usr/bin/env python3
"""Relocate text metadata in an extracted SDK; never modify static archives."""
from pathlib import Path
import argparse, json
p=argparse.ArgumentParser(description=__doc__); p.add_argument('sdk',type=Path); a=p.parse_args()
root=a.sdk.resolve(); assert (root/'usr/x86_64-w64-mingw32.static').is_dir()
changed=[]
for base in (root/'usr',):
    for path in base.rglob('*'):
        if path.is_symlink() or not path.is_file(): continue
        if path.suffix not in {'.cmake','.pc','.prl','.pri','.la'} and path.parent!=root/'usr/bin': continue
        if path.stat().st_size>2*1024*1024: continue
        data=path.read_bytes()
        if b'\x00' in data or b'/usr/lib/mxe' not in data: continue
        path.write_bytes(data.replace(b'/usr/lib/mxe',str(root).encode()))
        changed.append(path.relative_to(root).as_posix())
print(json.dumps({'textMetadataChanged':len(changed),'staticLibrariesChanged':False,'files':changed}))
