"""Check the method vocabulary, adapters and asset contract stay in agreement."""
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from verification_methods import METHODS, VALIDATORS, verification_plan


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
                         {v for v, spec in VALIDATORS.items() if spec['source'] == 'artifacts'})

    def test_physical_printing_cannot_be_fulfilled_by_software_adapters(self):
        plan = verification_plan(dict(verification=dict(requiredMethods=['physical']), checks=[
            dict(id='print-job', validator='gcode-job', required=True)]), dict(entries=[]))
        self.assertEqual(plan['missingMethods'], ['physical'])

    def test_duplicate_methods_rejected(self):
        with self.assertRaisesRegex(ValueError, 'distinct'):
            verification_plan(dict(verification=dict(requiredMethods=['state','state'])), dict(entries=[]))

    def test_check_id_collision_rejected(self):
        with self.assertRaisesRegex(ValueError, 'collide'):
            verification_plan(dict(verification=dict(requiredMethods=['state']), checks=[
                dict(id='journal-db', validator='gcode-job', required=True)]),
                dict(entries=[dict(kind='verify.db')]))


if __name__ == '__main__':
    unittest.main()
