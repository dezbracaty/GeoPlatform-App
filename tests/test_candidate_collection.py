"""Candidate capture preserves authored standards and cannot pass formal checks."""
import base64
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'tools/journal'))
from acceptance import digest, materialize_journal, run_case, write_json
from journal_runner import load_journal
from screenshot_review import collect_screenshots
import test_acceptance


class CandidateChecks(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.assets = self.root / 'assets'
        self.events = self.assets / 'scenarios/model/events'
        self.events.mkdir(parents=True)
        self.journal = self.events / 'Journal.json'
        self.app = self.root / 'GPlatform.exe'
        self.app.write_bytes(b'app fixture')
        write_json(self.assets / 'catalog.json', dict(schemaVersion=1, scenarioRoot='scenarios', modelCatalogs=[]))
        self.metadata = dict(schemaVersion=3, id='model', name='Model', testPurpose='Verify model state',
            scope='software-ui-workflow', inputs=[], unverifiedScopes=['physical-printing'],
            verification=dict(requiredMethods=['db']), acceptanceTarget='Expected DB state')
        write_json(self.events.parent / 'metadata.json', self.metadata)
        write_json(self.journal, dict(formatVersion=2, entries=[
            dict(atMs=0, kind='action', payload=dict(code='geometry.create.cube', params=dict(size=20))),
            dict(atMs=1000, kind='verify.db', payload=dict(dbInstanceId='18',
                serializationFormat='transdb.cereal-portable-binary.v1',
                expectedSerialized=base64.b64encode(b'expected DB').decode()))]))

    def test_db_capture_retains_original_hash_and_remains_not_verified(self):
        before = digest(self.journal)
        result = run_case(self.assets, 'model', self.app, self.root / 'run', None,
                          test_acceptance.SessionFixture(), load_journal, capture_candidates=True)
        self.assertEqual(result['status'], 'not_verified')
        self.assertTrue(result['candidateCollection'])
        self.assertEqual(result['methodResults'][0]['status'], 'not_verified')
        self.assertEqual(digest(self.journal), before)
        executed = json.loads(Path(result['provenance']['runtimeJournal']['path']).read_text(encoding='utf-8'))
        self.assertEqual(executed['entries'][0], json.loads(self.journal.read_text(encoding='utf-8'))['entries'][0])
        self.assertEqual(executed['entries'][1], dict(atMs=1000, kind='capture.db', payload=dict(dbInstanceId='18')))

    def test_screenshot_capture_preserves_time_and_collects_action_evidence(self):
        from PIL import Image
        standard = self.assets / 'expected.png'
        Image.new('RGB', (1382, 954), 'white').save(standard)
        write_json(self.journal, dict(formatVersion=2, entries=[dict(atMs=1500, kind='verify.screenshot',
            payload=dict(baseline='../../../expected.png', region=[5, 5, 20, 20], tolerance=1.0))]))
        original = self.journal.read_bytes()
        case = dict(journal=self.journal, metadata=self.metadata)
        path, _ = materialize_journal(case, self.root / 'run', None, capture_candidates=True)
        entry = json.loads(path.read_text(encoding='utf-8'))['entries'][0]
        self.assertEqual(entry['atMs'], 1500)
        self.assertEqual(entry['payload']['code'], 'screenshot.capture')
        self.assertEqual(self.journal.read_bytes(), original)
        output = self.root / 'evidence'
        output.mkdir()
        actual = output / 'actual.png'
        actual.write_bytes(standard.read_bytes())
        review = collect_screenshots([dict(index=1, kind='verify.screenshot', atMs=1500,
            baselineBytes=standard.read_bytes(), regions=[[5, 5, 20, 20]])],
            dict(results=[dict(index=1, kind='action', status='passed', evidence=['actual.png'])]), output)
        self.assertEqual(review['pendingCount'], 1)
        self.assertEqual(review['captureErrorCount'], 0)

    def test_foreign_window_capture_is_rejected_instead_of_silently_capturing_main(self):
        write_json(self.journal, dict(formatVersion=2, entries=[dict(atMs=1500, kind='verify.screenshot',
            payload=dict(window=dict(objectName='another.window'), baseline='../../../expected.png'))]))
        with self.assertRaisesRegex(ValueError, 'main application window only'):
            materialize_journal(dict(journal=self.journal, metadata=self.metadata),
                                self.root / 'run', None, capture_candidates=True)


if __name__ == '__main__':
    unittest.main()
