"""Check the method vocabulary, adapters and asset contract stay in agreement."""
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from verification_methods import METHODS, SUPPLEMENTAL_VALIDATORS, verification_plan


class MethodContractTest(unittest.TestCase):
    def test_asset_schemas_use_exactly_the_registered_methods(self):
        root = Path(__file__).resolve().parent / 'TestAssets/schemas'
        if not root.is_dir():
            self.skipTest('Optional pinned TestAssets not initialized')
        schema = json.loads((root/'scenario.schema.json').read_text(encoding='utf8'))
        self.assertEqual(set(schema['properties']['verification']['properties']['requiredMethods']['items']['enum']), set(METHODS))
        result = json.loads((root/'case-result.schema.json').read_text(encoding='utf8'))
        self.assertEqual(set(result['properties']['methodResults']['items']['properties']['method']['enum']), set(METHODS))
        self.assertEqual(set(schema['properties']['checks']['items']['properties']['validator']['enum']),
                         set(SUPPLEMENTAL_VALIDATORS))

    def test_special_files_do_not_fulfil_db_acceptance(self):
        plan = verification_plan(dict(verification=dict(requiredMethods=['db']), checks=[
            dict(id='print-job', validator='gcode-job', required=True)]), dict(entries=[]))
        self.assertEqual(plan['missingMethods'], ['db'])

    def test_scenario_must_choose_one_acceptance_type(self):
        with self.assertRaisesRegex(ValueError, 'exactly one'):
            verification_plan(dict(verification=dict(requiredMethods=['db','screenshot'])), dict(entries=[]))

    def test_images_are_required_alongside_the_primary_db_method(self):
        plan = verification_plan(dict(verification=dict(requiredMethods=['db'])),
                                 dict(entries=[dict(kind='verify.db'), dict(kind='verify.screenshot'),
                                      dict(kind='action', payload=dict(code='screenshot.capture'))]))
        self.assertEqual(plan['missingMethods'], [])
        self.assertEqual([check['validator'] for check in plan['checks']],
                         ['journal-db', 'journal-screenshot', 'journal-screenshot-capture'])
        self.assertTrue(all(check['required'] for check in plan['checks']))

    def test_check_id_collision_rejected(self):
        with self.assertRaisesRegex(ValueError, 'collide'):
            verification_plan(dict(verification=dict(requiredMethods=['db']), checks=[
                dict(id='journal-db', validator='gcode-job', required=True)]),
                dict(entries=[dict(kind='verify.db')]))


if __name__ == '__main__':
    unittest.main()
