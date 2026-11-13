"""Real calibration records must be fresh, correctly targeted and internally linked."""
import copy
import json
import os
from pathlib import Path
import sys
import tempfile
import time
import unittest
from uuid import uuid4

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from acceptance_calibration import verify_records


class CalibrationRecordTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name) / 'records'
        self.directory.mkdir()
        self.output = Path(self.temporary.name) / 'result'
        self.started = time.time_ns()
        self.expected = dict(mode=6, applied=True, materialSlot=0,
                             parameters=dict(start='230', end='220', step='5'),
                             observation=dict(observed=True, value='220'))

    def write(self, **updates):
        record = copy.deepcopy(self.expected)
        record.update(version=2, task=str(uuid4()), configurationRevision=1, appliedRevision=2)
        record.update(updates)
        path = self.directory / (record['task'] + '.json')
        path.write_text(json.dumps(record))
        os.utime(path, ns=(self.started+1, self.started+1))
        return path

    def verify(self):
        return verify_records(self.directory, self.started, [self.expected], self.output)

    def test_fresh_record_is_archived_and_uuid_is_not_a_fixed_expectation(self):
        path = self.write()
        report = self.verify()
        self.assertEqual(report['status'], 'passed')
        self.assertEqual((self.output / report['records'][0]['path']).read_bytes(), path.read_bytes())

    def test_stale_record_cannot_satisfy_save(self):
        path = self.write()
        os.utime(path, ns=(self.started-1, self.started-1))
        with self.assertRaisesRegex(ValueError, 'found 0'):
            self.verify()

    def test_wrong_target_or_observation_is_rejected_and_archived(self):
        self.write(materialSlot=1)
        with self.assertRaisesRegex(ValueError, 'independent expectations'):
            self.verify()
        self.assertEqual(len(list((self.output/'artifacts/calibration').glob('*.json'))), 1)

    def test_success_flag_requires_advanced_revision(self):
        self.write(appliedRevision=1)
        with self.assertRaisesRegex(ValueError, 'advance'):
            self.verify()

    def test_extra_new_record_is_not_silently_ignored(self):
        self.write(); self.write()
        with self.assertRaisesRegex(ValueError, 'found 2'):
            self.verify()

    def test_flow_observation_must_belong_to_the_saved_task(self):
        self.expected['mode'] = 5
        self.write(observation=dict(observed=True, value='220', taskId=str(uuid4())))
        with self.assertRaisesRegex(ValueError, 'another calibration task'):
            self.verify()


if __name__ == '__main__':
    unittest.main()
