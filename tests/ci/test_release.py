"""Release fixtures exercise real installer hashes without publishing to GitHub."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/ci'))
import publish_release
from common import digest


class ReleaseChecks(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.inputs = dict(repository='example/App', app_sha='a' * 40, sdk_sha='b' * 40,
                           workflow_sha='c' * 40, run_id='123', attempt='2', run_journal=True)
        self.calls = []
        self.uploaded = {}
        self.missing_uploaded_asset = False
        self.corrupt_uploaded_asset = False
        for platform, filename, architecture in [('windows', 'App setup.exe', 'x64'), ('macos', 'GPlatform.dmg', 'universal')]:
            directory = self.root / platform
            directory.mkdir()
            package = directory / filename
            package.write_bytes((platform + ' actual installer fixture').encode())
            checksum = digest(package)
            (directory / (filename + '.sha256')).write_text(f'{checksum}  {filename}\n')
            metadata = dict(app_sha=self.inputs['app_sha'], sdk_sha=self.inputs['sdk_sha'], platform=platform,
                            architecture=architecture, sdk_consumer_check='passed', installation_tested=False)
            installer = dict(package=filename, sha256=checksum)
            if platform == 'macos':
                metadata.update(installer=dict(installer, status='passed'), regression=dict(status='passed'))
            else:
                metadata.update(installer)
            (directory / 'build-result.json').write_text(json.dumps(metadata))

    def update_metadata(self, platform, **values):
        path = self.root / platform / 'build-result.json'
        metadata = json.loads(path.read_text())
        metadata.update(values)
        path.write_text(json.dumps(metadata))

    def execute(self, args, *, capture=False):
        args = [str(arg) for arg in args]
        self.calls.append(args)
        if args[:3] == ['gh', 'release', 'upload']:
            for name in args[4:-2]:
                path = Path(name)
                self.uploaded[path.name] = dict(name=path.name, state='uploaded', size=path.stat().st_size,
                                                digest='sha256:' + digest(path))
        if args[:2] == ['gh', 'api']:
            uploaded = list(self.uploaded.values())
            if self.missing_uploaded_asset:
                uploaded.pop()
            if self.corrupt_uploaded_asset:
                uploaded[0]['digest'] = 'sha256:' + '0' * 64
            return json.dumps(dict(tag_name='build-123-2', target_commitish=self.inputs['app_sha'],
                                   draft=True, prerelease=True, assets=uploaded,
                                   html_url='https://github.com/example/App/releases/tag/build-123-2')).encode()
        return b'' if capture else None

    def publish(self):
        with patch.object(publish_release, 'run', side_effect=self.execute):
            return publish_release.publish(self.root, **self.inputs)

    def test_complete_installers_are_published_only_after_remote_hash_validation(self):
        self.assertEqual(self.publish(), 'https://github.com/example/App/releases/tag/build-123-2')
        self.assertEqual([call[1:3] for call in self.calls], [
            ['release', 'create'], ['release', 'upload'], ['api', 'repos/example/App/releases/tags/build-123-2'],
            ['release', 'edit']])
        create, _, _, edit = self.calls
        self.assertIn('--draft', create)
        self.assertEqual(create[create.index('--target') + 1], self.inputs['app_sha'])
        self.assertIn('--prerelease', edit)
        self.assertIn('--latest=false', edit)
        self.assertIn('--draft=false', edit)
        self.assertEqual(set(self.uploaded), {'App setup.exe', 'App setup.exe.sha256', 'GPlatform.dmg',
                         'GPlatform.dmg.sha256', 'build-result-windows.json', 'build-result-macos.json'})

    def test_missing_platform_never_creates_a_release(self):
        (self.root / 'macos/build-result.json').unlink()
        with self.assertRaises(FileNotFoundError):
            self.publish()
        self.assertEqual(self.calls, [])

    def test_corrupt_installer_never_creates_a_release(self):
        (self.root / 'windows/App setup.exe').write_bytes(b'corruption')
        with self.assertRaisesRegex(RuntimeError, 'checksum'):
            self.publish()
        self.assertEqual(self.calls, [])

    def test_wrong_app_sdk_or_architecture_never_creates_a_release(self):
        for field in ['app_sha', 'sdk_sha', 'architecture']:
            with self.subTest(field=field):
                self.update_metadata('windows', **{field: 'wrong'})
                with self.assertRaisesRegex(RuntimeError, 'fixed inputs'):
                    self.publish()
                self.update_metadata('windows', **{field: self.inputs[field] if field != 'architecture' else 'x64'})
        self.assertEqual(self.calls, [])

    def test_failed_or_skipped_enabled_regression_never_creates_a_release(self):
        for status in ['failed', 'skipped']:
            self.update_metadata('macos', regression=dict(status=status))
            with self.assertRaisesRegex(RuntimeError, 'Journal regression'):
                self.publish()
        self.assertEqual(self.calls, [])

    def test_disabled_regression_preserves_skipped_status_in_release_evidence(self):
        self.inputs['run_journal'] = False
        self.update_metadata('macos', regression=dict(status='skipped', reason='run_journal=false'))
        notes = []
        original = self.execute
        def record(args, **kwargs):
            if args[1:3] == ['release', 'create']:
                notes.append(Path(args[args.index('--notes-file') + 1]).read_text(encoding='utf-8'))
            return original(args, **kwargs)
        with patch.object(publish_release, 'run', side_effect=record):
            publish_release.publish(self.root, **self.inputs)
        self.assertIn('**skipped**', notes[0])

    def test_upload_failure_never_publishes_the_draft(self):
        original = self.execute
        def fail(args, **kwargs):
            if args[1:3] == ['release', 'upload']:
                raise RuntimeError('upload failed')
            return original(args, **kwargs)
        with patch.object(publish_release, 'run', side_effect=fail), self.assertRaisesRegex(RuntimeError, 'upload failed'):
            publish_release.publish(self.root, **self.inputs)
        self.assertEqual([call[1:3] for call in self.calls], [['release', 'create']])

    def test_missing_or_corrupt_remote_assets_never_publish_the_draft(self):
        for flag in ['missing_uploaded_asset', 'corrupt_uploaded_asset']:
            self.calls.clear()
            setattr(self, flag, True)
            with self.assertRaisesRegex(RuntimeError, 'draft retained'):
                self.publish()
            self.assertFalse(any(call[1:3] == ['release', 'edit'] for call in self.calls))
            setattr(self, flag, False)

    def test_package_path_cannot_escape_the_artifact(self):
        self.update_metadata('windows', package='../outside.exe')
        with self.assertRaisesRegex(RuntimeError, 'invalid package name'):
            self.publish()
        self.assertEqual(self.calls, [])


class ReleaseWorkflowChecks(unittest.TestCase):
    def test_publish_is_default_on_and_requires_successful_current_attempt_summary(self):
        workflow = (ROOT / '.github/workflows/build-app.yml').read_text(encoding='utf-8')
        setting = workflow.split('      publish_release:\n', 1)[1].split('      request_id:\n', 1)[0]
        self.assertIn('type: boolean', setting)
        self.assertIn('default: true', setting)
        job = workflow.split('  publish:\n', 1)[1]
        self.assertIn('needs: [prepare, result]', job)
        self.assertIn('if: inputs.publish_release && needs.prepare.outputs.attempt == github.run_attempt', job)
        self.assertNotIn('always()', job)
        self.assertIn('app-windows-${{ github.run_id }}-${{ github.run_attempt }}', job)
        self.assertIn('app-macos-${{ github.run_id }}-${{ github.run_attempt }}', job)
        self.assertIn('HAS_RELEASE: ${{ !inputs.publish_release || secrets.GPLATFORM_RELEASE_TOKEN !=', workflow)
        build = workflow.split('  build:\n', 1)[1].split('  queue_guard:\n', 1)[0]
        self.assertNotIn('secrets.GPLATFORM_RELEASE_TOKEN', build)


if __name__ == '__main__':
    unittest.main()
