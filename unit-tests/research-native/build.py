"""Build native research cases against the exact application objects (MinGW/Ninja)."""
from pathlib import Path
import argparse, hashlib, json, shlex, shutil, subprocess, re
from concurrent.futures import ThreadPoolExecutor

p = argparse.ArgumentParser()
p.add_argument('--build', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--strip', default='')
p.add_argument('--jobs', type=int, default=1, help='Independent fixture compile/link jobs after the application is built')
p.add_argument('--build-jobs', type=int, default=2, help='Bound application compile concurrency to avoid exhausting the validation VM')
p.add_argument('--ipc-namespace', default='regression', help='Isolate independent native test suites from one another')
p.add_argument('--poppler-archive', type=Path, help='Test a corrected archive without replacing the installed dependency')
p.add_argument('--qtgui-archive', type=Path, help='Override only QtGui for a controlled layout-repair comparison')
p.add_argument('--application-source', type=Path, action='append', default=[], help='Compile an explicit frozen application source for paired baseline measurements')
p.add_argument('cases', nargs='+')
a = p.parse_args()
if not re.fullmatch(r'[a-z][a-z0-9-]{0,47}', a.ipc_namespace): p.error('Invalid IPC namespace')
build, output, cases = a.build.resolve(), a.output.resolve(), Path(__file__).resolve().parent
output.mkdir(parents=True, exist_ok=True)
subprocess.run(['ninja', '-j', str(max(1, a.build_jobs)), 'TeXworks.exe'], cwd=build, check=True)
# A regenerated UIC header can become newer after the dependency graph was
# scanned. Settle that graph before binding fixture manifests to the binary.
subprocess.run(['ninja', '-j', str(max(1, a.build_jobs)), 'TeXworks.exe'], cwd=build, check=True)
candidate = output / 'candidate.exe'
def command(target):
    return subprocess.check_output(['ninja', '-t', 'commands', target], cwd=build, text=True).splitlines()[-1]
application_link = command('TeXworks.exe')
if a.poppler_archive:
    archive = a.poppler_archive.resolve()
    if not archive.is_file(): raise ValueError('Corrected Poppler archive is missing')
    original = [token for token in shlex.split(application_link) if token.endswith('/libpoppler.a')]
    if len(original) != 1: raise ValueError('Expected one Poppler archive in the link command')
    application_link = application_link.replace(original[0], shlex.quote(str(archive)))
if a.qtgui_archive:
    archive = a.qtgui_archive.resolve()
    if not archive.is_file(): raise ValueError('QtGui archive is missing')
    original = [token for token in shlex.split(application_link) if token.endswith('/libQt6Gui.a')]
    if len(set(original)) != 1: raise ValueError('Expected one unique QtGui archive in the link command')
    application_link = application_link.replace(original[0], shlex.quote(str(archive)))
if a.poppler_archive or a.qtgui_archive:
    subprocess.run(application_link.replace('-o TeXworks.exe', '-o '+shlex.quote(str(candidate))), cwd=build, shell=True, check=True)
else:
    shutil.copy2(build / 'TeXworks.exe', candidate)
if a.strip: subprocess.run([a.strip, '--strip-unneeded', str(candidate)], check=True)
compile_command = shlex.split(command('src/CMakeFiles/TeXworks.dir/main.cpp.obj'))
def compile_source(source, obj):
    args = compile_command[:]
    args[args.index('-c') + 1] = str(source)
    args[args.index('-o') + 1] = str(obj)
    for flag, ending in [('-MT', ''), ('-MF', '.d')]:
        if flag in args: args[args.index(flag) + 1] = str(obj) + ending
    subprocess.run(args, cwd=build, check=True)
overrides = {}
override_objects = {}
for source in a.application_source:
    source = source.resolve()
    tokens = [item for item in shlex.split(application_link)
              if item.startswith('src/CMakeFiles/TeXworks.dir/') and item.endswith('/' + source.name + '.obj')]
    if not source.is_file() or len(tokens) != 1: raise ValueError('Invalid or ambiguous application source override: ' + str(source))
    token = tokens[0]
    obj = output / (source.name + '.obj')
    compile_source(source, obj)
    application_link = application_link.replace(token, shlex.quote(str(obj)))
    overrides[source.name] = hashlib.sha256(source.read_bytes()).hexdigest()
    override_objects[source.stem] = shlex.quote(str(obj))
if overrides:
    subprocess.run(application_link.replace('-o TeXworks.exe', '-o '+shlex.quote(str(candidate))), cwd=build, shell=True, check=True)
    if a.strip: subprocess.run([a.strip, '--strip-unneeded', str(candidate)], check=True)
ipc = build / 'research-test-ipc.cpp'
# Isolate IPC so test processes never send files to a running user editor.
source_root = next(Path(s[2:]) for s in compile_command if s.startswith('-I')
                   and (Path(s[2:]) / 'InterProcessCommunicatorWin.cpp').is_file())
ipc.write_text((source_root / 'InterProcessCommunicatorWin.cpp').read_text().replace(
    'org.tug.texworks-research-', 'org.tug.texworks-'+a.ipc_namespace+'-').replace(
    'TeXworksResearch:MessageTarget', ('TeXworksRegression' if a.ipc_namespace=='regression' else 'TeXworks'+a.ipc_namespace)+':MessageTarget'))
compile_source(ipc, build / 'research-test-ipc.obj')
if 'cancel-native-test' in a.cases:
    subprocess.run([compile_command[0], '-std=c++17', '-municode', '-static',
                    str(cases / 'cancel-helper.cpp'), '-o', str(output / 'cancel-helper.exe')], check=True)
excluded = {'system-motion-test': ['ResearchSlides'], 'slide-grid-drag-test': ['ResearchSlides'], 'onboarding-recording': ['ResearchSlides'], 'slide-title-input-test': ['ResearchSlides'], 'control-alignment-test': ['ResearchSlides'], 'slide-note-workflow-test': ['ResearchSlides'], 'real-formula-hover-test': ['ResearchReferences'], 'slide-reference-hover-test': ['ResearchReferences'], 'presentation-walkthrough-test': ['ResearchSlides'],
    'visual-authoring-test': ['ResearchSlides'], 'minimal-focus-test': ['ResearchSlides'], 'source-navigation-test': ['ResearchSlides'], 'slide-writing-test': ['ResearchSlides'], 'beamer-editing-test': ['ResearchSlides'], 'slide-operations-test': ['ResearchSlides'], 'review-flow-test': ['ResearchCollaboration'], 'review-retention-test': ['ResearchCollaboration'], 'external-dependency-test': ['ResearchCollaboration'],
            'recovery-test': ['ResearchWorkflow'], 'migration-recovery-test': ['ResearchWorkflow'],
            'reference-coverage-native-test': ['ResearchReferences'], 'stale-pdf-test': ['ResearchReferences']}
excluded['motion-cadence-test'] = ['ResearchSlides']
excluded['motion-race-test'] = ['ResearchSlides']
excluded['motion-soak-test'] = ['ResearchSlides']
excluded['slide-menu-lifecycle-test'] = ['ResearchSlides']
excluded['annotation-workflow-test'] = ['ResearchAnnotations']
excluded['intuitive-authoring-test'] = ['ResearchAuthoring']
excluded['visual-structures-test'] = ['ResearchAuthoring']
excluded['intuitive-slide-test'] = ['ResearchSlides']
excluded['intuitive-navigation-test'] = ['ResearchArticle']
excluded['project-transfer-review-test'] = ['ResearchProjectOperations']
excluded['project-write-race-test'] = ['ResearchProjectOperations']

def build_case(name):
    if not (cases / (name + '.cpp')).is_file(): raise ValueError('Unknown case: ' + name)
    obj = build / (name + '.obj')
    compile_source(cases / (name + '.cpp'), obj)
    link = application_link.replace('src/CMakeFiles/TeXworks.dir/main.cpp.obj', str(obj))
    link = link.replace('src/CMakeFiles/TeXworks.dir/InterProcessCommunicatorWin.cpp.obj', str(build / 'research-test-ipc.obj'))
    for module in excluded.get(name, []):
        link = link.replace('src/CMakeFiles/TeXworks.dir/' + module + '.cpp.obj', '')
        if module in override_objects: link = link.replace(override_objects[module], '')
    exe = build / (name + '.exe')
    subprocess.run(link.replace('-o TeXworks.exe', '-o ' + shlex.quote(str(exe))), cwd=build, shell=True, check=True)
    shutil.copy2(exe, output / exe.name)
    if a.strip: subprocess.run([a.strip, '--strip-unneeded', str(output / exe.name)], check=True)
    manifest = {'ipcNamespace': a.ipc_namespace, 'candidateSha256': hashlib.sha256(candidate.read_bytes()).hexdigest(),
                'testSha256': hashlib.sha256((output / exe.name).read_bytes()).hexdigest()}
    if a.poppler_archive: manifest['popplerArchiveSha256'] = hashlib.sha256(a.poppler_archive.read_bytes()).hexdigest()
    if a.qtgui_archive: manifest['qtGuiArchiveSha256'] = hashlib.sha256(a.qtgui_archive.read_bytes()).hexdigest()
    if overrides: manifest['applicationSourceOverrides'] = overrides
    (output / (exe.name + '.build.json')).write_text(json.dumps(manifest, indent=2))
with ThreadPoolExecutor(max_workers=max(1,a.jobs)) as workers:
    list(workers.map(build_case, a.cases))
