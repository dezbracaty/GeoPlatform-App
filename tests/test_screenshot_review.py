"""Batch visual review freezes raw evidence and never invents acceptance."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from acceptance import run_case, write_json, write_summary
from screenshot_review import checkpoint_declarations, collect_screenshots, snapshot_checkpoints

try:
    from PIL import Image
except ImportError:
    Image = None


def load_document(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


class CaptureSession:
    def __init__(self, baseline, *, fail_input=False, mutate_standard=False, mismatch=False):
        self.baseline, self.fail_input, self.mutate_standard = baseline, fail_input, mutate_standard
        self.mismatch = mismatch

    def run(self, journal, *, application, output_dir, startup_timeout):
        original = self.baseline.read_bytes()
        if self.mutate_standard:
            Image.new('RGBA', (1382, 954), 'blue').save(self.baseline)
        output_dir.mkdir(parents=True)
        (output_dir / 'screenshots').mkdir()
        document = load_document(journal)
        rows = []
        for index, entry in enumerate(document['entries'], 1):
            row = dict(index=index, kind=entry['kind'], atMs=entry['atMs'], status='passed', evidence=[])
            if entry['kind'] == 'verify.screenshot':
                expected = output_dir / 'screenshots' / f'{index}-expected.png'
                actual = output_dir / 'screenshots' / f'{index}-actual.png'
                expected.write_bytes(original)
                Image.new('RGBA', (2764, 1908) if self.mismatch else (1382, 954),
                          'black' if self.mismatch else 'white').save(actual)
                row.update(status='failed' if self.mismatch else 'passed', evidence=[str(expected), str(actual)])
            elif self.fail_input:
                row.update(status='failed', message='actual input target missing')
            rows.append(row)
            if row['status'] == 'failed':
                break
        failed = any(row['status'] == 'failed' for row in rows)
        result = dict(success=not failed, acceptance='failed' if failed else 'passed',
                      results=rows, completedEntries=len(rows), outputPath=str(output_dir),
                      journalSha256=hashlib.sha256(Path(journal).read_bytes()).hexdigest())
        write_json(output_dir / 'execution_result.json', result)
        return result


@unittest.skipIf(Image is None, 'Visual review needs Pillow')
class ScreenshotReviewCheck(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.assets = self.root / 'assets'
        self.scenario = self.assets / 'scenarios' / 'visual'
        (self.scenario / 'events').mkdir(parents=True)
        self.baseline = self.assets / 'standard.png'
        Image.new('RGBA', (1382, 954), 'white').save(self.baseline)
        self.original = self.baseline.read_bytes()
        self.application = self.root / 'program'
        self.application.write_bytes(b'not executed by test')
        write_json(self.assets / 'catalog.json', dict(schemaVersion=1, scenarioRoot='scenarios', modelCatalogs=[]))
        self.document = dict(formatVersion=2, entries=[
            dict(atMs=0, kind='verify.screenshot', payload=dict(label='<script>unsafe</script>', baseline='../../../standard.png', tolerance=1)),
            dict(atMs=1, kind='input.mouse', payload=dict(event='press', target='actual.button', button=1, buttons=1)),
            dict(atMs=2, kind='verify.screenshot', payload=dict(label='later', baseline='../../../standard.png', tolerance=1))])
        write_json(self.scenario / 'events' / 'Journal.json', self.document)
        write_json(self.scenario / 'metadata.json', dict(schemaVersion=3, id='visual', name='Visual',
            testPurpose='Review screenshots', acceptanceTarget='Review the actual appearance', scope='ui',
            verification=dict(requiredMethods=['screenshot']), inputs=[], unverifiedScopes=['physical-printing'], checks=[]))

    def run_visual(self, session):
        return run_case(self.assets, 'visual', self.application, self.root / 'run' / 'visual',
                        None, session, load_document, review_screenshots=True)

    def evidence(self):
        return load_document(self.root / 'run/visual/review/screenshots.json')['checkpoints']

    def test_raw_dpr1_and_dpr2_images_are_retained_and_difference_is_not_passed(self):
        result = self.run_visual(CaptureSession(self.baseline, mismatch=True))
        self.assertEqual(result['executionStatus'], 'failed')
        self.assertEqual(result['status'], 'failed')
        self.assertEqual(result['reviewStatus'], 'pending')
        self.assertEqual(result['screenshotReview']['pendingCount'], 1)
        row = self.evidence()[0]
        output = self.root / 'run/visual'
        self.assertEqual((output / row['expected']).read_bytes(), self.original)
        with Image.open(output / row['actual']) as image:
            self.assertEqual(image.size, (2764, 1908))
        with Image.open(output / row['diff']) as image:
            self.assertEqual(image.size, (1382, 954))
        self.assertEqual(row['comparisonStatus'], 'different')
        self.assertGreater(row['substantialPixelCount'], 0)
        self.assertEqual(self.baseline.read_bytes(), self.original)

    def test_unreviewed_images_do_not_downgrade_automatic_pass(self):
        result = self.run_visual(CaptureSession(self.baseline))
        self.assertEqual(result['status'], 'passed')
        self.assertEqual(result['journalAcceptance'], 'passed')
        self.assertEqual(result['screenshotReview']['pendingCount'], 2)

    def test_optional_hint_failure_falls_back_without_changing_verdict(self):
        from unittest.mock import patch
        with patch('screenshot_review.comparison_images', side_effect=ImportError('optional NumPy missing')):
            result = self.run_visual(CaptureSession(self.baseline))
        self.assertEqual(result['status'], 'passed')
        row = self.evidence()[0]
        self.assertIn('rawDiff', row)
        self.assertIn('optional NumPy', row['visualHintError'])
        self.assertEqual(row['comparisonMetadata']['visualHintsStatus'], 'unavailable')

    def test_review_sidecar_write_failure_does_not_change_automatic_verdict(self):
        from unittest.mock import patch
        with patch('acceptance.collect_screenshots', side_effect=OSError('review directory unavailable')):
            result = self.run_visual(CaptureSession(self.baseline))
        self.assertEqual(result['status'], 'passed')
        self.assertIn('review directory', result['screenshotReviewError'])

    def test_auxiliary_batch_report_failure_preserves_automatic_summary_and_html(self):
        from unittest.mock import patch
        output = self.root / 'summary-only'
        output.mkdir()
        summary = dict(status='passed', plannedCases=['visual'],
                       cases=[dict(id='visual', status='passed')])
        with patch('acceptance.write_batch_report', side_effect=RuntimeError('<missing image>')):
            write_summary(output, summary)
        self.assertEqual(load_document(output / 'summary.json')['status'], 'passed')
        report = (output / 'report.html').read_text(encoding="utf-8")
        self.assertIn('visual/case_report.html', report)
        self.assertIn('&lt;missing image&gt;', report)
        self.assertNotIn('<missing image>', report)

    def test_input_failure_keeps_later_checkpoint_visible_and_not_reviewable(self):
        result = self.run_visual(CaptureSession(self.baseline, fail_input=True))
        self.assertEqual(result['status'], 'failed')
        rows = self.evidence()
        self.assertEqual(len(rows), 2)
        self.assertEqual(rows[1]['executionStatus'], 'not_run')
        self.assertEqual(rows[1]['reviewStatus'], 'not_applicable')
        self.assertNotIn('actual', rows[1])
        self.assertIn('expected', rows[1])

    def test_changed_standard_fails_integrity_but_original_snapshot_survives(self):
        result = self.run_visual(CaptureSession(self.baseline, mutate_standard=True))
        self.assertEqual(result['status'], 'failed')
        row = self.evidence()[0]
        self.assertEqual((self.root / 'run/visual' / row['expected']).read_bytes(), self.original)
        self.assertNotEqual(self.baseline.read_bytes(), self.original)

    def test_missing_standard_blocks_run_and_lists_every_checkpoint(self):
        self.baseline.unlink()
        result = self.run_visual(CaptureSession(self.baseline))
        self.assertEqual(result['status'], 'blocked')
        self.assertEqual(result['executionStatus'], 'not_started')
        rows = self.evidence()
        self.assertEqual(len(rows), 2)
        self.assertTrue(all(row['executionStatus'] == 'not_run' for row in rows))
        self.assertTrue(all(row.get('baselineError') for row in rows))

    def test_report_lists_unstarted_case_and_escapes_labels(self):
        result = self.run_visual(CaptureSession(self.baseline))
        summary = dict(status='not_verified', plannedCases=['visual', 'unstarted'],
                       cases=[dict(id='visual', status=result['status'])],
                       screenshotCheckpoints={'unstarted': checkpoint_declarations(self.document)})
        write_summary(self.root / 'run', summary)
        report = (self.root / 'run/report.html').read_text(encoding="utf-8")
        self.assertIn('&lt;script&gt;unsafe&lt;/script&gt;', report)
        self.assertNotIn('<script>unsafe</script>', report)
        self.assertEqual(report.count('class="checkpoint"'), 4)
        self.assertEqual(len(load_document(self.root / 'run/screenshot_review.json')['checkpoints']), 4)
        self.assertEqual(summary['screenshotReviewPendingCount'], 2)
        self.assertIn('Python 回归结果窗口', report)
        self.assertEqual(load_document(self.root / 'run/summary.json')['status'], 'not_verified')

    def test_external_or_invalid_actual_evidence_cannot_be_reviewed_as_pass(self):
        case = dict(journal=self.scenario / 'events/Journal.json', document=self.document,
                    hashes=dict(baselines={'standard.png': hashlib.sha256(self.original).hexdigest()}))
        checkpoints = snapshot_checkpoints(case, self.assets)
        output = self.root / 'outside-run'
        output.mkdir()
        execution = dict(results=[dict(index=1, status='passed', evidence=['unused', str(self.baseline)])])
        review = collect_screenshots(checkpoints, execution, output)
        self.assertEqual(review['captureErrorCount'], 1)
        rows = load_document(output / review['path'])['checkpoints']
        self.assertEqual(rows[0]['reviewStatus'], 'not_applicable')
        self.assertEqual(rows[0]['comparisonStatus'], 'capture_error')


if __name__ == '__main__':
    unittest.main()
