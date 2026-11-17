"""Failure evidence must survive and zero/unverified suites cannot pass."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
import os
import subprocess
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts/ci'))
from native_regression import build_environment, command, comparison_verdict, phase, wait_windows_tree, windows_native_environment


class RegressionTests(unittest.TestCase):
    def test_native_ctest_uses_the_same_python_as_driver_admission(self):
        import native_regression as regression
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary).resolve()
            calls = []
            def command(args, cwd, env, log):
                calls.append((args, env))
                return 0
            def phase(report, name, operation):
                if name.endswith('/build-tests'):
                    operation()
                return False
            with patch('native_regression.command', side_effect=command), patch('native_regression.phase', side_effect=phase), \
                 patch('native_regression.runtime', return_value={}), patch('native_regression.text_git', return_value='a' * 40):
                regression.architecture(source, 'x64' if os.name == 'nt' else 'arm64',
                    source / 'evidence', {'phases': []}, {'Qt6_DIR': '/fixture/Qt'})
            arguments = json.loads(calls[0][1]['CI_CMAKE_ARGS']) if os.name == 'nt' else calls[0][0]
            self.assertIn('-DPython3_EXECUTABLE=' + sys.executable, arguments)
            self.assertIn('-DJOURNALSYSTEM_BUILD_TESTS=ON', arguments)

    def test_platform_assets_are_restored_to_source_pin_before_sdk_production(self):
        import contextlib
        import source_regression as driver
        with tempfile.TemporaryDirectory() as temporary, contextlib.ExitStack() as stack:
            source = Path(temporary).resolve()
            selections = []
            def execute(args, **kwargs):
                if '-c' in args and args[2] == driver.APP_NATIVE_ASSETS:
                    selections.append(args[4])
            def produce(*args):
                self.assertEqual(selections, ['c' * 40, ''])
                raise RuntimeError('SDK boundary reached after restoring source fixtures')
            stack.enter_context(patch.object(driver, 'run', side_effect=execute))
            stack.enter_context(patch.object(driver, 'machine_lock', return_value=contextlib.nullcontext()))
            stack.enter_context(patch.object(driver, 'prepare_cached_source'))
            stack.enter_context(patch.object(driver, 'init_host_submodules'))
            stack.enter_context(patch.object(driver, 'text_git', return_value='b' * 40))
            stack.enter_context(patch.object(driver.native_checks, 'build_environment', return_value={}))
            stack.enter_context(patch.object(driver.native_checks, 'architecture', return_value={'returncode': 0}))
            stack.enter_context(patch.object(driver, 'build_sdk_package', side_effect=produce))
            with self.assertRaisesRegex(RuntimeError, 'SDK boundary reached'):
                driver.regress(source, 'macos', 'a' * 40, 'c' * 40, source / 'report', app_sha='b'*40, app_repository='org/App')

    def test_native_comparisons_use_initialized_app_asset_catalogs_and_baselines(self):
        import native_regression as regression
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary).resolve()
            assets = source / 'App/tests/TestAssets/slicing_regression'
            assets.mkdir(parents=True)
            (assets / 'candidate-config.json').write_text('{}')
            for name in ('cases.json', 'cases-extended.json'):
                (assets / name).write_text(json.dumps({'cases': ['one', 'two']}))
            for suite in ('standard', 'extended'):
                (assets / 'baselines' / suite).mkdir(parents=True)
            native = source / 'build-arm64-Release/ci-orca-native'
            native.mkdir(parents=True)
            dependencies = {}
            for key, filename, location in [
                    ('OpenCASCADE_DIR', 'TKernel.dll', 'lib/cmake/occt'),
                    ('GMP_LIBRARY', 'libgmp-10.dll', 'lib/libgmp-10.lib')]:
                prefix = source / ('fixture-' + key) / 'x64/install-Release'
                (prefix / 'bin').mkdir(parents=True)
                (prefix / 'bin' / filename).write_bytes(b'runtime path fixture; never loaded')
                dependencies[key] = str(prefix / location)
            (native / 'provenance.json').write_text(json.dumps({
                'probe': str(native / 'probe'), 'dependencyCacheValues': dependencies}))
            seen = []
            def compare(args, *rest):
                catalog = Path(args[args.index('--cases') + 1])
                baseline = Path(args[args.index('--baseline-dir') + 1])
                self.assertEqual(catalog.parent, assets)
                self.assertTrue(baseline.is_dir())
                destination = Path(args[args.index('--output') + 1])
                destination.mkdir()
                (destination / 'report.json').write_text(json.dumps({'status': 'passed',
                    'summary': {'cases': len(json.loads(catalog.read_text())['cases']), 'failed': 0, 'strictFailed': 0}}))
                seen.append((catalog.name, baseline.name))
                return 0
            def phase(report, name, operation):
                if name.endswith('/original48') or name.endswith('/extended26'):
                    self.assertEqual(operation()['returncode'], 0)
                return name.endswith('/native-orca-build')
            with patch('native_regression.phase', side_effect=phase), patch('native_regression.command', side_effect=compare), \
                 patch('native_regression.text_git', return_value='a' * 40), patch('native_regression.runtime', return_value={}):
                regression.architecture(source, 'arm64', source / 'evidence', {'phases': []}, {'Qt6_DIR': '/fixture/Qt'})
            self.assertEqual(seen, [('cases.json', 'standard'), ('cases-extended.json', 'extended')])

    def test_native_fixtures_are_prepared_before_configure_and_failure_blocks_build(self):
        import contextlib
        import source_regression as driver
        for missing in (False, True):
            with self.subTest(missing=missing), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary).resolve()
                seen = []
                def execute(args, **kwargs):
                    if '-c' in args and args[2] == driver.APP_NATIVE_ASSETS:
                        seen.append('assets')
                        self.assertEqual(args[3], str(root / 'App'))
                        self.assertEqual(args[4], 'c' * 40)
                        self.assertIn('prepare_assets(app, sys.argv[2] or None)', args[2])
                        if missing:
                            raise RuntimeError('fixture checkout failed')
                def native(*args, **kwargs):
                    self.assertEqual(seen, ['assets'])
                    raise RuntimeError('reached configure after assets')
                with contextlib.ExitStack() as stack:
                    stack.enter_context(patch.object(driver, 'run', side_effect=execute))
                    stack.enter_context(patch.object(driver, 'machine_lock', return_value=contextlib.nullcontext()))
                    stack.enter_context(patch.object(driver, 'prepare_cached_source'))
                    stack.enter_context(patch.object(driver, 'init_host_submodules'))
                    stack.enter_context(patch.object(driver, 'text_git', return_value='b' * 40))
                    stack.enter_context(patch.object(driver.native_checks, 'build_environment', return_value={}))
                    build = stack.enter_context(patch.object(driver.native_checks, 'architecture', side_effect=native))
                    with self.assertRaisesRegex(RuntimeError, 'fixture checkout failed' if missing else 'reached configure'):
                        driver.regress(root, 'macos', 'a' * 40, 'c' * 40, root / 'report', app_sha='b'*40, app_repository='org/App')
                    self.assertEqual(build.call_count, 0 if missing else 1)
                report = json.loads((root / 'report/source-regression.json').read_text())
                self.assertEqual(report['status'], 'failed')
                if not missing:
                    self.assertEqual(report['native_assets_sha'], 'c' * 40)

    def test_native_describe_inherits_exact_linked_runtime_versions(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            occt = root/'occt-7.6/x64/install-Release'
            gmp = root/'gmp-vendored/x64/install-Release'
            for directory, name in [(occt/'bin/occt', 'TKernel.dll'), (gmp/'bin', 'libgmp-10.dll')]:
                directory.mkdir(parents=True)
                (directory/name).write_bytes(b'fixture')
            # A different installed version must not win through the base PATH.
            base = dict(os.environ, PATH=str(root/'occt-7.9/bin'), UNCHANGED='value')
            metadata = {'dependencyCacheValues': {'OpenCASCADE_DIR': str(occt/'lib/cmake/occt'),
                                                'GMP_LIBRARY': str(gmp/'lib/libgmp-10.lib')}}
            env = windows_native_environment(metadata, base)
            code = "import json,os;print(json.dumps(os.environ['PATH'].split(os.pathsep)))"
            inherited = json.loads(subprocess.check_output([sys.executable, '-c', code], env=env))
            self.assertEqual(inherited, [str(occt/'bin/occt'), str(gmp/'bin'), base['PATH']])
            self.assertEqual(env['UNCHANGED'], 'value')
            self.assertEqual(base['PATH'], str(root/'occt-7.9/bin'))
            # Ambiguous runtime selection fails instead of silently using a DLL
            # from another location or the unrelated product SDK deployment.
            (occt/'other').mkdir()
            (occt/'other/TKernel.dll').write_bytes(b'other')
            with self.assertRaisesRegex(RuntimeError, 'found 2'):
                windows_native_environment(metadata, base)

    def test_windows_bootstrap_cancellation_reaps_its_owned_tree(self):
        from unittest.mock import Mock
        process = Mock(pid=123)
        process.wait.side_effect = [KeyboardInterrupt(), 0]
        process.poll.return_value = None
        with patch('native_regression.subprocess.run') as stop:
            with self.assertRaises(KeyboardInterrupt):
                wait_windows_tree(process)
        self.assertEqual(stop.call_args.args[0], ['taskkill', '/PID', '123', '/T', '/F'])

    def test_workflow_passes_only_frozen_identities_to_its_owned_driver(self):
        import yaml
        flow=yaml.load((Path(__file__).resolve().parents[2]/'.github/workflows/run-regression.yml').read_text(),Loader=yaml.BaseLoader)
        code=next(s['run'] for s in flow['jobs']['regression']['steps'] if s.get('name')=='SDK consumer and complete platform regression')
        for mode in ['source','published']:
            env=dict(GITHUB_RUN_ID='123',GITHUB_RUN_ATTEMPT='1',PLATFORM='macos',SOURCE_SHA='a'*40,
                     APP_SHA='d'*40,GITHUB_REPOSITORY='org/App',ASSETS_SHA='b'*40,ASSETS_REF='macmini',SDK_SOURCE=mode,SDK_SHA='c'*40,SDK_REPOSITORY='org/SDK')
            with self.subTest(mode=mode),patch.dict(os.environ,env),patch('subprocess.run') as spawn:
                exec(compile(code,'actual-workflow-step','exec'),{})
                spawn.assert_called_once()
                args=spawn.call_args.args[0]
                self.assertEqual(args[1],'control/scripts/ci/source_regression.py')
                self.assertEqual(args[args.index('--source-sha')+1],'a'*40)
                self.assertEqual(args[args.index('--source-dir')+1],'host-source')
                self.assertEqual(args[args.index('--app-sha')+1],'d'*40)
                self.assertEqual(args[args.index('--app-repository')+1],'org/App')
                self.assertEqual(args[args.index('--assets-sha')+1],'b'*40)
                self.assertEqual(args[args.index('--sdk-source')+1],mode)
                self.assertEqual('--sdk-root' in args,mode=='published')
                self.assertEqual(spawn.call_args.kwargs,{'check':True})
        with patch.dict(os.environ,env),patch('subprocess.run',side_effect=subprocess.CalledProcessError(7,[])):
            with self.assertRaises(subprocess.CalledProcessError):exec(compile(code,'actual-workflow-step','exec'),{})

    def test_windows_environment_copy_keeps_qt_lookup_case_insensitive(self):
        with patch.dict(os.environ, {'QT6_DIR': 'D:/Qt SDK/6.11.2/msvc2022_64/lib/cmake/Qt6'}, clear=True):
            env = build_environment()
        self.assertEqual(env['Qt6_DIR'], 'D:/Qt SDK/6.11.2/msvc2022_64/lib/cmake/Qt6')

    def test_failed_process_is_retained_and_later_suite_still_runs(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            report = {'output': temp, 'phases': []}
            self.assertFalse(phase(report, 'first', lambda: {'returncode': command(
                [sys.executable, '-c', 'print("real failing test"); raise SystemExit(7)'], root, None, root/'test.log')}))
            self.assertTrue(phase(report, 'next', lambda: {'returncode': 0}))
            data = json.loads((root/'summary.json').read_text())
            self.assertEqual([p['status'] for p in data['phases']], ['failed', 'passed'])
            self.assertIn('real failing test', (root/'test.log').read_text())

    def test_setup_exception_is_blocked_not_passed(self):
        with tempfile.TemporaryDirectory() as temp:
            data = {'output': temp, 'phases': []}
            def missing():
                raise FileNotFoundError('missing probe')
            self.assertFalse(phase(data, 'native', missing))
            self.assertEqual(data['phases'][0]['status'], 'blocked')



    def test_semantic_diagnostic_cannot_hide_strict_gcode_difference(self):
        report = {'status': 'passed', 'summary': {'cases': 48, 'failed': 0, 'strictFailed': 1}}
        self.assertNotEqual(comparison_verdict(0, report, 48)['returncode'], 0)
        report['summary']['strictFailed'] = 0
        self.assertEqual(comparison_verdict(0, report, 48)['returncode'], 0)
        self.assertNotEqual(comparison_verdict(7, report, 48)['returncode'], 0)
        self.assertNotEqual(comparison_verdict(0, report, 26)['returncode'], 0)

    def test_only_explicitly_reviewed_metadata_can_differ(self):
        report = {'status': 'passed', 'summary': {'cases': 48, 'failed': 0,
            'strictFailed': 1, 'approvedMetadataDifferences': 1}}
        self.assertEqual(comparison_verdict(0, report, 48)['returncode'], 0)
        report['summary']['strictFailed'] = 2
        self.assertNotEqual(comparison_verdict(0, report, 48)['returncode'], 0)
        report['summary'].update(strictFailed=1, failed=1)
        self.assertNotEqual(comparison_verdict(0, report, 48)['returncode'], 0)

    def assert_stopped(self, pid):
        process = subprocess.run(['ps', '-o', 'stat=', '-p', str(pid)], capture_output=True, text=True)
        self.assertTrue(not process.stdout.strip() or process.stdout.strip().startswith('Z'), process.stdout)

    @unittest.skipIf(os.name == 'nt', 'POSIX group ownership fixture')
    def test_nested_native_builder_timeout_cleans_actual_compile_subtree(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root/'scripts').mkdir()
            child = "import signal,time;signal.signal(signal.SIGTERM,signal.SIG_IGN);time.sleep(60)"
            worker = f"import subprocess,sys,time;from pathlib import Path;p=subprocess.Popen([sys.executable,'-c',{child!r}]);Path({str(root/'pid')!r}).write_text(str(p.pid));time.sleep(60)"
            (root/'scripts/build_orca_candidate_probe.py').write_text(
                f"import sys\nfrom pathlib import Path\ndef main(argv):\n run([sys.executable,'-c',{worker!r}],Path({str(root/'native.log')!r}),60)\n")
            with self.assertRaises(TimeoutError):
                command([sys.executable, Path(__file__).resolve().parents[2]/'scripts/ci/native_child.py',
                         'native', '--source', root], root, None, root/'wrapper.log', timeout=1)
            pid = int((root/'pid').read_text())
            self.assert_stopped(pid)



if __name__ == '__main__':
    unittest.main()
