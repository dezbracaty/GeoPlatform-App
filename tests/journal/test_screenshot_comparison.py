"""Bounded screenshot comparisons retain strict pixels and original evidence."""
from io import BytesIO
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools/journal"))
from journal_runner import (
    compare_screenshots, finish_result, JournalFormatError, JournalRunner,
    load_journal,
)

try:
    from PIL import Image
except ImportError:
    Image = None


@unittest.skipIf(Image is None, "Screenshot comparison requires Pillow")
class ScreenshotRegionTest(unittest.TestCase):
    def test_native_import_animation_is_limited_to_reviewed_images(self):
        approved = [('three_mf_filepicker_geometry', 'import-mode.png'),
                    ('three_mf_native_decision_cancel', 'native-question.png'),
                    ('three_mf_target2_geometry', 'native-options.png')]
        # One-level changes in text/buttons/model and immediately outside each
        # animation must still fail, as must animation changes in other stages.
        variants = [(case, name, 'darwin', (1382, 954), None, True)
                    for case, name in approved]
        variants += [(approved[0][0], approved[0][1], 'darwin', (1382, 954), point, False)
                     for point in [(760, 906), (748, 893), (766, 912), (1066, 915),
                                   (790, 900), (620, 540), (450, 455), (600, 650)]]
        variants += [(case, name, platform, size, None, False) for case, name, platform, size in [
            (approved[0][0], 'ui-imported.png', 'darwin', (1382, 954)),
            ('other_case', approved[0][1], 'darwin', (1382, 954)),
            (approved[0][0], approved[0][1], 'win32', (1382, 954)),
            (approved[0][0], approved[0][1], 'darwin', (2764, 1908)),
        ]]
        for case, name, platform, size, outside, passes in variants:
            with self.subTest(case=case, image=name, platform=platform, size=size, outside=outside), \
                    tempfile.TemporaryDirectory() as directory, patch('journal_runner.sys.platform', platform):
                root = Path(directory)
                baseline = root / 'baselines/screenshots' / case / name
                baseline.parent.mkdir(parents=True)
                expected = Image.new('RGBA', size, 'white'); expected.save(baseline)
                original = baseline.read_bytes()
                actual = expected.copy()
                dpr = size[0] // 1382
                for x, y in [(748, 906), (900, 915)]:
                    actual.putpixel((x*dpr, y*dpr), (0, 0, 0, 255))
                if outside:
                    actual.putpixel(outside, (254, 255, 255, 255))
                data = BytesIO(); actual.save(data, format='PNG'); png = data.getvalue()

                class Client:
                    def capture_screenshot(self):
                        return dict(png=png)

                    def execute_entry(self, entry):
                        Path(entry['payload']['params']['filepath']).write_bytes(png)

                journal = root / 'Journal.json'
                journal.write_text(json.dumps(dict(formatVersion=2, entries=[
                    dict(atMs=0, kind='verify.screenshot', payload=dict(baseline=str(baseline))),
                    dict(atMs=0, kind='action', payload=dict(code='screenshot.capture',
                         params=dict(filepath='capture.png'), baseline=str(baseline))),
                ])))
                output = root / 'result'; (output / 'screenshots').mkdir(parents=True)
                result = finish_result(JournalRunner(Client()).run(load_journal(journal), output))
                self.assertEqual(result['acceptance'], 'passed' if passes else 'failed')
                for step in result['results']:
                    comparison = step['comparison']
                    enabled = (case, name) in approved \
                        and platform == 'darwin' and size == (1382, 954)
                    self.assertEqual(len(comparison['excludedRegions']), 3 if enabled else 1)
                    self.assertEqual(comparison['differentPixels'], (int(outside is not None) if enabled else 2))
                    self.assertEqual(Path(step['evidence'][0]).read_bytes(), original)
                    self.assertEqual(Path(step['evidence'][1]).read_bytes(), png)
                self.assertEqual(baseline.read_bytes(), original)

    def test_formal_capture_and_checkpoint_exclude_only_fps(self):
        for dpr in (1, 2):
            for outside in (False, True):
                with self.subTest(dpr=dpr, outside=outside), tempfile.TemporaryDirectory() as directory:
                    root = Path(directory)
                    baseline = root / 'expected.png'
                    expected = Image.new('RGBA', (1382 * dpr, 954 * dpr), 'white')
                    expected.save(baseline)
                    original = baseline.read_bytes()
                    actual = expected.copy()
                    actual.putpixel((1300 * dpr, 90 * dpr), (0, 0, 0, 255))
                    if outside:
                        # A single level at the immediate left of the FPS
                        # exclusion still fails the whole screenshot.
                        actual.putpixel((1260 * dpr - 1, 90 * dpr), (254, 255, 255, 255))
                    data = BytesIO(); actual.save(data, format='PNG'); png = data.getvalue()

                    class Client:
                        def capture_screenshot(self):
                            return dict(png=png)

                        def execute_entry(self, entry):
                            Path(entry['payload']['params']['filepath']).write_bytes(png)

                    journal = root / 'Journal.json'
                    journal.write_text(json.dumps(dict(formatVersion=2, entries=[
                        dict(atMs=0, kind='verify.screenshot', payload=dict(baseline='expected.png')),
                        dict(atMs=0, kind='action', payload=dict(code='screenshot.capture',
                             params=dict(filepath='captured.png'), baseline='expected.png')),
                    ])))
                    output = root / 'result'; (output / 'screenshots').mkdir(parents=True)
                    result = finish_result(JournalRunner(Client()).run(load_journal(journal), output))
                    self.assertEqual(result['acceptance'], 'failed' if outside else 'passed')
                    for step in result['results']:
                        comparison = step['comparison']
                        self.assertEqual(comparison['differentPixels'], int(outside))
                        self.assertEqual(comparison['excludedRegions'], [dict(reason='fps',
                            rectangle=[1260*dpr, (78 if sys.platform == 'win32' else 72)*dpr,
                                       110*dpr, 36*dpr])])
                        self.assertEqual(comparison['comparedPixels'],
                                         (1382*954 - 110*36) * dpr*dpr)
                        self.assertEqual(Path(step['evidence'][0]).read_bytes(), original)
                        self.assertEqual(Path(step['evidence'][1]).read_bytes(), png)
                    self.assertEqual(baseline.read_bytes(), original)

    def test_windows_fps_shadow_uses_native_offset_without_growing_exclusion(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            baseline = root / 'expected.png'
            Image.new('RGBA', (1382, 954), 'white').save(baseline)
            for platform, point, expected_pass in [
                    ('win32', (1279, 109), True),  # observed Windows lower button edge
                    ('darwin', (1279, 109), False),
                    ('win32', (1279, 74), False),  # no longer inside the moved footprint
                    ('darwin', (1279, 74), True),
                    ('win32', (1279, 114), False),
                    ('win32', (1370, 100), False),
                    ('win32', (1259, 100), False)]:
                with self.subTest(platform=platform, point=point), patch('journal_runner.sys.platform', platform):
                    actual = Image.new('RGBA', (1382, 954), 'white')
                    actual.putpixel(point, (254, 255, 255, 255))
                    data = BytesIO(); actual.save(data, format='PNG'); details = {}
                    result = compare_screenshots(baseline, data.getvalue(), 1.0,
                        root / 'diff.png', exclude_fps=True, details=details)
                    self.assertEqual(result[0], expected_pass)
                    self.assertEqual(details['comparedPixels'], 1382*954 - 110*36)

    def test_fps_exclusion_never_hides_size_mismatch_or_other_windows(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); baseline = root / 'expected.png'
            Image.new('RGBA', (1382, 954), 'white').save(baseline)
            data = BytesIO(); Image.new('RGBA', (1383, 954), 'white').save(data, format='PNG')
            self.assertFalse(compare_screenshots(baseline, data.getvalue(), 1.0,
                root/'size.png', exclude_fps=True)[0])
            Image.new('RGBA', (1400, 1000), 'white').save(baseline)
            actual = Image.open(baseline).convert('RGBA')
            actual.putpixel((1300, 90), (254, 255, 255, 255))
            data = BytesIO(); actual.save(data, format='PNG'); details = {}
            self.assertFalse(compare_screenshots(baseline, data.getvalue(), 1.0,
                root/'other.png', exclude_fps=True, details=details)[0])
            self.assertEqual(details['excludedRegions'], [])

    def test_report_keeps_expected_pixels_when_source_baseline_changes(self):
        for matches in (True, False):
            with self.subTest(matches=matches), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                baseline = root / "baseline.png"
                Image.new("RGBA", (12, 10), "white").save(baseline)
                original = baseline.read_bytes()
                changed = BytesIO()
                Image.new("RGBA", (12, 10), "black").save(changed, format="PNG")

                class Client:
                    def capture_screenshot(self):
                        # A later baseline edit must not rewrite this run's
                        # expected image or change the comparison in flight.
                        baseline.write_bytes(changed.getvalue())
                        return {"png": original if matches else changed.getvalue()}

                journal = root / "Journal.json"
                journal.write_text(json.dumps(dict(formatVersion=2, entries=[dict(
                    atMs=0, kind="verify.screenshot",
                    payload=dict(baseline="baseline.png", tolerance=1.0,
                                 region=[1, 1, 4, 3]))])), encoding="utf-8")
                output = root / "result"
                (output / "screenshots").mkdir(parents=True)
                result = finish_result(JournalRunner(Client()).run(load_journal(journal), output))
                self.assertEqual(result["acceptance"], "passed" if matches else "failed")
                expected = output / "screenshots/screenshot_001_expected.png"
                self.assertEqual(expected.read_bytes(), original)
                self.assertNotEqual(expected.read_bytes(), baseline.read_bytes())
                self.assertEqual(result["results"][0]["evidence"][0], expected)
                self.assertIn("screenshot_001_expected.png",
                              (output / "verification_report.html").read_text(encoding="utf-8"))

    def test_only_explicit_region_excludes_unrelated_pixels(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            expected = Image.new("RGBA", (12, 10), "white")
            expected.save(root / "expected.png")
            actual = expected.copy()
            actual.putpixel((11, 9), (255, 0, 0, 255))
            data = BytesIO()
            actual.save(data, format="PNG")
            self.assertFalse(compare_screenshots(
                root / "expected.png", data.getvalue(), 1.0, root / "whole.png")[0])
            self.assertTrue(compare_screenshots(
                root / "expected.png", data.getvalue(), 1.0, root / "region.png",
                region=[1, 1, 4, 3])[0])
            self.assertEqual(Image.open(root / "region.png").size, (12, 10))
            # A wrong pixel in the selected area must still fail, even by one level.
            actual.putpixel((2, 2), (254, 255, 255, 255))
            data = BytesIO()
            actual.save(data, format="PNG")
            passed, similarity, reason = compare_screenshots(
                root / "expected.png", data.getvalue(), 1.0, root / "wrong.png",
                region=[1, 1, 4, 3])
            self.assertFalse(passed)
            self.assertAlmostEqual(similarity, 11 / 12)
            self.assertIn("[1, 1, 4, 3]", reason)
            self.assertEqual(Image.open(root / "wrong.png").getpixel((2, 2)), (255, 0, 0, 255))

    def test_region_cannot_hide_image_size_mismatch_or_escape_bounds(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            Image.new("RGBA", (12, 10), "white").save(root / "expected.png")
            data = BytesIO()
            Image.new("RGBA", (13, 10), "white").save(data, format="PNG")
            self.assertFalse(compare_screenshots(
                root / "expected.png", data.getvalue(), 1.0, root / "size.png",
                region=[0, 0, 2, 2])[0])
            with self.assertRaisesRegex(ValueError, "exceeds image size"):
                compare_screenshots(root / "expected.png", (root / "expected.png").read_bytes(),
                                    1.0, root / "invalid.png", region=[11, 9, 2, 2])

    def test_invalid_regions_fail_before_application_launch(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Journal.json"
            for region in [[], [0, 0, 1], [-1, 0, 1, 1], [0, 0, 0, 1],
                           [0, 0, 1.0, 1], [False, 0, 1, 1], "0,0,1,1"]:
                with self.subTest(region=region):
                    path.write_text(json.dumps(dict(formatVersion=2, entries=[dict(
                        atMs=0, kind="verify.screenshot",
                        payload=dict(baseline="baseline.png", region=region))])), encoding="utf-8")
                    with self.assertRaises(JournalFormatError):
                        load_journal(path)


    def test_replay_uses_one_capture_and_compares_pixels_outside_legacy_regions(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            baseline = root / "expected.png"
            Image.new("RGBA", (12, 10), "white").save(baseline)
            actual = Image.new("RGBA", (12, 10), "black")
            for x in range(4):
                actual.putpixel((x, 0), (255, 255, 255, 255))
            actual.putpixel((3, 0), (254, 255, 255, 255))
            data = BytesIO(); actual.save(data, format="PNG")
            class Client:
                calls = 0
                def capture_screenshot(self):
                    self.calls += 1
                    return {"png": data.getvalue()}
            client = Client()
            journal = root / "Journal.json"
            journal.write_text(json.dumps(dict(formatVersion=2, entries=[dict(
                atMs=0, kind="verify.screenshot", payload=dict(
                    baseline="expected.png", tolerance=1.0,
                    regions=[[0, 0, 3, 1], [2, 0, 2, 1]]))])))
            output = root / "result"; (output / "screenshots").mkdir(parents=True)
            result = JournalRunner(client).run(load_journal(journal), output)
            self.assertEqual(client.calls, 1)
            self.assertEqual(result['results'][0]['status'], 'failed')
            self.assertEqual(result['results'][0]['comparison']['differentPixels'], 117)
            with Image.open(output / 'screenshots/screenshot_001_diff.png') as diff:
                self.assertEqual(diff.getpixel((3, 0)), (255, 0, 0, 255))
                self.assertEqual(diff.getpixel((11, 9)), (255, 0, 0, 255))

    def test_channel_limit_is_explicit_and_keeps_wrong_pixels_failing(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); baseline = root / 'expected.png'
            image = Image.new('RGBA', (4, 1), (100, 100, 100, 255)); image.save(baseline)
            actual = image.copy(); actual.putpixel((0, 0), (102, 100, 100, 255))
            data = BytesIO(); actual.save(data, format='PNG')
            self.assertFalse(compare_screenshots(baseline, data.getvalue(), 1.0, root/'strict.png')[0])
            self.assertTrue(compare_screenshots(baseline, data.getvalue(), 1.0, root/'bounded.png', max_channel_difference=2)[0])
            actual.putpixel((1, 0), (103, 100, 100, 255)); data = BytesIO(); actual.save(data, format='PNG')
            passed, score, reason = compare_screenshots(baseline, data.getvalue(), 1.0, root/'wrong.png', max_channel_difference=2)
            self.assertFalse(passed); self.assertEqual(score, .75)
            self.assertIn('maxChannelDifference=2', reason)

    def test_invalid_region_unions_and_channel_limits_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'Journal.json'
            invalid = [dict(regions=[]), dict(regions=[None]), dict(regions='all'),
                       dict(region=[0,0,2,2], regions=[[0,0,2,2]]),
                       *[dict(maxChannelDifference=v) for v in [-1,256,True,1.0,'2']],
                       *[dict(blurRadius=v) for v in [-1,3,True,1.0,'2']]]
            for value in invalid:
                with self.subTest(value=value):
                    path.write_text(json.dumps(dict(formatVersion=2, entries=[dict(
                        atMs=0, kind='verify.screenshot', payload=dict(baseline='expected.png', **value))])))
                    with self.assertRaises(JournalFormatError): load_journal(path)
            baseline=Path(directory)/'expected.png';Image.new('RGBA',(4,4),'white').save(baseline)
            with self.assertRaisesRegex(ValueError,'exceeds image size'):
                compare_screenshots(baseline,baseline.read_bytes(),1.0,Path(directory)/'diff.png',regions=[[0,0,2,2],[3,3,2,2]])

    def test_replay_cannot_opt_out_of_exact_pixels_with_filter_options(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            baseline = root / 'expected.png'
            expected = Image.new('RGBA', (64, 64), (160, 160, 160, 255))
            noisy = expected.copy()
            for y in range(64):
                for x in range(64):
                    value = 152 if (x + y) % 2 else 168
                    noisy.putpixel((x, y), (value, value, value, 255))
            missing = noisy.copy()
            for y in range(24, 40):
                for x in range(24, 40):
                    expected.putpixel((x, y), (80, 80, 80, 255))
                    noisy.putpixel((x, y), (80, 80, 80, 255))
            expected.save(baseline)
            original = baseline.read_bytes()
            data = BytesIO(); noisy.save(data, format='PNG')
            png = data.getvalue()
            self.assertFalse(compare_screenshots(
                baseline, png, 1.0, root/'strict.png', region=[8, 8, 48, 48])[0])

            class Client:
                def capture_screenshot(self):
                    return {'png': png}

            journal = root/'Journal.json'
            journal.write_text(json.dumps(dict(formatVersion=2, entries=[dict(
                atMs=0, kind='verify.screenshot', payload=dict(
                    baseline='expected.png', region=[8, 8, 48, 48],
                    tolerance=1.0, maxChannelDifference=1, blurRadius=2))])))
            output = root/'result'; (output/'screenshots').mkdir(parents=True)
            result = JournalRunner(Client()).run(load_journal(journal), output)
            self.assertEqual(result['results'][0]['status'], 'failed')
            self.assertEqual(result['results'][0]['comparison']['differentPixels'], 3840)
            self.assertNotIn('blurRadius=2', result['results'][0]['message'])
            self.assertEqual(baseline.read_bytes(), original)
            self.assertEqual((output/'screenshots/screenshot_001_expected.png').read_bytes(), original)
            self.assertEqual((output/'screenshots/screenshot_001_actual.png').read_bytes(), png)
            data = BytesIO(); missing.save(data, format='PNG')
            self.assertFalse(compare_screenshots(
                baseline, data.getvalue(), 1.0, root/'missing.png',
                region=[8, 8, 48, 48], max_channel_difference=1, blur_radius=2)[0])


if __name__ == "__main__":
    unittest.main()
