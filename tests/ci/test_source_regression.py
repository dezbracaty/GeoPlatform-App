import base64
"""MR controller boundaries; no platform build or Journal success is mocked as acceptance."""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import sys
import subprocess
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts/ci'))
import source_regression as controller


class SourceMRRegression(unittest.TestCase):
    def test_regression_build_keeps_full_log_locally_and_prints_only_failure_tail(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            for exit_code in (0, 7):
                console = io.StringIO()
                with self.subTest(exit_code=exit_code), contextlib.redirect_stdout(console):
                    def execute():
                        with controller.build_log(output, 'sdk-build') as log:
                            controller.run([sys.executable, '-c',
                                f'print("early output"); print("x" * 5000); print("last output"); raise SystemExit({exit_code})'],
                                log=log)
                    if exit_code:
                        with self.assertRaisesRegex(RuntimeError, 'exit 7'):
                            execute()
                    else:
                        execute()
                    self.assertIn('early output', (output / 'sdk-build.log').read_text())
                    self.assertNotIn('early output', console.getvalue())
                    self.assertEqual('last output' in console.getvalue(), bool(exit_code))

    def test_freezes_platform_branches_without_resolving_published_sdk(self):
        class API:
            def __init__(self): self.calls = []
            def request(self, method, path):
                self.calls.append((method, path))
                responses = {
                    '/repos/org/App/commits/' + 'b'*40: {'sha':'b'*40},
                    '/repos/org/Host/commits/' + 'a'*40: {'sha':'a'*40},
                    '/repos/org/Host/contents/App?ref=' + 'a'*40:
                        {'sha':'b'*40, 'submodule_git_url':'https://github.com/org/App.git'},
                    '/repos/org/App/contents/tests/TestAssets?ref=' + 'b'*40:
                        {'sha':'c'*40, 'submodule_git_url':'https://github.com/org/TestAssets.git'},
                    '/repos/org/App/contents/cmake/sdk-repository.json?ref=' + 'b'*40:
                        {'type':'file','encoding':'base64','content':base64.b64encode(b'{"repository":"org/SDK"}').decode()},
                    '/repos/org/TestAssets/branches/macmini': {'commit':{'sha':'d'*40}},
                    '/repos/org/TestAssets/branches/windows': {'commit':{'sha':'e'*40}},
                }
                return 200, responses[path]
        api = API()
        result = controller.prepare(api, 'org/App', 'b'*40, 'org/Host', 'a'*40)
        self.assertEqual(result, {'source_sha':'a'*40, 'app_sha':'b'*40,
                                 'declared_app_sha':'b'*40, 'app_repository':'org/App', 'host_repository':'org/Host',
                                 'macos_assets_sha':'d'*40, 'windows_assets_sha':'e'*40, 'sdk_repository':'org/SDK'})
        self.assertEqual(len(api.calls), 7)
        self.assertFalse(any('/repos/org/SDK/' in path for _, path in api.calls))

    def test_sdk_producer_uses_both_official_mac_presets_and_one_package(self):
        helper = controller.build_sdk_package.__globals__
        with patch.dict(helper, run=lambda args, **kw: calls.append(args)):
            calls=[]
            package=controller.build_sdk_package(Path('/fixed/host'), 'macos', Path('/owned/package'), {})
        presets=[c[c.index('--preset')+1] for c in calls if '--preset' in c]
        self.assertEqual(presets, ['arm_release','arm_release','x86_release','x86_release'])
        self.assertEqual(package, Path('/owned/package').resolve())
        self.assertEqual(sum('gplatform_sdk_artifacts' in c for c in calls), 2)
        exports=[c for c in calls if '--export' in c]
        self.assertEqual(len(exports),2)
        self.assertTrue(all(c[-1] == package for c in exports))

    def test_source_mode_rejects_sdk_refs_before_any_api_access(self):
        class API:
            def request(self, *args): raise AssertionError('Must reject invalid inputs before API')
        with self.assertRaisesRegex(ValueError, 'forbidden'):
            controller.prepare(API(), 'org/App', 'b'*40, 'org/Host', 'a'*40, windows_sdk_ref='windows')

    def test_published_freezes_only_selected_platform_sdk_and_asset_shas(self):
        class API:
            def __init__(self): self.calls=[]
            def request(self, method, path):
                self.calls.append(path)
                if path.endswith('/commits/'+'a'*40): result={'sha':'a'*40}
                elif path.endswith('/commits/'+'b'*40): result={'sha':'b'*40}
                elif '/contents/App?' in path: result={'sha':'b'*40,'submodule_git_url':'https://github.com/org/App.git'}
                elif '/contents/tests/' in path: result={'sha':'c'*40,'submodule_git_url':'https://github.com/org/Assets.git'}
                elif '/contents/cmake/sdk-repository.json?' in path: result={'type':'file','encoding':'base64','content':base64.b64encode(b'{"repository":"org/SDK"}').decode()}
                elif path.endswith('/branches/windows'): result={'commit':{'sha':'d'*40}}
                elif path.endswith('/commits/'+'e'*40): result={'sha':'e'*40}
                else: raise AssertionError(path)
                return 200,result
        api=API()
        result=controller.prepare(api,'org/App','b'*40,'org/Host','a'*40,platform='windows',sdk_source='published',windows_sdk_ref='e'*40)
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
                    controller.regress(root, 'macos', 'a'*40, 'd'*40, output, app_sha='b'*40, app_repository='org/App')
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
                    controller.regress(root, 'macos', 'a'*40, 'd'*40, root/'evidence', app_sha='b'*40, app_repository='org/App')

    def pipeline(self, root, output, *, compiler_error=False, receipt_assets='d'*40):
        executable=root/'consumer-app'; executable.write_bytes(b'actual fixture executable')
        def run(args, **kw):
            if '-c' in args and args[2] == controller.APP_CONSUMER:
                payload=json.loads(args[3])
                self.assertEqual(Path(payload['ci_dir']), Path(controller.__file__).resolve().parent)
                self.assertEqual(payload['producer_root_sha'],'a'*40)
                self.assertEqual(payload['producer_app_sha'],'b'*40)
                (output/'consumer-receipt.json').write_text(json.dumps({'executable':str(executable),'sdk_root':payload['sdk_root']}))
            if '-c' in args and args[2] == controller.APP_REGRESSION:
                self.assertEqual(Path(json.loads(args[3])['ci_dir']), Path(controller.__file__).resolve().parent)
                # No duplicate Authorization headers leak between the two common modules.
                self.assertEqual(kw['env'].get('GIT_CONFIG_COUNT'),'1')
                directory=output/'journals/run/session_fixture';directory.mkdir(parents=True)
                summary=directory/'summary.json'
                summary.write_text(json.dumps({'status':'passed','runComplete':True,'selectionScope':'all',
                    'plannedCases':['one','two'],'completedCaseCount':2,
                    'cases':[{'id':'one','status':'passed'},{'id':'two','status':'passed'}]}))
                (directory/'one').mkdir()
                (directory/'one/temporary.gcode').write_bytes(b'generated output')
                (output/'journal-receipt.json').write_text(json.dumps({'status':'passed','assets_sha':receipt_assets,
                    'expected_cases':['one','two'],'summary_path':str(summary)}))
        def produce(source, platform, package, env, *, log):
            if compiler_error: raise RuntimeError('compiler failed')
            self.assertEqual(package.parent, root.resolve())
            self.assertEqual(package.name, 'regression-sdk')
            (package/'package-manifest.json').write_text('{}')
            for slot in ('arm64/Release', 'x86_64/Release'):
                directory = package / 'SDK' / slot
                directory.mkdir(parents=True)
                for name in ('sdk-build.json', 'sdk-manifest.json'):
                    (directory / name).write_text('{}')
        with contextlib.ExitStack() as stack:
            stack.enter_context(patch.dict(os.environ, {'GIT_CONFIG_COUNT':'1','GIT_CONFIG_KEY_0':'core.eol','GIT_CONFIG_VALUE_0':'lf',
                'RUNNER_TEMP':str(root)}))
            stack.enter_context(patch.object(controller,'machine_lock',return_value=contextlib.nullcontext()))
            stack.enter_context(patch.object(controller,'prepare_cached_source'))
            stack.enter_context(patch.object(controller,'init_host_submodules'))
            stack.enter_context(patch.object(controller,'text_git',return_value='b'*40))
            stack.enter_context(patch.object(controller,'run',side_effect=run))
            stack.enter_context(patch.object(controller.native_checks,'build_environment',return_value={}))
            native=stack.enter_context(patch.object(controller.native_checks,'architecture',return_value={'returncode':0}))
            build=stack.enter_context(patch.object(controller,'build_sdk_package',side_effect=produce))
            publish=stack.enter_context(patch.object(controller,'update_sdk',return_value={'published':False,'sdk_sha':'',
                'selected_slots':['arm64/Release','x86_64/Release']}))
            def check_after_journal(ci_root, build, report):
                self.assertTrue((output/'journal-receipt.json').exists())
                self.assertEqual(report['final_counts'], {'passed': 2})
            checks=stack.enter_context(patch.object(controller, 'check_ci_tools', side_effect=check_after_journal))
            result=controller.regress(root,'macos','a'*40,'d'*40,output,app_sha='b'*40,app_repository='org/App',sdk_repository='org/SDK')
            self.assertEqual(build.call_count,1)
            checks.assert_called_once()
            self.assertEqual(publish.call_args.kwargs,{'app_sha':'b'*40,'publish':False})
            self.assertEqual([c.args[1] for c in native.call_args_list],['arm64','x86_64'])
            self.assertTrue(all(not c.kwargs for c in native.call_args_list))
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
            self.assertEqual({file.relative_to(output).as_posix() for file in output.rglob('*') if file.is_file()},
                             {'source-regression.json', 'summary.json', 'journals/run/session_fixture/summary.json'})

    def test_failed_journal_keeps_non_screenshot_evidence_and_incomplete_runs(self):
        for complete in (True, False):
            with self.subTest(complete=complete), tempfile.TemporaryDirectory() as temporary:
                output = Path(temporary).resolve()
                run = output/'journals/run/session_fixture'
                exports = output/'journals/exports/session_fixture'
                for folder in (run, exports):
                    for stage in ('', 'serial-recheck', 'serial-recheck-2'):
                        for case in ('recovered', 'broken'):
                            path = folder/stage/case/'generated.bin'
                            path.parent.mkdir(parents=True)
                            path.write_bytes(b'evidence')
                native = output/'arm64/orca-gcode/results'
                native.parent.mkdir(parents=True)
                native.write_bytes(b'passed native output')
                controller.write_json(output/'source-regression.json', {'status':'failed',
                    'phases':[{'name':'arm64/ctest','status':'passed'}]})
                controller.write_json(run/'summary.json', {'status':'failed', 'stage':'finished',
                    'runComplete':complete, 'plannedCases':['recovered','broken'],'completedCaseCount':2,
                    'cases':[{'id':'recovered','status':'passed'},{'id':'broken','status':'failed'}]})
                controller.compact_regression_report(output)
                controller.compact_regression_report(output)  # Safe to retry.
                self.assertTrue((run/'summary.json').is_file())
                self.assertTrue((output/'source-regression.json').is_file())
                self.assertEqual(native.exists(), not complete)
                for folder in (run, exports):
                    for stage in ('', 'serial-recheck', 'serial-recheck-2'):
                        self.assertEqual((folder/stage/'recovered/generated.bin').exists(), not complete)
                        self.assertTrue((folder/stage/'broken/generated.bin').is_file())

    def screenshot_attempt(self, directory, baseline, *, changed=(), capture_error=None, review_error=False):
        # Exercise the maintained executor and review writer with real images.
        app = Path(__file__).resolve().parents[2]
        sys.path[:0] = [str(app / 'tools'), str(app / 'tools/journal')]
        try:
            from PIL import Image
            from journal_runner import finish_result, JournalRunner, load_journal
            from screenshot_review import collect_screenshots, snapshot_checkpoints
        finally:
            del sys.path[:2]

        directory.mkdir(parents=True)
        (directory / 'screenshots').mkdir()
        Image.new('RGB', (12, 10), 'white').save(baseline)
        mismatch = io.BytesIO()
        Image.new('RGB', (12, 10), 'black').save(mismatch, format='PNG')
        png = baseline.read_bytes()

        class Client:
            def capture_screenshot(self):
                return {'png': mismatch.getvalue() if 1 in changed else png}

            def execute_entry(self, entry):
                path = Path(entry['payload']['params']['filepath'])
                if capture_error != 'before':
                    path.write_bytes(mismatch.getvalue() if 2 in changed else png)
                if capture_error:
                    raise RuntimeError('Capture dispatch failed')

        journal = baseline.with_name('Journal.json')
        controller.write_json(journal, dict(formatVersion=2, entries=[
            dict(atMs=0, kind='verify.screenshot', payload=dict(baseline=baseline.name)),
            dict(atMs=0, kind='action', payload=dict(code='screenshot.capture',
                 params=dict(filepath='captures/customer-view.png'), baseline=baseline.name)),
        ]))
        document = load_journal(journal)
        execution = finish_result(JournalRunner(Client()).run(document, directory))
        checkpoints = snapshot_checkpoints(dict(document=document, journal=journal), baseline.parent, verify_hashes=False)
        if review_error:
            # Fail only this review copy and let the producer report the checkpoint.
            (directory / 'review/checkpoint-0002/expected.png').mkdir(parents=True)
        review = collect_screenshots(checkpoints, execution, directory)
        controller.write_json(directory / 'case_result.json', dict(
            id=directory.name, status='failed', executionResultPath='execution_result.json', screenshotReview=review))
        (directory / 'application.log').write_text('app diagnostic', encoding='utf-8')
        return execution, checkpoints

    def screenshot_files(self, directory):
        return {path.relative_to(directory).as_posix() for path in directory.rglob('*.png') if path.is_file()}

    def test_failed_journal_keeps_only_each_attempts_failed_screenshots(self):
        with tempfile.TemporaryDirectory() as temporary:
            baseline = Path(temporary).resolve() / 'source-standard.png'
            output = baseline.parent / 'regression-results/123-1/macos'
            run = output / 'journals/run/session_fixture'
            attempts = []
            for stage, failed_index in [('initial', 2), ('serial-recheck', 1), ('serial-recheck-2', None)]:
                directory = (run if stage == 'initial' else run / stage) / 'broken'
                execution, checkpoints = self.screenshot_attempt(directory, baseline,
                    changed=() if failed_index is None else (failed_index,))
                self.assertEqual([step['status'] for step in execution['results']],
                                 ['failed' if index == failed_index else 'passed' for index in (1, 2)])
                attempts.append(dict(stage=stage, status='failed', result=
                    (directory / 'case_result.json').relative_to(run).as_posix()))
            business_failure = run / 'serial-recheck-2/broken/case_result.json'
            controller.write_json(business_failure, dict(json.loads(business_failure.read_text(encoding='utf-8')),
                error='Export did not pass its business check'))
            sibling = run.parent / 'session_unrelated/independent.png'
            sibling.parent.mkdir()
            sibling.write_bytes(b'other session')
            other_round = output.parents[1] / '123-2/macos/image.png'
            other_round.parent.mkdir(parents=True)
            other_round.write_bytes(b'other platform attempt')
            user_file = run / 'broken/screenshots/user.png'
            user_file.write_bytes(b'untracked image')
            adjacent_file = run / 'user.png'
            adjacent_file.write_bytes(b'outside the case attempt')
            redirected = run / 'broken/screenshots/redirected.png'
            try:
                redirected.symlink_to(other_round)
            except (NotImplementedError, OSError):
                redirected = None  # Symlink creation can require Windows privileges.
            execution_path = run / 'broken/execution_result.json'
            execution_report = json.loads(execution_path.read_text(encoding='utf-8'))
            for index, path in enumerate((baseline, '../user.png', redirected), 10):
                if path is not None:
                    evidence = path.relative_to(run / 'broken').as_posix() if path == redirected else str(path)
                    execution_report['results'].append(dict(index=index, kind='verify.screenshot',
                        status='passed', evidence=[evidence]))
            controller.write_json(execution_path, execution_report)
            server_log = output / 'journals/regression.log'
            server_log.write_text('batch review diagnostics', encoding='utf-8')
            controller.write_json(output / 'source-regression.json', dict(status='failed'))
            controller.write_json(run / 'summary.json', dict(status='failed', stage='finished',
                runComplete=True, plannedCases=['broken'], completedCaseCount=1,
                cases=[dict(id='broken', status='failed', attempts=attempts, result=attempts[-1]['result'])],
                screenshotReviewError='Unattributed error while writing batch index',
                screenshotCheckpoints={'broken': [{k: v for k, v in row.items() if k != 'baselineBytes'}
                                                   for row in checkpoints]}))
            original_baseline = baseline.read_bytes()
            for _ in range(2):
                controller.compact_regression_report(output)
            for path in (baseline, sibling, other_round, user_file, adjacent_file, server_log, business_failure):
                self.assertTrue(path.is_file(), path)
            self.assertEqual(baseline.read_bytes(), original_baseline)
            for attempt, index in zip(attempts, (2, 1, None)):
                directory = (run / attempt['result']).parent
                expected = set()
                if index:
                    stem = f'screenshots/screenshot_{index:03d}'
                    checkpoint = f'review/checkpoint-{index:04d}'
                    expected.update({f'{stem}_expected.png', f'{stem}_diff.png',
                        f'{checkpoint}/expected.png', f'{checkpoint}/actual.png', f'{checkpoint}/raw-diff.png',
                        f'{stem}_actual.png' if index == 1 else 'captures/customer-view.png'})
                if index == 2:
                    expected.add('screenshots/user.png')
                    if redirected is not None:
                        self.assertTrue(redirected.is_symlink())
                        expected.add('screenshots/redirected.png')
                self.assertEqual(self.screenshot_files(directory), expected)
                for diagnostic in ('case_result.json', 'execution_result.json', 'review/screenshots.json', 'application.log'):
                    self.assertTrue((directory / diagnostic).is_file(), diagnostic)

    def test_attributed_review_or_capture_error_retains_only_an_existing_failed_actual(self):
        for error in ('review', 'batch-index', 'before', 'after'):
            with self.subTest(error=error), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary).resolve()
                output = root / 'report'
                run = output / 'journals/run/session_fixture'
                directory = run / 'scene'
                review_error = error in ('review', 'batch-index')
                execution, _ = self.screenshot_attempt(directory, root / 'baseline.png',
                    capture_error=None if review_error else error, review_error=review_error)
                if review_error:
                    self.assertEqual(execution['results'][1]['status'], 'passed')
                    report = json.loads((directory / 'review/screenshots.json').read_text(encoding='utf-8'))
                    self.assertEqual(report['checkpoints'][1]['comparisonStatus'], 'capture_error')
                    self.assertEqual(report['checkpoints'][0]['comparisonStatus'], 'identical')
                elif error == 'after':
                    report = json.loads((directory / 'case_result.json').read_text(encoding='utf-8'))
                    self.assertEqual(report['screenshotReview']['pendingCount'], 0)
                    self.assertEqual(execution['results'][1]['evidence'], [directory / 'captures/customer-view.png'])
                else:
                    self.assertEqual(execution['results'][1]['evidence'], [])
                controller.write_json(output / 'source-regression.json', dict(status='failed'))
                summary = dict(status='failed', stage='finished', runComplete=True,
                    plannedCases=['scene'], completedCaseCount=1, cases=[dict(id='scene', status='failed')])
                if error == 'batch-index':
                    from screenshot_review import write_batch_report
                    write_batch_report(run, summary)
                    (directory / 'review/screenshots.json').write_text('truncated', encoding='utf-8')
                    summary['screenshotReviewError'] = 'Could not refresh the batch index'
                controller.write_json(run / 'summary.json', summary)
                controller.compact_regression_report(output)
                expected = {'captures/customer-view.png', 'screenshots/screenshot_002_expected.png'}
                if review_error:
                    expected.add('screenshots/screenshot_002_diff.png')
                elif error == 'after':
                    expected.update({'review/checkpoint-0002/expected.png', 'review/checkpoint-0002/actual.png'})
                else:
                    expected.clear()
                self.assertEqual(self.screenshot_files(directory), expected)

    def test_unattributed_batch_review_error_keeps_diagnostics_with_formally_passed_cases(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary).resolve()
            run = output / 'journals/run/session_fixture'
            passing = run / 'scene/screenshots/passed.png'
            passing.parent.mkdir(parents=True)
            passing.write_bytes(b'passing image')
            log = output / 'journals/regression.log'
            log.write_text('diagnostic', encoding='utf-8')
            controller.write_json(output / 'source-regression.json', dict(status='passed'))
            controller.write_json(run / 'summary.json', dict(status='passed', stage='finished',
                runComplete=True, plannedCases=['scene'], completedCaseCount=1,
                cases=[dict(id='scene', status='passed')], screenshotReviewError='Batch index write failure'))
            controller.compact_regression_report(output)
            self.assertFalse(passing.exists())
            self.assertTrue(log.is_file())
            self.assertEqual(json.loads((run / 'summary.json').read_text(encoding='utf-8'))['screenshotReviewError'],
                             'Batch index write failure')

    def test_interrupted_or_incomplete_summaries_leave_evidence(self):
        boundaries = (
            {'stage': 'serial-recheck'}, {'cancelled': True}, {'runComplete': False},
            {'cases': []}, {'screenshotReviewPendingCount': 1},
        )
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            output = root / 'report'
            run = output / 'journals/run/session_fixture'
            directory = run / 'scene'
            self.screenshot_attempt(directory, root / 'baseline.png')
            expected = self.screenshot_files(directory)
            self.assertTrue(expected)
            complete = dict(status='failed', stage='finished', runComplete=True,
                plannedCases=['scene'], completedCaseCount=1, cases=[dict(id='scene', status='failed')])
            controller.write_json(output / 'source-regression.json', dict(status='failed'))
            for changes in boundaries:
                with self.subTest(changes=changes):
                    controller.write_json(run / 'summary.json', dict(complete, **changes))
                    controller.compact_regression_report(output)
                    self.assertEqual(self.screenshot_files(directory), expected)
            controller.write_json(output / 'source-regression.json', dict(status='failed', interrupted=True))
            controller.write_json(run / 'summary.json', complete)
            controller.compact_regression_report(output)
            self.assertEqual(self.screenshot_files(directory), expected)

    def test_different_assets_receipt_cannot_pass(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);output=root/'evidence'
            with self.assertRaisesRegex(RuntimeError,'prepared assets'):
                self.pipeline(root,output,receipt_assets='f'*40)
            self.assertEqual(json.loads((output/'source-regression.json').read_text())['status'],'failed')

    def test_invalid_published_identity_is_rejected_before_any_build(self):
        with tempfile.TemporaryDirectory() as temporary, patch.object(controller,'build_sdk_package') as build:
            with self.assertRaisesRegex(ValueError,'frozen SDK'):
                controller.regress(temporary,'windows','a'*40,'d'*40,Path(temporary)/'output',app_sha='b'*40,app_repository='org/App',sdk_source='published')
            build.assert_not_called()

    def test_ci_tools_receipt_requires_matching_inputs_and_success(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            report = {'control_sha': 'a' * 40}
            receipt = build / 'ci-tools-passed.json'
            with patch.object(controller, 'text_git', return_value=''), \
                 patch.object(controller, 'ci_tools_key', return_value='first') as key, \
                 patch.object(controller, 'run') as checks:
                controller.check_ci_tools(build, build, report)
                self.assertFalse(report['ci_tools']['reused'])
                controller.check_ci_tools(build, build, report)
                self.assertTrue(report['ci_tools']['reused'])
                checks.assert_called_once()

                key.return_value = 'changed'
                checks.side_effect = RuntimeError('tool suite failed')
                with self.assertRaisesRegex(RuntimeError, 'tool suite failed'):
                    controller.check_ci_tools(build, build, report)
                self.assertEqual(report['ci_tools']['status'], 'failed')
                self.assertEqual(json.loads(receipt.read_text())['key'], 'first')

                checks.side_effect = None
                controller.check_ci_tools(build, build, report)
                self.assertFalse(report['ci_tools']['reused'])
                self.assertEqual(json.loads(receipt.read_text())['key'], 'changed')


if __name__ == '__main__': unittest.main()
