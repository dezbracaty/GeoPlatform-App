"""Selection regressions for the shared, versioned asset runner."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from run_acceptance import select_cases, update_run_summary


class SelectionTests(unittest.TestCase):
    def test_explicit_all_runs_every_mode(self):
        names = ['first-geometry', 'first-project', 'second-geometry', 'second-project']
        self.assertEqual(select_cases(names, 'all'), names)

    def test_explicit_filter_is_not_expanded(self):
        names = ['first-geometry', 'first-project', 'second-geometry', 'second-project']
        self.assertEqual(select_cases(names, 'first-project'), ['first-project'])

    def test_regular_default_remains_height_color(self):
        self.assertEqual(select_cases(['other', 'p3_height_color_export']), ['p3_height_color_export'])

    def test_unknown_duplicate_and_empty_selections_fail(self):
        for selection in ['missing', 'a,a', '']:
            with self.assertRaises(ValueError):
                select_cases(['a', 'b'], selection)

    def test_explicit_all_and_exclude_are_respected(self):
        self.assertEqual(select_cases(['a', 'b'], 'all', exclude='a'), ['b'])

    def test_incomplete_run_cannot_be_reported_passed(self):
        summary = dict(plannedCases=['a', 'b'], cases=[dict(id='a', status='passed')])
        update_run_summary(summary)
        self.assertEqual(summary['status'], 'not_verified')
        self.assertFalse(summary['runComplete'])
        self.assertEqual(summary['notRunCases'], ['b'])
        summary['cases'].append(dict(id='b', status='passed'))
        update_run_summary(summary)
        self.assertEqual(summary['status'], 'passed')
        self.assertNotIn('observedOutcomes', summary)


if __name__ == '__main__':
    unittest.main()
