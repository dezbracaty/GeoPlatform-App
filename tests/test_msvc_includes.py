"""Check the compiler launcher's dependency translation and failure propagation."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'cmake/msvc_includes.py'
SPEC = importlib.util.spec_from_file_location('msvc_includes', SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class IncludesTest(unittest.TestCase):
    def test_localized_prefix_and_path(self):
        prefix = '\u6ce8\u610f: \u5305\u542b\u6587\u4ef6:'
        path = 'C:\\source\\\u6a21\u578b.h'
        markers = MODULE.prefixes(prefix)
        for encoding in ['utf-8', 'cp936']:
            raw = (prefix + '   ' + path + '\r\n').encode(encoding)
            self.assertEqual(MODULE.normalize(raw, markers),
                             MODULE.CANONICAL + path.encode('utf-8') + b'\n')

    def test_diagnostics_unchanged(self):
        raw = b'file.cpp(8): error C1234: example\r\n'
        self.assertEqual(MODULE.normalize(raw, MODULE.prefixes('')), raw)

    def test_child_output_and_exit_status(self):
        raw = '\u6ce8\u610f: \u5305\u542b\u6587\u4ef6:  C:\\source\\header.h\r\n'.encode('cp936')
        code = f'import sys; sys.stdout.buffer.write({raw!r}); sys.exit(7)'
        result = subprocess.run([sys.executable, str(SCRIPT), '--', sys.executable,
                                 '-c', code], capture_output=True)
        self.assertEqual(result.returncode, 7)
        self.assertEqual(result.stdout, MODULE.CANONICAL + b'C:\\source\\header.h\n')
        self.assertEqual(result.stderr, b'')


if __name__ == '__main__':
    unittest.main()
