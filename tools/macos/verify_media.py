#!/usr/bin/env python3
"""Verify native lifted video against an independent decoder, with owned disc data.

No x86 original-code oracle is run. Raw frames are temporary and removed per
video; only the commands, logs and aggregate results stay in ignored work/.
FFmpeg is a separate verification executable, never linked into the port.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--exe', type=Path, default=ROOT / 'build/native/pod')
parser.add_argument('--minimum-correlation', type=float, default=0.97)
parser.add_argument('names', nargs='*')
args = parser.parse_args()
exe = args.exe.resolve()
for name in ('ffmpeg', 'ffprobe'):
    if not shutil.which(name):
        parser.error(f'{name} is required for this independent comparison')
if not exe.is_file():
    parser.error(f'build the native executable first: {exe}')
source = ROOT / 'game/DATA/BINARY/VIDEO'
files = sorted(source.glob('*.AVI'))
if args.names:
    requested = {name.upper() for name in args.names}
    files = [p for p in files if p.stem.upper() in requested]
    missing = requested - {p.stem.upper() for p in files}
    if missing:
        parser.error(f'missing owned videos: {sorted(missing)}')
if not files:
    parser.error('no supported videos found; use your own extracted POD Gold disc')
spec = importlib.util.spec_from_file_location('pod_video_reference', ROOT / 'tools/verify_indeo.py')
reference = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reference)
destination = ROOT / 'work/media-verification'
destination.mkdir(parents=True, exist_ok=True)
results = []
for avi in files:
    with tempfile.TemporaryDirectory(prefix='native-video-', dir=destination) as tmp:
        tmp = Path(tmp)
        output = tmp / 'frames.bgr'
        command = [str(exe), '--decode-test', str(avi), str(output),
                   '--state-dir', str(tmp / 'state'), '--watchdog', '120']
        run = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                             errors='replace', timeout=130)
        log = run.stdout + run.stderr
        (destination / f'{avi.stem}.log').write_text(log)
        entry = {'video': avi.name, 'command': command, 'exit': run.returncode}
        decoded = re.search(r'\[codec\] decoded (\d+) frames', log)
        if run.returncode == 0 and decoded:
            w, h = reference.size_of(str(avi))
            frames = int(decoded.group(1))
            info = subprocess.check_output([
                'ffprobe', '-v', 'error', '-select_streams', 'v:0',
                '-show_entries', 'stream=nb_frames', '-of', 'default=nw=1:nk=1', str(avi)], text=True).strip()
            expected = int(info)
            correlation = reference.correlation(str(output), str(avi), w, h)
            entry.update(frames=frames, expected_frames=expected, correlation=correlation,
                         bytes=output.stat().st_size)
            seek = tmp / 'seek.bgr'
            seek_command = command.copy()
            seek_command[seek_command.index(str(output))] = str(seek)
            seek_command += ['--decode-start', str(max(0, frames - 2))]
            seek_run = subprocess.run(seek_command, cwd=ROOT, capture_output=True, text=True,
                                      errors='replace', timeout=130)
            (destination / f'{avi.stem}-seek.log').write_text(seek_run.stdout + seek_run.stderr)
            tail_bytes = ((w * 3 + 3) & ~3) * h * min(2, frames)
            with output.open('rb') as raw:
                raw.seek(-tail_bytes, 2)
                sequential_tail = raw.read()
            seek_matches = seek_run.returncode == 0 and seek.exists() and seek.read_bytes() == sequential_tail
            entry['seek_tail_identical'] = seek_matches
            entry['passed'] = (frames == expected and entry['bytes'] == ((w * 3 + 3) & ~3) * h * frames
                               and correlation >= args.minimum_correlation and seek_matches)
        else:
            entry['passed'] = False
        results.append(entry)
        (destination / 'results.json').write_text(json.dumps({
            'oracle': 'independent FFmpeg; no original x86 execution',
            'minimum_correlation': args.minimum_correlation, 'results': results}, indent=2) + '\n')
        print(json.dumps(entry), flush=True)
raise SystemExit(0 if all(r['passed'] for r in results) else 1)
