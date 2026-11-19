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
                def produce(source, platform, package, env):
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

    def test_windows_native_ctest_resolves_runtime_before_provenance_exists(self):
        from types import SimpleNamespace
        from unittest.mock import Mock
        import native_child as child
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            build = root / 'native-build'
            build.mkdir()
            dependencies = {}
            directories = []
            for key, filename, location in [
                    ('OpenCASCADE_DIR', 'TKernel.dll', 'lib/cmake/occt'),
                    ('GMP_LIBRARY', 'libgmp-10.dll', 'lib/libgmp-10.lib')]:
                prefix = root / key / 'install-Release'
                (prefix / 'bin').mkdir(parents=True)
                (prefix / 'bin' / filename).write_bytes(b'path fixture; never loaded')
                dependencies[key] = str(prefix / location)
                directories.append(str(prefix / 'bin'))
            base = dict(PATH='unrelated-sdk-runtime', UNCHANGED='value')
            cache = Mock(return_value=dependencies)
            process = Mock()
            process.wait.return_value = 0
            with patch.object(child, 'os', SimpleNamespace(name='nt', environ=base)), \
                 patch.object(child.subprocess, 'Popen', return_value=process) as spawn:
                child.attached_build(['ctest.exe', '--test-dir', str(build)], root / 'test.log',
                                     10, env=base, cache_values=cache)
            cache.assert_called_once_with(build / 'CMakeCache.txt')
            self.assertEqual(spawn.call_args.kwargs['env']['PATH'],
                             os.pathsep.join(directories + [base['PATH']]))
            self.assertEqual(spawn.call_args.kwargs['env']['UNCHANGED'], 'value')
            self.assertEqual(base['PATH'], 'unrelated-sdk-runtime')
            self.assertFalse((build.parent / 'provenance.json').exists())

            # Invalid or ambiguous configured dependencies fail before launching
            # the check instead of borrowing an unrelated DLL from PATH.
            for condition in ('missing-key', 'missing-dll', 'ambiguous-dll'):
                with self.subTest(condition=condition):
                    fixture = dict(dependencies)
                    if condition == 'missing-key':
                        del fixture['GMP_LIBRARY']
                    elif condition == 'missing-dll':
                        (root / 'GMP_LIBRARY/install-Release/bin/libgmp-10.dll').unlink()
                    else:
                        other = root / 'OpenCASCADE_DIR/install-Release/other'
                        other.mkdir()
                        (other / 'TKernel.dll').write_bytes(b'ambiguous')
                    with patch.object(child, 'os', SimpleNamespace(name='nt', environ=base)), \
                         patch.object(child.subprocess, 'Popen') as spawn:
                        with self.assertRaises(RuntimeError):
                            child.attached_build(['ctest', '--test-dir', str(build)], root / 'bad.log',
                                                 10, cache_values=Mock(return_value=fixture))
                    spawn.assert_not_called()

    def test_native_non_ctest_and_mac_commands_keep_the_original_environment(self):
        from types import SimpleNamespace
        from unittest.mock import Mock
        import native_child as child
        with tempfile.TemporaryDirectory() as temporary:
            for system, command in [('nt', ['cmake', '--preset', 'release']),
                                    ('nt', ['cmake', '--build', '--preset', 'release']),
                                    ('posix', ['ctest', '--test-dir', temporary])]:
                for env in (None, {'PATH': 'original'}):
                    with self.subTest(system=system, command=command, env=env):
                        process = Mock()
                        process.wait.return_value = 0
                        cache = Mock()
                        with patch.object(child, 'os', SimpleNamespace(name=system)), \
                             patch.object(child.subprocess, 'Popen', return_value=process) as spawn:
                            child.attached_build(command, Path(temporary) / 'test.log', 10,
                                                 env=env, cache_values=cache)
                        self.assertIs(spawn.call_args.kwargs['env'], env)
                        cache.assert_not_called()

    def test_native_child_uses_the_selected_builders_cache_reader(self):
        from types import SimpleNamespace
        from unittest.mock import Mock
        import native_child as child
        builder = SimpleNamespace(cache_values=Mock(), main=Mock(return_value=0))
        with tempfile.TemporaryDirectory() as temporary, \
             patch.dict(sys.modules, {'build_orca_candidate_probe': builder}), \
             patch.object(sys, 'argv', ['native_child', 'native', '--source', temporary, '--jobs', '16']):
            original_path = list(sys.path)
            try:
                self.assertEqual(child.main(), 0)
            finally:
                sys.path[:] = original_path
        self.assertIs(builder.run.func, child.attached_build)
        self.assertIs(builder.run.keywords['cache_values'], builder.cache_values)
        builder.main.assert_called_once_with(['--jobs', '16'])

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
            def produce(*args):
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
            native = source / 'evidence/native-orca'
            dependencies = {}
            for key, filename, location in [
                    ('OpenCASCADE_DIR', 'TKernel.dll', 'lib/cmake/occt'),
                    ('GMP_LIBRARY', 'libgmp-10.dll', 'lib/libgmp-10.lib')]:
                prefix = source / ('fixture-' + key) / 'x64/install-Release'
                (prefix / 'bin').mkdir(parents=True)
                (prefix / 'bin' / filename).write_bytes(b'runtime path fixture; never loaded')
                dependencies[key] = str(prefix / location)
            seen = []
            def compare(args, *rest):
                if '--producer-build-dir' in args:
                    self.assertEqual(args[args.index('--producer-build-dir') + 1], source / 'producer/build-Release')
                    self.assertNotIn('--jobs', args)
                    native.mkdir()
                    (native / 'provenance.json').write_text(json.dumps({
                        'probe': str(native / 'probe'), 'dependencyCacheValues': dependencies}))
                    return 0
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
                if name.endswith('/native-orca-checks'):
                    self.assertEqual(operation()['returncode'], 0)
                return name.endswith(('/build-tests', '/native-orca-checks'))
            with patch('native_regression.phase', side_effect=phase), patch('native_regression.command', side_effect=compare), \
                 patch('native_regression.cache_value', side_effect=[str(source / 'producer'), 'Release']), \
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
                "import sys\nfrom pathlib import Path\n"
                "def cache_values(path):\n raise AssertionError('POSIX compile must not resolve DLLs')\n"
                f"def main(argv):\n run([sys.executable,'-c',{worker!r}],Path({str(root/'native.log')!r}),60)\n")
            with self.assertRaises(TimeoutError):
                command([sys.executable, Path(__file__).resolve().parents[2]/'scripts/ci/native_child.py',
                         'native', '--source', root], root, None, root/'wrapper.log', timeout=1)
            pid = int((root/'pid').read_text())
            self.assert_stopped(pid)



if __name__ == '__main__':
    unittest.main()
