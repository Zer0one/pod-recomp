#!/usr/bin/env python3
"""Bounded standalone regression checks with the user's external POD data.

No GUI, physical-device, listening, race-speed or Windows-oracle result is
inferred. Explicit WM_CLOSE exercises guest cleanup; a forced host cutoff
would not establish this. Outputs and isolated state stay below --output.
"""
import argparse
import json
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=ROOT / 'build/native/pod')
    parser.add_argument('--game', type=Path, default=ROOT / 'game')
    parser.add_argument('--cue', type=Path)
    parser.add_argument('--output', type=Path, default=ROOT / 'work/standalone-regression')
    parser.add_argument('--only', choices=['effects', 'movies'])
    args = parser.parse_args()
    exe, game, destination = args.exe.resolve(), args.game.resolve(), args.output.resolve()
    if not exe.is_file() or not (game / 'PODMMX.EXE').is_file():
        parser.error('build the native standalone and supply your own extracted game first')
    cue = args.cue.resolve() if args.cue else None
    if cue and not cue.is_file():
        parser.error(f'CUE does not exist: {cue}')
    destination.mkdir(parents=True, exist_ok=True)
    # Display flips are paced even headless. Allow the normal startup/menu
    # animations to reach CD playback before requesting cleanup.
    race_keys = [arg for key in ('ESC@6500', 'ENTER@7000', 'ENTER@7800',
                                'ENTER@9000', 'ENTER@10500', 'ENTER@14000')
                 for arg in ('--key', key)]
    cases = [('effects', 26000, race_keys),
             ('movies', 24000, ['--videos', '--key', 'ESC@7000'])]
    results = []
    for name, close_ms, extra in cases:
        if args.only and name != args.only:
            continue
        state = destination / (name + '-state')
        state.mkdir(exist_ok=True)
        watchdog = close_ms // 1000 + 15
        command = [str(exe), '--headless', '--window-log', '--game', str(game),
                   '--state-dir', str(state), '--watchdog', str(watchdog),
                   '--close-after', str(close_ms)]
        command += ['--cd-cue', str(cue)] if cue else ['--no-cd']
        command += extra
        log_file = destination / (name + '.log')
        started = time.monotonic()
        timed_out = False
        with log_file.open('w') as log:
            try:
                run = subprocess.run(command, cwd=destination, stdout=log,
                                     stderr=subprocess.STDOUT, timeout=watchdog + 5)
                code = run.returncode
            except subprocess.TimeoutExpired:
                # subprocess.run kills and reaps its own child on timeout.
                code, timed_out = None, True
        text = log_file.read_text(errors='replace')
        entry = dict(case=name, command=command, exit=code,
                     elapsed_seconds=round(time.monotonic() - started, 3),
                     external_timeout=timed_out, watchdog='[watchdog]' in text,
                     guest_exit_zero='[win32hle] ExitProcess(0)' in text and '[host] exit=0' in text,
                     close_requested='requesting window close' in text,
                     effects_opened='[audio] PCM ' in text,
                     cd_opened='[cd] PCM 44100 Hz stereo' in text,
                     cd_required=bool(cue) and name == 'effects',
                     video_opened='[avi] opened ' in text)
        entry['passed'] = (code == 0 and not timed_out and not entry['watchdog']
                           and entry['guest_exit_zero'] and entry['close_requested']
                           and entry['effects_opened'] and (not entry['cd_required'] or entry['cd_opened'])
                           and (name != 'movies' or entry['video_opened']))
        results.append(entry)
        (destination / 'results.json').write_text(json.dumps(dict(
            scope='bounded headless standalone startup and WM_CLOSE cleanup',
            physical_audio_input_and_game_speed_verified=False, results=results), indent=2) + '\n')
        print(json.dumps({k: v for k, v in entry.items() if k != 'command'}), flush=True)
    return 0 if all(r['passed'] for r in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
