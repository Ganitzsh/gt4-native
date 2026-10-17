import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

MODULE = Path(__file__).resolve().parents[1] / 'tools' / 'import_decomp.py'
spec = importlib.util.spec_from_file_location('import_decomp', MODULE)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

class ImportTests(unittest.TestCase):
    def test_copy_inventory_and_cleanup(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            upstream = root / 'upstream'
            (upstream / 'src' / 'race').mkdir(parents=True)
            (upstream / 'include').mkdir()
            (upstream / 'src' / 'race' / 'foo.cpp').write_text('#include "types.h"\nvoid func_00123456() {}\n')
            (upstream / 'include' / 'types.h').write_text('typedef int s32;\n')
            (upstream / 'src' / 'race' / 'notes.txt').write_text('ignored')
            dest, manifest = root / 'out', root / 'manifest.json'
            result = mod.import_tree(upstream, dest, manifest)
            self.assertEqual(result['files'], 2)
            self.assertEqual(result['translation_units'], 1)
            source = next(r for r in result['records'] if r['source'])
            self.assertEqual(source['symbol_mentions'], ['func_00123456'])
            self.assertEqual(source['missing_local_includes'], [])
            self.assertTrue((dest / 'src' / 'race' / 'foo.cpp').exists())
            self.assertFalse((dest / 'src' / 'race' / 'notes.txt').exists())
            self.assertEqual(json.loads(manifest.read_text())['files'], 2)
            (upstream / 'src' / 'race' / 'foo.cpp').unlink()
            mod.import_tree(upstream, dest, manifest)
            self.assertFalse((dest / 'src' / 'race' / 'foo.cpp').exists())

    def test_dry_run(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'src').mkdir()
            (root / 'src' / 'a.c').write_text('int f;')
            mod.import_tree(root, root.parent / (root.name + '_output'), root.parent / (root.name + '_manifest'), dry_run=True)
            self.assertFalse((root.parent / (root.name + '_output')).exists())

    def test_nested_dest_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'src').mkdir()
            (root / 'src' / 'a.c').write_text('int f;')
            with self.assertRaises(ValueError):
                mod.import_tree(root, root / 'out', root / 'manifest')

if __name__ == '__main__':
    unittest.main()
