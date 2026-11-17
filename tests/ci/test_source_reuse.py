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
import common
from common import prepare_cached_source
from fs_fixture import directory_link


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

    def test_host_and_installer_source_directories_cannot_replace_each_other(self):
        host = self.workspace / 'host-source'
        self.source.rename(host)
        self.source.mkdir()
        marker = self.source / 'installer-cache'
        marker.write_text('preserve separate App checkout')
        (host / 'build-Release').mkdir()
        (host / 'build-Release/objects').write_text('host cache')
        with self.assertRaisesRegex(RuntimeError, 'outside'):
            prepare_cached_source(host, self.sha, 'windows')
        prepare_cached_source(host, self.sha, 'windows', host=True)
        self.assertEqual(marker.read_text(), 'preserve separate App checkout')
        self.assertEqual((host / 'build-Release/objects').read_text(), 'host cache')
        self.assertEqual(subprocess.check_output(['git', '-C', str(host), 'rev-parse', 'HEAD'],
                                                text=True).strip(), self.sha)

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
        directory_link(self.source / ".git", external)
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
        directory_link(self.source / "build-Release", target)
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


    def test_old_app_tree_and_nested_ui_cache_migrate_to_real_owner_gitlinks(self):
        def git(repo, *args):
            return subprocess.check_output(['git', '-C', str(repo), *args], stderr=subprocess.PIPE).decode().strip()
        def repository(name):
            repo = self.workspace / name
            repo.mkdir();git(repo, 'init', '-q');git(repo, 'config', 'user.name', 'Fixture')
            git(repo, 'config', 'user.email', 'fixture@example.invalid')
            return repo
        with patch.dict(os.environ, GIT_ALLOW_PROTOCOL='file'):
            ui = repository('ui-upstream')
            (ui / 'source.cpp').write_text('managed fork')
            git(ui, 'add', '.');git(ui, 'commit', '-qm', 'UI input')
            pin = git(ui, 'rev-parse', 'HEAD')
            (self.source / 'App').mkdir()
            (self.source / 'App/old.cpp').write_text('old ordinary App')
            self.git('submodule', 'add', '--name', 'ThirdParty/SmoothUI', str(ui), 'App/ThirdParty/SmoothUI')
            self.git('add', '.');self.git('commit', '-qm', 'Old App tree and real old logical UI name')
            ui_store = Path(git(self.source / 'App/ThirdParty/SmoothUI', 'rev-parse', '--absolute-git-dir'))
            app = repository('app-upstream')
            (app / 'CMakeLists.txt').write_text('# new App owner')
            (app / '.gitmodules').write_text(
                f'[submodule "orca"]\npath = ThirdParty/OrcaSlicer\nurl = {ui.as_posix()}\n'
            )
            git(app, 'add', '.')
            for path in ('ThirdParty/OrcaSlicer',):
                git(app, 'update-index', '--add', '--cacheinfo', '160000', pin, path)
            git(app, 'commit', '-qm', 'App owns Orca')
            self.git('rm', '-r', '--cached', 'App')
            (self.source / '.gitmodules').write_text(
                f'[submodule "App"]\npath = App\nurl = {app.as_posix()}\n'
                f'[submodule "ThirdParty/SmoothUI"]\npath = ThirdParty/SmoothUI\nurl = {ui.as_posix()}\n')
            self.git('add', '.gitmodules')
            self.git('update-index', '--add', '--cacheinfo', '160000', git(app, 'rev-parse', 'HEAD'), 'App')
            self.git('update-index', '--add', '--cacheinfo', '160000', pin, 'ThirdParty/SmoothUI')
            self.git('commit', '-qm', 'New owner graph')
            sha = self.git('rev-parse', 'HEAD')
            self.assertTrue((self.source / 'App/old.cpp').exists())
            prepare_cached_source(self.source, sha, 'macos')
            self.assertFalse((self.source / 'App/old.cpp').exists())
            self.assertTrue((ui_store / 'objects').is_dir())
            common._init_pinned_submodules(self.source, common.git_env(), producer=True)
            self.assertEqual(git(self.source / 'ThirdParty/SmoothUI', 'rev-parse', 'HEAD'), pin)
            self.assertEqual(git(self.source / 'App/ThirdParty/OrcaSlicer', 'rev-parse', 'HEAD'), pin)
            self.assertFalse((self.source / 'App/ThirdParty/libs/GPlatformSDK/.git').exists())
            app_store = Path(git(self.source / 'App', 'rev-parse', '--absolute-git-dir'))
            sentinel = app_store / 'keep-object-store'
            sentinel.write_text('preserve')
            prepare_cached_source(self.source, sha, 'macos')
            self.assertEqual(sentinel.read_text(), 'preserve')
            self.assertTrue((self.source / 'App/.git').exists())
            self.assertEqual(self.git('status', '--porcelain'), '')


if __name__ == "__main__":
    unittest.main()
