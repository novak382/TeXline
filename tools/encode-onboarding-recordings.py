"""Encode captured application frames, preserving their measured timing."""
from pathlib import Path
import argparse
import hashlib
import json
from PIL import Image

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--recordings', type=Path, required=True)
parser.add_argument('--destination', type=Path, required=True)
args = parser.parse_args()
checks = json.loads((args.recordings / 'recording-checks.json').read_text(encoding='utf-8'))
identity = json.loads((args.recordings / 'build-identity.json').read_text(encoding='utf-8'))
if checks['failures']:
    raise ValueError('The recording run contains failed workflow checks')
summary_path = args.recordings.parent / 'summary.json'
if summary_path.exists():
    summary = json.loads(summary_path.read_text(encoding='utf-8'))
    if not any(row.get('case') == 'onboarding-recording' and row.get('exit') == 0 and row.get('validReport') and not row.get('failed') for row in summary):
        raise ValueError('The recording process did not complete successfully')
args.destination.mkdir(parents=True, exist_ok=True)
records = []
content = json.loads((Path(__file__).resolve().parents[1] / 'res/research/onboarding/content.json').read_text(encoding='utf-8'))
for name in [page['id'] for page in content['pages']]:
    for language in ('zh', 'en'):
        folder = args.recordings / f'{name}-{language}'
        metadata = json.loads((folder / 'recording.json').read_text(encoding='utf-8'))
        if metadata['failures']:
            raise ValueError(f'Capture failed: {folder}')
        frames, durations = [], []
        # Quantize absolute boundaries, not each interval (16/33 ms must not accumulate drift).
        samples=[]
        for sample in metadata['frames']:
            entry=dict(sample, tick=round(sample['atMs']/10)*10)
            if samples and entry['tick']<=samples[-1]['tick']:samples[-1]=entry
            else:samples.append(entry)
        decoded={}
        for index,sample in enumerate(samples):
            if sample['file'] not in decoded:
                with Image.open(folder/sample['file']) as raw:
                    frame=raw.convert('RGB');frame.thumbnail((1440,900),Image.Resampling.LANCZOS);decoded[sample['file']]=frame
            frames.append(decoded[sample['file']])
            following=samples[index+1]['tick'] if index+1<len(samples) else round((metadata['durationMs']+750)/10)*10
            durations.append(max(10,following-sample['tick']))
        # A shared palette prevents distracting color changes between frames.
        atlas = Image.new('RGB', (frames[0].width * 4, frames[0].height))
        for index in range(4):
            atlas.paste(frames[min(len(frames) - 1, index * len(frames) // 4)], (index * frames[0].width, 0))
        palette = atlas.quantize(colors=192, method=Image.Quantize.MEDIANCUT)
        quantized = [frame.quantize(palette=palette, dither=Image.Dither.NONE) for frame in frames]
        target = args.destination / f'{name}-{language}.gif'
        quantized[0].save(target, save_all=True, append_images=quantized[1:], duration=durations, loop=0, optimize=True, disposal=1)
        with Image.open(target) as image:
            records.append({'file': target.name, 'bytes': target.stat().st_size, 'sha256': hashlib.sha256(target.read_bytes()).hexdigest(), 'frames': image.n_frames, 'width': image.width, 'height': image.height, 'durationMs': sum(durations), 'rawDurationMs': metadata['durationMs'], 'endHoldMs': 750, 'actions': metadata['actions']})
(args.destination / 'recordings.json').write_text(json.dumps({'source': 'Actual TeXline Windows widgets on a private desktop', 'pointer': 'Scripted input-target indicator', 'timing': 'Measured absolute capture boundaries quantized to 10 ms; no acceleration', 'candidateSha256': identity['candidateSha256'], 'recordingTestSha256': identity['testSha256'], 'workflowChecksPassed': len(checks['checks']), 'materialSources': json.loads((args.recordings / 'material-sources.json').read_text(encoding='utf-8')), 'clips': records}, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps({'clips': len(records), 'bytes': sum(item['bytes'] for item in records)}, ensure_ascii=False))
