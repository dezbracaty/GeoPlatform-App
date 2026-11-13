"""Versioned, display-only visual hints. No image score grants acceptance.

The vectorized OKLab/HyAB formulation follows Pixelmatch 8 (ISC notice in
licenses/pixelmatch.txt). Local matching is bounded to one normalized pixel;
there is no image registration, arbitrary stretching, or geometric warping.
"""
import json
import math
from pathlib import Path
import re
import shutil

from screenshot_text import recognize

WINDOW_SIZE = (1382, 954)
POLICY_VERSION = 'visual-hints-v1'


def _oklab(rgb):
    import numpy as np
    value = rgb.astype(np.float32) / 255
    linear = np.where(value <= .04045, value / 12.92, ((value + .055) / 1.055) ** 2.4)
    lms = np.cbrt(linear @ np.array([[.4122214708, .2119034982, .0883024619],
                                   [.5363325363, .6806995451, .2817188376],
                                   [.0514459929, .1073969566, .6299787005]], dtype=np.float32))
    lab = lms @ np.array([[.2104542553, 1.9779984951, .0259040371],
                         [.7936177850, -2.4285922050, .7827717662],
                         [-.0040720468, .4505937099, -.8086757660]], dtype=np.float32)
    lightness = lab[:, :, 0]
    toe = 1.206 / 1.03 * lightness - .206
    lab[:, :, 0] = .5 * (toe + np.sqrt(toe * toe + 4 * .03 * 1.206 / 1.03 * lightness))
    return lab


def _nearby_difference(a, b):
    import numpy as np
    h, w = a.shape[:2]
    pa = np.pad(a, ((1, 1), (1, 1), (0, 0)), mode='edge')
    pb = np.pad(b, ((1, 1), (1, 1), (0, 0)), mode='edge')
    ab = np.full((h, w), 255, dtype=np.int16)
    ba = ab.copy()
    for y in range(3):
        for x in range(3):
            ab = np.minimum(ab, np.max(np.abs(a - pb[y:y+h, x:x+w]), axis=2))
            ba = np.minimum(ba, np.max(np.abs(b - pa[y:y+h, x:x+w]), axis=2))
    return (ab > 26) | (ba > 26)


def _bounds(observation, size):
    x, y, width, height = observation['box']
    return [max(0, math.floor(x - 2)), max(0, math.floor(y - 2)),
            min(size[0], math.ceil(x + width + 2)), min(size[1], math.ceil(y + height + 2))]


def _text_hints(first, second, selected):
    import numpy as np
    h, w = selected.shape
    equal = np.zeros_like(selected)
    used, numeric, uncertain = set(), [], []
    equal_count = 0
    for a in first.get('observations', []):
        ax, ay, aw, ah = a['box']
        choices = []
        for index, b in enumerate(second.get('observations', [])):
            if index in used:
                continue
            bx, by, bw, bh = b['box']
            dy, dx = abs(ay + ah/2 - by - bh/2), abs(ax + aw/2 - bx - bw/2)
            overlap = max(0, min(ax+aw, bx+bw) - max(ax, bx)) / max(1, min(aw, bw))
            if dy <= max(5, min(ah, bh)*.6) and overlap > .35 and dx < max(18, max(aw, bw)*.4):
                choices.append((dy * 3 + dx, index, b))
        if not choices:
            continue
        _, index, b = min(choices, key=lambda item: item[0])
        used.add(index)
        aa, bb = _bounds(a, (w, h)), _bounds(b, (w, h))
        bounds = [min(aa[0], bb[0]), min(aa[1], bb[1]), max(aa[2], bb[2]), max(aa[3], bb[3])]
        x0, y0, x1, y1 = bounds
        if not selected[y0:y1, x0:x1].any():
            continue
        t1, t2 = ''.join(a['text'].split()), ''.join(b['text'].split())
        if t1 == t2:
            equal_count += 1
            if min(a['confidence'], b['confidence']) >= .5:
                equal[y0:y1, x0:x1] = True
        else:
            hint = dict(before=a['text'], after=b['text'], bounds=bounds,
                        confidence=[a['confidence'], b['confidence']], status='recognition_candidate')
            pattern = r'[+-]?\d+(?:\.\d+)?(?:mm|%)?'
            (numeric if re.fullmatch(pattern, t1) and re.fullmatch(pattern, t2) else uncertain).append(hint)
    return equal, numeric, uncertain, equal_count


