"""Bounded screenshot comparisons retain strict pixels and original evidence."""
from io import BytesIO
import json
from pathlib import Path
import sys
import tempfile
import unittest

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


    def test_regions_use_one_capture_and_do_not_count_unselected_pixels(self):
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
            self.assertIn('similarity=0.750000', result['results'][0]['message'])
            with Image.open(output / 'screenshots/screenshot_001_diff.png') as diff:
                self.assertEqual(diff.getpixel((3, 0)), (255, 0, 0, 255))
                self.assertEqual(diff.getpixel((11, 9)), (255, 255, 255, 255))

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

    def test_opt_in_filter_accepts_pixel_noise_but_rejects_missing_geometry(self):
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
            self.assertEqual(result['results'][0]['status'], 'passed')
            self.assertIn('blurRadius=2', result['results'][0]['message'])
            self.assertEqual(baseline.read_bytes(), original)
            self.assertEqual((output/'screenshots/screenshot_001_expected.png').read_bytes(), original)
            self.assertEqual((output/'screenshots/screenshot_001_actual.png').read_bytes(), png)
            data = BytesIO(); missing.save(data, format='PNG')
            self.assertFalse(compare_screenshots(
                baseline, data.getvalue(), 1.0, root/'missing.png',
                region=[8, 8, 48, 48], max_channel_difference=1, blur_radius=2)[0])


if __name__ == "__main__":
    unittest.main()
