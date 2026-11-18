"""Atomic report writes survive transient Windows readers without hiding errors."""
import json
from pathlib import Path
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts/ci'))
import common


def denied(code):
    error = PermissionError('destination is open')
    error.winerror = code
    return error


class JsonOutputTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / 'state.json'
        common.write_json(self.path, {'stage': 'old'})
        self.original = self.path.read_bytes()

    def test_transient_windows_sharing_preserves_old_file_until_atomic_commit(self):
        original_replace = Path.replace
        for code in (5, 32):
            with self.subTest(winerror=code):
                self.path.write_bytes(self.original)
                attempts = []
                def replace(temporary, destination):
                    attempts.append(temporary.read_bytes())
                    self.assertEqual(self.path.read_bytes(), self.original)
                    if len(attempts) < 3:
                        raise denied(code)
                    return original_replace(temporary, destination)
                with patch.object(common.sys, 'platform', 'win32'), \
                        patch.object(Path, 'replace', replace), patch.object(common.time, 'sleep') as sleep:
                    common.write_json(self.path, {'stage': 'new'})
                self.assertEqual(json.loads(self.path.read_text()), {'stage': 'new'})
                self.assertEqual(len(set(attempts)), 1)
                self.assertEqual(sleep.call_count, 2)
                self.assertFalse(self.path.with_suffix('.json.tmp').exists())

    def test_persistent_sharing_failure_is_bounded_and_keeps_original(self):
        error = denied(5)
        with patch.object(common.sys, 'platform', 'win32'), \
                patch.object(Path, 'replace', side_effect=error) as replace, \
                patch.object(common.time, 'sleep') as sleep:
            with self.assertRaises(PermissionError) as caught:
                common.write_json(self.path, {'stage': 'new'})
        self.assertIs(caught.exception, error)
        self.assertEqual(replace.call_count, 20)
        self.assertEqual(sleep.call_count, 19)
        self.assertEqual(self.path.read_bytes(), self.original)

    def test_other_platforms_errors_and_cancellation_are_not_retried(self):
        for platform, error in (('darwin', denied(5)), ('win32', denied(13)),
                                ('win32', FileNotFoundError('missing parent')),
                                ('win32', KeyboardInterrupt())):
            with self.subTest(platform=platform, error=type(error).__name__), \
                    patch.object(common.sys, 'platform', platform), \
                    patch.object(Path, 'replace', side_effect=error) as replace, \
                    patch.object(common.time, 'sleep') as sleep:
                with self.assertRaises(type(error)):
                    common.write_json(self.path, {'stage': 'new'})
                self.assertEqual(replace.call_count, 1)
                sleep.assert_not_called()
                self.assertEqual(self.path.read_bytes(), self.original)

    @unittest.skipUnless(sys.platform == 'win32', 'Requires real Windows file sharing')
    def test_real_windows_reader_released_after_replace_conflict(self):
        conflict = threading.Event()
        errors = []
        original_replace = Path.replace
        def replace(temporary, destination):
            try:
                return original_replace(temporary, destination)
            except PermissionError:
                conflict.set()
                raise
        def write():
            try:
                common.write_json(self.path, {'stage': 'new'})
            except BaseException as error:
                errors.append(error)
        with patch.object(Path, 'replace', replace):
            with self.path.open('rb'):
                writer = threading.Thread(target=write)
                writer.start()
                observed = conflict.wait(2)
            writer.join(7)
        self.assertTrue(observed, 'Did not exercise an actual sharing conflict')
        self.assertFalse(writer.is_alive())
        self.assertEqual(errors, [])
        self.assertEqual(json.loads(self.path.read_text()), {'stage': 'new'})

    @unittest.skipUnless(sys.platform == 'win32', 'Requires real Windows file sharing')
    def test_real_windows_persistent_reader_fails_without_partial_json(self):
        started = time.monotonic()
        with self.path.open('rb'), self.assertRaises(PermissionError):
            common.write_json(self.path, {'stage': 'new'})
        self.assertLess(time.monotonic() - started, 8)
        self.assertEqual(self.path.read_bytes(), self.original)


if __name__ == '__main__':
    unittest.main()