def _regions(mask):
    """Group actual candidate pixels, rather than joining every tiny raw noise tile."""
    h, w = mask.shape
    tile = 8
    cells = {(x//tile, y//tile) for y in range(0, h, tile) for x in range(0, w, tile)
             if mask[y:y+tile, x:x+tile].any()}
    result = []
    while cells:
        group = {cells.pop()}
        pending = list(group)
        while pending:
            x, y = pending.pop()
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    point = x+dx, y+dy
                    if point in cells:
                        cells.remove(point)
                        group.add(point)
                        pending.append(point)
        result.append([max(0, min(x for x, y in group)*tile-3),
                       max(0, min(y for x, y in group)*tile-3),
                       min(w-1, (max(x for x, y in group)+1)*tile+3),
                       min(h-1, (max(y for x, y in group)+1)*tile+3)])
    return sorted(result)


def _prepared_images(expected_path, actual_path, destination, regions):
    from PIL import Image
    destination = Path(destination)
    destination.mkdir(parents=True, exist_ok=True)
    def load(path):
        with Image.open(path) as image:
            return image.convert('RGBA')

    def factor(size):
        return next((n for n in (1, 2, 3, 4) if size == (WINDOW_SIZE[0]*n, WINDOW_SIZE[1]*n)), 1)

    expected, actual = load(expected_path), load(actual_path)
    sizes = [list(expected.size), list(actual.size)]
    scale = factor(expected.size)
    mapped = []
    for x, y, width, height in regions or [[0, 0, *expected.size]]:
        if (any(type(v) is not int for v in (x, y, width, height)) or min(x, y) < 0 or
                min(width, height) <= 0 or x+width > expected.width or y+height > expected.height):
            raise ValueError('Comparison region exceeds the original standard image')
        left, top = x//scale, y//scale
        mapped.append([left, top, math.ceil((x+width)/scale)-left, math.ceil((y+height)/scale)-top])
    if scale != 1:
        expected = expected.resize(WINDOW_SIZE, Image.Resampling.LANCZOS)
    if factor(actual.size) != 1:
        actual = actual.resize(WINDOW_SIZE, Image.Resampling.LANCZOS)
    expected.save(destination / 'expected-view.png')
    actual.save(destination / 'actual-view.png')
    common = dict(policyVersion=POLICY_VERSION, sourceImageSizes=sizes, comparisonSize=list(expected.size),
                  normalizedSize=list(expected.size), comparisonRegions=mapped,
                  expectedView='expected-view.png', actualView='actual-view.png')
    return expected, actual, mapped, common


def comparison_images(expected_path, actual_path, destination, regions=None, *, text_reader=recognize):
    import numpy as np
    from PIL import Image, ImageDraw, ImageFilter
    destination = Path(destination)
    destination.mkdir(parents=True, exist_ok=True)

    expected, actual, mapped, common = _prepared_images(expected_path, actual_path, destination, regions)
    if expected.size != actual.size:
        return dict(common, comparisonStatus='unavailable',
                    message='截图尺寸不兼容；未拉伸或拟合未知窗口尺寸，请查看原始证据。')
    w, h = expected.size
    selected = np.zeros((h, w), dtype=bool)
    for x, y, width, height in mapped:
        selected[y:y+height, x:x+width] = True
    ea, aa = np.asarray(expected), np.asarray(actual)
    raw = np.any(ea != aa, axis=2) & selected
    if not raw.any():
        # Exact agreement in every authored region needs no visual/text hints.
        for name in ('raw-diff.png', 'diff.png'):
            shutil.copyfile(destination / 'expected-view.png', destination / name)
        metadata = dict(policyVersion=POLICY_VERSION, normalizedSize=[w, h],
            sourceImageSizes=common['sourceImageSizes'], rawDifferencePixelCount=0,
            reviewDifferencePixelCount=0, redPixelCount=0, amberPixelCount=0,
            equalRecognizedTextCount=0, numericChanges=[], uncertainTextChanges=[],
            structuralBounds=[], ocrStatus='not_needed', ocrBackend=[None, None],
            ocrMessage='比较区域像素完全一致，无需文字辅助分析。', textDiagnostics=[])
        (destination / 'comparison.json').write_text(
            json.dumps(metadata, ensure_ascii=False, indent=2)+'\n', encoding='utf8')
        return dict(common, comparisonMetadata=metadata, comparisonStatus='identical',
            differencePixelCount=0, substantialPixelCount=0, suspectedRenderingPixelCount=0,
            numericChanges=[], rawDiff='raw-diff.png', diff='diff.png',
            message='比较区域像素完全一致；原始证据和自动验收结果保持不变。')
    # Composite alpha consistently for perceptual color; raw RGBA remains separately inspectable.
    white = Image.new('RGBA', expected.size, 'white')
    ec, ac = Image.alpha_composite(white, expected).convert('RGB'), Image.alpha_composite(white, actual).convert('RGB')
    a, b = np.asarray(ec, dtype=np.int16), np.asarray(ac, dtype=np.int16)
    delta = _oklab(a) - _oklab(b)
    perceptual = np.abs(delta[:, :, 0]) + np.sqrt(delta[:, :, 1]**2 + delta[:, :, 2]**2) > .1
    candidate = raw & perceptual & _nearby_difference(a, b)
    unavailable = dict(status='unavailable', observations=[], message='Text hints disabled')
    first = text_reader(ec) if text_reader else unavailable
    second = text_reader(ac) if text_reader else unavailable
    equal, numeric, uncertain, equal_count = _text_hints(first, second, selected)
    candidate &= ~equal
    integral = np.pad(candidate.astype(np.int32), ((1, 0), (1, 0))).cumsum(0).cumsum(1)
    if h >= 16 and w >= 16:
        counts = integral[16:, 16:] - integral[:-16, 16:] - integral[16:, :-16] + integral[:-16, :-16]
        dense = np.pad(counts >= 64, ((8, 7), (8, 7)), mode='edge')
    else:
        dense = np.zeros_like(candidate)
    af = np.asarray(ec.filter(ImageFilter.GaussianBlur(1.2)), dtype=np.int16)
    bf = np.asarray(ac.filter(ImageFilter.GaussianBlur(1.2)), dtype=np.int16)
    red = candidate & dense & _nearby_difference(af, bf)
    amber = candidate & ~red
    overlay = np.zeros((h, w, 4), dtype=np.uint8)
    overlay[red], overlay[amber] = [229, 55, 67, 240], [225, 150, 34, 75]
    review_mask = Image.fromarray(overlay)
    draw = ImageDraw.Draw(review_mask)
    structural = _regions(red)
    for bounds in structural:
        if (bounds[2]-bounds[0])*(bounds[3]-bounds[1]) < w*h*.2:
            draw.rectangle(bounds, outline=(229, 55, 67, 255), width=2)
    for change in numeric:
        draw.rectangle(change['bounds'], outline=(229, 55, 67, 255), width=3)
    # Outlines and text hints must also respect the authored comparison regions.
    pixels = np.array(review_mask)
    pixels[~selected, 3] = 0
    review_mask = Image.fromarray(pixels)
    raw_overlay = np.zeros((h, w, 4), dtype=np.uint8)
    raw_overlay[raw] = [255, 48, 48, 255]
    Image.alpha_composite(expected, Image.fromarray(raw_overlay)).save(destination / 'raw-diff.png')
    Image.alpha_composite(expected, review_mask).save(destination / 'diff.png')
    text_status = 'available' if first['status'] == second['status'] == 'available' else 'unavailable'
    metadata = dict(policyVersion=POLICY_VERSION, normalizedSize=[w, h], sourceImageSizes=common['sourceImageSizes'],
        rawDifferencePixelCount=int(raw.sum()), reviewDifferencePixelCount=int(candidate.sum()),
        redPixelCount=int(red.sum()), amberPixelCount=int(amber.sum()), equalRecognizedTextCount=equal_count,
        numericChanges=numeric, uncertainTextChanges=uncertain, structuralBounds=structural,
        ocrStatus=text_status, ocrBackend=[first.get('backend'), second.get('backend')],
        ocrMessage='识别结果仅为候选，请核对原图。' if text_status == 'available' else
                   '文字辅助不可用；小数字变化可能仅出现在原始像素差异中，请人工核对。',
        textDiagnostics=[{k: v for k, v in item.items() if k != 'observations'} for item in (first, second)])
    (destination / 'comparison.json').write_text(json.dumps(metadata, ensure_ascii=False, indent=2)+'\n', encoding='utf8')
    return dict(common, comparisonMetadata=metadata, comparisonStatus='different' if raw.any() else 'identical',
        differencePixelCount=int(raw.sum()), substantialPixelCount=int(red.sum()),
        suspectedRenderingPixelCount=int(amber.sum()), numericChanges=numeric,
        rawDiff='raw-diff.png', diff='diff.png',
        message='差异图以标准图为底图。红为重点核对候选；橙为疑似渲染差异或弱提示，均不代表通过。')


def raw_comparison_images(expected_path, actual_path, destination, regions=None):
    """Pillow-only fallback: preserve the raw comparison when optional hints fail."""
    from PIL import Image, ImageChops, ImageDraw
    destination = Path(destination)
    expected, actual, mapped, common = _prepared_images(expected_path, actual_path, destination, regions)
    if expected.size != actual.size:
        return dict(common, comparisonStatus='unavailable',
                    message='截图尺寸不兼容；辅助提示不可用，请查看原始证据。')
    selected = Image.new('L', expected.size, 0)
    draw = ImageDraw.Draw(selected)
    for x, y, width, height in mapped:
        draw.rectangle((x, y, x+width-1, y+height-1), fill=255)
    channels = ImageChops.difference(expected, actual).split()
    magnitude = channels[0]
    for channel in channels[1:]:
        magnitude = ImageChops.lighter(magnitude, channel)
    changed = ImageChops.multiply(magnitude.point(lambda value: 255 if value else 0), selected)
    Image.composite(Image.new('RGBA', expected.size, '#ff3030'), expected, changed).save(destination/'raw-diff.png')
    count = changed.histogram()[255]
    return dict(common, comparisonStatus='different' if count else 'identical',
        diff='raw-diff.png', rawDiff='raw-diff.png', differencePixelCount=count,
        comparisonMetadata=dict(policyVersion=POLICY_VERSION, normalizedSize=list(expected.size),
            rawDifferencePixelCount=count, visualHintsStatus='unavailable', ocrStatus='unavailable',
            ocrMessage='辅助提示不可用；当前展示原始像素差异。自动验证结果不变。'),
        message='辅助提示不可用；可查看标准图、本次图和原始像素差异。')
