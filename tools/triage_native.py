#!/usr/bin/env python3
"""Conservative native-integration triage. Never modifies upstream sources."""
import argparse
import json
from pathlib import Path
import re
import subprocess

SOURCE_SUFFIXES = {'.c', '.cc', '.cpp', '.cxx'}
INCLUDE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]', re.M)
SYMBOL = re.compile(r'\bfunc_[0-9a-fA-F]{8}\b')
PS2_MARKERS = re.compile(r'\b(asm|__asm__|sce[A-Z]\w*|Dma\w*|vu0\w*|vu1\w*|__attribute__\s*\(\(\s*naked)\b')
EXTERNAL = re.compile(r'\b(?:extern|__asm__|asm)\b')

def analyze(manifest, source_root, limit=None):
    records = manifest['records']
    known = {r['path'] for r in records}
    results = []
    for record in records:
        if not record.get('source'):
            continue
        path = record['path']
        file = source_root / path
        if not file.is_file():
            raise FileNotFoundError(file)
        data = file.read_text(encoding='utf-8', errors='replace')
        quoted = [value for delim, value in INCLUDE.findall(data) if delim == '"']
        missing = []
        for inc in quoted:
            candidates = [(Path(path).parent / inc).as_posix(), (Path('include') / inc).as_posix(), (Path('src') / inc).as_posix()]
            if not any(x in known for x in candidates):
                missing.append(inc)
        reasons = []
        if missing: reasons.append('unresolved_quoted_includes')
        if PS2_MARKERS.search(data): reasons.append('ps2_specific_markers')
        if EXTERNAL.search(data): reasons.append('external_or_assembly_declarations')
        if not data.strip(): reasons.append('empty_translation_unit')
        if len(data) > 15000: reasons.append('large_translation_unit')
        score = 100 - (35 if missing else 0) - (30 if 'ps2_specific_markers' in reasons else 0) - (15 if 'external_or_assembly_declarations' in reasons else 0) - (10 if 'large_translation_unit' in reasons else 0)
        results.append({'path': path, 'score': max(0, score), 'reasons': reasons, 'missing_quoted_includes': sorted(set(missing)), 'symbol_mentions': sorted(set(SYMBOL.findall(data))), 'bytes': len(data.encode('utf-8'))})
    results.sort(key=lambda x: (-x['score'], len(x['reasons']), x['bytes'], x['path']))
    if limit is not None:
        results = results[:limit]
    return {'schema_version': 1, 'meaning': 'heuristic triage only; no native compatibility claim', 'upstream_commit': manifest.get('upstream_commit'), 'total_translation_units': sum(bool(r.get('source')) for r in records), 'candidates': results}

def compile_probe(report, source_root, compiler, max_probes, timeout):
    """Syntax-only probe, never links or executes untrusted upstream code."""
    checked = 0
    for candidate in report['candidates']:
        if checked >= max_probes:
            break
        if candidate['missing_quoted_includes']:
            candidate['probe'] = 'skipped_missing_headers'
            continue
        path = source_root / candidate['path']
        lang = 'c' if path.suffix == '.c' else 'c++'
        command = [compiler, '-fsyntax-only', '-x', lang, '-I', str(source_root / 'include'), '-I', str(source_root / 'src'), str(path)]
        try:
            result = subprocess.run(command, capture_output=True, text=True, timeout=timeout, check=False)
            candidate['probe'] = 'syntax_pass' if result.returncode == 0 else 'syntax_fail'
            candidate['diagnostic_excerpt'] = (result.stderr or '')[:1200]
        except subprocess.TimeoutExpired:
            candidate['probe'] = 'timeout'
        checked += 1
    report['probed'] = checked
    report['syntax_passes'] = sum(x.get('probe') == 'syntax_pass' for x in report['candidates'])
    return report

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--manifest', type=Path, required=True)
    p.add_argument('--source-root', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--top', type=int, default=250)
    p.add_argument('--compiler', default='clang++')
    p.add_argument('--probe', type=int, default=30, help='Number of syntax-only probes; zero disables')
    p.add_argument('--timeout', type=int, default=15)
    args = p.parse_args()
    if args.top < 1 or args.probe < 0 or args.timeout < 1: p.error('invalid limits')
    report = analyze(json.loads(args.manifest.read_text()), args.source_root, args.top)
    if args.probe:
        compile_probe(report, args.source_root, args.compiler, args.probe, args.timeout)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(f"Ranked {len(report['candidates'])} of {report['total_translation_units']} translation units; syntax passed {report.get('syntax_passes', 0)} of {report.get('probed', 0)} probes")
    print('IMPORTANT: syntax pass is NOT a linked, integrated, or playable function.')

if __name__ == '__main__':
    main()
