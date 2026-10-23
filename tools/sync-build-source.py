"""Stage this checkout into the existing WSL x64 source cache; never install."""
from pathlib import Path, PurePosixPath
import argparse, hashlib, io, json, os, subprocess, zipfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--distribution', default='Ubuntu-24.04')
parser.add_argument('--target', default='/tmp/texworks-x64/source')
parser.add_argument('--dry-run', action='store_true')
args = parser.parse_args()
repo = Path(__file__).resolve().parents[1]
if not (repo / 'src/ResearchCommandCompletion.cpp').is_file():
    raise SystemExit('Run the copy of this tool inside the TeXline checkout')
target = PurePosixPath(args.target)
if not target.is_absolute() or '..' in target.parts or len(target.parts) < 3:
    raise SystemExit('Choose a dedicated absolute WSL source directory')
excluded = {'.git', '.local', 'out', 'build', '__pycache__', '.pytest_cache'}
paths = []
# Prune private outputs before descending: filtering an rglob result still
# walks private cache junctions and fails on Windows paths beyond MAX_PATH.
for directory, folders, names in os.walk(repo):
    folders[:] = [name for name in folders if name not in excluded
                  and not getattr(os.lstat(Path(directory) / name), 'st_reparse_tag', 0)]
    for name in names:
        path = Path(directory) / name
        if path.suffix not in {'.pyc', '.pyo', '.exe', '.obj', '.o'} and path.is_file():
            paths.append(path)
paths.sort()
files = {p.relative_to(repo).as_posix(): p.read_bytes() for p in paths}
# WSL receives an exported tree without .git, so resolve archive placeholders
# from this checkout instead of changing the version of otherwise identical code.
if (repo / '.git').exists():
    command = ['git', '--no-optional-locks', '-c', 'safe.directory='+str(repo), '-C', str(repo)]
    commit = subprocess.check_output(command+['show', '-s', '--format=%h%n%cI', 'HEAD'], text=True).splitlines()
    dirty = bool(subprocess.check_output(command+['status', '--porcelain']))
    files['GitArchiveInfo.txt'] = ('// Exported source version; application patches are recorded in source-manifest.json.\n'
                                  '#define GIT_COMMIT_HASH "'+commit[0]+('*' if dirty else '')+'"\n'
                                  '#define GIT_COMMIT_DATE "'+commit[1]+'"\n').encode()
digests = {name: hashlib.sha256(data).hexdigest() for name, data in files.items()}
if args.dry_run:
    print(json.dumps({'files': len(files), 'bytes': sum(map(len, files.values())),
                      'repository': str(repo), 'target': str(target)}, ensure_ascii=False))
    raise SystemExit(0)
buffer = io.BytesIO()
with zipfile.ZipFile(buffer, 'w', zipfile.ZIP_DEFLATED) as archive:
    for name, data in files.items():
        archive.writestr(name, data)
code = '''import hashlib,io,json,pathlib,sys,zipfile
base=pathlib.Path(sys.argv[1]).resolve()
base.mkdir(parents=True,exist_ok=True)
digests={};changed=0
with zipfile.ZipFile(io.BytesIO(sys.stdin.buffer.read())) as archive:
 for item in archive.infolist():
  path=(base/item.filename).resolve()
  if not path.is_relative_to(base):raise ValueError(item.filename)
  data=archive.read(item)
  path.parent.mkdir(parents=True,exist_ok=True)
  if not path.exists() or path.read_bytes()!=data:
   path.write_bytes(data);changed+=1
  digests[item.filename]=hashlib.sha256(path.read_bytes()).hexdigest()
print(json.dumps({'changed':changed,'digests':digests}))
'''
reply = subprocess.run(['wsl', '-d', args.distribution, '-u', 'root', '--',
                        'python3', '-c', code, str(target)],
                       input=buffer.getvalue(), capture_output=True, check=True)
result = json.loads(reply.stdout)
if result['digests'] != digests:
    raise SystemExit('Staged source hash mismatch')
print(json.dumps({'files': len(files), 'changed': result['changed'],
                  'verified': True, 'target': str(target)}))
