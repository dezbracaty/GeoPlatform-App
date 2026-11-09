"""Deployment binding preserves complete DB comparisons and authored standards."""
import base64
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from acceptance import materialize_journal, write_json


def string_token(value):
    raw = str(value).encode('utf-8')
    return struct.pack('<Q', 11) + b'string.utf8' + struct.pack('<Q', len(raw)) + raw


class DeploymentResources(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.app = self.root / 'new-longer-deployment/GPlatform.exe'
        self.app.parent.mkdir()
        self.app.write_bytes(b'app')
        self.relative = 'resources/bed.stl'
        self.resource = self.app.parent / self.relative
        self.resource.parent.mkdir()
        self.resource.write_bytes(b'independently pinned model')
        self.captured = self.root / 'captured'
        self.metadata = dict(preconditions=dict(applicationDirectory=str(self.captured)),
            deploymentResources={self.relative: hashlib.sha256(self.resource.read_bytes()).hexdigest()})
        self.raw = b'\x01SBDT' + b'other fields' + string_token(self.captured / self.relative) + b'untouched fields'
        self.journal = self.root / 'events/Journal.json'
        self.journal.parent.mkdir()
        self.save(self.raw)

    def save(self, raw):
        write_json(self.journal, dict(formatVersion=2, entries=[dict(atMs=1234, kind='verify.db',
            payload=dict(dbInstanceId='18', serializationFormat='transdb.cereal-portable-binary.v1',
                         expectedSerialized=base64.b64encode(raw).decode()))]))

    def materialize(self, **kwargs):
        return materialize_journal(dict(journal=self.journal, metadata=self.metadata),
            self.root / 'run', None, application=self.app, **kwargs)[0]

    def test_relocation_changes_only_declared_typed_path_and_length(self):
        authored = self.journal.read_bytes()
        runtime = json.loads(self.materialize().read_text(encoding='utf-8'))
        entry = runtime['entries'][0]
        expected = b'\x01SBDTother fields' + string_token(self.resource) + b'untouched fields'
        self.assertEqual(base64.b64decode(entry['payload']['expectedSerialized']), expected)
        self.assertEqual(entry['atMs'], 1234)
        self.assertEqual(self.journal.read_bytes(), authored)

    def test_changed_or_missing_resource_is_rejected(self):
        self.resource.write_bytes(b'different model')
        with self.assertRaisesRegex(ValueError, 'hash differs'):
            self.materialize()
        self.resource.unlink()
        with self.assertRaisesRegex(ValueError, 'Missing deployment resource'):
            self.materialize()

    def test_unreferenced_or_ambiguous_path_is_rejected(self):
        self.save(b'\x01SBDTother data')
        with self.assertRaisesRegex(ValueError, 'missing from DB standards'):
            self.materialize()
        self.save(self.raw + string_token(self.captured / self.relative))
        with self.assertRaisesRegex(ValueError, 'Ambiguous'):
            self.materialize()

    def test_invalid_relative_paths_are_rejected(self):
        for name in ['resources/../bed.stl', 'resources//bed.stl', 'C:/bed.stl', 'resources/bed:stl',
                     'resources/./bed.stl', 'resources/bed\\model.stl']:
            with self.subTest(name=name):
                self.metadata['deploymentResources'] = {name: '0' * 64}
                with self.assertRaisesRegex(ValueError, 'Invalid deployment resource path'):
                    self.materialize()

    def test_no_manifest_preserves_existing_authored_journal(self):
        del self.metadata['deploymentResources']
        self.assertEqual(self.materialize(), self.journal)

    def test_candidate_capture_still_checks_pinned_resources(self):
        self.resource.write_bytes(b'different model')
        with self.assertRaisesRegex(ValueError, 'hash differs'):
            self.materialize(capture_candidates=True)


if __name__ == '__main__':
    unittest.main()
