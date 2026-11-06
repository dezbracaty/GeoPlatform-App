"""Visual hints retain raw evidence and small content changes, without passing screenshots."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
try:
    import numpy
    from PIL import Image, ImageDraw
except ImportError:
    Image = None
from screenshot_comparison import comparison_images


@unittest.skipIf(Image is None, 'Requires tools/requirements.txt')
class VisualComparisonTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def compare(self, first, second, **kwargs):
        first.save(self.root / 'expected.png')
        second.save(self.root / 'actual.png')
        before = [(self.root / name).read_bytes() for name in ['expected.png', 'actual.png']]
        result = comparison_images(self.root / 'expected.png', self.root / 'actual.png', self.root / 'view', **kwargs)
        self.assertEqual(before, [(self.root / name).read_bytes() for name in ['expected.png', 'actual.png']])
        self.assertNotIn('acceptance', result)
        self.assertNotIn('passed', result)
        return result

    def test_small_background_variation_is_not_flooded_red_or_automatically_passed(self):
        result = self.compare(Image.new('RGB', (128, 96), (180, 180, 180)),
                              Image.new('RGB', (128, 96), (181, 181, 181)), text_reader=None)
        meta = result['comparisonMetadata']
        self.assertEqual(meta['rawDifferencePixelCount'], 128*96)
        self.assertEqual(meta['reviewDifferencePixelCount'], 0)
        self.assertEqual(result['comparisonStatus'], 'different')
        self.assertEqual(meta['ocrStatus'], 'unavailable')
        self.assertTrue((self.root / 'view/raw-diff.png').is_file())

    def test_recognized_last_digit_change_has_explicit_region_even_with_few_pixels(self):
        a = Image.new('RGB', (128, 96), 'white');b = a.copy()
        ImageDraw.Draw(b).point((48, 42), fill='black')
        observations = iter([dict(status='available', observations=[dict(text='0.1600', confidence=1., box=[30, 35, 45, 12])]),
                             dict(status='available', observations=[dict(text='0.1601', confidence=1., box=[30, 35, 45, 12])])])
        result = self.compare(a, b, text_reader=lambda _: next(observations))
        numeric = result['comparisonMetadata']['numericChanges']
        self.assertEqual([(c['before'], c['after']) for c in numeric], [('0.1600', '0.1601')])
        with Image.open(self.root / 'view/diff.png') as image:
            red = image.getpixel((28, 33))
        self.assertGreater(red[0], red[1])
        self.assertEqual(result['comparisonStatus'], 'different')

    def test_model_removal_retains_dense_structural_region_without_ocr(self):
        a = Image.new('RGB', (180, 120), '#d0d0d0');b = a.copy()
        ImageDraw.Draw(a).rectangle((60, 35, 105, 90), fill='#a63a20')
        result = self.compare(a, b, text_reader=None)
        self.assertGreater(result['comparisonMetadata']['redPixelCount'], 100)
        self.assertTrue(result['comparisonMetadata']['structuralBounds'])

    def test_cross_dpr_is_normalized_only_in_derived_views(self):
        a = Image.new('RGB', (1382, 954), '#eee');ImageDraw.Draw(a).rectangle((300, 300, 380, 430), fill='#b64b20')
        result = self.compare(a, a.resize((2764, 1908)), text_reader=None)
        self.assertEqual(result['sourceImageSizes'], [[1382, 954], [2764, 1908]])
        self.assertEqual(result['comparisonSize'], [1382, 954])
        for name in ['expected-view.png', 'actual-view.png', 'diff.png', 'raw-diff.png']:
            with Image.open(self.root / 'view' / name) as image:
                self.assertEqual(image.size, (1382, 954))

    def test_unknown_different_dimensions_are_not_arbitrarily_stretched(self):
        result = self.compare(Image.new('RGB', (120, 80)), Image.new('RGB', (240, 160)), text_reader=None)
        self.assertEqual(result['comparisonStatus'], 'unavailable')
        self.assertNotIn('diff', result)

    def test_authored_regions_constrain_pixel_and_text_highlights(self):
        a = Image.new('RGB', (128, 96), 'white');b = a.copy()
        ImageDraw.Draw(b).rectangle((80, 40, 110, 70), fill='black')
        observations = iter([dict(status='available', observations=[dict(text='1', confidence=1., box=[80, 40, 25, 20])]),
                             dict(status='available', observations=[dict(text='0', confidence=1., box=[80, 40, 25, 20])])])
        result = self.compare(a, b, regions=[[0, 0, 64, 96]], text_reader=lambda _: next(observations))
        self.assertEqual(result['differencePixelCount'], 0)
        self.assertEqual(result['comparisonMetadata']['numericChanges'], [])
        self.assertEqual(result['comparisonStatus'], 'identical')


if __name__ == '__main__':
    unittest.main()
