#!/usr/bin/env python3
"""Conservative compile/link gate for triaged upstream units.

Never runs upstream code. A link success is not a gameplay or ABI compatibility claim.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import tempfile

def run(cmd, timeout):
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, check=False)
        return {'ok': result.returncode == 0, 'returncode': result.returncode,
                'diagnostics': (result.stderr or result.stdout)[-1800:]}
    except subprocess.TimeoutExpired:
        return {'ok': False, 'returncode': None, 'diagnostics': 'timeout'}

def gate(report, root, c_compiler, cxx_compiler, max_units=20, timeout=20):
    results = []
    for candidate in report['candidates']:
        if len(results) >= max_units:
            break
        if candidate.get('probe') != 'syntax_pass':
            continue
        relative = Path(candidate['path'])
        if relative.is_absolute() or '..' in relative.parts:
            raise ValueError('unsafe candidate path')
        source = root / relative
        if not source.is_file():
            raise FileNotFoundError(source)
        compiler = c_compiler if source.suffix == '.c' else cxx_compiler
        language = 'c' if source.suffix == '.c' else 'c++'
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            obj = work / 'candidate.o'
            common = ['-x', language, '-I', str(root / 'include'), '-I', str(root / 'src')]
            compile_result = run([compiler, '-c', *common, str(source), '-o', str(obj)], timeout)
            entry = {'path': candidate['path'], 'compile': 'pass' if compile_result['ok'] else 'fail',
                     'compile_diagnostics': compile_result['diagnostics'], 'link': 'not_attempted'}
            if compile_result['ok']:
                # Use a relocatable link: checks object linkability without requiring a main().
                # Unresolved symbols may remain, so never call this a full link success.
                linked = work / 'linked.o'
                link_result = run([compiler, '-r', str(obj), '-o', str(linked)], timeout)
                entry['link'] = 'relocatable_pass' if link_result['ok'] else 'fail'
                entry['link_diagnostics'] = link_result['diagnostics']
                # Attempt a true executable link with a minimal host entrypoint.
                main = work / ('main.c' if language == 'c' else 'main.cpp')
                main.write_text('int main(void) { return 0; }\n')
                exe = work / 'candidate-exe'
                full = run([compiler, str(main), str(obj), '-o', str(exe)], timeout)
                entry['executable_link'] = 'pass' if full['ok'] else 'fail'
                entry['executable_link_diagnostics'] = full['diagnostics']
            results.append(entry)
    return {'schema_version': 1, 'scope': 'isolated host compile/link; no execution; no ABI or gameplay guarantee',
            'tested': len(results), 'compiled': sum(x['compile'] == 'pass' for x in results),
            'relocatable_linked': sum(x['link'] == 'relocatable_pass' for x in results),
            'executable_linked': sum(x.get('executable_link') == 'pass' for x in results),
            'results': results}

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--triage', type=Path, required=True)
    p.add_argument('--source-root', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--cc', default='clang')
    p.add_argument('--cxx', default='clang++')
    p.add_argument('--max-units', type=int, default=20)
    p.add_argument('--timeout', type=int, default=20)
    a = p.parse_args()
    if a.max_units < 1 or a.timeout < 1:
        p.error('max-units and timeout must be positive')
    report = gate(json.loads(a.triage.read_text()), a.source_root, a.cc, a.cxx, a.max_units, a.timeout)
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k != 'results'}))
    print('No imported code executed. Link success does not establish functional compatibility.')

if __name__ == '__main__':
    main()
