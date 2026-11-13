"""Windows CI rejects missing, incomplete, candidate and wrong-display results."""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch
try:
    from PIL import Image
except ImportError:
    Image = None

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts/ci'))
import windows_regression as subject


@unittest.skipUnless(Image, 'Screenshot gate fixtures require Pillow')
class WindowsRegressionChecks(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.source = Path(self.temp.name)
        self.tools = self.source / 'tools/journal'
        self.tools.mkdir(parents=True)
        for name in ['journal_runner.py', 'journal_execution_session.py', 'journal_display_profile.py']:
            (self.tools / name).write_text('fixture', encoding='utf-8')
        shutil.copyfile(Path(__file__).resolve().parents[2] / 'tools/journal/session_repository.py',
                        self.tools / 'session_repository.py')
        self.app = self.source / 'build-Release/GPlatform.exe'
        self.app.parent.mkdir()
        self.app.write_bytes(b'current deployed app')
        self.output = self.source / 'regression'
        scene = self.source / 'tests/TestAssets/scenarios/scene'
        scene.mkdir(parents=True)
        (scene / 'events').mkdir()
        (scene / 'events/Journal.json').write_text(
            json.dumps(dict(formatVersion=2, entries=[])), encoding='utf-8')
        (scene / 'metadata.json').write_text(json.dumps(dict(baselineReview=dict(
            platform='Windows amd64', status='approved'))), encoding='utf-8')
        self.summary = dict(status='passed', runComplete=True, selectionScope='all',
                            plannedCases=['scene'], completedCaseCount=1, cases=[dict(id='scene', status='passed')])
        self.window = dict(devicePixelRatio=1, logicalSize=[1382, 954])
        self.size = (1382, 954)
        self.exit = 0

    def execute(self, command, **kwargs):
        self.assertNotIn('QT_QPA_PLATFORM', kwargs['env'])
        self.assertIn('--cases', command)
        self.assertEqual(command[command.index('--cases') + 1], 'all')
        self.assertNotIn('--capture-candidates', command)
        run = self.output / 'run'
        case = run / 'scene'
        case.mkdir(parents=True)
        (run / 'summary.json').write_text(json.dumps(self.summary), encoding='utf-8')
        Image.new('RGB', self.size, 'white').save(case / 'actual.png')
        (case / 'execution_result.json').write_text(json.dumps(dict(actualWindow=self.window,
            results=[dict(kind='verify.screenshot', evidence=['expected.png', 'actual.png'])])), encoding='utf-8')
        return type('Result', (), dict(returncode=self.exit))()

    def run_regression(self):
        with patch.object(subject, 'require_desktop'), patch.object(subject, 'prepare_assets', return_value='c' * 40), \
                patch.object(subject, 'text_git', return_value='a' * 40), \
                patch.object(subject.subprocess, 'run', side_effect=self.execute):
            return subject.regression(self.source, self.app, self.output, dict(QT_QPA_PLATFORM='offscreen'), 'c' * 40)

    def test_pass_uses_deployed_app_all_cases_and_records_fixed_inputs(self):
        self.assertEqual(self.run_regression()['status'], 'passed')
        inputs = json.loads((self.output / 'regression-inputs.json').read_text(encoding='utf-8'))
        self.assertEqual(inputs['assets_sha'], 'c' * 40)
        self.assertEqual(inputs['application'], str(self.app))

    def test_candidate_collection_cannot_pass_the_ci_gate(self):
        self.summary['candidateCollection'] = True
        with self.assertRaisesRegex(RuntimeError, 'did not pass'):
            self.run_regression()

    def test_incomplete_run_cannot_pass_the_ci_gate(self):
        self.summary['runComplete'] = False
        with self.assertRaisesRegex(RuntimeError, 'did not pass'):
            self.run_regression()

    def test_summary_cannot_hide_a_failed_case(self):
        self.summary['cases'][0]['status'] = 'failed'
        with self.assertRaisesRegex(RuntimeError, 'every scene'):
            self.run_regression()

    def test_wrong_dpr_is_rejected(self):
        self.window['devicePixelRatio'] = 2
        with self.assertRaisesRegex(RuntimeError, 'display profile'):
            self.run_regression()

    def test_wrong_screenshot_size_is_rejected(self):
        self.size = (2764, 1908)
        with self.assertRaisesRegex(RuntimeError, 'screenshot size'):
            self.run_regression()

    def test_unsupported_desktop_platform_is_rejected(self):
        with patch.object(subject.sys, 'platform', 'linux'):
            with self.assertRaisesRegex(RuntimeError, 'requires Windows'):
                subject.require_desktop()

    def test_pending_standards_cannot_be_consumed_as_approved_windows_baselines(self):
        metadata = self.source / 'tests/TestAssets/scenarios/scene/metadata.json'
        metadata.write_text(json.dumps(dict(baselineReview=dict(platform='Windows amd64', status='pending'))), encoding='utf-8')
        with self.assertRaisesRegex(RuntimeError, 'reviewed baselines'):
            self.run_regression()

    def test_deployment_specific_db_baselines_require_the_declared_directory(self):
        metadata = self.source / 'tests/TestAssets/scenarios/scene/metadata.json'
        declaration = json.loads(metadata.read_text(encoding='utf-8'))
        declaration['preconditions'] = dict(applicationDirectory=str(self.source / 'another-build'))
        metadata.write_text(json.dumps(declaration), encoding='utf-8')
        with self.assertRaisesRegex(RuntimeError, 'deployment directory differs'):
            self.run_regression()

    def test_explicit_resource_binding_defers_validation_to_maintained_acceptance(self):
        metadata = self.source / 'tests/TestAssets/scenarios/scene/metadata.json'
        declaration = json.loads(metadata.read_text(encoding='utf-8'))
        declaration['preconditions'] = dict(applicationDirectory=str(self.source / 'another-build'))
        declaration['deploymentResources'] = {'resources/bed.stl': 'a' * 64}
        metadata.write_text(json.dumps(declaration), encoding='utf-8')
        self.assertEqual(self.run_regression()['status'], 'passed')


if __name__ == '__main__':
    unittest.main()
