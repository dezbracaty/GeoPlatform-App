"""Review decisions are separate from automatic results and tied to immutable evidence."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from review_model import ReviewBatch


class ReviewModelTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name in ['expected', 'actual']:(self.root / name).write_bytes(name.encode())
        self.row = dict(id='checkpoint', kind='verify.screenshot', reviewStatus='pending',
            executionStatus='captured', comparisonStatus='different', expected='expected', actual='actual',
            baselineSha256=hashlib.sha256(b'expected').hexdigest(), actualSha256=hashlib.sha256(b'actual').hexdigest())
        self.document = dict(schemaVersion=1, scope='batch-screenshot-review', runId='run-a', status='failed', checkpoints=[self.row])
        self.write_manifest()
        self.batch = ReviewBatch(self.root)

    def write_manifest(self):
        (self.root / 'screenshot_review.json').write_text(json.dumps(self.document))

    def test_decision_survives_reopen_without_overwriting_failed_execution(self):
        original = (self.root / 'screenshot_review.json').read_bytes()
        self.batch.save(self.row, 'accepted', 'only rendering variation', reviewer='reviewer')
        batch = ReviewBatch(self.root)
        self.assertEqual(batch.decision(batch.rows[0])['decision'], 'accepted')
        self.assertEqual(batch.document['status'], 'failed')
        self.assertEqual((self.root / 'screenshot_review.json').read_bytes(), original)
        self.assertEqual(batch.decision(batch.rows[0])['reviewer'], 'reviewer')

    def test_changed_evidence_and_missing_images_cannot_be_reviewed(self):
        (self.root / 'actual').write_bytes(b'replaced')
        self.assertFalse(self.batch.can_review(self.row))
        with self.assertRaises(ValueError):self.batch.save(self.row, 'accepted', '', reviewer='reviewer')
        (self.root / 'actual').unlink()
        self.assertFalse(self.batch.can_review(self.row))

    def test_escaped_paths_are_blocked(self):
        row = dict(self.row, expected='../outside')
        self.assertFalse(self.batch.can_review(row))
        with self.assertRaises(ValueError):self.batch.path(row, 'expected')

    def test_other_batch_record_and_blank_reviewer_are_rejected(self):
        with self.assertRaises(ValueError):self.batch.save(self.row, 'accepted', '', reviewer='')
        (self.root / 'review-decisions.json').write_text(json.dumps(dict(schemaVersion=1, runId='other', reviews={})))
        with self.assertRaises(ValueError):ReviewBatch(self.root)

    def test_diagnostics_and_not_run_are_not_reviewable(self):
        for changes in [dict(kind='action'), dict(executionStatus='not_run'), dict(reviewStatus='not_applicable')]:
            self.assertFalse(self.batch.can_review(dict(self.row, **changes)))

    def test_latest_automatic_summary_overrides_stale_auxiliary_status(self):
        self.document['status'] = 'passed'
        self.document['cases'] = [dict(id='first', status='passed')]
        self.write_manifest()
        summary = dict(reviewRunId='run-a', status='failed', screenshotReviewError='image processing failed',
                       plannedCases=['first', 'second', 'later'],
                       cases=[dict(id='first', status='passed'), dict(id='second', status='failed')])
        (self.root / 'summary.json').write_text(json.dumps(summary))
        batch = ReviewBatch(self.root)
        self.assertEqual(batch.document['status'], 'failed')
        self.assertEqual([case['status'] for case in batch.document['cases']], ['passed', 'failed', 'not_run'])
        self.assertIn('image processing failed', batch.document['screenshotReviewError'])
        self.assertEqual(batch.rows, [self.row])

    def test_summary_of_other_batch_cannot_override_the_manifest(self):
        (self.root / 'summary.json').write_text(json.dumps(dict(reviewRunId='other', status='passed')))
        with self.assertRaisesRegex(ValueError, 'different batch'):
            ReviewBatch(self.root)


if __name__ == '__main__':unittest.main()
