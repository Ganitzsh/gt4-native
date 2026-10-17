import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

path = Path(__file__).resolve().parents[1] / 'tools' / 'triage_native.py'
spec = importlib.util.spec_from_file_location('triage_native', path)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

class TriageTests(unittest.TestCase):
    def test_ranking_and_missing_headers(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'src').mkdir()
            (root / 'src' / 'clean.c').write_text('int add(int x) { return x + 1; }')
            (root / 'src' / 'missing.cpp').write_text('#include "not_found.h"\nint f() { return 1; }')
            manifest = {'upstream_commit': 'abc', 'records': [
                {'path': 'src/clean.c', 'source': True},
                {'path': 'src/missing.cpp', 'source': True}]}
            result = mod.analyze(manifest, root)
            self.assertEqual(result['total_translation_units'], 2)
            self.assertEqual(result['candidates'][0]['path'], 'src/clean.c')
            self.assertIn('unresolved_quoted_includes', result['candidates'][1]['reasons'])
            self.assertEqual(result['upstream_commit'], 'abc')

    def test_probe_compiler(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'src').mkdir()
            (root / 'src' / 'ok.c').write_text('int f(void) { return 3; }')
            report = mod.analyze({'records': [{'path': 'src/ok.c', 'source': True}]}, root)
            import shutil
            compiler = shutil.which('clang') or shutil.which('gcc')
            if compiler is None:
                self.skipTest('no C compiler')
            mod.compile_probe(report, root, compiler, 1, 10)
            self.assertEqual(report['syntax_passes'], 1)
            self.assertEqual(report['candidates'][0]['probe'], 'syntax_pass')

if __name__ == '__main__':
    unittest.main()
