"""Failure-oriented tests of asset contracts, collected artifacts and final gates."""
import hashlib
import base64
import json
import math
import os
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from acceptance import digest, final_status, read_json, run_case, write_json, materialize_journal
from acceptance_checks import verify_project_gcode, project_arc_points


def load_journal(path):
    return read_json(path)


def print_files(directory):
    lines = ['G90', 'M83', '; external perimeters extrusion width = 0.42mm']
    for layer in range(1, 10):
        tool = (layer - 1) // 3 + 1
        lines += [f'T{tool}', 'M109 S210', f';Z:{layer*.2:.1f}', f'G1 Z{layer*.2:.1f}', ';TYPE:Outer wall',
                  'G1 X.21 Y.21', 'G1 X9.79 E.1', 'G1 Y9.79 E.1', 'G1 X.21 E.1', 'G1 Y.21 E.1']
    raw = ('\n'.join(lines) + '\n').encode()
    config = dict(printer_settings_id='P3 0.4 nozzle', single_extruder_multi_material=0,
                  filament_map=[1, 2, 3], physical_extruder_map=[1, 2, 3], filament_type=['PLA']*3, filament_diameter=['1.75']*3)
    (directory / 'job.gcode').write_bytes(raw)
    with zipfile.ZipFile(directory / 'job.gcode.3mf', 'w') as archive:
        archive.writestr('Metadata/plate_1.gcode', raw)
        archive.writestr('Metadata/plate_1.gcode.md5', hashlib.md5(raw).hexdigest())
        archive.writestr('Metadata/project_settings.config', json.dumps(config))


class SessionFixture:
    def __init__(self, writer=None, overrides=None):
        self.writer, self.overrides, self.calls = writer, overrides or {}, 0

    def run(self, journal, *, application, output_dir, startup_timeout):
        self.calls += 1
        output_dir.mkdir(parents=True)
        if self.writer:
            self.writer()
        entries = read_json(journal)['entries']
        result = dict(success=True, acceptance='passed' if any(e['kind'].startswith('verify.') for e in entries) else 'not_verified',
                      results=[dict(index=i, kind=e['kind'], status='passed') for i,e in enumerate(entries,1)], outputPath=str(output_dir),
                      completedEntries=len(read_json(journal)['entries']), journalSha256=digest(journal))
        result.update(self.overrides)
        write_json(output_dir / 'execution_result.json', result)
        return result


