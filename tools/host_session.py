#!/usr/bin/env python3
"""Control and inspect native-port sessions using only Python's standard library."""
import argparse
import array
import hashlib
import html
import json
import math
import os
import re
from pathlib import Path
import struct
import subprocess
import sys
import time
import uuid
import wave
import zlib

ROOT = Path(__file__).resolve().parents[1]


def send(directory, command):
    if not command.strip() or '\n' in command or '\r' in command:
        raise ValueError('Send one nonempty command at a time')
    pending = directory / 'commands'
    pending.mkdir(parents=True, exist_ok=True)
    # Unique, sortable names also permit independent interactive clients.
    name = f'{time.time_ns():020d}-{uuid.uuid4().hex[:8]}'
    temporary = pending / (name + '.tmp')
    temporary.write_text(command + '\n', encoding='utf-8')
    temporary.rename(pending / (name + '.cmd'))
    return name + '.cmd'


def ppm(path):
    with path.open('rb') as stream:
        if stream.readline().strip() != b'P6':
            raise ValueError(f'Unsupported capture format: {path}')
        width, height = map(int, stream.readline().split())
        if stream.readline().strip() != b'255':
            raise ValueError('Only RGB8 captures are supported')
        pixels = stream.read()
    if len(pixels) != width * height * 3:
        raise ValueError(f'Truncated capture: {path}')
    return width, height, pixels


def png(path):
    width, height, pixels = ppm(path)
    def chunk(kind, data):
        return struct.pack('!I', len(data)) + kind + data + struct.pack('!I', zlib.crc32(kind + data))
    rows = b''.join(b'\0' + pixels[y * width * 3:(y + 1) * width * 3] for y in range(height))
    data = b'\x89PNG\r\n\x1a\n'
    data += chunk(b'IHDR', struct.pack('!2I5B', width, height, 8, 2, 0, 0, 0))
    data += chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b'')
    target = path.with_suffix('.png')
    target.write_bytes(data)
    return {'name': path.stem, 'file': str(target.relative_to(path.parent.parent)),
            'width': width, 'height': height, 'sha256': hashlib.sha256(pixels).hexdigest(),
            'near_black_fraction': sum(max(pixels[i:i + 3]) <= 8 for i in range(0, len(pixels), 3)) / (width * height)}


def audio_report(path):
    if not path.exists():
        return None
    with wave.open(str(path)) as stream:
        channels, rate, frames = stream.getnchannels(), stream.getframerate(), stream.getnframes()
        if stream.getsampwidth() != 2:
            raise ValueError('Expected signed-16 PCM audio')
        values = array.array('h', stream.readframes(frames))
    if sys.byteorder != 'little':
        values.byteswap()
    squares = sum(int(v) ** 2 for v in values)
    rms = math.sqrt(squares / len(values)) / 32768 if values else 0
    mono = values[::channels]
    active = [i for i, v in enumerate(mono) if abs(v) > 1]
    active_seconds = (active[-1] - active[0] + 1) / rate if active else 0
    crossings = [i for i in range(active[0] + 1, active[-1] + 1) if mono[i - 1] <= 0 < mono[i]] if active else []
    frequency = (len(crossings) - 1) * rate / (crossings[-1] - crossings[0]) if len(crossings) > 1 else 0
    return {'active_span_seconds': active_seconds, 'zero_crossing_hz': frequency,
            'seconds': frames / rate, 'channels': channels, 'sample_rate': rate,
            'peak': max(map(abs, values), default=0), 'rms': rms,
            'rms_dbfs': 20 * math.log10(rms) if rms else None,
            'clipped_samples': sum(abs(v) >= 32767 for v in values),
            'silent_sample_fraction': sum(v == 0 for v in values) / len(values) if values else 1}


