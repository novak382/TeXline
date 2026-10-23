"""Validate and stage the standalone introduction pack without rebuilding TeXline."""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import shutil
import uuid
from PIL import Image

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[1] / 'res/research/onboarding')
parser.add_argument('--destination', type=Path, help='The full resources/onboarding directory to update')
parser.add_argument('--check', action='store_true', help='Validate only; do not write any files')
args = parser.parse_args()
root = args.source.resolve()
content = json.loads((root / 'content.json').read_text(encoding='utf-8-sig'))
if content.get('schemaVersion') != 1 or not 1 <= len(content.get('pages', [])) <= 12:
    raise ValueError('Unsupported schema or page count')
files = {'content.json', 'style.qss'}
(root / 'style.qss').read_text(encoding='utf-8-sig')
ids = set()
for page in content['pages']:
    if not page.get('id') or page['id'] in ids:
        raise ValueError('Pages must have unique ids')
    ids.add(page['id'])
    for language in ('zh_CN', 'en'):
        for key in ('title', 'body'):
            text = page.get(key, {}).get(language, '')
            if not isinstance(text, str) or not text.strip() or len(text) > 512:
                raise ValueError(f'Invalid {language} {key}: {page["id"]}')
        relative = page.get('media', {}).get(language, '')
        if relative:
            media = (root / relative).resolve()
            if not media.is_relative_to(root) or not media.is_file() or media.stat().st_size > 32 * 1024 * 1024:
                raise ValueError(f'Invalid media path: {relative}')
            with Image.open(media) as image:
                if image.format != 'GIF' or image.width > 1920 or image.height > 1200 or image.n_frames < 2:
                    raise ValueError(f'Not a supported animated recording: {relative}')
                image.seek(image.n_frames - 1)
                image.load()
            files.add(media.relative_to(root).as_posix())
if (root / 'media/recordings.json').is_file():
    files.add('media/recordings.json')
for name in ('sources.md', 'sources.json'):
    attribution = root / name
    if attribution.is_file():
        if not attribution.resolve().is_relative_to(root) or attribution.stat().st_size > 128 * 1024:
            raise ValueError(f'Invalid source note: {name}')
        note = attribution.read_text(encoding='utf-8-sig')
        if name.endswith('.json'):
            json.loads(note)
        files.add(name)
manifest = {'schemaVersion': 1, 'revision': content.get('revision'), 'files': {name: {'bytes': (root / name).stat().st_size, 'sha256': hashlib.sha256((root / name).read_bytes()).hexdigest()} for name in sorted(files)}}
if args.check:
    print(json.dumps({'validated': True, 'pages': len(ids), 'files': len(files), 'bytes': sum(item['bytes'] for item in manifest['files'].values())}))
else:
    if not args.destination:
        parser.error('--destination is required unless --check is used')
    destination = args.destination.resolve()
    if destination == root or root.is_relative_to(destination) or destination.is_relative_to(root):
        raise ValueError('Stage outside the source pack')
    staging = destination.with_name(destination.name + '.staged-' + uuid.uuid4().hex)
    staging.mkdir(parents=True)
    for name in sorted(files):
        target = staging / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(root / name, target)
        if hashlib.sha256(target.read_bytes()).hexdigest() != manifest['files'][name]['sha256']:
            raise ValueError('Staged copy mismatch: ' + name)
    (staging / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
    backup = None
    if destination.exists():
        backup = destination.with_name(destination.name + '.backup-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:6])
        destination.rename(backup)
    try:
        staging.rename(destination)
    except BaseException:
        if backup:
            backup.rename(destination)
        raise
    print(json.dumps({'validated': True, 'destination': str(destination), 'backup': str(backup) if backup else None, 'files': len(files), 'bytes': sum(item['bytes'] for item in manifest['files'].values())}, ensure_ascii=False))
