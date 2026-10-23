#!/usr/bin/env python3
"""Prepare an allowlisted public source export and a clean Windows payload."""
from pathlib import Path
import argparse, hashlib, json, os, re, shutil, struct, zipfile

ROOT=Path(__file__).resolve().parents[1]
VERSION='1.0.0'
DOCS={'WRITING-CONTEXT.md','VISUAL-AUTHORING.md','MATURITY-UI.md','MENU-CUSTOMIZATION.md','SDK-UPDATE.md','UPSTREAM-README.md','INSTALL.md','FIRST-SESSION.md','FEEDBACK.md','KNOWN-LIMITATIONS.md',
      'BUILDING.md','LICENSING.md','RELEASE-REPORT.md','ARTICLE.md','BEAMER.md','ONBOARDING.md',
      'MOTION.md','dependency-provenance.json','RELEASE-V1.0.0.md','ONBOARDING-SIX-2026-10-21.md'}
ROOT_FILES={'LICENSE','README.md','README.zh-CN.md','CONTRIBUTING.md','THIRD-PARTY-NOTICES.md','COPYING',
            'CMakeLists.txt','GitArchiveInfo.txt','BUILD_RESEARCH_WINDOWS.md','NEWS','PACKAGING','.gitignore','.gitattributes'}
TREES={'src','modules','plugins-src','res','trans','unit-tests','CMake','tools','LICENSES','examples','win32','testcases'}
EXCLUDE={'.git','.local','out','outputs','__pycache__','.pytest_cache','build','.agents','.codex','.aws'}
def sha(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        while b:=f.read(1024*1024): h.update(b)
    return h.hexdigest()
def files(root): return sorted(p for p in root.rglob('*') if p.is_file())
def public_files():
    for directory,dirs,names in os.walk(ROOT):
        dirs[:]=[d for d in dirs if d not in EXCLUDE and not d.startswith('build')
                 and not (Path(directory)/d).is_symlink()]
        for name in names:
            p=Path(directory)/name; r=p.relative_to(ROOT); parts=r.parts
            if p.suffix.lower() in {'.exe','.obj','.o','.pyc','.pyo','.xlsx','.tmp','.bak'}: continue
            take=(len(parts)==1 and name in ROOT_FILES) or parts[0] in TREES
            take=take or (parts[0]=='docs' and len(parts)==2 and name in DOCS)
            take=take or (parts[:3]==('.github','actions','setup-windows'))
            take=take or r.as_posix()=='.github/workflows/texline-preview.yml'
            if take and not p.is_symlink(): yield p
def write_zip(path,data):
    path.parent.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(path,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        for name,body in sorted(data.items()):
            info=zipfile.ZipInfo(name,(2026,10,21,0,0,0)); info.compress_type=zipfile.ZIP_DEFLATED
            info.external_attr=0o100644<<16; z.writestr(info,body)
    with zipfile.ZipFile(path) as z:
        assert z.testzip() is None
        assert all(z.read(n)==v for n,v in data.items())
def manifest(root): return {p.relative_to(root).as_posix():{'bytes':p.stat().st_size,'sha256':sha(p)} for p in files(root)}
def main():
    p=argparse.ArgumentParser(description=__doc__); sub=p.add_subparsers(dest='action',required=True)
    s=sub.add_parser('source'); s.add_argument('--output',type=Path,required=True)
    s.add_argument('--candidate',type=Path,help='Bind the source export to a verified application executable')
    s=sub.add_parser('payload'); s.add_argument('--exe',type=Path,required=True); s.add_argument('--output',type=Path,required=True)
    s=sub.add_parser('verify'); s.add_argument('path',type=Path)
    s=sub.add_parser('portable'); s.add_argument('--payload',type=Path,required=True); s.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.action=='source':
        data={p.relative_to(ROOT).as_posix():p.read_bytes() for p in public_files()}
        candidate_identity={'candidateSha256':sha(a.candidate)} if a.candidate else {}
        # Preserve the upstream base identity without exposing a private checkout.
        data['GitArchiveInfo.txt']=b'#define GIT_COMMIT_HASH "7951fd8*"\n#define GIT_COMMIT_DATE "2026-02-10T06:40:06+01:00"\n'
        identity={'release':VERSION,'upstreamBase':'7951fd8',**candidate_identity,
                  'files':{n:hashlib.sha256(b).hexdigest() for n,b in data.items()}}
        data['source-manifest.json']=(json.dumps(identity,indent=2)+'\n').encode()
        write_zip(a.output,data)
        print(json.dumps({'sourceFiles':len(data),'bytes':a.output.stat().st_size,'sha256':sha(a.output)}))
    elif a.action=='payload':
        out=a.output.resolve()
        if out.exists() and any(out.iterdir()): raise SystemExit('Payload directory must be empty.')
        out.mkdir(parents=True,exist_ok=True)
        body=a.exe.read_bytes(); pe=struct.unpack_from('<I',body,0x3c)[0]
        assert body[:2]==b'MZ' and struct.unpack_from('<H',body,pe+4)[0]==0x8664
        shutil.copy2(a.exe,out/'TeXline.exe')

        import subprocess,sys
        subprocess.run([sys.executable,str(ROOT/'tools/package-onboarding.py'),'--destination',str(out/'resources/onboarding')],check=True)
        shutil.copytree(ROOT/'res/research/pdf-support/poppler',out/'share/poppler')
        shutil.copytree(ROOT/'res/research/pdf-support/fonts',out/'share/fonts')
        shutil.copytree(ROOT/'examples/first-session',out/'examples/first-session')
        for name in ('README.md','README.zh-CN.md','CONTRIBUTING.md','THIRD-PARTY-NOTICES.md','COPYING'):
            shutil.copy2(ROOT/name,out/name)
        shutil.copytree(ROOT/'LICENSES',out/'LICENSES')
        (out/'docs').mkdir()
        for name in sorted(DOCS-{'dependency-provenance.json'}): shutil.copy2(ROOT/'docs'/name,out/'docs'/name)
        shutil.copy2(ROOT/'res/research/JetBrainsMono-OFL.txt',out/'LICENSES/JetBrainsMono-OFL.txt')
        shutil.copytree(ROOT/'res/research/icons/codicons',out/'LICENSES/codicons')
        release={'version':VERSION,'architecture':'x64','applicationSha256':sha(out/'TeXline.exe'),
                 'sourcePackage':'TeXline-'+VERSION+'-Source.zip','rebuildKit':'TeXline-2026.10.20-preview.1-RebuildKit.tar.xz',
                 'sdkUpdate':'TeXline-1.0.0-SDK-Update.tar.xz',
                 'publiclyReleased':False,'externalHumanTrialCompleted':False,'files':manifest(out)}
        (out/'release-manifest.json').write_text(json.dumps(release,indent=2)+'\n',encoding='utf-8')
        print(json.dumps({'payloadFiles':len(release['files']),'bytes':sum(x['bytes'] for x in release['files'].values()),'applicationSha256':release['applicationSha256']}))
    elif a.action=='portable':
        release=json.loads((a.payload/'release-manifest.json').read_text(encoding='utf-8'))
        for name,value in release['files'].items():
            target=(a.payload/name).resolve()
            assert target.is_relative_to(a.payload.resolve()) and sha(target)==value['sha256'],name
        write_zip(a.output,{p.relative_to(a.payload).as_posix():p.read_bytes() for p in files(a.payload)})
        print(json.dumps({'portableFiles':len(release['files'])+1,'bytes':a.output.stat().st_size,'sha256':sha(a.output)}))
    else:
        release=json.loads((a.path/'release-manifest.json').read_text(encoding='utf-8'))
        for name,value in release['files'].items():
            target=(a.path/name).resolve()
            assert target.is_relative_to(a.path.resolve()) and target.stat().st_size==value['bytes'] and sha(target)==value['sha256'],name
        print(json.dumps({'verified':True,'files':len(release['files']),'version':release['version']}))
if __name__=='__main__': main()