class AcceptanceCheck(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.assets = self.root / 'assets'
        self.scenario = self.assets / 'scenarios/job'
        (self.scenario / 'events').mkdir(parents=True)
        self.exports = self.root / 'exports'
        self.exports.mkdir()
        self.application = self.root / 'test-program.exe'
        self.application.write_bytes(b'Unit-test fixture; never executed')
        write_json(self.assets / 'catalog.json', dict(schemaVersion=1, scenarioRoot='scenarios', modelCatalogs=[]))
        write_json(self.scenario / 'events/Journal.json', dict(formatVersion=2,
                   entries=[dict(atMs=0, kind='action', payload=dict(code='ui.ai.close', params={})),
                            dict(atMs=1, kind='verify.db', payload=dict(dbInstanceId='42',
                                 serializationFormat='transdb.cereal-portable-binary.v1', expectedSerialized='ZGI='))]))
        self.metadata = dict(schemaVersion=3, id='job', name='Job', testPurpose='Verify nine-layer output',
            verification=dict(requiredMethods=['db']), acceptanceTarget='Expected DB state',
            scope='software-print-export', unverifiedScopes=['physical-printing'], inputs=[],
            artifacts={k: dict(location='exports', fileName=v) for k, v in
                       [('gcode', 'job.gcode'), ('package', 'job.gcode.3mf')]},
            checks=[dict(id='print-job', validator='gcode-job', required=True, artifacts=['gcode', 'package'])],
            expected=dict(cubeDimensionsMm=[10, 10, 1.8], layerHeightMm=.2, layerCount=9, filament='PLA',
                          physicalToolBands=[dict(minHeightMm=a, maxHeightMm=b, tool=t)
                                             for a, b, t in [(0, .6, 1), (.6, 1.2, 2), (1.2, 1.8, 3)]]))
        self.save_metadata()

    def save_metadata(self):
        write_json(self.scenario / 'metadata.json', self.metadata)

    def run_job(self, session):
        return run_case(self.assets, 'job', self.application, self.root/'result', self.exports, session, load_journal)

    def writer(self):
        print_files(self.exports)

    def test_cli_shared_session_keeps_business_checks_and_restores_settings(self):
        import run_acceptance
        from unittest.mock import patch
        settings = self.root / 'GPlatform.ini'
        settings.write_bytes(b'original settings')
        def writer():
            self.writer()
            settings.write_bytes(b'changed by application')
        session = SessionFixture(writer)
        # The integration can be run from a standalone checkout with explicit tools.
        tools = Path(os.environ.get('JOURNAL_TOOLS', str(Path(__file__).resolve().parents[2] / 'ThirdParty/JournalSystem/tools')))
        if not (tools / 'journal_execution_session.py').is_file():
            self.skipTest('requires maintained Journal tools through JOURNAL_TOOLS')
        output = self.root / 'cli-result'
        with patch('builtins.print'):
            code = run_acceptance.main([
                '--assets', str(self.assets), '--journal-tools', str(tools),
                '--application', str(self.application), '--cases', 'job',
                '--exports', str(self.exports), '--output', str(output),
                '--settings-file', str(settings)], session=session)
        self.assertEqual(code, 0)
        self.assertEqual(session.calls, 1)
        self.assertEqual(settings.read_bytes(), b'original settings')
        self.assertEqual(read_json(output / 'job/case_result.json')['status'], 'passed')
        self.assertTrue(read_json(output / 'summary.json')['settingsRestored'])

    def test_batch_cancellation_stops_before_next_case_and_keeps_results(self):
        import run_acceptance
        import shutil
        import threading
        from unittest.mock import patch
        self.metadata['artifacts'] = {}
        self.metadata['checks'] = []
        self.save_metadata()
        clone = self.assets / 'scenarios/job2'
        shutil.copytree(self.scenario, clone)
        copied = dict(self.metadata, id='job2', name='Second case')
        write_json(clone / 'metadata.json', copied)
        cancelled = threading.Event()
        session = SessionFixture(writer=cancelled.set)
        session.cancel_event = cancelled
        output = self.root / 'cancelled-batch'
        tools = Path(__file__).resolve().parents[2] / 'ThirdParty/JournalSystem/tools'
        events = []
        with patch('builtins.print'):
            code = run_acceptance.main(['--assets', str(self.assets), '--journal-tools', str(tools),
                '--application', str(self.application), '--cases', 'all', '--output', str(output)],
                session=session, progress=events.append)
        self.assertEqual(code, 1)
        self.assertEqual(session.calls, 1)
        summary = read_json(output / 'summary.json')
        self.assertTrue(summary['cancelled'])
        self.assertEqual(summary['completedCaseCount'], 1)
        self.assertEqual(summary['notRunCases'], ['job2'])
        self.assertEqual([e['phase'] for e in events], ['begin', 'end'])
        manifest = read_json(output / 'screenshot_review.json')
        self.assertEqual([c['status'] for c in manifest['cases']], ['passed', 'not_run'])

    def test_batch_prepares_each_theme_and_restores_original_settings(self):
        import configparser
        import run_acceptance
        import shutil
        from unittest.mock import patch
        self.metadata.update(artifacts={}, checks=[], preconditions={'theme': 'light'})
        self.save_metadata()
        clone = self.assets / 'scenarios/job2'
        shutil.copytree(self.scenario, clone)
        write_json(clone / 'metadata.json', dict(self.metadata, id='job2', preconditions={'theme': 'dark'}))
        settings = self.root / 'GPlatform.ini'
        original = b'; preserve exact original bytes\r\n[General]\r\ndarkMode=2\r\nlocale=zh_CN\r\n[importFile]\r\nsortKey=name\r\n'
        settings.write_bytes(original)
        observed = []
        def writer():
            config = configparser.ConfigParser()
            config.read(settings)
            observed.append(config['General']['darkMode'])
            self.assertEqual(config['General']['locale'], 'zh_CN')
            self.assertEqual(config['importFile']['sortKey'], 'name')
            settings.write_bytes(b'changed by application')
        tools = Path(__file__).resolve().parents[2] / 'ThirdParty/JournalSystem/tools'
        with patch('builtins.print'):
            code = run_acceptance.main(['--assets', str(self.assets), '--journal-tools', str(tools),
                '--application', str(self.application), '--cases', 'all',
                '--settings-file', str(settings), '--output', str(self.root / 'themed-batch')],
                session=SessionFixture(writer))
        self.assertEqual(code, 0)
        self.assertEqual(observed, ['0', '1'])
        self.assertEqual(settings.read_bytes(), original)

    def test_failed_replay_removes_settings_created_only_for_the_test(self):
        import run_acceptance
        from unittest.mock import patch
        self.metadata.update(artifacts={}, checks=[], preconditions={'theme': 'light'})
        self.save_metadata()
        settings = self.root / 'new-profile/GPlatform.ini'
        def writer():
            self.assertIn('darkMode = 0', settings.read_text())
            raise RuntimeError('application startup failed')
        tools = Path(__file__).resolve().parents[2] / 'ThirdParty/JournalSystem/tools'
        with patch('builtins.print'):
            code = run_acceptance.main(['--assets', str(self.assets), '--journal-tools', str(tools),
                '--application', str(self.application), '--cases', 'job',
                '--settings-file', str(settings), '--output', str(self.root / 'failed-theme')],
                session=SessionFixture(writer))
        self.assertEqual(code, 1)
        self.assertFalse(settings.exists())

    def test_pass_archives_real_files_and_preserves_original_acceptance(self):
        result = self.run_job(SessionFixture(self.writer))
        self.assertEqual(result['status'], 'passed')
        self.assertEqual(result['journalAcceptance'], 'passed')
        self.assertEqual(read_json(self.root/'result/execution_result.json')['acceptance'], 'passed')
        self.assertEqual(result['requiredCheckCount'], 2)
        self.assertEqual({m['method']: m['status'] for m in result['methodResults']},
                         dict(db='passed'))
        self.assertEqual(result['unverifiedScopes'], ['physical-printing'])
        for artifact in result['artifacts'].values():
            archived = self.root/'result'/artifact['path']
            self.assertTrue(archived.is_file())
            self.assertEqual(digest(archived), artifact['sha256'])

    def test_batch_exports_with_identical_names_are_isolated_and_archived(self):
        import run_acceptance
        import shutil
        from unittest.mock import patch
        document = read_json(self.scenario / 'events/Journal.json')
        document['entries'].insert(0, dict(atMs=0, kind='input.key', payload=dict(
            event='press', key=0, target='fileBrowserLocation', text='${exports}')))
        write_json(self.scenario / 'events/Journal.json', document)
        clone = self.assets / 'scenarios/job2'
        shutil.copytree(self.scenario, clone)
        write_json(clone / 'metadata.json', dict(self.metadata, id='job2', name='Second case'))
        destinations = []

        class ExportSession(SessionFixture):
            def run(inner, journal, **kwargs):
                destination = Path(read_json(journal)['entries'][0]['payload']['text'])
                destinations.append(destination)
                inner.writer = lambda: print_files(destination)
                return super().run(journal, **kwargs)

        tools = Path(__file__).resolve().parents[2] / 'ThirdParty/JournalSystem/tools'
        output = self.root / 'batch-exports'
        with patch('builtins.print'):
            code = run_acceptance.main(['--assets', str(self.assets), '--journal-tools', str(tools),
                '--application', str(self.application), '--cases', 'all', '--output', str(output),
                '--exports', str(self.exports)], session=ExportSession())
        self.assertEqual(code, 0)
        self.assertEqual(len(set(destinations)), 2)
        self.assertFalse((self.exports / 'job.gcode').exists())
        for name in ['job', 'job2']:
            result = read_json(output / name / 'case_result.json')
            self.assertEqual(result['status'], 'passed')
            archived = output / name / result['artifacts']['gcode']['path']
            self.assertEqual(archived.read_bytes(), (self.exports / output.name / name / 'job.gcode').read_bytes())

    def test_excluded_batch_is_not_labeled_as_complete_asset_selection(self):
        import run_acceptance
        import shutil
        from unittest.mock import patch
        self.metadata.update(artifacts={}, checks=[])
        self.save_metadata()
        clone = self.assets / 'scenarios/job2'
        shutil.copytree(self.scenario, clone)
        write_json(clone / 'metadata.json', dict(self.metadata, id='job2', name='Second case'))
        tools = Path(__file__).resolve().parents[2] / 'ThirdParty/JournalSystem/tools'
        output = self.root / 'filtered-batch'
        with patch('builtins.print'):
            code = run_acceptance.main(['--assets', str(self.assets), '--journal-tools', str(tools),
                '--application', str(self.application), '--cases', 'all', '--exclude', 'job2',
                '--output', str(output)], session=SessionFixture())
        self.assertEqual(code, 0)
        summary = read_json(output / 'summary.json')
        self.assertEqual(summary['selectionScope'], 'explicit')
        self.assertEqual(summary['plannedCases'], ['job'])

    def test_wrong_tool_band_fails_even_when_every_step_completes(self):
        self.metadata['expected']['physicalToolBands'][0]['tool'] = 2
        self.save_metadata()
        self.assertEqual(self.run_job(SessionFixture(self.writer))['status'], 'failed')
        check = next(c for c in read_json(self.root/'result/checks.json') if c['validator']=='gcode-job')
        self.assertIn('expected T2', check['message'])

    def test_old_valid_artifacts_cannot_pass(self):
        self.writer()
        for p in self.exports.iterdir():
            os.utime(p, ns=(1_000_000_000, 1_000_000_000))
        result = self.run_job(SessionFixture())
        self.assertEqual(result['status'], 'blocked')
        self.assertIn('destination already exists', result['error'])

    def test_missing_artifacts_fail_after_successful_replay(self):
        result = self.run_job(SessionFixture())
        self.assertEqual(result['status'], 'failed')
        self.assertIn('Missing exported', result['error'])

    def test_corrupt_package_fails(self):
        def writer():
            self.writer()
            (self.exports/'job.gcode.3mf').write_bytes(b'invalid zip')
        self.assertEqual(self.run_job(SessionFixture(writer))['status'], 'failed')

    def test_unknown_validator_blocks_before_start(self):
        self.metadata['checks'][0]['validator'] = 'run-arbitrary-command'
        self.save_metadata()
        session = SessionFixture(self.writer)
        self.assertEqual(self.run_job(session)['status'], 'blocked')
        self.assertEqual(session.calls, 0)

    def test_malformed_expected_geometry_blocks_before_start(self):
        self.metadata['expected']['layerHeightMm'] = 0
        self.save_metadata()
        session = SessionFixture()
        self.assertEqual(self.run_job(session)['status'], 'blocked')
        self.assertEqual(session.calls, 0)

    def test_unsupported_expectation_is_never_silently_ignored(self):
        self.metadata['expected']['unimplementedRequirement'] = True
        self.save_metadata()
        session = SessionFixture()
        self.assertEqual(self.run_job(session)['status'], 'blocked')
        self.assertEqual(session.calls, 0)

    def test_declared_mmu_mode_must_match_serialized_mode(self):
        self.metadata['expected']['singleExtruderMultiMaterial'] = True
        self.save_metadata()
        self.assertEqual(self.run_job(SessionFixture(self.writer))['status'], 'failed')

    def test_declared_filament_diameter_must_match_package(self):
        self.metadata['expected']['filamentDiameterMm'] = 2.85
        self.save_metadata()
        self.assertEqual(self.run_job(SessionFixture(self.writer))['status'], 'failed')

    def test_model_hash_mismatch_blocks_before_start(self):
        model = self.assets/'models/cube.stl'
        model.parent.mkdir()
        model.write_bytes(b'original')
        write_json(self.assets/'models/manifest.json', dict(fixtures=[dict(id='cube', path='cube.stl', byteCount=8, sha256=digest(model))]))
        write_json(self.assets/'catalog.json', dict(schemaVersion=1, scenarioRoot='scenarios', modelCatalogs=[dict(namespace='basic', path='models/manifest.json')]))
        self.metadata['inputs'] = [dict(modelId='basic/cube')]
        self.save_metadata()
        model.write_bytes(b'changed!')
        session = SessionFixture()
        result = self.run_job(session)
        self.assertEqual(result['status'], 'blocked')
        self.assertIn('integrity mismatch', result['error'])
        self.assertEqual(session.calls, 0)

    def test_input_path_escape_blocks_before_start(self):
        write_json(self.assets/'catalog.json', dict(schemaVersion=1, scenarioRoot='scenarios', modelCatalogs=[dict(namespace='x', path='../outside.json')]))
        session = SessionFixture()
        self.assertEqual(self.run_job(session)['status'], 'blocked')
        self.assertEqual(session.calls, 0)

    def test_changed_expectation_during_replay_fails(self):
        def writer():
            self.writer()
            self.metadata['testPurpose'] = 'Modified while replay was running'
            self.save_metadata()
        result = self.run_job(SessionFixture(writer))
        self.assertEqual(result['status'], 'failed')
        self.assertIn('changed during execution', result['error'])

    def test_cleanup_failure_cannot_pass(self):
        result = self.run_job(SessionFixture(self.writer, dict(success=False, acceptance='failed', cleanupErrors=['failed cleanup'])))
        self.assertEqual(result['status'], 'failed')
        self.assertEqual(next(c for c in read_json(self.root/'result/checks.json') if c['validator']=='gcode-job')['status'], 'not_run')
        self.assertEqual(final_status(dict(success=True, acceptance='not_verified'), [dict(required=True, status='not_run')], []), 'not_verified')

    def test_failed_ui_step_reason_is_preserved_in_business_result(self):
        result = self.run_job(SessionFixture(overrides=dict(success=False, acceptance='failed', results=[
            dict(index=2, kind='input.mouse', status='failed', message='UI target not found: removed.control')
        ])))
        self.assertEqual(result['status'], 'failed')
        self.assertIn('removed.control', result['error'])
        self.assertEqual(result['failedStep']['index'], 2)

    def test_incomplete_replay_cannot_pass(self):
        result = self.run_job(SessionFixture(self.writer, dict(completedEntries=0)))
        self.assertEqual(result['status'], 'failed')
        self.assertIn('did not complete every step', result['error'])

    def test_no_checks_is_unverified(self):
        write_json(self.scenario/'events/Journal.json', dict(formatVersion=2, entries=[
            dict(atMs=0, kind='action', payload=dict(code='ui.ai.close', params={}))]))
        for field in ['checks', 'artifacts', 'expected']:
            self.metadata.pop(field)
        self.save_metadata()
        self.assertEqual(self.run_job(SessionFixture())['status'], 'not_verified')

    def test_unknown_method_blocks_before_start(self):
        self.metadata['verification']['requiredMethods'] = ['one-off-job-check']
        self.save_metadata()
        session = SessionFixture()
        self.assertEqual(self.run_job(session)['status'], 'blocked')
        self.assertEqual(session.calls, 0)

    def test_empty_method_plan_blocks_before_start(self):
        self.metadata['verification']['requiredMethods'] = []
        self.save_metadata()
        session = SessionFixture()
        self.assertEqual(self.run_job(session)['status'], 'blocked')
        self.assertEqual(session.calls, 0)

    def test_special_file_checks_cannot_replace_db_acceptance(self):
        write_json(self.scenario/'events/Journal.json', dict(formatVersion=2, entries=[
            dict(atMs=0, kind='action', payload=dict(code='ui.ai.close', params={}))]))
        result = self.run_job(SessionFixture(self.writer))
        self.assertEqual(result['status'], 'not_verified')
        self.assertEqual(result['journalAcceptance'], 'not_verified')
        self.assertEqual(result['verificationPlan']['missingMethods'], ['db'])
        self.assertEqual(result['methodResults'], [dict(method='db', status='not_verified', checkIds=[])])
        self.assertEqual(result['supplementalResults'][0]['status'], 'passed')

    def test_optional_file_check_cannot_replace_db_acceptance(self):
        self.metadata['checks'][0]['required'] = False
        self.save_metadata()
        self.test_special_file_checks_cannot_replace_db_acceptance()

    def state_case(self):
        for field in ['checks', 'artifacts', 'expected']:
            self.metadata.pop(field)
        self.metadata['verification']['requiredMethods'] = ['db']
        self.save_metadata()
        write_json(self.scenario/'events/Journal.json', dict(formatVersion=2, entries=[
            dict(atMs=0, kind='verify.db', payload=dict(dbInstanceId='42',
                 serializationFormat='transdb.cereal-portable-binary.v1', expectedSerialized='ZGI='))]))

    def test_state_method_uses_formal_per_entry_assertion(self):
        self.state_case()
        result = self.run_job(SessionFixture(overrides=dict(acceptance='passed', results=[
            dict(index=1, kind='verify.db', status='passed')
        ])))
        self.assertEqual(result['status'], 'passed')
        self.assertEqual(result['methodResults'], [dict(method='db', status='passed', checkIds=['journal-db'])])

    def test_journal_acceptance_alone_cannot_substitute_for_declared_checks(self):
        self.state_case()
        result = self.run_job(SessionFixture(overrides=dict(acceptance='passed', results=[])))
        self.assertEqual(result['status'], 'not_verified')

    def test_standard_db_file_must_match_the_expected_checkpoint(self):
        self.state_case()
        standard=self.assets/'baselines/db/standard.bin'
        standard.parent.mkdir(parents=True)
        standard.write_bytes(b'db')
        self.metadata['dbBaselines']=[dict(entryIndex=1,file='baselines/db/standard.bin')]
        self.save_metadata()
        result=self.run_job(SessionFixture())
        self.assertEqual(result['status'],'passed')
        self.assertEqual(result['provenance']['inputs']['baselines'],{'baselines/db/standard.bin':digest(standard)})

    def test_changed_standard_db_blocks_before_launch(self):
        self.state_case()
        standard=self.assets/'baselines/db/standard.bin'
        standard.parent.mkdir(parents=True)
        standard.write_bytes(b'wrong DB')
        self.metadata['dbBaselines']=[dict(entryIndex=1,file='baselines/db/standard.bin')]
        self.save_metadata()
        session=SessionFixture()
        self.assertEqual(self.run_job(session)['status'],'blocked')
        self.assertEqual(session.calls,0)

    def test_wrong_assertion_kind_cannot_fulfil_state_method(self):
        self.state_case()
        result = self.run_job(SessionFixture(overrides=dict(acceptance='passed', results=[
            dict(index=1, kind='action', status='passed')
        ])))
        self.assertEqual(result['status'], 'not_verified')

    def test_screenshot_capture_cannot_fulfil_visual_method(self):
        for field in ['checks', 'artifacts', 'expected']:
            self.metadata.pop(field)
        self.metadata['verification']['requiredMethods'] = ['screenshot']
        self.save_metadata()
        write_json(self.scenario/'events/Journal.json', dict(formatVersion=2, entries=[
            dict(atMs=0, kind='action', payload=dict(code='screenshot.capture', params={}))]))
        result = self.run_job(SessionFixture())
        self.assertEqual(result['status'], 'not_verified')
        self.assertEqual(result['verificationPlan']['missingMethods'], ['screenshot'])

    def test_visual_comparison_uses_formal_assertion_and_tracks_baseline(self):
        for field in ['checks', 'artifacts', 'expected']:
            self.metadata.pop(field)
        self.metadata['verification']['requiredMethods'] = ['screenshot']
        self.save_metadata()
        baseline = self.assets/'baselines/approved.png'
        baseline.parent.mkdir()
        baseline.write_bytes(b'Unit-test baseline; not a production image')
        write_json(self.scenario/'events/Journal.json', dict(formatVersion=2, entries=[
            dict(atMs=0, kind='verify.screenshot', payload=dict(baseline='../../../baselines/approved.png', tolerance=.99))]))
        result = self.run_job(SessionFixture(overrides=dict(acceptance='passed', results=[
            dict(index=1, kind='verify.screenshot', status='passed')
        ])))
        self.assertEqual(result['status'], 'passed')
        self.assertEqual(result['provenance']['inputs']['baselines'], {'baselines/approved.png': digest(baseline)})

    def test_failed_state_assertion_is_preserved(self):
        self.state_case()
        result = self.run_job(SessionFixture(overrides=dict(success=False, acceptance='failed', results=[
            dict(index=1, kind='verify.db', status='failed', message='DB state differs')
        ])))
        self.assertEqual(result['status'], 'failed')
        self.assertEqual(result['methodResults'][0]['status'], 'failed')

    def test_skipped_journal_assertion_cannot_pass(self):
        self.state_case()
        result = self.run_job(SessionFixture(overrides=dict(acceptance='not_verified', results=[
            dict(index=1, kind='verify.db', status='skipped')
        ])))
        self.assertEqual(result['status'], 'not_verified')

    def test_capture_only_does_not_fulfil_state_method(self):
        self.state_case()
        write_json(self.scenario/'events/Journal.json', dict(formatVersion=2, entries=[
            dict(atMs=0, kind='capture.db', payload=dict(dbInstanceId='42'))]))
        result = self.run_job(SessionFixture())
        self.assertEqual(result['status'], 'not_verified')
        self.assertEqual(result['verificationPlan']['missingMethods'], ['db'])

    def test_external_visual_baseline_blocks_before_start(self):
        self.state_case()
        self.metadata['verification']['requiredMethods'] = ['screenshot']
        self.save_metadata()
        outside = self.root/'outside.png'
        outside.write_bytes(b'Not an asset')
        write_json(self.scenario/'events/Journal.json', dict(formatVersion=2, entries=[
            dict(atMs=0, kind='verify.screenshot', payload=dict(baseline='../../../../outside.png'))]))
        session = SessionFixture()
        result = self.run_job(session)
        self.assertEqual(result['status'], 'blocked')
        self.assertEqual(session.calls, 0)

    def test_existing_output_directory_is_never_overwritten(self):
        (self.root/'result').mkdir()
        with self.assertRaises(FileExistsError):
            self.run_job(SessionFixture())

    def test_file_browser_location_binding_keeps_filename_separate(self):
        journal = self.scenario / 'events/Journal.json'
        write_json(journal, dict(formatVersion=2, entries=[dict(atMs=n, kind='input.key', payload=dict(
            event='press', key=0, target=target, text=text)) for n, (target, text) in enumerate([
                ('fileBrowserLocation', '${exports}'), ('fileBrowserFileName', 'snapshot.json')])]))
        self.metadata['artifacts'] = dict(snapshot=dict(location='exports', fileName='snapshot.json'))
        self.metadata.pop('checks'); self.metadata.pop('expected'); self.save_metadata()
        result = self.run_job(SessionFixture(lambda: write_json(self.exports / 'snapshot.json', {})))
        self.assertEqual(result['status'], 'not_verified')
        entries = read_json(self.root / 'result/inputs/executed-Journal.json')['entries']
        self.assertEqual(entries[0]['payload']['text'], str(self.exports.resolve()))
        self.assertEqual(entries[1]['payload']['text'], 'snapshot.json')

    def test_picker_and_qt_drop_only_bind_declared_input_files(self):
        journal = self.scenario / 'events/Journal.json'
        encoded = base64.b64encode(b'${inputUri:fixture}\r\n').decode()
        write_json(journal, dict(formatVersion=2, entries=[
            dict(atMs=0, kind='input.key', payload=dict(target='fileBrowserLocation', text='${inputDirectory:fixture}')),
            dict(atMs=1, kind='input.dragDrop', payload=dict(event='drop', mime={'text/uri-list': encoded}))]))
        model = self.root / 'Folder with spaces/模型.3mf'
        case = dict(journal=journal, metadata=dict(artifacts={}), modelPaths={'fixture': model})
        runtime, _ = materialize_journal(case, self.root / 'binding-result', self.exports)
        entries = read_json(runtime)['entries']
        self.assertEqual(entries[0]['payload']['text'], str(model.parent))
        self.assertEqual(base64.b64decode(entries[1]['payload']['mime']['text/uri-list']).decode(), model.as_uri() + '\r\n')
        case['modelPaths'] = {}
        with self.assertRaisesRegex(ValueError, 'Undeclared picker'):
            materialize_journal(case, self.root / 'bad-binding-result', self.exports)

    def test_materialized_picker_keeps_source_baseline_and_session_setup(self):
        journal = self.scenario / 'events/Journal.json'
        reference = self.assets / 'baselines/screenshots/imported.png'
        reference.parent.mkdir(parents=True)
        reference.write_bytes(b'independently-reviewed-reference')
        write_json(journal, dict(formatVersion=2, entries=[
            dict(atMs=0, kind='input.key', payload=dict(target='fileBrowserLocation',
                 text='${inputDirectory:fixture}')),
            dict(atMs=1, kind='verify.screenshot', payload=dict(
                 baseline='../../../baselines/screenshots/imported.png', tolerance=1.0))]))
        metadata = dict(artifacts={}, applicationEnvironment={'GP_LOG_CONSOLE': '1'},
                        preconditions={'devicePixelRatio': 1, 'mainWindowLogicalSize': [1382, 954]})
        original = journal.read_bytes()
        case = dict(journal=journal, metadata=metadata, modelPaths={'fixture': self.root / 'fixture.3mf'})
        runtime, _ = materialize_journal(case, self.root / 'display-binding-result', self.exports)
        resolved = Path(read_json(runtime)['entries'][1]['payload']['baseline'])
        self.assertEqual(resolved, reference.resolve())
        self.assertEqual(resolved.read_bytes(), b'independently-reviewed-reference')
        self.assertEqual(read_json(runtime.parent.parent / 'metadata.json'), metadata)
        self.assertEqual(journal.read_bytes(), original)

    def test_project_gcode_checks_model_bounds_and_packaged_bytes(self):
        self.writer()
        expected = dict(requiredMarkers=[';TYPE:Outer wall'], modelBoundsXYZ=[0, 0, 0, 10, 10, 1.8], minimumLayerCount=9,
                        configurationValues={'printer_settings_id': 'P3 0.4 nozzle'})
        self.assertTrue(verify_project_gcode(self.exports/'job.gcode', self.exports/'job.gcode.3mf', expected)['verified'])
        with self.assertRaisesRegex(ValueError, 'outside plate-local'):
            verify_project_gcode(self.exports/'job.gcode', self.exports/'job.gcode.3mf', dict(expected, modelBoundsXYZ=[200, 0, 0, 210, 10, 1.8]))
        with self.assertRaisesRegex(ValueError, 'Missing source-plate'):
            verify_project_gcode(self.exports/'job.gcode', self.exports/'job.gcode.3mf', dict(expected, requiredMarkers=[';OTHER_PLATE']))
        self.exports.joinpath('job.gcode').write_text(self.exports.joinpath('job.gcode').read_text()+'\n;different\n')
        with self.assertRaisesRegex(ValueError, 'Packaged and standalone'):
            verify_project_gcode(self.exports/'job.gcode', self.exports/'job.gcode.3mf', expected)

    def test_arc_bounds_include_interior_extrema_direction_full_circle_and_helix(self):
        start, end = dict(X=1., Y=0., Z=.2), dict(X=0., Y=1., Z=.4)
        ccw = project_arc_points(start, end, dict(I=-1., J=0.), False)
        cw = project_arc_points(start, end, dict(I=-1., J=0.), True)
        self.assertGreaterEqual(min(p[0] for p in ccw), -1e-12)
        self.assertEqual(min(p[0] for p in cw), -1.)
        self.assertEqual(min(p[1] for p in cw), -1.)
        self.assertTrue(any(abs(p[2] - (.2 + .2 / 3)) < 1e-12 for p in cw))
        circle = project_arc_points(start, start, dict(I=-1.), False)
        self.assertEqual(min(p[0] for p in circle), -1.)
        self.assertEqual(min(p[1] for p in circle), -1.)
        near = lambda angle: dict(X=math.cos(math.radians(angle)), Y=math.sin(math.radians(angle)), Z=.2)
        for first, last, clockwise in [(350,10,False),(10,350,True)]:
            a,b=near(first),near(last)
            arc=project_arc_points(a,b,dict(I=-a['X'],J=-a['Y']),clockwise)
            self.assertEqual(max(p[0] for p in arc),1.)
            self.assertGreater(min(p[0] for p in arc),.98)
        with self.assertRaisesRegex(ValueError, 'center encoding'):
            project_arc_points(start, end, dict(R=1.), False)
        with self.assertRaisesRegex(ValueError, 'radius'):
            project_arc_points(start, dict(end, Y=4.), dict(I=-1.), False)

    def test_project_feature_tags_distinguish_model_from_support_and_custom_paths(self):
        expected = dict(requiredMarkers=[], modelBoundsXYZ=[0,0,0,10,10,.2], minimumLayerCount=1,
                        layerPolicy=dict(initialLayerHeightMm=.2, layerHeightMm=.16))
        for tag in [';TYPE:Outer wall', '; FEATURE: Outer wall', '; FEATURE: Support', '; FEATURE: Custom']:
            raw = ('G90\nM83\nG0 X0 Y0 Z.2\n' + tag +
                   '\nG1 X10 Y0 E1\nG1 X10 Y10 E1\nG1 X0 Y10 E1\n').encode()
            gcode, package = self.exports/'job.gcode', self.exports/'job.gcode.3mf'
            gcode.write_bytes(raw)
            with zipfile.ZipFile(package, 'w') as archive:
                archive.writestr('Metadata/plate_1.gcode', raw)
                archive.writestr('Metadata/plate_1.gcode.md5', hashlib.md5(raw).hexdigest())
                archive.writestr('Metadata/project_settings.config',
                                 json.dumps(dict(initial_layer_print_height='0.2', layer_height='0.16')))
            with self.subTest(tag=tag):
                if 'Outer wall' in tag:
                    self.assertTrue(verify_project_gcode(gcode, package, expected)['verified'])
                else:
                    with self.assertRaisesRegex(ValueError, 'No actual model'):
                        verify_project_gcode(gcode, package, expected)

    def test_native_project_package_requires_its_matching_checksum(self):
        for replacement in [None, '0' * 32]:
            self.writer()
            package=self.exports/'job.gcode.3mf'
            with zipfile.ZipFile(package) as archive:
                members={name:archive.read(name) for name in archive.namelist() if not name.endswith('.md5')}
            if replacement is not None:
                members['Metadata/plate_1.gcode.md5']=replacement.encode()
            with zipfile.ZipFile(package,'w') as archive:
                for name,raw in members.items(): archive.writestr(name,raw)
            with self.assertRaisesRegex(ValueError,'checksum'):
                verify_project_gcode(self.exports/'job.gcode',package,dict(requiredMarkers=[],
                    modelBoundsXYZ=[0,0,0,10,10,1.8],minimumLayerCount=9))

    def test_thin_and_multilayer_outputs_use_independent_source_layer_policy(self):
        def verify(zs, height=.2, first=.2, *, extra='', markers=None):
            raw = ('G90\nM83\nG0 Z0\n;TYPE:Outer wall\n' + ''.join(
                f'G0 X0 Y0 Z{z}\nG1 X10 Y0 E1\nG1 X10 Y10 E1\nG1 X0 Y10 E1\n' for z in zs) + extra).encode()
            self.exports.mkdir(exist_ok=True)
            gcode, package = self.exports/'thin.gcode', self.exports/'thin.gcode.3mf'
            gcode.write_bytes(raw)
            with zipfile.ZipFile(package, 'w') as archive:
                archive.writestr('Metadata/plate_1.gcode', raw)
                archive.writestr('Metadata/plate_1.gcode.md5', hashlib.md5(raw).hexdigest())
                archive.writestr('Metadata/project_settings.config', json.dumps(dict(initial_layer_print_height=str(first), layer_height='0.16')))
            count = 1 + max(0, math.ceil((height - first) / .16 - .5 - 1e-6))
            return verify_project_gcode(gcode, package, dict(requiredMarkers=markers or [], modelBoundsXYZ=[0,0,0,10,10,height],
                minimumLayerCount=count, layerPolicy=dict(initialLayerHeightMm=first, layerHeightMm=.16)))
        result = verify([.2])
        self.assertEqual(result['layerCount'], 1)
        self.assertEqual(result['customGcodeStatus'], 'not-evaluated-no-source-markers')
        self.assertTrue(verify([.25], first=.25)['verified'])
        self.assertTrue(verify([.2,.36,.52,.68,.84], height=.85)['verified'])
        self.assertTrue(verify([.2],height=.28)['verified'])
        self.assertTrue(verify([.2,.36],height=.2802)['verified'])
        self.assertTrue(verify([.2,.36],height=.3)['verified'])
        for height in [.2802,.3]:
            with self.assertRaisesRegex(ValueError,'layer generation'):
                verify([.2],height=height)
        for zs, height, first in [([.2], .2, .25), ([.4], .2, .25), ([.2], .85, .2), ([.2,.36,.52,.68,1.2], .85, .2)]:
            with self.assertRaises(ValueError): verify(zs, height, first)
        with self.assertRaisesRegex(ValueError, 'exactly one'):
            verify([.2,.25])
        with self.assertRaisesRegex(ValueError, 'outside plate-local'):
            verify([.2], extra='G1 X50 Y50 E1\n')
        with self.assertRaisesRegex(ValueError, 'No actual model'):
            verify([])
        with self.assertRaisesRegex(ValueError, 'nonplanar'):
            verify([.2], extra='G1 X0 Y0 Z.25 E1\n')
        with self.assertRaisesRegex(ValueError,'uniform layer spacing'):
            verify([.2,.32,.44,.56,.68,.8],height=.85)
        with self.assertRaisesRegex(ValueError, 'Missing source-plate'):
            verify([.2], markers=[';SOURCE_CUSTOM'])
        verify([.2])
        package=self.exports/'thin.gcode.3mf'
        with zipfile.ZipFile(package) as archive:
            raw=archive.read('Metadata/plate_1.gcode')
        with zipfile.ZipFile(package,'w') as archive:
            archive.writestr('Metadata/plate_1.gcode',raw)
            archive.writestr('Metadata/plate_1.gcode.md5', hashlib.md5(raw).hexdigest())
            archive.writestr('Metadata/project_settings.config','{"initial_layer_print_height":"0.25","layer_height":"0.16"}')
        with self.assertRaisesRegex(ValueError,'Packaged layer height'):
            verify_project_gcode(self.exports/'thin.gcode',package,dict(requiredMarkers=[],modelBoundsXYZ=[0,0,0,10,10,.2],
                minimumLayerCount=1,layerPolicy=dict(initialLayerHeightMm=.2,layerHeightMm=.16)))

    def test_extruding_arc_cannot_hide_outside_source_bounds_at_its_endpoints(self):
        self.exports.mkdir(exist_ok=True)
        raw = b'G90\nM83\nG0 X1 Y0 Z.2\n;TYPE:Outer wall\nG2 X0 Y1 I-1 J0 E1\n'
        gcode, package = self.exports/'arc.gcode', self.exports/'arc.gcode.3mf'
        gcode.write_bytes(raw)
        with zipfile.ZipFile(package, 'w') as archive:
            archive.writestr('Metadata/plate_1.gcode', raw)
            archive.writestr('Metadata/plate_1.gcode.md5', hashlib.md5(raw).hexdigest())
            archive.writestr('Metadata/project_settings.config', '{"initial_layer_print_height":"0.2","layer_height":"0.16"}')
        expected = dict(requiredMarkers=[],modelBoundsXYZ=[0,0,0,1,1,.2],minimumLayerCount=1,
                        layerPolicy=dict(initialLayerHeightMm=.2,layerHeightMm=.16))
        with self.assertRaisesRegex(ValueError, 'outside plate-local'):
            verify_project_gcode(gcode,package,expected)
        expected['modelBoundsXYZ']=[-1,-1,0,1,1,.2]
        self.assertTrue(verify_project_gcode(gcode,package,expected)['verified'])
        def replace_raw(changed):
            gcode.write_bytes(changed)
            with zipfile.ZipFile(package,'w') as archive:
                archive.writestr('Metadata/plate_1.gcode',changed)
                archive.writestr('Metadata/plate_1.gcode.md5', hashlib.md5(changed).hexdigest())
                archive.writestr('Metadata/project_settings.config','{"initial_layer_print_height":"0.2","layer_height":"0.16"}')
        replace_raw(raw.replace(b'G2 X0 Y1',b'G2 X1 Y0'))
        self.assertTrue(verify_project_gcode(gcode,package,expected)['verified'])
        with self.assertRaisesRegex(ValueError,'outside plate-local'):
            verify_project_gcode(gcode,package,dict(expected,modelBoundsXYZ=[0,0,0,1,1,.2]))
        for changed in [raw.replace(b'G2 ',b'G90.1\nG2 '),raw.replace(b'G2 ',b'G18\nG2 '),
                        raw.replace(b'G2 ',b'G19\nG2 '),raw.replace(b'I-1 J0',b'R1'),
                        raw.replace(b'I-1 J0',b'I-1 J0 P2'),raw.replace(b'I-1 J0',b'I-1 J0 K0')]:
            replace_raw(changed)
            with self.assertRaisesRegex(ValueError,'Unsupported project arc'):
                verify_project_gcode(gcode,package,expected)


if __name__ == '__main__':
    unittest.main()
