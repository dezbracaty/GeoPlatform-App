"""3MF samples retain their bytes; behavior coverage and formal gates stay explicit."""
import hashlib
import copy
import json
from pathlib import Path
import sys
import unittest
from jsonschema import Draft202012Validator, ValidationError

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from acceptance import final_status
from verification_methods import verification_plan, method_results

ASSETS = Path(__file__).resolve().parent / 'TestAssets'
CORE_SCENARIOS = {
    'three_mf_complete_workflow',
    'three_mf_unified_import_workflow',
    'three_mf_filepicker_geometry',
    'three_mf_geometry_history',
    'three_mf_invalid_index_geometry',
    'three_mf_native_decision_cancel',
    'three_mf_native_decision_yes',
    'three_mf_shared_instances_geometry',
    'three_mf_switch_p3',
    'three_mf_target2_geometry',
    'three_mf_target2_project_geometry',
    'three_mf_ui_choose_geometry',
    'three_mf_ui_replace_cancel',
    'three_mf_unassigned_project',
    'ui_3mf_multiplate_visible',
    'ui_3mf_preview_import_visible',
}


class ProjectDbAssetsTests(unittest.TestCase):
    def test_retained_3mf_scenarios_match_the_metadata_schema(self):
        schema = json.loads((ASSETS / 'schemas/scenario.schema.json').read_text(encoding="utf-8"))
        Draft202012Validator.check_schema(schema)
        validator = Draft202012Validator(schema)
        for name in sorted(CORE_SCENARIOS):
            with self.subTest(case=name):
                metadata = json.loads((ASSETS / 'scenarios' / name / 'metadata.json').read_text(encoding="utf-8"))
                validator.validate(metadata)

    def test_supplemental_artifact_contract_rejects_missing_duplicate_and_obsolete_inputs(self):
        schema = json.loads((ASSETS / 'schemas/scenario.schema.json').read_text(encoding="utf-8"))
        validator = Draft202012Validator(schema)
        metadata = json.loads((ASSETS / 'scenarios/three_mf_complete_workflow/metadata.json').read_text(encoding="utf-8"))
        for artifacts in (['snapshot'], ['gcode1'], ['gcode1', 'gcode1'],
                          ['gcode1', 'package1', 'extra']):
            with self.subTest(artifacts=artifacts):
                invalid = copy.deepcopy(metadata)
                invalid['checks'][0]['artifacts'] = artifacts
                with self.assertRaises(ValidationError):
                    validator.validate(invalid)
        invalid = copy.deepcopy(metadata)
        invalid['checks'][0]['validator'] = 'gcode-job'
        with self.assertRaises(ValidationError):
            validator.validate(invalid)
        invalid['checks'][0]['artifacts'] = ['gcode', 'package']
        validator.validate(invalid)

    def test_archived_inputs_match_declared_original_bytes(self):
        root = ASSETS / 'models/three_mf_official'
        fixtures = json.loads((root / 'manifest.json').read_text(encoding="utf-8"))['fixtures']
        self.assertEqual(len(fixtures), 56)
        for fixture in fixtures:
            with self.subTest(path=fixture['path']):
                data = (root / fixture['path']).read_bytes()
                self.assertEqual(len(data), fixture['byteCount'])
                self.assertEqual(hashlib.sha256(data).hexdigest(), fixture['sha256'])
                self.assertIn(fixture['source']['ref'], fixture['source']['url'])

    def test_gui_coverage_is_organized_by_business_behavior(self):
        scenarios = ASSETS / 'scenarios'
        self.assertFalse(list(scenarios.glob('external-*')),
                         'Per-file GUI templates must not replace native corpus regression')
        actual = {p.name for p in scenarios.iterdir()
                  if p.is_dir() and p.name.startswith(('three_mf_', 'ui_3mf_'))}
        self.assertEqual(actual, CORE_SCENARIOS)
        for name in sorted(CORE_SCENARIOS):
            with self.subTest(case=name):
                case = scenarios / name
                metadata = json.loads((case / 'metadata.json').read_text(encoding="utf-8"))
                document = json.loads((case / 'events/Journal.json').read_text(encoding="utf-8"))
                self.assertEqual(metadata['id'], name)
                self.assertTrue(document['entries'])
                plan = verification_plan(metadata, document)
                self.assertEqual(plan['missingMethods'], [],
                                 'Retained 3MF scenarios must have formal assertions')
                self.assertEqual(plan['readiness'], 'ready')

    def test_3mf_baseline_index_matches_formal_checkpoints_and_bytes(self):
        index = json.loads((ASSETS / 'baselines/manifest.json').read_text(encoding="utf-8"))
        registered = {row['case']: row for row in index['cases']
                      if row['case'].startswith(('three_mf_', 'ui_3mf_'))}
        self.assertEqual(set(registered), CORE_SCENARIOS)
        for name, row in registered.items():
            with self.subTest(case=name):
                metadata_path = ASSETS / 'scenarios' / name / 'metadata.json'
                metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
                journal = metadata_path.parent / 'events/Journal.json'
                entries = json.loads(journal.read_text(encoding="utf-8"))['entries']
                method, = metadata['verification']['requiredMethods']
                assertions = [e for e in entries if e['kind'] == 'verify.' + method]
                self.assertEqual(row['metadata'], metadata_path.relative_to(ASSETS).as_posix())
                self.assertEqual(row['type'], method)
                self.assertEqual(row['checkpoints'], len(assertions))
                if method == 'db':
                    used = {(ASSETS / item['file']).resolve()
                            for item in metadata['dbBaselines']}
                else:
                    used = {(journal.parent / e['payload']['baseline']).resolve()
                            for e in assertions}
                self.assertEqual(used, {(ASSETS / item['path']).resolve()
                                        for item in row['files']})
                for item in row['files']:
                    data = (ASSETS / item['path']).read_bytes()
                    self.assertEqual(len(data), item['byteCount'])
                    self.assertEqual(hashlib.sha256(data).hexdigest(), item['sha256'])

    def test_raw_captures_cannot_satisfy_either_formal_gate(self):
        for method in ('db', 'screenshot'):
            with self.subTest(method=method):
                metadata = {'verification': {'requiredMethods': [method]}, 'checks': []}
                # Capturing other real DB IDs must remain supported; ID 1 is not
                # a project-document snapshot or a special acceptance contract.
                document = {'entries': [
                    {'kind': 'capture.db', 'payload': {'dbInstanceId': '18'}},
                    {'kind': 'action', 'payload': {'code': 'screenshot.capture', 'params': {}}},
                ]}
                plan = verification_plan(metadata, document)
                self.assertEqual(plan['missingMethods'], [method])
                self.assertEqual(plan['readiness'], 'missing_checks')
                methods = method_results(plan, [])
                self.assertEqual(methods[0]['status'], 'not_verified')
                self.assertEqual(final_status(
                    {'success': True, 'acceptance': 'not_verified'}, [], methods), 'not_verified')

    def test_supplemental_output_success_does_not_fill_a_missing_gate(self):
        metadata = {
            'verification': {'requiredMethods': ['db']},
            'checks': [{'id': 'plate-1-gcode', 'validator': 'project-gcode', 'required': True}],
        }
        plan = verification_plan(metadata, {'entries': []})
        checks = [dict(plan['checks'][0], status='passed')]
        self.assertEqual(plan['missingMethods'], ['db'])
        self.assertEqual(final_status(
            {'success': True, 'acceptance': 'not_verified'}, checks,
            method_results(plan, checks)), 'not_verified')
