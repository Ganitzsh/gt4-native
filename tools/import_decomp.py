#!/usr/bin/env python3
"""Bulk GT4 source import: pinned revision, reproducible inventory, no host-build claims."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

EXT = {'.c', '.cc', '.cpp', '.cxx', '.h', '.hpp'}
SRC = {'.c', '.cc', '.cpp', '.cxx'}
INCLUDE = re.compile(r'^\\s*#\\s*include\\s*[<"]([^>"]+)[>"]', re.M)
SYMBOL = re.compile(r'\\b(?:func_[0-9A-Fa-f]{8}|[A-Za-z_]\\w*__(?:structor|tf|filter|less|vtable)[A-Za-z_0-9]*)\\b')

def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()

def import_tree(source, dest, manifest_path, expected_commit=None, dry_run=False):
    source, dest = source.resolve(), dest.resolve()
    if not (source / 'src').is_dir():
        raise ValueError('Upstream must contain src/')
    if source == dest or source in dest.parents or dest in source.parents:
        raise ValueError('Source and destination must not contain one another')
    if expected_commit:
        actual = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
        if actual != expected_commit:
            raise ValueError(f'Wrong upstream revision: expected {expected_commit}, got {actual}')
    paths = sorted(p for root in ('src', 'include') if (source / root).exists()
                   for p in (source / root).rglob('*') if p.is_file() and p.suffix.lower() in EXT)
    if not paths:
        raise ValueError('No source files found')
    records = []
    for path in paths:
        rel = path.relative_to(source)
        data = path.read_text(encoding='utf-8', errors='replace')
        records.append({'path': rel.as_posix(), 'sha256': digest(path),
                        'bytes': path.stat().st_size, 'source': path.suffix.lower() in SRC,
                        'includes': sorted(set(INCLUDE.findall(data))),
                        'symbol_mentions': sorted(set(SYMBOL.findall(data)))})
    known = {r['path'] for r in records}
    for record in records:
        record['missing_local_includes'] = [inc for inc in record['includes']
            if not any((base / inc).as_posix() in known
            for base in (Path('include'), Path(record['path']).parent, Path('src')))]
    if not dry_run:
        staging = dest.with_name(dest.name + '.staging')
        if staging.exists():
            shutil.rmtree(staging)
        for record in records:
            target = staging / record['path']
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source / record['path'], target)
        previous = dest.with_name(dest.name + '.previous')
        if previous.exists():
            shutil.rmtree(previous)
        if dest.exists():
            dest.rename(previous)
        staging.rename(dest)
        if previous.exists():
            shutil.rmtree(previous)
    report = {'format_version': 1, 'upstream_commit': expected_commit,
              'files': len(records), 'translation_units': sum(r['source'] for r in records),
              'total_bytes': sum(r['bytes'] for r in records), 'records': records}
    if not dry_run:
        manifest_path.parent.mkdir(parents=True, exist_ok=True)
        manifest_path.write_text(json.dumps(report, indent=2, sort_keys=True) + '\\n', encoding='utf-8')
    return report

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--upstream', type=Path, required=True)
    parser.add_argument('--dest', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--commit', required=True, help='Exact upstream commit SHA')
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args()
    report = import_tree(args.upstream, args.dest, args.manifest, args.commit, args.dry_run)
    print(json.dumps({k: v for k, v in report.items() if k != 'records'}, sort_keys=True))
    print('NOTE: inventory/import only; sources are NOT automatically native-compatible.')

if __name__ == '__main__':
    main()
