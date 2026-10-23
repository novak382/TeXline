#!/usr/bin/env python3
"""Stream a WSL release artifact without holding a second archive in memory."""
import argparse, hashlib, json, subprocess, tarfile
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--file'); p.add_argument('--tree'); p.add_argument('--output',type=Path,required=True)
p.add_argument('--replace',action='store_true',help='Allow explicit replacement of changed notice text')
a=p.parse_args(); assert bool(a.file)!=bool(a.tree)
prefix=['wsl','-d','Ubuntu-24.04','-u','root','--']
if a.file:
    a.output.parent.mkdir(parents=True,exist_ok=True)
    if a.output.exists(): raise SystemExit('Output already exists.')
    proc=subprocess.Popen(prefix+['cat','--',a.file],stdout=subprocess.PIPE)
    h=hashlib.sha256(); count=0
    with a.output.open('xb') as out:
        while data:=proc.stdout.read(1024*1024): out.write(data); h.update(data); count+=len(data)
    proc.stdout.close()
    if proc.wait()!=0: raise SystemExit('Artifact transfer failed')
    print(json.dumps({'bytes':count,'sha256':h.hexdigest()}))
else:
    root=a.output.resolve(); root.mkdir(parents=True,exist_ok=True)
    proc=subprocess.Popen(prefix+['tar','-C',a.tree,'-czf','-','.'],stdout=subprocess.PIPE)
    count=0
    with tarfile.open(fileobj=proc.stdout,mode='r|gz') as archive:
        for item in archive:
            target=(root/item.name).resolve()
            if not target.is_relative_to(root): raise ValueError(item.name)
            if item.isdir(): target.mkdir(parents=True,exist_ok=True)
            elif item.isfile():
                if item.size>4*1024*1024: raise ValueError('Unexpected large notice')
                target.parent.mkdir(parents=True,exist_ok=True)
                data=archive.extractfile(item).read()
                if target.exists() and target.read_bytes()!=data and not a.replace: raise ValueError('Would overwrite '+item.name)
                target.write_bytes(data); count+=1
            else: raise ValueError('Links are not accepted in a notice tree')
    proc.stdout.close()
    if proc.wait()!=0: raise SystemExit('Notice transfer failed')
    print(json.dumps({'files':count}))
