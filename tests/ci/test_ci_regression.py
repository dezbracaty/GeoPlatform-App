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
from native_regression import build_environment, command, phase, wait_windows_tree


class RegressionTests(unittest.TestCase):
    def test_source_sdk_scratch_lifetime_and_metadata_survive_each_boundary(self):
        import contextlib
        import source_regression as driver
        for failure in (None, 'producer', 'consumer', 'cancel', 'journal', 'cleanup'):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as temporary, contextlib.ExitStack() as stack:
                root = Path(temporary).resolve()
                scratch = root / 'short'
                scratch.mkdir()
                output = root / 'deep/report'
                metadata = {'package-manifest.json': b'{"package":"original bytes"}',
                    'SDK/AMD64/Release/sdk-build.json': b'{"source":"original bytes"}',
                    'SDK/AMD64/Release/sdk-manifest.json': b'{"payload":"original bytes"}'}
                created = []
                def produce(source, platform, package, env, *, log):
                    self.assertEqual(package.parent, scratch)
                    self.assertFalse(package.is_relative_to(output))
                    created.append(package)
                    if failure == 'producer':
                        raise RuntimeError('producer failure')
                    for name, value in metadata.items():
                        path = package / name
                        path.parent.mkdir(parents=True, exist_ok=True)
                        path.write_bytes(value)
                def execute(args, **kwargs):
                    if '-c' not in args:
                        return
                    if args[2] not in (driver.APP_CONSUMER, driver.APP_REGRESSION):
                        return
                    payload = json.loads(args[3])
                    self.assertTrue(created[0].is_dir())
                    if args[2] == driver.APP_CONSUMER:
                        self.assertEqual(Path(payload['sdk_root']), created[0] / 'SDK')
                        if failure == 'consumer':
                            raise RuntimeError('consumer failure')
                        if failure == 'cancel':
                            raise KeyboardInterrupt('cancel failure')
                        executable = root / 'App.exe'
                        executable.write_bytes(b'fixture; never executed')
                        receipt = {'executable': str(executable)}
                    else:
                        if failure == 'journal':
                            raise RuntimeError('journal failure')
                        summary = output / 'journals/run/task/summary.json'
                        summary.parent.mkdir(parents=True)
                        summary.write_text(json.dumps(dict(status='passed', runComplete=True,
                            selectionScope='all', plannedCases=['one'], completedCaseCount=1,
                            cases=[dict(id='one', status='passed')])) )
                        receipt = dict(status='passed', assets_sha='c'*40,
                            expected_cases=['one'], summary_path=str(summary))
                    Path(payload['receipt']).write_text(json.dumps(receipt))
                stack.enter_context(patch.dict(os.environ, RUNNER_TEMP=str(scratch)))
                stack.enter_context(patch.object(driver, 'run', side_effect=execute))
                stack.enter_context(patch.object(driver, 'machine_lock', return_value=contextlib.nullcontext()))
                stack.enter_context(patch.object(driver, 'prepare_cached_source'))
                stack.enter_context(patch.object(driver, 'init_host_submodules'))
                stack.enter_context(patch.object(driver, 'text_git', return_value='b'*40))
                stack.enter_context(patch.object(driver.native_checks, 'build_environment', return_value={}))
                stack.enter_context(patch.object(driver.native_checks, 'architecture', return_value={'returncode': 0}))
                stack.enter_context(patch.object(driver, 'build_sdk_package', side_effect=produce))
                stack.enter_context(patch.object(driver, 'update_sdk', return_value={'selected_slots': ['AMD64/Release']}))
                check_tools = stack.enter_context(patch.object(driver, 'check_ci_tools'))
                if failure == 'cleanup':
                    stack.enter_context(patch.object(driver, 'remove_owned_tree', side_effect=RuntimeError('cleanup failure')))
                if failure:
                    with self.assertRaisesRegex(KeyboardInterrupt if failure == 'cancel' else RuntimeError, failure + ' failure'):
                        driver.regress(root, 'windows', 'a'*40, 'c'*40, output, app_sha='b'*40,
                            app_repository='org/App', sdk_repository='org/SDK')
                else:
                    driver.regress(root, 'windows', 'a'*40, 'c'*40, output, app_sha='b'*40,
                        app_repository='org/App', sdk_repository='org/SDK')
                if failure:
                    check_tools.assert_not_called()
                else:
                    check_tools.assert_called_once()
                report = json.loads((output / 'source-regression.json').read_text())
                self.assertEqual(report['sdk_package']['path'], str(created[0]))
                self.assertEqual(report['status'], 'failed' if failure else 'passed')
                self.assertEqual(created[0].exists(), bool(failure))
                self.assertEqual(report['sdk_package']['retained'], bool(failure))
                if failure == 'cleanup':
                    self.assertIn('cleanup failure', report['cleanupError'])
                if failure != 'producer':
                    for name, value in metadata.items():
                        self.assertEqual((output / 'sdk-package' / name).read_bytes(), value)
                        self.assertEqual(report['sdk_package']['metadata'][name], driver.digest(output / 'sdk-package' / name))

    def test_published_sdk_never_creates_or_cleans_source_scratch(self):
        import contextlib
        import source_regression as driver
        with tempfile.TemporaryDirectory() as temporary, contextlib.ExitStack() as stack:
            root = Path(temporary).resolve()
            sdk = root / 'external-sdk'
            sdk.mkdir()
            (sdk / 'sentinel').write_bytes(b'keep')
            def execute(args, **kwargs):
                if '-c' in args and args[2] == driver.APP_CONSUMER:
                    self.assertEqual(json.loads(args[3])['sdk_root'], str(sdk))
                    raise RuntimeError('consumer failure')
            stack.enter_context(patch.object(driver, 'run', side_effect=execute))
            stack.enter_context(patch.object(driver, 'machine_lock', return_value=contextlib.nullcontext()))
            stack.enter_context(patch.object(driver, 'prepare_cached_source'))
            stack.enter_context(patch.object(driver, 'init_host_submodules'))
            stack.enter_context(patch.object(driver, 'text_git', return_value='b'*40))
            stack.enter_context(patch.object(driver.native_checks, 'build_environment', return_value={}))
            stack.enter_context(patch.object(driver.native_checks, 'architecture', return_value={'returncode': 0}))
            allocate = stack.enter_context(patch.object(driver.tempfile, 'mkdtemp'))
            cleanup = stack.enter_context(patch.object(driver, 'remove_owned_tree'))
            with self.assertRaisesRegex(RuntimeError, 'consumer failure'):
                driver.regress(root, 'windows', 'a'*40, 'c'*40, root / 'report', app_sha='b'*40,
                    app_repository='org/App', sdk_source='published', sdk_root=sdk, sdk_sha='d'*40)
            allocate.assert_not_called()
            cleanup.assert_not_called()
            self.assertEqual((sdk / 'sentinel').read_bytes(), b'keep')

    def test_native_ctest_uses_the_same_python_as_driver_admission(self):
        import native_regression as regression
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary).resolve()
            windows = os.name == 'nt'
            calls = []
            output = source / 'evidence'
            report = {'output': str(output), 'phases': []}

            def command(args, cwd, env, log):
                calls.append((args, env))
                # The Mac configure succeeds; the host build fails on both platforms.
                return 0 if not windows and len(calls) == 1 else 1

            with patch('native_regression.command', side_effect=command), patch('native_regression.runtime', return_value={}):
                result = regression.architecture(source, 'x64' if windows else 'arm64',
                    output, report, {'Qt6_DIR': '/fixture/Qt'})
            self.assertEqual(result, {'returncode': 1})
            self.assertEqual(len(calls), 1 if windows else 2, 'A failed host build must not start another Orca build')
            if not windows:
                self.assertIn('--configure-only', calls[0][0])
                self.assertEqual(calls[1][0], ['cmake', '--build', '--preset', 'arm_release', '--target', 'all'])
            arguments = json.loads(calls[0][1]['CI_CMAKE_ARGS']) if windows else calls[0][0]
            self.assertIn('-DPython3_EXECUTABLE=' + sys.executable, arguments)
            self.assertIn('-DJOURNALSYSTEM_BUILD_TESTS=ON', arguments)
            self.assertIn('-DGPLATFORM_BUILD_ORCA_NATIVE_PROBES=ON', arguments)

    def test_platform_assets_are_restored_to_source_pin_before_sdk_production(self):
        import contextlib
        import source_regression as driver
        with tempfile.TemporaryDirectory() as temporary, contextlib.ExitStack() as stack:
            source = Path(temporary).resolve()
            selections = []
            def execute(args, **kwargs):
                if '-c' in args and args[2] == driver.APP_NATIVE_ASSETS:
                    selections.append(args[4])
            def produce(*args, log):
                self.assertEqual(selections, ['c' * 40, ''])
                raise RuntimeError('SDK boundary reached after restoring source fixtures')
            stack.enter_context(patch.dict(os.environ, RUNNER_TEMP=str(source)))
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

    def test_native_regression_uses_one_ctest_after_build(self):
        import native_regression as regression
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary).resolve()
            output = source / 'evidence'
            windows = os.name == 'nt'
            build = source / ('build-Release' if windows else 'build-arm64-Release')
            tests = []
            for name, executable in regression.NATIVE_TESTS.items():
                arguments = [str(build / executable)]
                if name.startswith('OrcaNativeRegression_'):
                    arguments += [name.split('_', 1)[1], str(source), 'assets', 'platform',
                                  str(build / 'OrcaCandidateGCodeProbe'), 'commit', 'resources', 'reports']
                if name == 'OrcaMicroFillCoverageCheck':
                    arguments.append('--check-micro-fill-coverage')
                tests.append({'name': name, 'command': arguments})

            calls = []
            def run(args, cwd, env, log):
                calls.append(args[0])
                if args[0] == 'ctest':
                    self.assertEqual(env['GPLATFORM_NATIVE_REGRESSION_REPORT_DIR'], str(output / 'orca-gcode'))
                    cases = ''.join(f'<testcase name="{name}" status="run"/>' for name in regression.NATIVE_TESTS)
                    (output / 'ctest.xml').write_text(f'<testsuite>{cases}</testsuite>')
                return 0

            inventory = json.dumps({'tests': tests}).encode()
            with (patch.object(regression, 'command', side_effect=run),
                  patch.object(regression, 'runtime', return_value={}),
                  patch.object(regression.subprocess, 'check_output', return_value=inventory)):
                report = {'output': str(output), 'phases': []}
                result = regression.architecture(source, 'x64' if windows else 'arm64', output, report, {'Qt6_DIR': '/fixture/Qt'})
            self.assertEqual(result, {'returncode': 0})
            self.assertEqual(calls, ['pwsh', 'ctest'] if windows else ['bash', 'cmake', 'ctest'])

            with self.assertRaisesRegex(RuntimeError, 'exactly one registered'):
                regression.verify_native_inventory(tests[:-1], build)
            tests[-1]['command'][5] = str(source / 'previous-build' / 'OrcaCandidateGCodeProbe')
            with self.assertRaisesRegex(RuntimeError, 'probe from this build'):
                regression.verify_native_inventory(tests, build)
            junit = output / 'ctest.xml'
            content = junit.read_text().replace('status="run"/>', 'status="run"><skipped/></testcase>', 1)
            junit.write_text(content)
            with self.assertRaisesRegex(RuntimeError, 'did not pass exactly once'):
                regression.verify_native_junit(junit)

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
        workspace = Path('fixture-workspace').resolve()
        report_directory = (workspace / 'regression-results/123-1/macos').as_posix()
        for mode in ['source','published']:
            env=dict(GITHUB_WORKSPACE=str(workspace), REPORT_DIRECTORY=report_directory,
                     GITHUB_RUN_ID='123',GITHUB_RUN_ATTEMPT='1',PLATFORM='macos',SOURCE_SHA='a'*40,
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
                self.assertEqual(args[args.index('--output')+1], report_directory)
                self.assertEqual(args[args.index('--sdk-source')+1],mode)
                if mode == 'published':
                    self.assertEqual(args[args.index('--sdk-root') + 1], 'sdk')
                    self.assertEqual(args[args.index('--sdk-sha') + 1], 'c' * 40)
                else:
                    self.assertNotIn('--sdk-root', args)
                    self.assertNotIn('--sdk-sha', args)
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



    def assert_stopped(self, pid):
        process = subprocess.run(['ps', '-o', 'stat=', '-p', str(pid)], capture_output=True, text=True)
        self.assertTrue(not process.stdout.strip() or process.stdout.strip().startswith('Z'), process.stdout)

    @unittest.skipIf(os.name == 'nt', 'POSIX group ownership fixture')
    def test_ctest_timeout_cleans_actual_probe_subtree(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            child = "import signal,time;signal.signal(signal.SIGTERM,signal.SIG_IGN);time.sleep(60)"
            worker = f"import subprocess,sys,time;from pathlib import Path;p=subprocess.Popen([sys.executable,'-c',{child!r}]);Path({str(root/'pid')!r}).write_text(str(p.pid));time.sleep(60)"
            with self.assertRaises(TimeoutError):
                command([sys.executable, '-c', worker], root, None, root/'ctest.log', timeout=1)
            pid = int((root/'pid').read_text())
            self.assert_stopped(pid)



if __name__ == '__main__':
    unittest.main()
