#!/usr/bin/env python3
"""Export the existing corrected MXE SDK, recipes and cached source archives.

Run on Linux/WSL. Nothing in the SDK is rebuilt or overwritten.
"""
import argparse, hashlib, json, os, subprocess, tarfile
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--mxe',type=Path,default=Path('/usr/lib/mxe'))
p.add_argument('--output',type=Path,required=True)
p.add_argument('--notices',type=Path,required=True)
p.add_argument('--notices-only',action='store_true')
a=p.parse_args(); base=a.mxe.resolve(); out=a.output.resolve()
assert (base/'usr/x86_64-w64-mingw32.static/qt6/lib/libQt6Gui.a').is_file()
out.parent.mkdir(parents=True,exist_ok=True); a.notices.mkdir(parents=True,exist_ok=True)
def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        while b:=f.read(1024*1024): h.update(b)
    return h.hexdigest()
archives=sorted((base/'pkg').iterdir()); sources={}
notice_index={}; notice_origins={}
for source in archives:
    if not source.is_file(): continue
    sources[source.name]={'sha256':sha(source),'bytes':source.stat().st_size}
    found=[]
    if source.name.endswith('.tar.lz'):
        proc=subprocess.Popen(['lzip','-dc',str(source)],stdout=subprocess.PIPE)
        archive=tarfile.open(fileobj=proc.stdout,mode='r|')
    else:
        proc=None; archive=tarfile.open(source,'r:*')
    with archive:
        for member in archive:
            parts=Path(member.name).parts; name=Path(member.name).name.lower()
            # Root grants plus Qt's complete per-module license texts.
            selected=(name.startswith(('copying','license','licence','notice','copyright'))
                      or name in ('ftl.txt','gplv2.txt','gplv3.txt','qt_attribution.json'))
            selected=selected or (source.name.startswith('jpeg') and name=='readme')
            selected=selected or (source.name.startswith('lua-') and name=='readme.html')
            selected=selected or ('LICENSES' in parts and name.endswith('.txt'))
            if member.isfile() and selected and member.size<=1024*1024:
                data=archive.extractfile(member).read()
                # Keep Windows installation paths short without losing provenance.
                slug=source.name.split('.tar')[0][:44]
                key=hashlib.sha256(member.name.encode()).hexdigest()[:16]
                relative=slug+'/'+key+'-'+Path(member.name).name[:40]
                target=(a.notices/relative).resolve()
                if not target.is_relative_to(a.notices.resolve()): raise ValueError(relative)
                target.parent.mkdir(parents=True,exist_ok=True); target.write_bytes(data)
                found.append(relative)
                notice_origins[relative]={'archive':source.name,'member':member.name,
                                         'sha256':hashlib.sha256(data).hexdigest()}
    if proc:
        proc.stdout.close()
        if proc.wait()!=0: raise RuntimeError('lzip failed')
    notice_index[source.name]=found
manifest={'schemaVersion':1,'target':'x86_64-w64-mingw32.static',
          'sourceArchives':sources,'licenseFiles':notice_index,'licenseOrigins':notice_origins,
          'note':'Captured SDK and recipe snapshot; a from-zero dependency rebuild has not been certified.'}
(a.notices/'source-index.json').write_text(json.dumps(manifest,indent=2)+'\n')
if a.notices_only:
    print(json.dumps({'sourceArchives':len(sources),'noticeFiles':sum(map(len,notice_index.values()))}),flush=True)
    raise SystemExit(0)
members=['LICENSE.md','Makefile','README.md','settings.mk','mxe.github.mk','mxe.patch.mk','mxe.updates.mk',
         'src','ext','plugins','tools','pkg','usr/bin','usr/include','usr/share',
         'usr/lib/gcc/x86_64-w64-mingw32.static','usr/libexec/gcc/x86_64-w64-mingw32.static',
         'usr/x86_64-w64-mingw32.static','usr/x86_64-pc-linux-gnu']
if out.exists(): raise SystemExit('Output exists; choose a new path.')
# Preserve symlinks and hardlinks. GNU tar refuses a changed source file.
cmd=['tar','-C',str(base),'-cf','-',*members]
tar=subprocess.Popen(cmd,stdout=subprocess.PIPE)
with out.open('xb') as stream:
    compressed=subprocess.run(['xz','-T4','-3','-c'],stdin=tar.stdout,stdout=stream)
tar.stdout.close()
if tar.wait()!=0 or compressed.returncode!=0: raise SystemExit('SDK export failed')
subprocess.run(['xz','-t',str(out)],check=True)
manifest.update(archiveSha256=sha(out),archiveBytes=out.stat().st_size)
out.with_suffix(out.suffix+'.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps({'archive':out.name,'bytes':out.stat().st_size,'sources':len(sources),
                  'noticeFiles':sum(map(len,notice_index.values())),'sha256':manifest['archiveSha256']}),flush=True)
