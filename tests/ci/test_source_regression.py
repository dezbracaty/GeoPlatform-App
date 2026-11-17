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
    saved_modules = {name: sys.modules.pop(name, None) for name in ('common', 'update_sdk', 'regression')}
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
    def test_freezes_platform_branches_without_resolving_published_sdk(self):
        class API:
            def __init__(self): self.calls = []
            def request(self, method, path):
                self.calls.append((method, path))
                responses = {
                    '/repos/org/Host/contents/App?ref=' + 'a'*40:
                        {'sha':'b'*40, 'submodule_git_url':'https://github.com/org/App.git'},
                    '/repos/org/App/contents/tests/TestAssets?ref=' + 'b'*40:
                        {'sha':'c'*40, 'submodule_git_url':'https://github.com/org/TestAssets.git'},
                    '/repos/org/App/contents/ThirdParty/libs/GPlatformSDK?ref=' + 'b'*40:
                        {'sha':'f'*40, 'submodule_git_url':'https://github.com/org/SDK.git'},
                    '/repos/org/TestAssets/branches/macmini': {'commit':{'sha':'d'*40}},
                    '/repos/org/TestAssets/branches/windows': {'commit':{'sha':'e'*40}},
                }
                return 200, responses[path]
        api = API()
        result = controller.prepare(api, 'org/Host', 'a'*40, 'org/App')
        self.assertEqual(result, {'source_sha':'a'*40, 'app_sha':'b'*40,
                                 'macos_assets_sha':'d'*40, 'windows_assets_sha':'e'*40, 'sdk_repository':'org/SDK'})
        self.assertEqual(len(api.calls), 5)
        self.assertFalse(any('/repos/org/SDK/' in path for _, path in api.calls))

    def test_sdk_producer_uses_both_official_mac_presets_and_one_package(self):
        helper = controller.build_sdk_package.__globals__
        with patch.dict(helper, run=lambda args, **kw: calls.append(args)):
            calls=[]
            package=controller.build_sdk_package(Path('/fixed/host'), 'macos', Path('/owned/package'), {})
        presets=[c[c.index('--preset')+1] for c in calls if '--preset' in c]
        self.assertEqual(presets, ['arm_release','arm_release','x86_release','x86_release'])
        self.assertEqual(package, Path('/owned/package'))
        self.assertEqual(sum('gplatform_sdk_artifacts' in c for c in calls), 2)
        exports=[c for c in calls if '--export' in c]
        self.assertEqual(len(exports),2)
        self.assertTrue(all(c[-1] == package for c in exports))

    def test_source_mode_rejects_sdk_refs_before_any_api_access(self):
        class API:
            def request(self, *args): raise AssertionError('Must reject invalid inputs before API')
        with self.assertRaisesRegex(ValueError, 'forbidden'):
            controller.prepare(API(), 'org/Host', 'a'*40, 'org/App', windows_sdk_ref='windows')

    def test_published_freezes_only_selected_platform_sdk_and_asset_shas(self):
        class API:
            def __init__(self): self.calls=[]
            def request(self, method, path):
                self.calls.append(path)
                if '/contents/App?' in path: result={'sha':'b'*40,'submodule_git_url':'https://github.com/org/App.git'}
                elif '/contents/tests/' in path: result={'sha':'c'*40,'submodule_git_url':'https://github.com/org/Assets.git'}
                elif '/contents/ThirdParty/' in path: result={'sha':'f'*40,'submodule_git_url':'https://github.com/org/SDK.git'}
                elif path.endswith('/branches/windows'): result={'commit':{'sha':'d'*40}}
                elif path.endswith('/commits/'+'e'*40): result={'sha':'e'*40}
                else: raise AssertionError(path)
                return 200,result
        api=API()
        result=controller.prepare(api,'org/Host','a'*40,'org/App',platform='windows',sdk_source='published',windows_sdk_ref='e'*40)
        self.assertEqual(result['windows_sdk_sha'],'e'*40)
        self.assertEqual(result['windows_assets_sha'],'d'*40)
        self.assertNotIn('macos_assets_sha',result)
        self.assertNotIn('macos_sdk_sha',result)
        self.assertFalse(any('macmini' in p or '/branches/macos' in p for p in api.calls))

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
                 patch.object(controller, 'build_sdk_package') as build:
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

    def pipeline(self, root, output, *, compiler_error=False, receipt_assets='d'*40):
        executable=root/'consumer-app'; executable.write_bytes(b'actual fixture executable')
        def run(args, **kw):
            if '-c' in args and args[2] == controller.APP_CONSUMER:
                payload=json.loads(args[3])
                self.assertEqual(payload['producer_root_sha'],'a'*40)
                self.assertEqual(payload['producer_app_sha'],'b'*40)
                (output/'consumer-receipt.json').write_text(json.dumps({'executable':str(executable),'sdk_root':payload['sdk_root']}))
            if '-c' in args and args[2] == controller.APP_REGRESSION:
                # No duplicate Authorization headers leak between the two common modules.
                self.assertEqual(kw['env'].get('GIT_CONFIG_COUNT'),'1')
                directory=output/'journals/run/session_fixture';directory.mkdir(parents=True)
                summary=directory/'summary.json'
                summary.write_text(json.dumps({'status':'passed','runComplete':True,'selectionScope':'all',
                    'plannedCases':['one','two'],'completedCaseCount':2,
                    'cases':[{'id':'one','status':'passed'},{'id':'two','status':'passed'}]}))
                (output/'journal-receipt.json').write_text(json.dumps({'status':'passed','assets_sha':receipt_assets,
                    'expected_cases':['one','two'],'summary_path':str(summary)}))
        def produce(source, platform, package, env):
            if compiler_error: raise RuntimeError('compiler failed')
            package.mkdir();(package/'package-manifest.json').write_text('{}')
        with contextlib.ExitStack() as stack:
            stack.enter_context(patch.dict(os.environ, {'GIT_CONFIG_COUNT':'1','GIT_CONFIG_KEY_0':'core.eol','GIT_CONFIG_VALUE_0':'lf'}))
            stack.enter_context(patch.object(controller,'machine_lock',return_value=contextlib.nullcontext()))
            stack.enter_context(patch.object(controller,'prepare_cached_source'))
            stack.enter_context(patch.object(controller,'init_source_submodules'))
            stack.enter_context(patch.object(controller,'text_git',return_value='b'*40))
            stack.enter_context(patch.object(controller,'run',side_effect=run))
            stack.enter_context(patch.object(controller.native_checks,'build_environment',return_value={}))
            native=stack.enter_context(patch.object(controller.native_checks,'architecture',return_value={'returncode':0}))
            build=stack.enter_context(patch.object(controller,'build_sdk_package',side_effect=produce))
            publish=stack.enter_context(patch.object(controller,'update_sdk',return_value={'published':False,'sdk_sha':''}))
            result=controller.regress(root,'macos','a'*40,'d'*40,output,sdk_repository='org/SDK')
            self.assertEqual(build.call_count,1)
            self.assertEqual(publish.call_args.kwargs,{'publish':False})
            self.assertEqual([c.args[1] for c in native.call_args_list],['arm64','x86_64'])
            self.assertTrue(all(c.kwargs['run_journal'] is False for c in native.call_args_list))
            return result

    def test_compile_failure_stops_before_gui_and_retains_failure_evidence(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);output=root/'evidence'
            with self.assertRaisesRegex(RuntimeError,'compiler failed'):
                self.pipeline(root,output,compiler_error=True)
            evidence=json.loads((output/'source-regression.json').read_text())
            self.assertEqual(evidence['status'],'failed')
            self.assertNotIn('case_count',evidence)

    def test_success_binds_source_package_consumer_and_server_inventory(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);output=root/'evidence'
            result=self.pipeline(root,output)
            self.assertEqual(result['status'],'passed')
            self.assertEqual(result['case_count'],2)
            self.assertEqual(result['sdk']['sdk_sha'],'')
            self.assertIn('package_manifest_sha256',result['sdk'])
            self.assertEqual(result['application_sha256'],controller.digest(root/'consumer-app'))

    def test_different_assets_receipt_cannot_pass(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);output=root/'evidence'
            with self.assertRaisesRegex(RuntimeError,'prepared assets'):
                self.pipeline(root,output,receipt_assets='f'*40)
            self.assertEqual(json.loads((output/'source-regression.json').read_text())['status'],'failed')

    def test_invalid_published_identity_is_rejected_before_any_build(self):
        with tempfile.TemporaryDirectory() as temporary, patch.object(controller,'build_sdk_package') as build:
            with self.assertRaisesRegex(ValueError,'frozen SDK'):
                controller.regress(temporary,'windows','a'*40,'d'*40,Path(temporary)/'output',sdk_source='published')
            build.assert_not_called()


if __name__ == '__main__': unittest.main()