def inspect(directory):
    events = []
    for line in (directory / 'events.jsonl').read_text(encoding='utf-8').splitlines():
        events.append(json.loads(line))
    frames = [e for e in events if e['type'] == 'frame']
    captures = [png(path) for path in sorted((directory / 'frames').glob('*.ppm'))]
    errors = [e for e in events if e['type'] == 'command' and not e.get('ok')]
    process_log = (directory / 'process.log').read_text(encoding='utf-8', errors='replace') if (directory / 'process.log').exists() else ''
    native_log = (directory / 'native.log').read_text(encoding='utf-8', errors='replace') if (directory / 'native.log').exists() else ''
    validation_errors = [line for line in (process_log + native_log).splitlines() if 'Validation Error' in line or re.search(r'VUID-[A-Za-z]', line)]
    report = {'validation_errors': validation_errors, 'frames': len(frames), 'max_frame_ms': max((e['duration_ms'] for e in frames), default=0),
              'frames_over_50ms': sum(e['duration_ms'] > 50 for e in frames),
              'command_errors': errors, 'captures': captures, 'audio': audio_report(directory / 'audio.wav')}
    (directory / 'report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    pictures = ''.join(f'<figure><figcaption>{html.escape(c["name"])} ({c["width"]}×{c["height"]})</figcaption>'
                       f'<img src="{html.escape(c["file"])}" alt="{html.escape(c["name"])}"></figure>' for c in captures)
    player = '<audio controls src="audio.wav"></audio>' if report['audio'] else '<p>Audio disabled.</p>'
    (directory / 'report.html').write_text(
        '<!doctype html><meta charset="utf-8"><title>GT4 port playtest</title>'
        '<style>body{font:16px system-ui;background:#111a2a;color:#eee;margin:2rem}img{max-width:100%;height:auto}'
        'figure{margin:2rem 0}pre{white-space:pre-wrap}audio{width:100%;max-width:700px}</style>'
        '<h1>GT4 port playtest</h1><p>Captured native framebuffer and audio submitted to playback. '
        'Audio recording does not verify speaker output. Timing and silence observations require context.</p>' + player + pictures +
        '<h2>Measurements</h2><pre>' + html.escape(json.dumps(report, indent=2)) + '</pre>', encoding='utf-8')
    return report


def compare(current, baseline):
    differences = []
    paths = {p.name: p for p in (current / 'frames').glob('*.ppm')}
    reference = {p.name: p for p in (baseline / 'frames').glob('*.ppm')}
    for name in sorted(paths.keys() | reference.keys()):
        if name not in paths or name not in reference:
            differences.append({'file': name, 'missing_from': 'current' if name not in paths else 'baseline'})
            continue
        w, h, pixels = ppm(paths[name]); rw, rh, expected = ppm(reference[name])
        if (w, h) != (rw, rh):
            differences.append({'file': name, 'size_changed': [[rw, rh], [w, h]]})
            continue
        squared = sum((a - b) ** 2 for a, b in zip(pixels, expected))
        differences.append({'file': name, 'rms_byte_error': math.sqrt(squared / len(pixels)),
                            'changed_channel_fraction': sum(a != b for a, b in zip(pixels, expected)) / len(pixels)})
    return {'images': differences, 'audio_current': audio_report(current / 'audio.wav'),
            'audio_baseline': audio_report(baseline / 'audio.wav')}


