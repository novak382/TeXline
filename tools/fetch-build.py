"""Transfer Linux build artifacts without relying on WSL drive mounts."""
from pathlib import Path
import argparse, io, json, subprocess, zipfile
p=argparse.ArgumentParser()
p.add_argument('source')
p.add_argument('destination', type=Path)
p.add_argument('--pattern', default='*')
a=p.parse_args()
code="""import io,pathlib,sys,zipfile
root=pathlib.Path(sys.argv[1]);pattern=sys.argv[2]
buffer=io.BytesIO()
with zipfile.ZipFile(buffer,'w',zipfile.ZIP_DEFLATED,compresslevel=3) as z:
 for p in sorted(set(p for token in pattern.split(',') for p in root.glob(token))):
  if p.is_file(): z.write(p,p.name)
sys.stdout.buffer.write(buffer.getvalue())
"""
r=subprocess.run(['wsl','-d','Ubuntu-24.04','-u','root','--','python3','-c',code,a.source,a.pattern],capture_output=True,check=True)
a.destination.mkdir(parents=True,exist_ok=True)
with zipfile.ZipFile(io.BytesIO(r.stdout)) as z:
 for i in z.infolist():
  target=(a.destination/i.filename).resolve()
  if not target.is_relative_to(a.destination.resolve()):raise ValueError(i.filename)
  target.write_bytes(z.read(i))
 print(json.dumps({'files':z.namelist(),'compressedBytes':len(r.stdout),'destination':str(a.destination)},ensure_ascii=False))
