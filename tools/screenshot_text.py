"""Optional local text evidence for visual review; never an acceptance gate."""
import ctypes as C
import csv
import io
import json
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile
from functools import lru_cache


@lru_cache(maxsize=64)
def _recognize_cached(png, size):
    from PIL import Image
    with tempfile.TemporaryDirectory(prefix='gplatform-review-text-') as temporary:
        path = Path(temporary) / 'text.png'
        Image.open(io.BytesIO(png)).convert('RGB').resize(
            (size[0] * 2, size[1] * 2), Image.Resampling.BICUBIC).save(path)
        if sys.platform == 'darwin':
            return _vision(path, size)
        executable = shutil.which('tesseract')
        if not executable:
            raise RuntimeError('No local OCR backend; install Tesseract for optional text hints')
        completed = subprocess.run([executable, str(path), 'stdout', '--psm', '11', 'tsv'],
                                   capture_output=True, text=True, encoding='utf8', timeout=45)
        if completed.returncode:
            raise RuntimeError(completed.stderr.strip() or 'Tesseract failed')
        observations = []
        for row in csv.DictReader(io.StringIO(completed.stdout), delimiter='\t'):
            if row.get('level') == '5' and row.get('text', '').strip():
                observations.append(dict(text=row['text'], confidence=max(0, float(row['conf'])) / 100,
                    box=[float(row[k]) / 2 for k in ('left', 'top', 'width', 'height')]))
        return dict(backend='tesseract', observations=observations)


def recognize(image):
    """Unavailable/ambiguous recognition remains explicit and cannot imply success."""
    buffer = io.BytesIO()
    image.save(buffer, 'PNG')
    try:
        result = _recognize_cached(buffer.getvalue(), image.size)
        # Do not share mutable observations between comparisons.
        return dict(status='available', **json.loads(json.dumps(result)))
    except Exception as exc:
        return dict(status='unavailable', backend=None, observations=[], message=str(exc))


def _vision(path, size):
    C.CDLL('/System/Library/Frameworks/Foundation.framework/Foundation')
    C.CDLL('/System/Library/Frameworks/Vision.framework/Vision')
    objc = C.CDLL('/usr/lib/libobjc.A.dylib')
    pointer = C.c_void_p
    objc.objc_getClass.argtypes = [C.c_char_p]
    objc.objc_getClass.restype = pointer
    objc.sel_registerName.argtypes = [C.c_char_p]
    objc.sel_registerName.restype = pointer
    address = C.cast(objc.objc_msgSend, pointer).value
    functions = {}

    def send(obj, name, restype=pointer, args=(), types=()):
        key = restype, types
        if key not in functions:
            functions[key] = C.CFUNCTYPE(restype, pointer, pointer, *types)(address)
        return functions[key](obj, objc.sel_registerName(name.encode()), *args)

    def cls(name):
        return objc.objc_getClass(name.encode())

    def ns(text):
        return send(cls('NSString'), 'stringWithUTF8String:', args=(text.encode(),), types=(C.c_char_p,))

    def string(obj):
        return send(obj, 'UTF8String', C.c_char_p).decode()

    class Point(C.Structure):
        _fields_ = [('x', C.c_double), ('y', C.c_double)]

    class Size(C.Structure):
        _fields_ = [('width', C.c_double), ('height', C.c_double)]

    class Rect(C.Structure):
        _fields_ = [('origin', Point), ('size', Size)]

    pool = send(send(cls('NSAutoreleasePool'), 'alloc'), 'init')
    request = handler = None
    try:
        request = send(send(cls('VNRecognizeTextRequest'), 'alloc'), 'init')
        send(request, 'setRecognitionLevel:', None, (0,), (C.c_long,))
        send(request, 'setRevision:', None, (3,), (C.c_ulong,))
        send(request, 'setUsesLanguageCorrection:', None, (False,), (C.c_bool,))
        languages = send(cls('NSMutableArray'), 'array')
        for language in ['zh-Hans', 'en-US']:
            send(languages, 'addObject:', None, (ns(language),), (pointer,))
        send(request, 'setRecognitionLanguages:', None, (languages,), (pointer,))
        url = send(cls('NSURL'), 'fileURLWithPath:', args=(ns(str(path)),), types=(pointer,))
        handler = send(send(cls('VNImageRequestHandler'), 'alloc'), 'initWithURL:options:',
                       args=(url, send(cls('NSDictionary'), 'dictionary')), types=(pointer, pointer))
        requests = send(cls('NSArray'), 'arrayWithObject:', args=(request,), types=(pointer,))
        error = pointer()
        if not send(handler, 'performRequests:error:', C.c_bool, (requests, C.byref(error)),
                    (pointer, C.POINTER(pointer))):
            raise RuntimeError(string(send(error, 'localizedDescription')) if error.value else 'Vision failed')
        observations = send(request, 'results')
        result = []
        for index in range(send(observations, 'count', C.c_ulong)):
            observation = send(observations, 'objectAtIndex:', args=(index,), types=(C.c_ulong,))
            candidates = send(observation, 'topCandidates:', args=(1,), types=(C.c_ulong,))
            if not send(candidates, 'count', C.c_ulong):
                continue
            candidate = send(candidates, 'objectAtIndex:', args=(0,), types=(C.c_ulong,))
            if platform.machine() == 'x86_64':
                box = Rect()
                stret = C.CFUNCTYPE(None, C.POINTER(Rect), pointer, pointer)(
                    C.cast(objc.objc_msgSend_stret, pointer).value)
                stret(C.byref(box), observation, objc.sel_registerName(b'boundingBox'))
            else:
                box = send(observation, 'boundingBox', Rect)
            result.append(dict(text=string(send(candidate, 'string')),
                confidence=float(send(candidate, 'confidence', C.c_float)),
                box=[box.origin.x * size[0], (1 - box.origin.y - box.size.height) * size[1],
                     box.size.width * size[0], box.size.height * size[1]]))
        return dict(backend='apple-vision-revision-3', observations=result)
    finally:
        if handler:
            send(handler, 'release', None)
        if request:
            send(request, 'release', None)
        send(pool, 'drain', None)