def verify_diagnostics(directory, report):
    captures = {c['name']: c for c in report['captures']}
    for name in ['neutral', 'left', 'action', 'resized', 'restored']:
        if name not in captures:
            raise RuntimeError(f'Missing diagnostic capture: {name}')
    if captures['neutral']['sha256'] == captures['left']['sha256'] or captures['left']['sha256'] == captures['action']['sha256']:
        raise RuntimeError('Virtual stick/button input did not change the captured scene')
    if captures['neutral']['sha256'] != captures['restored']['sha256']:
        raise RuntimeError('Reset/restore failed to reproduce the neutral framebuffer')
    if (captures['resized']['width'], captures['resized']['height']) != (800, 450):
        raise RuntimeError('Resize capture did not reach 800x450')
    if report['audio']:
        audio = report['audio']
        if audio['clipped_samples'] or audio['peak'] != 8000 or abs(audio['active_span_seconds'] - 0.5) > 0.001 or abs(audio['zero_crossing_hz'] - 440) > 1:
            raise RuntimeError('Diagnostic tone is clipped, silent, wrong pitch, or wrong length')
    report['diagnostic_checks'] = 'passed'
    (directory / 'report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


def run(args):
    executable = args.exe.resolve()
    directory = (args.session or ROOT / 'build/host-sessions' / (time.strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:6])).resolve()
    directory.mkdir(parents=True, exist_ok=False)
    script = args.script or ROOT / 'tests/scenarios/controller.json'
    plan = json.loads(script.read_text(encoding='utf-8'))
    for event in plan:
        if not isinstance(event['at'], (int, float)) or not math.isfinite(event['at']) or event['at'] < 0:
            raise ValueError('Script times must be finite, nonnegative seconds')
        if not isinstance(event['command'], str) or '\n' in event['command'] or '\r' in event['command']:
            raise ValueError('Script commands must be single lines')
    plan.sort(key=lambda e: e['at'])
    if args.no_audio:
        plan = [e for e in plan if not e['command'].startswith('tone ')]
    native_directory = str(directory)
    if os.name != 'nt' and executable.suffix.lower() == '.exe':
        native_directory = subprocess.check_output(['wslpath', '-w', str(directory)], text=True).strip()
    command = [str(executable), '--session-dir', native_directory]
    if args.no_audio:
        command.append('--no-audio')
    if os.name != 'nt' and executable.suffix.lower() == '.exe':
        windows_session = subprocess.check_output(
            ['powershell.exe', '-NoProfile', '-Command', '[System.Diagnostics.Process]::GetCurrentProcess().SessionId'], text=True).strip()
        if windows_session == '0':
            def windows(path):
                return subprocess.check_output(['wslpath', '-w', str(path)], text=True).strip()
            command = ['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                       windows(ROOT / 'tools/host_windows.ps1'), '-Executable', windows(executable),
                       '-Session', native_directory, '-TimeoutSeconds', str(int(max((e['at'] for e in plan), default=0) + 45))]
            if args.no_audio:
                command.append('-NoAudio')
    (directory / 'script.json').write_text(json.dumps(plan, indent=2) + '\n', encoding='utf-8')
    print(f'Session: {directory}', flush=True)
    environment = dict(os.environ)
    if os.name != 'nt' and executable.suffix.lower() != '.exe':
        local_data = Path(environment.get('XDG_DATA_HOME', str(Path.home() / '.local/share')))
        libraries = local_data / 'gt4/native-deps/usr/lib/x86_64-linux-gnu'
        if libraries.exists():
            environment['LD_LIBRARY_PATH'] = str(libraries) + ':' + str(libraries / 'pulseaudio') + ':' + environment.get('LD_LIBRARY_PATH', '')
    with (directory / 'process.log').open('w', encoding='utf-8') as output:
        process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT, env=environment)
        try:
            started = time.monotonic()
            while True:
                log = directory / 'events.jsonl'
                if log.exists() and '"type":"session"' in log.read_text(encoding='utf-8'):
                    break
                if process.poll() is not None or time.monotonic() - started > 15:
                    raise RuntimeError('Native app failed to initialize; see process.log')
                time.sleep(0.01)
            started = time.monotonic()
            for event in plan:
                while time.monotonic() - started < event['at']:
                    if process.poll() is not None:
                        raise RuntimeError('Native app exited before script completion')
                    time.sleep(0.005)
                send(directory, event['command'])
            if not any(e['command'] == 'quit' for e in plan):
                time.sleep(0.25)
                send(directory, 'quit')
            code = process.wait(timeout=45)
            if code:
                raise RuntimeError(f'Native app exited with code {code}; see process.log')
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill(); process.wait()
    report = inspect(directory)
    print(f'{report["frames"]} frames, {len(report["captures"])} screenshots, {len(report["command_errors"])} command errors')
    print(f'View/listen: {directory / "report.html"}')
    if report['command_errors'] or report['validation_errors']:
        raise RuntimeError('Script or Vulkan validation errors; see report.json')
    if args.verify_diagnostics:
        if args.script:
            raise ValueError('--verify-diagnostics is for the built-in diagnostic scenario')
        verify_diagnostics(directory, report)
        print('Controller, resize/reset framebuffer, and tone checks passed')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='action', required=True)
    launch = commands.add_parser('run', help='Run a scripted controller/capture session')
    launch.add_argument('--exe', type=Path, required=True)
    launch.add_argument('--session', type=Path)
    launch.add_argument('--script', type=Path)
    launch.add_argument('--no-audio', action='store_true')
    launch.add_argument('--verify-diagnostics', action='store_true')
    control = commands.add_parser('send', help='Send a command to an active session')
    control.add_argument('session', type=Path); control.add_argument('command', nargs='+')
    analyze = commands.add_parser('inspect', help='Create PNGs and an HTML/WAV playtest report')
    analyze.add_argument('session', type=Path)
    comparison = commands.add_parser('compare', help='Compare matching screenshot names and audio metrics')
    comparison.add_argument('current', type=Path); comparison.add_argument('baseline', type=Path)
    args = parser.parse_args()
    try:
        if args.action == 'run': run(args)
        elif args.action == 'send': print(send(args.session, ' '.join(args.command)))
        elif args.action == 'inspect': print(json.dumps(inspect(args.session), indent=2))
        else: print(json.dumps(compare(args.current, args.baseline), indent=2))
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.SubprocessError) as error:
        parser.exit(1, f'{error}\n')

if __name__ == '__main__':
    main()
