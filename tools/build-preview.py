#!/usr/bin/env python3
"""Build the application from an exported source tree using a fixed MXE SDK.

Use a fresh build directory to test an application rebuild. This does not
certify a from-zero rebuild of all dependency libraries.
"""
from pathlib import Path
import argparse, hashlib, json, os, subprocess, sys

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--mxe',type=Path,required=True)
p.add_argument('--build',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--jobs',type=int,default=4)
a=p.parse_args(); root=Path(__file__).resolve().parents[1]; mxe=a.mxe.resolve()
target=mxe/'usr/x86_64-w64-mingw32.static'
expected=json.loads((root/'docs/dependency-provenance.json').read_text())['archives']['x86_64-w64-mingw32.static']
actual={}
for name,digest in expected.items():
    value=hashlib.sha256((target/name).read_bytes()).hexdigest(); actual[name]=value
    if value!=digest: raise SystemExit('Unrecognized corrected SDK archive: '+name)
env=os.environ.copy()
env['LD_LIBRARY_PATH']=str(mxe/'usr/x86_64-pc-linux-gnu/qt6/lib')+':'+str(mxe/'usr/x86_64-pc-linux-gnu/lib')+':'+env.get('LD_LIBRARY_PATH','')
subprocess.run([sys.executable,str(root/'.github/actions/setup-windows/build-research-x64.py'),
    '--source',str(root),'--build',str(a.build.resolve()),'--output',str(a.output.resolve()),
    '--mxe',str(mxe),'--jobs',str(a.jobs)],env=env,check=True)
record=json.loads((a.output/'candidate-build.json').read_text())
record.update(correctedArchives=actual,sourceManifestSha256=hashlib.sha256((root/'source-manifest.json').read_bytes()).hexdigest(),
              dependencyBuild='captured corrected SDK; not a from-zero dependency rebuild')
(a.output/'application-rebuild.json').write_text(json.dumps(record,indent=2)+'\n')
