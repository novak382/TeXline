#!/usr/bin/env python3
"""Repair Unicode paths in the pinned Poppler archive without installing it.

Retains every other member of the already mutex-corrected archive. Reuses the
matching build's flags, redirecting only the target prefix and two sources.
"""
import argparse,difflib,hashlib,json,shlex,shutil,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source',type=Path,required=True);p.add_argument('--build',type=Path,required=True)
p.add_argument('--prefix',type=Path,required=True);p.add_argument('--compiler',required=True)
p.add_argument('--archiver',required=True);p.add_argument('--output',type=Path,required=True)
p.add_argument('--expected-archive',default='0df3782e25f55badacfb8eb4aec7a8fbde1f1e2e3d96aff060e8bb22b5fac63b')
a=p.parse_args();source=a.source.resolve();build=a.build.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
sha=lambda data:hashlib.sha256(data).hexdigest()
original=a.prefix.resolve()/'lib/libpoppler.a'
if sha(original.read_bytes())!=a.expected_archive:raise SystemExit('Expected the pinned mutex-corrected Poppler archive.')
expected={'poppler/GlobalParams.cc':'21941a20dadd4fc9b6aa6d94425da7be685c5f1b86959993122cff40fab4ea77','goo/gfile.cc':'cb79d0f6766b264bf83caa9928db8a80ba78ddd6cc44e6b60b55fba382dc432e'}
texts={}
for name,digest in expected.items():
 data=(source/name).read_bytes()
 if sha(data)!=digest:raise SystemExit('Expected unmodified Poppler 26.01.0 source: '+name)
 texts[name]=data.decode()
global_text=texts['poppler/GlobalParams.cc'];start=global_text.index('static std::string get_poppler_localdir(');end=global_text.index('\nstatic const char *get_poppler_datadir',start)
replacement=r'''static std::string get_poppler_localdir(const std::string &suffix)
{
    // Win32 paths are UTF-16; Poppler's openFile consumes UTF-8. The ANSI
    // module path cannot round-trip Chinese or supplementary characters.
    std::wstring path(MAX_PATH, L'\0');
    DWORD length;
    for (;;) {
        length = GetModuleFileNameW(hmodule, path.data(), static_cast<DWORD>(path.size()));
        if (!length) return POPPLER_DATADIR;
        if (length < path.size() - 1) break;
        if (path.size() >= 32768) return POPPLER_DATADIR;
        path.resize(std::min<size_t>(32768, path.size() * 2));
    }
    path.resize(length);
    const auto slash = path.rfind(L'\\');
    if (slash != std::wstring::npos) {
        path.erase(slash);
        if (path.size() > 4 && _wcsicmp(path.substr(path.size() - 4).c_str(), L"\\bin") == 0) path.resize(path.size() - 4);
    }
    path.append(suffix.begin(), suffix.end());
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, path.data(), static_cast<int>(path.size()), nullptr, 0, nullptr, nullptr);
    if (!bytes) return POPPLER_DATADIR;
    std::string result(bytes, '\0');
    if (!WideCharToMultiByte(CP_UTF8, 0, path.data(), static_cast<int>(path.size()), result.data(), bytes, nullptr, nullptr)) return POPPLER_DATADIR;
    return result;
}
'''
global_text=global_text[:start]+replacement+global_text[end:]
helper='''// Convert filesystem paths explicitly; path::string() uses a locale-dependent
// conversion on Windows and can abort a no-exceptions build for a Unicode name.
static std::string filesystemUtf8(const std::filesystem::path &path)
{
    const auto bytes = path.u8string();
    return { reinterpret_cast<const char *>(bytes.data()), bytes.size() };
}

'''
global_text=global_text.replace('void GlobalParams::scanEncodingDirs()',helper+'void GlobalParams::scanEncodingDirs()',1)
for name in ['nameToUnicode','cidToUnicode','unicodeMap','cMap']:
 needle='dataRoot + "/'+name+'", ec'
 if global_text.count(needle)!=1:raise SystemExit('Unexpected resource scan.')
 global_text=global_text.replace(needle,'std::filesystem::u8path(dataRoot + "/'+name+'"), ec')
