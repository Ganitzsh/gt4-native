import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT/'tools'/f'{name}.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


adapter = module('adapt_object_fields')
queue = module('integration_queue')


class AdapterTests(unittest.TestCase):
    def test_thirteen_bodies_and_one_explicit_caller_argument(self):
        generated, metadata = adapter.generate()
        report = json.loads(metadata)
        self.assertEqual(len(report['sources']), 13)
        changed = [r for r in report['sources'] if r['body_changes']]
        self.assertEqual([r['symbol'] for r in changed], ['func_005C9970'])
        self.assertIn('func_001465D8(arg0)', generated)
        self.assertNotIn('divisor_object', generated)
        for record in report['sources']:
            if not record['body_changes']:
                self.assertEqual(record['original_body_sha256'], record['native_body_sha256'])
        self.assertEqual(hashlib.sha256(generated.encode()).hexdigest(), report['generated_sha256'])

    def test_checked_in_generation_is_current(self):
        generated, report = adapter.generate()
        self.assertEqual((ROOT/'compat/abi/object_fields_functions.inc').read_text(), generated)
        self.assertEqual((ROOT/'compat/abi/object_fields_sources.json').read_text(), report)

    def test_selected_originals_agree_with_bulk_pin(self):
        selected = json.loads((ROOT/'upstream/gt4/integration.json').read_text())
        manifest = dict(upstream_commit=selected['upstream_commit'], records=selected['sources'])
        queue.verify_integrated_revision(ROOT, manifest)
        with self.assertRaisesRegex(ValueError, 'different upstream revisions'):
            queue.verify_integrated_revision(ROOT, dict(manifest, upstream_commit='old'))
        manifest['records'] = [dict(r, sha256='incorrect') for r in selected['sources']]
        with self.assertRaisesRegex(ValueError, 'does not match pinned import'):
            queue.verify_integrated_revision(ROOT, manifest)

    def fixture(self, root):
        shutil.copytree(ROOT/'upstream/gt4', root/'upstream/gt4')
        shutil.copytree(ROOT/'compat/abi', root/'compat/abi')
        (root/'CMakeLists.txt').write_text('add_library(runtime compat/abi/object_fields.cpp)')

    def test_source_hash_change_requires_review(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.fixture(root)
            p = root/'upstream/gt4'/adapter.FUNCTIONS[0][1]
            p.write_bytes(p.read_bytes()+b'\n// changed\n')
            with self.assertRaisesRegex(ValueError, 'source changed'):
                adapter.generate(root)

    def test_wrong_provenance_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.fixture(root)
            p = root/'upstream/gt4'/adapter.FUNCTIONS[0][1]
            p.write_bytes(p.read_bytes().replace(b'// Imported', b'// Replaced', 1))
            with self.assertRaisesRegex(ValueError, 'provenance'):
                adapter.generate(root)

    def test_queue_recognizes_only_linked_verified_adaptations(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.fixture(root)
            self.assertEqual(queue.discover_integrated_paths(root), {path for _, path in adapter.FUNCTIONS})
            (root/'CMakeLists.txt').write_text('add_library(other unrelated.cpp)')
            self.assertEqual(queue.discover_integrated_paths(root), set())

    def test_queue_rejects_modified_generated_code(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.fixture(root)
            p = root/'compat/abi/object_fields_functions.inc'
            p.write_bytes(p.read_bytes()+b'\n')
            with self.assertRaisesRegex(ValueError, 'adapter changed'):
                queue.discover_integrated_paths(root)

    def test_queue_rejects_modified_original_code(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.fixture(root)
            p = root/'upstream/gt4'/adapter.FUNCTIONS[-1][1]
            p.write_bytes(p.read_bytes()+b'\n')
            with self.assertRaisesRegex(ValueError, 'original adapter source changed'):
                queue.discover_integrated_paths(root)


if __name__ == '__main__':
    unittest.main()
