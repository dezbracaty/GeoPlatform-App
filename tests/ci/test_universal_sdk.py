"""Exercise explicit SDK propagation through the real Universal driver, without building an App."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

SCRIPTS = Path(__file__).resolve().parents[2] / 'scripts'
sys.path.insert(0, str(SCRIPTS))
spec = importlib.util.spec_from_file_location('universal_sdk_driver', SCRIPTS / 'build_macos_universal.py')
driver = importlib.util.module_from_spec(spec)
spec.loader.exec_module(driver)


@unittest.skipUnless(sys.platform == 'darwin', 'Requires the macOS Universal driver and native fcntl lock')
class UniversalSDKTests(unittest.TestCase):
    def fixture(self, root, *, producer=False, roots=None):
        source = root / 'selected product'
        source.mkdir()
        (source / 'CMakeLists.txt').write_text('# selected preset project')
        if producer:
            (source / 'App').mkdir()
            (source / 'App/CMakeLists.txt').write_text('# full source App')
        sdk = root / 'selected SDK'
        sdk.mkdir()
        qt = root / 'Qt/lib/cmake/Qt6'
        qt.mkdir(parents=True)
        for index, arch in enumerate(('arm64', 'x86_64')):
            build = source / f'build-{arch}-Release'
            build.mkdir()
            chosen = roots[index] if roots else str(sdk)
            (build / 'CMakeCache.txt').write_text(
                f'Qt6_DIR:PATH={qt}\nGPLATFORM_SDK_ROOT:PATH={chosen}\n')
            (build / ('GPlatform/GPlatform.app' if producer else 'GPlatform.app')).mkdir(parents=True)
        return source, sdk

    def execute(self, source, *arguments):
        commands = []
        def deploy(_, destination, *args):
            destination.mkdir(parents=True)
            return []
        def merge(_, __, destination, *args):
            destination.mkdir()
        with patch.object(driver.platform, 'system', return_value='Darwin'), \
                patch.object(driver, 'run', side_effect=lambda args, *a, **kw: commands.append(args)), \
                patch.object(driver, 'deploy_bundle', side_effect=deploy), \
                patch.object(driver, 'merge_bundles', side_effect=merge), \
                patch.object(driver, 'audit_bundle', return_value=[]):
            driver.main(['--source-dir', str(source), *arguments])
        return commands

    def test_missing_and_empty_sdk_fail_before_configure(self):
        for extra in ([], ['--sdk-root', '']):
            with self.subTest(extra=extra), tempfile.TemporaryDirectory() as temporary:
                source, _ = self.fixture(Path(temporary))
                # Empty CLI paths must not select the current directory implicitly.
                with self.assertRaises(SystemExit):
                    self.execute(source, *extra)

    def test_fresh_thin_configures_receive_the_same_explicit_sdk(self):
        with tempfile.TemporaryDirectory() as temporary:
            source, sdk = self.fixture(Path(temporary))
            for cache in source.glob('build-*/CMakeCache.txt'):
                cache.write_text('\n'.join(line for line in cache.read_text().splitlines()
                                           if not line.startswith('GPLATFORM_SDK_ROOT')) + '\n')
            commands = self.execute(source, '--sdk-root', str(sdk))
            configures = [args for args in commands if '--preset' in args and '--build' not in args]
            self.assertEqual(len(configures), 2)
            self.assertEqual({args[args.index('--preset') + 1] for args in configures},
                             {'arm_release', 'x86_release'})
            for args in configures:
                self.assertIn(f'-DGPLATFORM_SDK_ROOT={sdk.resolve()}', args)
            report = json.loads((source / 'build-universal-Release/report.json').read_text())
            self.assertEqual(report['architectures'], ['arm64', 'x86_64'])

    def test_merge_rejects_missing_or_different_sdk_before_deployment(self):
        for roots in (('', ''), ('/different/sdk', '/different/sdk'), ('/one/sdk', '/two/sdk')):
            with self.subTest(roots=roots), tempfile.TemporaryDirectory() as temporary:
                source, sdk = self.fixture(Path(temporary), roots=roots)
                with patch.object(driver, 'deploy_bundle') as deploy:
                    with self.assertRaisesRegex(driver.BuildError, 'SDK_ROOT differs'):
                        with patch.object(driver.platform, 'system', return_value='Darwin'):
                            driver.main(['--source-dir', str(source), '--sdk-root', str(sdk), '--merge-only'])
                    deploy.assert_not_called()

    def test_matching_merge_and_full_source_without_sdk_are_preserved(self):
        for producer in (False, True):
            with self.subTest(producer=producer), tempfile.TemporaryDirectory() as temporary:
                source, sdk = self.fixture(Path(temporary), producer=producer)
                commands = self.execute(source, '--merge-only', *([] if producer else ['--sdk-root', str(sdk)]))
                self.assertFalse(any('--preset' in args for args in commands))


if __name__ == '__main__':
    unittest.main()
