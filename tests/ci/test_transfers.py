"""Bounded transfer retries use real child processes and preserve the final error."""
import contextlib
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts/ci"))
import common


class TransferTests(unittest.TestCase):
    def child(self, directory, succeed):
        counter = Path(directory) / "attempt-count"
        code = ("import pathlib,sys; p=pathlib.Path(sys.argv[1]); "
                "n=int(p.read_text())+1 if p.exists() else 1; p.write_text(str(n)); "
                "print('simulated transport error', file=sys.stderr) if n<3 else None; "
                f"sys.stdout.buffer.write(b'payload') if n>=3 and {succeed!r} else None; "
                f"sys.exit(0 if n>=3 and {succeed!r} else 56)")
        return [sys.executable, "-c", code, counter], counter

    def test_interrupted_transfer_succeeds_on_third_attempt(self):
        with tempfile.TemporaryDirectory() as directory:
            args, counter = self.child(directory, True)
            output = io.StringIO()
            with patch.object(common.time, "sleep") as sleep, contextlib.redirect_stdout(output):
                self.assertEqual(common.run(args, attempts=3, capture=True), b"payload")
            self.assertEqual(counter.read_text(), "3")
            self.assertEqual([c.args for c in sleep.call_args_list], [(1,), (2,)])
            self.assertIn("Transfer attempt 3/3", output.getvalue())

    def test_exhausted_transfer_fails_after_three_attempts(self):
        with tempfile.TemporaryDirectory() as directory:
            args, counter = self.child(directory, False)
            with patch.object(common.time, "sleep") as sleep:
                with self.assertRaisesRegex(RuntimeError, r"exit 56.*after 3 attempt"):
                    common.run(args, attempts=3, capture=True)
            self.assertEqual(counter.read_text(), "3")
            self.assertEqual(sleep.call_count, 2)

    def test_normal_commands_are_not_retried(self):
        with tempfile.TemporaryDirectory() as directory:
            args, counter = self.child(directory, False)
            with patch.object(common.time, "sleep") as sleep, self.assertRaises(RuntimeError):
                common.run(args, capture=True)
            self.assertEqual(counter.read_text(), "1")
            sleep.assert_not_called()

    def test_missing_executable_fails_without_retry(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(common.time, "sleep") as sleep, self.assertRaises(FileNotFoundError):
                common.run([Path(directory) / "not-installed"], attempts=3)
            sleep.assert_not_called()

    def test_lfs_upload_and_download_use_native_three_retry_limit(self):
        env = common.git_env()
        value = subprocess.check_output(["git", "config", "--get", "lfs.transfer.maxretries"], env=env)
        self.assertEqual(value.strip(), b"3")


if __name__ == "__main__":
    unittest.main()
