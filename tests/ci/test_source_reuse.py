"""Real Git fixtures for retained runner sources and preset build directories."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts/ci"))
from common import prepare_cached_source


class SourceReuseTests(unittest.TestCase):
    def git(self, *args):
        return subprocess.check_output(["git", "-C", str(self.source), *args], stderr=subprocess.PIPE).decode().strip()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.workspace = Path(self.temp.name).resolve()
        self.source = self.workspace / "source"
        self.source.mkdir()
        self.git("init", "-q")
        self.git("config", "user.name", "Test")
        self.git("config", "user.email", "test@example.invalid")
        (self.source / "payload").write_text("pinned")
        (self.source / ".gitignore").write_text("build*/\n*.local\n")
        self.git("add", ".")
        self.git("commit", "-qm", "Pinned source")
        self.sha = self.git("rev-parse", "HEAD")
        self.environment = patch.dict(os.environ, GITHUB_WORKSPACE=str(self.workspace))
        self.environment.start()
        self.addCleanup(self.environment.stop)

    def marker(self, name):
        path = self.source / name / "sentinel"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("reuse")
        return path

    def test_repeat_retains_git_and_only_current_preset_builds(self):
        marker = self.marker("build-Release")
        nested = self.marker("old/build-Release")
        unrelated = self.marker("build-Debug")
        (self.source / "payload").write_text("dirty")
        (self.source / "config.local").write_text("old ignored config")
        (self.source / "CMakeUserPresets.json").write_text("old user preset")
        git_marker = self.source / ".git/retained-test-marker"
        git_marker.write_text("objects stay")
        for _ in range(2):
            prepare_cached_source(self.source, self.sha, "windows")
            self.assertEqual(marker.read_text(), "reuse")
            self.assertEqual(git_marker.read_text(), "objects stay")
            self.assertEqual((self.source / "payload").read_text(), "pinned")
            self.assertEqual(self.git("status", "--porcelain"), "")
        for path in (nested, unrelated, self.source / "config.local", self.source / "CMakeUserPresets.json"):
            self.assertFalse(path.exists())

    def test_new_fixed_commit_preserves_build_objects(self):
        marker = self.marker("build-Release")
        (self.source / "payload").write_text("next pin")
        self.git("commit", "-qam", "Advance source")
        sha = self.git("rev-parse", "HEAD")
        prepare_cached_source(self.source, sha, "windows")
        self.assertEqual((self.source / "payload").read_text(), "next pin")
        self.assertTrue(marker.is_file())

    def test_wrong_sha_or_external_checkout_fails_before_reset(self):
        (self.source / "payload").write_text("do not touch")
        with self.assertRaisesRegex(RuntimeError, "fixed source SHA"):
            prepare_cached_source(self.source, "0" * 40, "windows")
        with patch.dict(os.environ, GITHUB_WORKSPACE=str(self.workspace / "other")):
            with self.assertRaisesRegex(RuntimeError, "outside"):
                prepare_cached_source(self.source, self.sha, "windows")
        self.assertEqual((self.source / "payload").read_text(), "do not touch")

    def test_external_git_metadata_is_rejected_without_resetting_index(self):
        external = self.workspace / "external-metadata"
        (self.source / ".git").rename(external)
        (self.source / ".git").symlink_to(external, target_is_directory=True)
        (self.source / "payload").write_text("do not reset")
        before = (external / "index").read_bytes(), (external / "index").stat().st_mtime_ns
        with self.assertRaisesRegex(RuntimeError, "outside"):
            prepare_cached_source(self.source, self.sha, "windows")
        self.assertEqual((self.source / "payload").read_text(), "do not reset")
        self.assertEqual(((external / "index").read_bytes(), (external / "index").stat().st_mtime_ns), before)

    def test_build_symlink_is_rejected_without_touching_target(self):
        target = self.workspace / "external"
        target.mkdir()
        (target / "sentinel").write_text("preserve")
        (self.source / "build-Release").symlink_to(target, target_is_directory=True)
        with self.assertRaisesRegex(RuntimeError, "untracked directory"):
            prepare_cached_source(self.source, self.sha, "windows")
        self.assertEqual((target / "sentinel").read_text(), "preserve")

    def test_new_commit_cannot_turn_a_cache_into_tracked_sources(self):
        marker = self.marker("build-Release")
        self.git("add", "-f", str(marker))
        self.git("commit", "-qm", "Tracked directory conflicts with cache")
        with self.assertRaisesRegex(RuntimeError, "untracked directory"):
            prepare_cached_source(self.source, self.git("rev-parse", "HEAD"), "windows")
        self.assertTrue(marker.is_file())

    def test_macos_retains_both_arches_and_consumer_universal(self):
        markers = [self.marker(name) for name in (
            "build-arm64-Release", "build-x86_64-Release", "build-universal-Release")]
        prepare_cached_source(self.source, self.sha, "macos", consumer=True)
        self.assertTrue(all(path.is_file() for path in markers))
        prepare_cached_source(self.source, self.sha, "macos")
        self.assertTrue(all(path.is_file() for path in markers[:2]))
        self.assertFalse(markers[2].exists())

    def test_incompatible_cmake_cache_fails_before_deleting_anything(self):
        marker = self.marker("build-Release")
        cache = marker.parent / "CMakeCache.txt"
        baseline = {"CMAKE_HOME_DIRECTORY": str(self.source), "CMAKE_GENERATOR": "Ninja",
                    "CMAKE_BUILD_TYPE": "Release", "Qt6_DIR": "/qt/current"}
        with patch.dict(os.environ, Qt6_DIR="/qt/current"):
            for key, wrong in (("CMAKE_HOME_DIRECTORY", "/another/source"),
                               ("CMAKE_BUILD_TYPE", "Debug"), ("Qt6_DIR", "/qt/old")):
                values = dict(baseline, **{key: wrong})
                cache.write_text("\n".join(f"{k}:STRING={v}" for k, v in values.items()))
                with self.assertRaisesRegex(RuntimeError, "Preset cache mismatch for " + key):
                    prepare_cached_source(self.source, self.sha, "windows")
                self.assertTrue(marker.is_file())
            cache.write_text("\n".join(f"{k}:STRING={v}" for k, v in baseline.items()))
            prepare_cached_source(self.source, self.sha, "windows")
        self.assertTrue(marker.is_file())

    def test_wrong_mac_architecture_is_rejected(self):
        marker = self.marker("build-arm64-Release")
        (marker.parent / "CMakeCache.txt").write_text(
            f"CMAKE_HOME_DIRECTORY:INTERNAL={self.source}\nCMAKE_GENERATOR:INTERNAL=Ninja\n"
            "CMAKE_BUILD_TYPE:STRING=Release\nGPLATFORM_TARGET_ARCH:STRING=x86_64\n"
            "CMAKE_OSX_ARCHITECTURES:STRING=x86_64\n")
        with self.assertRaisesRegex(RuntimeError, "Preset cache mismatch for GPLATFORM_TARGET_ARCH"):
            prepare_cached_source(self.source, self.sha, "macos")
        self.assertTrue(marker.is_file())


if __name__ == "__main__":
    unittest.main()
