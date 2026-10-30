"""Cleanup ownership and Windows readonly recovery behavior."""
import os
from pathlib import Path
import stat
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts/ci"))
from common import _retry_readonly, remove_owned_tree


class CleanupTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_real_owned_checkout_with_readonly_git_object_is_removed(self):
        source = self.root / "source"
        (source / ".git/objects").mkdir(parents=True)
        obj = source / ".git/objects/object"
        obj.write_bytes(b"git object")
        obj.chmod(stat.S_IREAD)
        remove_owned_tree(source, self.root, require_git=True)
        self.assertFalse(source.exists())

    def test_windows_callback_retries_only_readonly_permission_error(self):
        path = self.root / "object"
        path.write_bytes(b"readonly")
        path.chmod(stat.S_IREAD)
        calls = []
        def remove(filename):
            calls.append(filename)
            self.assertTrue(os.lstat(filename).st_mode & stat.S_IWRITE)
            os.unlink(filename)
        _retry_readonly(remove, str(path), PermissionError("readonly"), windows=True)
        self.assertEqual(calls, [str(path)])
        self.assertFalse(path.exists())

    def test_other_errors_and_writable_permission_error_are_not_hidden(self):
        path = self.root / "object"
        path.write_bytes(b"writable")
        with self.assertRaises(FileNotFoundError):
            _retry_readonly(os.unlink, str(path), FileNotFoundError("missing"), windows=True)
        with self.assertRaises(PermissionError):
            _retry_readonly(os.unlink, str(path), PermissionError("not readonly"), windows=True)
        self.assertTrue(path.exists())

    def test_symlink_cannot_clean_external_directory(self):
        outside = self.root / "outside"
        outside.mkdir()
        sentinel = outside / "sentinel"
        sentinel.write_text("preserve")
        parent = self.root / "workspace"
        parent.mkdir()
        (parent / "source").symlink_to(outside, target_is_directory=True)
        with self.assertRaises(RuntimeError):
            remove_owned_tree(parent / "source", parent)
        self.assertEqual(sentinel.read_text(), "preserve")

    def test_checkout_without_git_marker_is_not_removed(self):
        source = self.root / "source"
        source.mkdir()
        with self.assertRaisesRegex(RuntimeError, "marker"):
            remove_owned_tree(source, self.root, require_git=True)
        self.assertTrue(source.exists())


if __name__ == "__main__":
    unittest.main()
