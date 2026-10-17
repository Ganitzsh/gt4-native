import importlib.util
from pathlib import Path
import shutil
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('native_link_gate', Path(__file__).resolve().parents[1] / 'tools' / 'native_link_gate.py')
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

class LinkGateTests(unittest.TestCase):
    def test_independent_function_links(self):
        cc = shutil.which('clang') or shutil.which('gcc')
        cxx = shutil.which('clang++') or shutil.which('g++')
        if not cc or not cxx:
            self.skipTest('compiler unavailable')
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'src').mkdir()
            (root / 'src' / 'unit.c').write_text('int square(int x) { return x*x; }\n')
            report = {'candidates': [{'path': 'src/unit.c', 'probe': 'syntax_pass'}]}
            result = mod.gate(report, root, cc, cxx)
            self.assertEqual(result['compiled'], 1)
            self.assertEqual(result['executable_linked'], 1)

    def test_unresolved_dependency_is_not_full_link(self):
        cc = shutil.which('clang') or shutil.which('gcc')
        cxx = shutil.which('clang++') or shutil.which('g++')
        if not cc or not cxx:
            self.skipTest('compiler unavailable')
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'src').mkdir()
            (root / 'src' / 'unit.c').write_text('extern int missing(void); int f(void) { return missing(); }\n')
            result = mod.gate({'candidates': [{'path': 'src/unit.c', 'probe': 'syntax_pass'}]}, root, cc, cxx)
            self.assertEqual(result['compiled'], 1)
            self.assertEqual(result['executable_linked'], 0)

    def test_rejects_traversal(self):
        with self.assertRaises(ValueError):
            mod.gate({'candidates': [{'path': '../secret.c', 'probe': 'syntax_pass'}]}, Path('/tmp'), 'cc', 'c++')

if __name__ == '__main__':
    unittest.main()
