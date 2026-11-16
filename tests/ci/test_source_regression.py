"""MR controller boundaries; no platform build or Journal success is mocked as acceptance."""
import contextlib
import importlib.util
import json
import os
from pathlib import Path
import sys
import subprocess
import tempfile
import unittest
from unittest.mock import patch

CONTROL = Path(os.environ['GPLATFORM_CI_CONTROL_ROOT']) if os.environ.get('GPLATFORM_CI_CONTROL_ROOT') else None
controller = None
if CONTROL and (CONTROL / 'scripts/ci/source_regression.py').is_file():
    directory = str(CONTROL / 'scripts/ci')
    saved_path = list(sys.path)
    saved_modules = {name: sys.modules.pop(name, None) for name in ('common', 'dispatch_app_build')}
    try:
        sys.path.insert(0, directory)
        spec = importlib.util.spec_from_file_location('host_source_regression', Path(directory) / 'source_regression.py')
        controller = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(controller)
    finally:
        sys.path[:] = saved_path
        for name, value in saved_modules.items():
            sys.modules.pop(name, None)
            if value is not None:
                sys.modules[name] = value


@unittest.skipUnless(controller, 'Full-host MR controller is not selected for this standalone App check')
class SourceMRRegression(unittest.TestCase):
    def test_freezes_platform_branches_without_reading_or_requiring_sdk(self):
        class API:
            def __init__(self): self.calls = []
            def request(self, method, path):
                self.calls.append((method, path))
                responses = {
                    '/repos/org/Host/contents/App?ref=' + 'a'*40:
                        {'sha':'b'*40, 'submodule_git_url':'https://github.com/org/App.git'},
                    '/repos/org/App/contents/tests/TestAssets?ref=' + 'b'*40:
                        {'sha':'c'*40, 'submodule_git_url':'https://github.com/org/TestAssets.git'},
                    '/repos/org/TestAssets/branches/macmini': {'commit':{'sha':'d'*40}},
                    '/repos/org/TestAssets/branches/windows': {'commit':{'sha':'e'*40}},
                }
                return 200, responses[path]
        api = API()
        result = controller.prepare(api, 'org/Host', 'a'*40, 'org/App')
        self.assertEqual(result, {'source_sha':'a'*40, 'app_sha':'b'*40,
                                 'macos_assets_sha':'d'*40, 'windows_assets_sha':'e'*40})
        self.assertEqual(len(api.calls), 4)
        self.assertFalse(any('SDK' in path for _, path in api.calls))

    def test_build_uses_host_entry_and_both_maintained_mac_presets(self):
        with patch.object(controller, 'run') as run:
            executable = controller.build_host(Path('/fixed/host'), 'macos', {})
        self.assertEqual(executable, Path('/fixed/host/build-arm64-Release/GPlatform/GPlatform.app/Contents/MacOS/GPlatform'))
        commands = [call.args[0] for call in run.call_args_list]
        self.assertEqual([c[c.index('--preset')+1] for c in commands],
                         ['arm_release','arm_release','x86_release','x86_release'])
        self.assertEqual(commands[0][1], Path('/fixed/host/scripts/build.sh'))
        self.assertEqual(commands[1][-2:], ['--target','gplatform_build'])
        self.assertFalse(any('SDK_ROOT' in str(arg) for cmd in commands for arg in cmd))

    def test_complete_inventory_gate_rejects_partial_empty_pending_and_duplicate_results(self):
        valid = {'status':'passed','runComplete':True,'selectionScope':'all',
                 'plannedCases':['one','two'],'completedCaseCount':2,
                 'cases':[{'id':'one','status':'passed'},{'id':'two','status':'passed'}]}
        controller.verify_inventory(valid, ['one','two'])
        invalid = [dict(valid, cases=valid['cases'][:1]), dict(valid, plannedCases=['one']),
                   dict(valid, runComplete=False), dict(valid, candidateCollection=True),
                   dict(valid, selectionScope='selected'), dict(valid, completedCaseCount=0),
                   dict(valid, cases=[{'id':'one','status':'passed'},{'id':'two','status':'pending'}]),
                   dict(valid, cases=[{'id':'one','status':'passed'},{'id':'one','status':'passed'}])]
        for summary in invalid:
            with self.subTest(summary=summary), self.assertRaises(RuntimeError):
                controller.verify_inventory(summary, ['one','two'])
        with self.assertRaises(RuntimeError): controller.verify_inventory(valid, [])

    def test_missing_runtime_dependency_fails_before_build_instead_of_skipping(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary); output=root/'evidence'
            with patch.object(controller, 'run', side_effect=RuntimeError('Pillow missing')), \
                 patch.object(controller, 'build_host') as build:
                with self.assertRaisesRegex(RuntimeError, 'Pillow missing'):
                    controller.regress(root, 'macos', 'a'*40, 'd'*40, output)
            build.assert_not_called()
            self.assertEqual(json.loads((output/'source-regression.json').read_text())['status'],'failed')

    def test_optional_review_dependencies_do_not_block_regression_admission(self):
        blocker = '''
import importlib.abc, sys
class NoOptionalPackages(importlib.abc.MetaPathFinder):
    def find_spec(self, fullname, path=None, target=None):
        if fullname.split('.')[0] in ('numpy', 'jsonschema'):
            raise ModuleNotFoundError(fullname)
sys.meta_path.insert(0, NoOptionalPackages())
'''
        def admission(args, **kwargs):
            subprocess.run([args[0], '-c', blocker + args[2]], check=True, env=kwargs['env'],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with patch.object(controller, 'run', side_effect=admission), \
                 patch.object(controller, 'machine_lock', side_effect=RuntimeError('admission passed')):
                with self.assertRaisesRegex(RuntimeError, 'admission passed'):
                    controller.regress(root, 'macos', 'a'*40, 'd'*40, root/'evidence')

    def test_compile_failure_stops_before_gui_and_retains_failure_evidence(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); output = root/'evidence'
            with patch.object(controller, 'machine_lock', return_value=contextlib.nullcontext()), \
                 patch.object(controller, 'prepare_cached_source'), patch.object(controller, 'init_source_submodules'), \
                 patch.object(controller, 'text_git', return_value='b'*40), \
                 patch.object(controller, 'run') as run, \
                 patch.object(controller, 'build_host', side_effect=RuntimeError('compiler failed')):
                with self.assertRaisesRegex(RuntimeError, 'compiler failed'):
                    controller.regress(root, 'macos', 'a'*40, 'd'*40, output)
            self.assertEqual(run.call_count, 2)  # dependency admission and controller contracts; no GUI execution
            evidence = json.loads((output/'source-regression.json').read_text())
            self.assertEqual(evidence['status'], 'failed')
            self.assertIn('compiler failed', evidence['error'])
            self.assertNotIn('case_count', evidence)

    def test_success_requires_matching_receipt_and_all_inventory_results(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary); output=root/'evidence'; executable=root/'app'; executable.write_bytes(b'executable')
            def run(args, **kwargs):
                if '-c' in args and args[2] == controller.APP_REGRESSION:
                    # App owns its Git credentials. Applying App's real helper
                    # to the child environment must produce exactly one header.
                    spec = importlib.util.spec_from_file_location('app_git_common',
                        Path(__file__).resolve().parents[2] / 'scripts/ci/common.py')
                    common = importlib.util.module_from_spec(spec)
                    spec.loader.exec_module(common)
                    with patch.dict(os.environ, kwargs['env'], clear=True):
                        child_git_env = common.git_env()
                    headers = subprocess.run(['git', 'config', '--get-all',
                        'http.https://github.com/.extraheader'], env=child_git_env,
                        check=True, capture_output=True, text=True).stdout.splitlines()
                    self.assertEqual(len(headers), 1)
                    self.assertEqual(child_git_env['GIT_CONFIG_VALUE_0'], 'lf')
                    self.assertEqual(kwargs['env']['GPLATFORM_SOURCE_TOKEN'], 'test-only-token')
                    (output/'journal-receipt.json').write_text(json.dumps(
                        {'status':'passed','assets_sha':'d'*40,'expected_cases':['one','two']}))
                    directory=output/'journals/run'; directory.mkdir(parents=True)
                    (directory/'summary.json').write_text(json.dumps(
                        {'status':'passed','runComplete':True,'selectionScope':'all',
                         'plannedCases':['one','two'],'completedCaseCount':2,
                         'cases':[{'id':'one','status':'passed'},{'id':'two','status':'passed'}]}))
            with patch.dict(os.environ, {'GPLATFORM_SOURCE_TOKEN':'test-only-token',
                    'GITHUB_ACTIONS':'false', 'GIT_CONFIG_COUNT':'1',
                    'GIT_CONFIG_KEY_0':'core.eol', 'GIT_CONFIG_VALUE_0':'lf'}), \
                 patch.object(controller, 'machine_lock', return_value=contextlib.nullcontext()), \
                 patch.object(controller, 'prepare_cached_source'), patch.object(controller, 'init_source_submodules'), \
                 patch.object(controller, 'text_git', return_value='b'*40), \
                 patch.object(controller, 'run', side_effect=run), \
                 patch.object(controller, 'build_host', return_value=executable):
                result=controller.regress(root, 'macos', 'a'*40, 'd'*40, output)
            self.assertEqual(result['status'],'passed')
            self.assertEqual(result['case_count'],2)
            self.assertEqual(result['application_sha256'],controller.digest(executable))
            self.assertEqual(json.loads((output/'source-regression.json').read_text()),result)

    def test_receipt_for_different_assets_cannot_pass(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary); output=root/'evidence'; executable=root/'app'; executable.write_bytes(b'executable')
            def run(args, **kwargs):
                if '-c' in args and args[2] == controller.APP_REGRESSION:
                    (output/'journal-receipt.json').write_text(json.dumps({'status':'passed','assets_sha':'f'*40}))
            with patch.object(controller, 'machine_lock', return_value=contextlib.nullcontext()), \
                 patch.object(controller, 'prepare_cached_source'), patch.object(controller, 'init_source_submodules'), \
                 patch.object(controller, 'text_git', return_value='b'*40), \
                 patch.object(controller, 'run', side_effect=run), \
                 patch.object(controller, 'build_host', return_value=executable):
                with self.assertRaisesRegex(RuntimeError, 'prepared assets'):
                    controller.regress(root, 'macos', 'a'*40, 'd'*40, output)
            self.assertEqual(json.loads((output/'source-regression.json').read_text())['status'], 'failed')


if __name__ == '__main__': unittest.main()
