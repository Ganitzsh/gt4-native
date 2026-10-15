#!/usr/bin/env python3
"""Produce a conservative, reproducible review queue for native GT4 integration."""
import argparse
import hashlib
import json
from pathlib import Path
import re

def discover_integrated_paths(repository_root):
    """Only recognize exact upstream source paths explicitly wired into CMake."""
    cmake = (repository_root / 'CMakeLists.txt').read_text(encoding='utf-8')
    return set(re.findall(r'upstream/gt4/(src/[^\s)]+\.(?:c|cc|cpp|cxx))', cmake))

def queue_candidates(manifest, triage, link_report, root, already_integrated=()):
    inventory = {r['path']: r for r in manifest['records'] if r.get('source')}
    triaged = {r['path']: r for r in triage['candidates']}
    existing = set(already_integrated)
    queue = []
    for result in link_report['results']:
        path = result['path']
        if path not in inventory or path not in triaged:
            raise ValueError(f'unrecognized candidate: {path}')
        relative = Path(path)
        if relative.is_absolute() or '..' in relative.parts:
            raise ValueError(f'unsafe candidate: {path}')
        source = root / relative
        if not source.is_file():
            raise FileNotFoundError(source)
        actual = hashlib.sha256(source.read_bytes()).hexdigest()
        expected = inventory[path]['sha256']
        if actual != expected:
            raise ValueError(f'source changed since import: {path}')
        if path in existing:
            status = 'already_integrated'
        elif result.get('executable_link') == 'pass':
            status = 'review_ready'
        elif result.get('compile') == 'pass':
            status = 'needs_link_dependencies'
        else:
            status = 'needs_compile_fixes'
        queue.append({'path': path, 'sha256': actual, 'status': status,
                      'score': triaged[path]['score'],
                      'compile': result.get('compile'),
                      'executable_link': result.get('executable_link', 'not_attempted'),
                      'symbol_mentions': triaged[path].get('symbol_mentions', []),
                      'caveat': 'Review required: no runtime behavior or PS2 ABI verified'})
    priority = {'review_ready': 0, 'needs_link_dependencies': 1, 'needs_compile_fixes': 2, 'already_integrated': 3}
    queue.sort(key=lambda x: (priority[x['status']], -x['score'], x['path']))
    counts = {status: sum(item['status'] == status for item in queue) for status in priority}
    return {'schema_version': 1, 'upstream_commit': manifest.get('upstream_commit'),
            'scope': 'review queue only; zero automatically integrated functions',
            'total_imported_translation_units': manifest['translation_units'],
            'evaluated_candidates': len(queue), 'counts': counts, 'queue': queue}

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--manifest', type=Path, required=True)
    p.add_argument('--triage', type=Path, required=True)
    p.add_argument('--link-report', type=Path, required=True)
    p.add_argument('--source-root', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--native-repo', type=Path, help='Check CMake for previously integrated sources')
    a = p.parse_args()
    manifest = json.loads(a.manifest.read_text())
    triage = json.loads(a.triage.read_text())
    links = json.loads(a.link_report.read_text())
    report = queue_candidates(manifest, triage, links, a.source_root, discover_integrated_paths(a.native_repo) if a.native_repo else ())
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k != 'queue'}))
    print('Review-ready means isolated executable link passed, NOT runtime integration.')

if __name__ == '__main__':
    main()
