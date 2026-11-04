"""Failure-oriented tests of asset contracts, collected artifacts and final gates."""
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from acceptance import digest, final_status, read_json, run_case, write_json


def load_journal(path):
    return read_json(path)


def print_files(directory):
    lines = ['G90', 'M83', '; external perimeters extrusion width = 0.42mm']
    for layer in range(1, 10):
        tool = (layer - 1) // 3 + 1
        lines += [f'T{tool}', 'M109 S210', f';Z:{layer*.2:.1f}', ';TYPE:Outer wall',
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
        result = dict(success=True, acceptance='not_verified', outputPath=str(output_dir),
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
                   entries=[dict(atMs=0, kind='action', payload=dict(code='ui.ai.close', params={}))]))
        self.metadata = dict(schemaVersion=1, id='job', name='Job', testPurpose='Verify nine-layer output',
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

    def test_pass_archives_real_files_and_preserves_original_acceptance(self):
        result = self.run_job(SessionFixture(self.writer))
        self.assertEqual(result['status'], 'passed')
        self.assertEqual(result['journalAcceptance'], 'not_verified')
        self.assertEqual(read_json(self.root/'result/execution_result.json')['acceptance'], 'not_verified')
        self.assertEqual(result['requiredCheckCount'], 1)
        self.assertEqual(result['unverifiedScopes'], ['physical-printing'])
        for artifact in result['artifacts'].values():
            archived = self.root/'result'/artifact['path']
            self.assertTrue(archived.is_file())
            self.assertEqual(digest(archived), artifact['sha256'])

    def test_wrong_tool_band_fails_even_when_every_step_completes(self):
        self.metadata['expected']['physicalToolBands'][0]['tool'] = 2
        self.save_metadata()
        self.assertEqual(self.run_job(SessionFixture(self.writer))['status'], 'failed')
        check = read_json(self.root/'result/checks.json')[0]
        self.assertIn('expected T2', check['message'])

    def test_old_valid_artifacts_cannot_pass(self):
        self.writer()
        for p in self.exports.iterdir():
            os.utime(p, ns=(1_000_000_000, 1_000_000_000))
        result = self.run_job(SessionFixture())
        self.assertEqual(result['status'], 'failed')
        self.assertIn('Stale artifact', result['error'])

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
        self.assertEqual(read_json(self.root/'result/checks.json')[0]['status'], 'not_run')
        self.assertEqual(final_status(dict(success=True, acceptance='not_verified'), [dict(required=True, status='not_run')]), 'not_verified')

    def test_incomplete_replay_cannot_pass(self):
        result = self.run_job(SessionFixture(self.writer, dict(completedEntries=0)))
        self.assertEqual(result['status'], 'failed')
        self.assertIn('did not complete every step', result['error'])

    def test_no_checks_is_unverified(self):
        for field in ['checks', 'artifacts', 'expected']:
            self.metadata.pop(field)
        self.save_metadata()
        self.assertEqual(self.run_job(SessionFixture())['status'], 'not_verified')

    def test_existing_output_directory_is_never_overwritten(self):
        (self.root/'result').mkdir()
        with self.assertRaises(FileExistsError):
            self.run_job(SessionFixture())


if __name__ == '__main__':
    unittest.main()
