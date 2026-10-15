import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('integration_queue', Path(__file__).resolve().parents[1] / 'tools' / 'integration_queue.py')
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

class QueueTests(unittest.TestCase):
    def test_queue_and_hash_verification(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'src').mkdir()
            path = root / 'src' / 'func.c'
            path.write_text('int f(void) { return 1; }')
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            manifest = {'translation_units': 1, 'upstream_commit': 'abc',
                        'records': [{'path': 'src/func.c', 'source': True, 'sha256': digest}]}
            triage = {'candidates': [{'path': 'src/func.c', 'score': 100, 'symbol_mentions': []}]}
            link = {'results': [{'path': 'src/func.c', 'compile': 'pass', 'executable_link': 'pass'}]}
            result = mod.queue_candidates(manifest, triage, link, root)
            self.assertEqual(result['counts']['review_ready'], 1)
            self.assertEqual(result['scope'], 'review queue only; zero automatically integrated functions')
            path.write_text('modified')
            with self.assertRaises(ValueError):
                mod.queue_candidates(manifest, triage, link, root)

    def test_detects_cmake_integrated_sources(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'CMakeLists.txt').write_text(
                'add_executable(test tests/a.cpp upstream/gt4/src/runtime/foo.cpp)\\n')
            self.assertEqual(mod.discover_integrated_paths(root), {'src/runtime/foo.cpp'})

    def test_missing_symbols_are_not_review_ready(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'src').mkdir()
            path = root / 'src' / 'unit.c'
            path.write_text('extern int missing(); int f() { return missing(); }')
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            m = {'translation_units': 1, 'records': [{'path': 'src/unit.c', 'source': True, 'sha256': digest}]}
            t = {'candidates': [{'path': 'src/unit.c', 'score': 90}]}
            l = {'results': [{'path': 'src/unit.c', 'compile': 'pass', 'executable_link': 'fail'}]}
            result = mod.queue_candidates(m, t, l, root)
            self.assertEqual(result['counts']['needs_link_dependencies'], 1)

if __name__ == '__main__':
    unittest.main()
