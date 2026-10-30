"""Initialize real pinned Git dependencies despite obsolete declarations."""
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

CONSUMER = True


class SubmoduleTests(unittest.TestCase):
    def git(self, repo, *args):
        return subprocess.check_output(["git", "-C", str(repo), *args], stderr=subprocess.PIPE).decode().strip()

    def repository(self, path):
        path.mkdir()
        self.git(path, "init", "-q")
        self.git(path, "config", "user.name", "CI fixture")
        self.git(path, "config", "user.email", "ci@example.invalid")

    def commit(self):
        self.git(self.source, "add", ".gitmodules")
        self.git(self.source, "commit", "-qm", "Fixed dependencies")

    def declare(self, name, path, url):
        self.git(self.source, "config", "--file", ".gitmodules", f"submodule.{name}.path", path)
        self.git(self.source, "config", "--file", ".gitmodules", f"submodule.{name}.url", str(url))

    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        self.remote = root / "remote"
        self.source = root / "source"
        for repo in [self.remote, self.source]:
            self.repository(repo)
        (self.remote / "payload").write_text("ancestor")
        self.git(self.remote, "add", "payload")
        self.git(self.remote, "commit", "-qm", "Older dependency history")
        (self.remote / "payload").write_text("original")
        self.git(self.remote, "add", "payload")
        self.git(self.remote, "commit", "-qm", "Original dependency")
        self.pinned = self.git(self.remote, "rev-parse", "HEAD")
        (self.remote / "payload").write_text("new branch head")
        self.git(self.remote, "commit", "-qam", "New dependency head")
        self.library = "deps/library with space"
        self.sdk = common.SDK_PATH if CONSUMER else "App/" + common.SDK_PATH
        for name, path in [("library", self.library), ("sdk", self.sdk)]:
            self.declare(name, path, self.remote.as_uri() if CONSUMER or name == "library" else root / "must-not-fetch")
            self.git(self.source, "update-index", "--add", "--cacheinfo", "160000", self.pinned, path)
        self.declare("stale", "ThirdParty/testTransaction2", root / "removed-repository")
        self.commit()
        self.environment = patch.dict(os.environ, GIT_ALLOW_PROTOCOL="file")
        self.environment.start()
        self.addCleanup(self.environment.stop)

    def test_stale_declaration_is_skipped_and_real_dependency_uses_gitlink(self):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            common.init_source_submodules(self.source, consumer=CONSUMER)
        self.assertIn("Skipping stale submodule declaration", output.getvalue())
        self.assertEqual(self.git(self.source / self.library, "rev-parse", "HEAD"), self.pinned)
        self.assertEqual((self.source / self.library / "payload").read_text(), "original")
        self.assertFalse((self.source / "ThirdParty/testTransaction2").exists())
        self.assertEqual((self.source / self.sdk / ".git").exists(), CONSUMER)
        for path in [self.library] + ([self.sdk] if CONSUMER else []):
            self.assertEqual(self.git(self.source / path, "rev-parse", "--is-shallow-repository"), "true")
            self.assertEqual(self.git(self.source / path, "rev-list", "--count", "HEAD"), "1")

    def test_failed_submodule_update_keeps_git_error_visible(self):
        self.declare("library", self.library, (self.remote.parent / "missing.git").as_uri())
        self.commit()
        code = ("import sys; from pathlib import Path; "
                "sys.path.insert(0, sys.argv[1]); import common; "
                "common.init_source_submodules(Path(sys.argv[2]), consumer=sys.argv[3] == 'true')")
        env = {key:value for key,value in os.environ.items()
               if key not in ("GPLATFORM_SOURCE_TOKEN", "GITHUB_ACTIONS")}
        result = subprocess.run([sys.executable, "-c", code, str(Path(common.__file__).parent),
                                 str(self.source), str(CONSUMER).lower()],
                                env=env, capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(f"Initializing pinned submodule {self.library!r} at {self.pinned}", result.stdout)
        self.assertIn("does not appear to be a git repository", result.stderr)

    def test_derived_git_authentication_is_masked_only_in_actions(self):
        fixture_token = "ci-fixture-not-a-real-token"
        for actions in ("true", "false"):
            output = io.StringIO()
            with patch.dict(os.environ, GITHUB_ACTIONS=actions, GPLATFORM_SOURCE_TOKEN=fixture_token):
                with contextlib.redirect_stdout(output):
                    env = common.git_env()
            header = env["GIT_CONFIG_VALUE_" + str(int(env["GIT_CONFIG_COUNT"]) - 1)].split()[-1]
            expected = [f"::add-mask::{fixture_token}", f"::add-mask::{header}"] if actions == "true" else []
            self.assertEqual(output.getvalue().splitlines(), expected)

    def test_missing_declaration_fails_before_any_dependency_checkout(self):
        self.git(self.source, "config", "--file", ".gitmodules", "--remove-section", "submodule.library")
        self.commit()
        with self.assertRaisesRegex(RuntimeError, "missing its .gitmodules declaration"):
            common.init_source_submodules(self.source, consumer=CONSUMER)
        self.assertFalse((self.source / self.library / ".git").exists())
        self.assertFalse((self.source / self.sdk / ".git").exists())

    def test_consumer_requires_actual_sdk_gitlink(self):
        self.git(self.source, "update-index", "--force-remove", "--", self.sdk)
        self.git(self.source, "commit", "-qm", "Remove SDK gitlink")
        with self.assertRaisesRegex(RuntimeError, "SDK gitlink is missing"):
            common.init_source_submodules(self.source, consumer=True)
        self.assertFalse((self.source / self.library / ".git").exists())

    def test_duplicate_declaration_is_rejected(self):
        self.declare("duplicate", self.library, self.remote)
        self.commit()
        with self.assertRaisesRegex(RuntimeError, "Duplicate submodule path"):
            common.init_source_submodules(self.source, consumer=CONSUMER)

    def test_missing_gitmodules_is_rejected_before_checkout(self):
        (self.source / ".gitmodules").unlink()
        self.git(self.source, "add", "-u", ".gitmodules")
        self.git(self.source, "commit", "-qm", "Remove declarations")
        with self.assertRaisesRegex(RuntimeError, "no .gitmodules declarations"):
            common.init_source_submodules(self.source, consumer=CONSUMER)
        self.assertFalse((self.source / self.library / ".git").exists())


if __name__ == "__main__":
    unittest.main()