global_text=global_text.replace('entry.path().filename().string()','filesystemUtf8(entry.path().filename())').replace('entry.path().string()','filesystemUtf8(entry.path())').replace('name.string()','filesystemUtf8(name)')
file_text=texts['goo/gfile.cc'];start=file_text.index('        for (p = path, i = 0; *p && i < _MAX_PATH; ++i) {',file_text.index('FILE *openFile('));end=file_text.index('        return _wfopen(wPath, wMode);',start)+len('        return _wfopen(wPath, wMode);')
replacement='''        const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, nullptr, 0);
        if (!length) return nullptr;
        std::wstring widePath(length, L'\\0');
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, widePath.data(), length)) return nullptr;
        for (i = 0; (i < sizeof(wMode) / sizeof(wMode[0]) - 1) && mode[i]; ++i) wMode[i] = static_cast<wchar_t>(mode[i]);
        wMode[i] = L'\\0';
        return _wfopen(widePath.c_str(), wMode);'''
file_text=file_text[:start]+replacement+file_text[end:];file_text=file_text.replace('    wchar_t wPath[_MAX_PATH + 1];\n','',1)
patched={'poppler/GlobalParams.cc':global_text,'goo/gfile.cc':file_text}
patch='';commands=[];objects=[]
for name,text in patched.items():
 patch+=''.join(difflib.unified_diff(texts[name].splitlines(True),text.splitlines(True),fromfile='a/'+name,tofile='b/'+name))
 path=out/Path(name).name;path.write_text(text)
 obj=out/(path.name+'.obj');objects.append(obj)
 target='CMakeFiles/poppler.dir/'+name+'.obj'
 command=shlex.split(subprocess.check_output(['ninja','-t','commands',target],cwd=build,text=True).splitlines()[-1])
 command=[s.replace('i686-w64-mingw32.static','x86_64-w64-mingw32.static') for s in command]
 command[0]=a.compiler
 command.insert(1,'-I'+str((source/name).parent))
 for flag,value in [('-c',str(path)),('-o',str(obj)),('-MT',str(obj)),('-MF',str(obj)+'.d')]:command[command.index(flag)+1]=value
 if '-D_GTHREAD_USE_MUTEX_INIT_FUNC' not in command or '-D_GTHREAD_USE_RECURSIVE_MUTEX_INIT_FUNC' not in command:raise SystemExit('Both mutex repairs must be retained.')
 commands.append(command);subprocess.run(command,cwd=build,check=True)
archive=out/'libpoppler.a';shutil.copy2(original,archive)
before=subprocess.check_output([a.archiver,'t',str(original)],text=True).splitlines()
if any(before.count(obj.name)!=1 for obj in objects):raise SystemExit('Expected exactly one of each original object.')
subprocess.run([a.archiver,'r',str(archive),*(str(obj) for obj in objects)],check=True)
after=subprocess.check_output([a.archiver,'t',str(archive)],text=True).splitlines()
if before!=after:raise SystemExit('The archive member list changed.')
replaced={obj.name for obj in objects}
for member in before:
 if member not in replaced:
  old=subprocess.check_output([a.archiver,'p',str(original),member]);new=subprocess.check_output([a.archiver,'p',str(archive),member])
  if old!=new:raise SystemExit('Unrelated member changed: '+member)
metadata={'popplerVersion':'26.01.0','originalSha256':sha(original.read_bytes()),'correctedSha256':sha(archive.read_bytes()),'sourceSha256':expected,'patchedSha256':{name:sha(text.encode()) for name,text in patched.items()},'replacedMembers':sorted(replaced),'unrelatedMembersVerified':len(before)-len(replaced),'compileCommands':commands,'compilerVersion':subprocess.check_output([a.compiler,'--version'],text=True).splitlines()[0]}
(out/'unicode-paths.patch').write_text(patch);(out/'provenance.json').write_text(json.dumps(metadata,indent=2))
print(json.dumps({k:v for k,v in metadata.items() if k not in ['compileCommands']},indent=2))
