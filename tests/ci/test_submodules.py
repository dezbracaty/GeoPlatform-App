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

    def test_cached_dirty_dependency_is_reset_without_losing_git_objects(self):
        common.init_source_submodules(self.source, consumer=CONSUMER)
        library = self.source / self.library
        (library / "payload").write_text("dirty dependency")
        (library / "untracked.h").write_text("must not enter build")
        sha = self.git(self.source, "rev-parse", "HEAD")
        with patch.dict(os.environ, GITHUB_WORKSPACE=str(self.source.parent)):
            common.prepare_cached_source(self.source, sha, "windows", consumer=CONSUMER)
        common.init_source_submodules(self.source, consumer=CONSUMER)
        self.assertEqual((library / "payload").read_text(), "original")
        self.assertFalse((library / "untracked.h").exists())
        self.assertTrue((library / ".git").is_file())

    def test_new_parent_pin_syncs_nested_url_and_removes_retired_child(self):
        old_child = self.source.parent / "old child remote"
        new_child = self.source.parent / "new child remote"
        for repo, payload in ((old_child, "old nested payload"), (new_child, "new nested payload")):
            self.repository(repo)
            (repo / "payload").write_text(payload)
            self.git(repo, "add", "payload")
            self.git(repo, "commit", "-qm", payload)
        old_sha = self.git(old_child, "rev-parse", "HEAD")
        new_sha = self.git(new_child, "rev-parse", "HEAD")
        child_path = "nested/child with space"
        retired_path = "retired/old child"
        for name, path in (("nested", child_path), ("retired", retired_path)):
            self.git(self.remote, "config", "--file", ".gitmodules", f"submodule.{name}.path", path)
            self.git(self.remote, "config", "--file", ".gitmodules", f"submodule.{name}.url", old_child.as_uri())
            self.git(self.remote, "update-index", "--add", "--cacheinfo", "160000", old_sha, path)
        self.git(self.remote, "add", ".gitmodules")
        self.git(self.remote, "commit", "-qm", "Old nested dependencies")
        parent_a = self.git(self.remote, "rev-parse", "HEAD")
        self.git(self.source, "update-index", "--cacheinfo", "160000", parent_a, self.library)
        self.commit()
        common.init_source_submodules(self.source, consumer=CONSUMER)
        library = self.source / self.library
        self.assertEqual(self.git(library / child_path, "rev-parse", "HEAD"), old_sha)
        self.assertTrue((library / retired_path / ".git").is_file())

        self.git(self.remote, "config", "--file", ".gitmodules", "submodule.nested.url", new_child.as_uri())
        self.git(self.remote, "config", "--file", ".gitmodules", "--remove-section", "submodule.retired")
        self.git(self.remote, "update-index", "--cacheinfo", "160000", new_sha, child_path)
        self.git(self.remote, "update-index", "--force-remove", "--", retired_path)
        self.git(self.remote, "add", ".gitmodules")
        self.git(self.remote, "commit", "-qm", "Move nested URL and retire old dependency")
        parent_b = self.git(self.remote, "rev-parse", "HEAD")
        self.git(self.source, "update-index", "--cacheinfo", "160000", parent_b, self.library)
        self.commit()
        old_child.rename(old_child.with_name("old child unavailable"))
        sha = self.git(self.source, "rev-parse", "HEAD")
        with patch.dict(os.environ, GITHUB_WORKSPACE=str(self.source.parent)):
            common.prepare_cached_source(self.source, sha, "windows", consumer=CONSUMER)
        common.init_source_submodules(self.source, consumer=CONSUMER)
        self.assertEqual(self.git(library, "rev-parse", "HEAD"), parent_b)
        self.assertEqual(self.git(library / child_path, "rev-parse", "HEAD"), new_sha)
        self.assertEqual(self.git(library / child_path, "config", "--get", "remote.origin.url"), new_child.as_uri())
        self.assertEqual((library / child_path / "payload").read_text(), "new nested payload")
        self.assertFalse((library / retired_path).exists(), "Removed nested source must not survive as untracked build input")
        self.assertEqual(self.git(self.source, "status", "--porcelain", "--untracked-files=all"), "")

    def test_producer_deinitializes_cached_sdk_after_pin_change_without_fetch(self):
        producer_sdk = "App/" + common.SDK_PATH
        if self.sdk != producer_sdk:
            self.git(self.source, "update-index", "--force-remove", "--", self.sdk)
            self.sdk = producer_sdk
        self.declare("sdk", self.sdk, self.remote.as_uri())
        self.git(self.source, "update-index", "--add", "--cacheinfo", "160000", self.pinned, self.sdk)
        self.commit()
        common.init_source_submodules(self.source, consumer=False)
        self.git(self.source, "submodule", "update", "--init", "--checkout", "--depth=1", "--", self.sdk)
        sdk_worktree = self.source / self.sdk
        sdk_gitdir = Path(self.git(sdk_worktree, "rev-parse", "--absolute-git-dir"))
        self.assertEqual(self.git(sdk_worktree, "rev-parse", "HEAD"), self.pinned)

        (self.remote / "payload").write_text("new SDK pin not available in cached SDK")
        self.git(self.remote, "commit", "-qam", "Advance SDK after old checkout")
        new_sdk = self.git(self.remote, "rev-parse", "HEAD")
        self.git(self.source, "update-index", "--cacheinfo", "160000", new_sdk, self.sdk)
        self.commit()
        self.remote.rename(self.remote.with_name("remote unavailable"))
        sha = self.git(self.source, "rev-parse", "HEAD")
        with patch.dict(os.environ, GITHUB_WORKSPACE=str(self.source.parent)):
            common.prepare_cached_source(self.source, sha, "windows", consumer=False)
        common.init_source_submodules(self.source, consumer=False)
        self.assertFalse((sdk_worktree / ".git").exists())
        self.assertTrue(sdk_gitdir.is_dir(), "Deinit must preserve the SDK Git/LFS object store")
        self.git(sdk_gitdir, "cat-file", "-e", self.pinned + "^{commit}")
        missing = subprocess.run(["git", "-C", str(sdk_gitdir), "cat-file", "-e", new_sdk + "^{commit}"],
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertNotEqual(missing.returncode, 0, "Producer must not fetch the new SDK pin")
        self.assertEqual(self.git(self.source, "status", "--porcelain", "--untracked-files=no"), "")

    def test_submodule_recovers_after_two_transport_failures(self):
        original = subprocess.run
        failures = []

        def interrupted(args, **kwargs):
            if "submodule" in args and "update" in args and len(failures) < 2:
                failures.append(args)
                return subprocess.CompletedProcess(args, 128)
            return original(args, **kwargs)

        with patch.object(common.subprocess, "run", side_effect=interrupted):
            with patch.object(common.time, "sleep") as sleep:
                common.init_source_submodules(self.source, consumer=CONSUMER)
        self.assertEqual(len(failures), 2)
        self.assertEqual(sleep.call_count, 2)
        self.assertEqual(self.git(self.source / self.library, "rev-parse", "HEAD"), self.pinned)
        self.assertEqual((self.source / self.library / "payload").read_text(), "original")

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
