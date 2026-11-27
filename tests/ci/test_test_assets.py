"""Exercise the shared preparation with real Git and LFS in isolated fixtures."""
from contextlib import redirect_stderr
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts/ci'))
import common
from common import TEST_ASSETS_PATH as ASSETS_PATH


class TestAssetsPreparation(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name).resolve()
        self.remote = self.root / 'remote'
        self.app = self.root / 'source'
        self.url = common.repository_url(common.test_assets_repository())
        self.env = dict(os.environ, GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_NOSYSTEM='1',
                        GIT_CONFIG_COUNT='1', GIT_CONFIG_KEY_0=f'url.{self.remote.as_uri()}.insteadOf',
                        GIT_CONFIG_VALUE_0=self.url, GIT_ALLOW_PROTOCOL='file',
                        GITHUB_WORKSPACE=str(self.root))
        for path in (self.remote, self.app):
            path.mkdir()
            self.git(path, 'init', '--initial-branch=main')
            self.git(path, 'config', 'user.name', 'Fixture')
            self.git(path, 'config', 'user.email', 'fixture@example.invalid')
        self.git(self.remote, 'lfs', 'install', '--local', '--skip-repo')
        self.git(self.remote, 'lfs', 'track', '*.3mf')
        files = {
            'catalog.json': json.dumps({'schemaVersion': 1, 'scenarioRoot': 'scenarios',
                'scenarioSchema': 'schemas/scenario.schema.json', 'baselineManifest': 'baselines/manifest.json',
                'modelCatalogs': [{'path': 'models/basic/manifest.json'}]}),
            'schemas/scenario.schema.json': '{}',
            'baselines/manifest.json': '{}',
            'models/basic/manifest.json': '{}',
            'models/basic/editing.json': '{}',
            'models/imported/printer-preset-switch/111.3mf': 'fixture asset' * 75,
            'slicing_regression/cases.json': '{}',
            'slicing_regression/cases-extended.json': '{}',
            'scenarios/fixture/events/Journal.json': '{}',
        }
        self.lfs_file = 'models/imported/printer-preset-switch/111.3mf'
        for relative, content in files.items():
            file = self.remote / relative
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text(content, encoding='utf-8')
        self.git(self.remote, 'add', '.')
        self.git(self.remote, 'commit', '-m', 'Known asset fixture')
        self.sha = self.git(self.remote, 'rev-parse', 'HEAD')
        self.git(self.remote, 'branch', 'macmini')
        self.git(self.app, 'commit', '--allow-empty', '-m', 'Empty source')
        self.assets = self.app / ASSETS_PATH

    def git(self, path, *args):
        result = subprocess.run(['git', '-C', str(path), *args], env=self.env,
                                text=True, capture_output=True)
        if result.returncode:
            raise RuntimeError(f'git {args[0]}: {result.stderr}')
        return result.stdout.strip()

    def prepare(self, sha=None, *, app=None, windows=False):
        options = [f'-DWIN32={"ON" if windows else "OFF"}']
        if sha:
            options.append(f'-DGPLATFORM_TEST_ASSETS_SHA={sha}')
        diagnostic = io.StringIO()
        with redirect_stderr(diagnostic):
            try:
                return common.test_assets_checkout(app or self.app, *options, env=self.env)
            except RuntimeError as error:
                raise RuntimeError(diagnostic.getvalue()) from error

    def historical_checkout(self, sha):
        for name, value in [('path', ASSETS_PATH), ('url', self.url), ('update', 'none')]:
            self.git(self.app, 'config', '--file', '.gitmodules', f'submodule.{ASSETS_PATH}.{name}', value)
        self.git(self.app, 'update-index', '--add', '--cacheinfo', '160000', sha, ASSETS_PATH)
        self.git(self.app, 'add', '.gitmodules')
        self.git(self.app, 'commit', '-m', 'Historical App')

    def test_branch_exact_sha_and_repeat_do_not_rewrite_the_existing_branch_or_index(self):
        self.assertEqual(self.prepare(), (self.assets, self.sha))
        self.assertEqual((self.assets / self.lfs_file).read_bytes(), (self.remote / self.lfs_file).read_bytes())
        self.git(self.assets, 'switch', '-c', 'developer')
        index = (self.assets / '.git/index').read_bytes()
        with patch.dict(self.env, GIT_ALLOW_PROTOCOL=''):
            self.assertEqual(self.prepare(self.sha), (self.assets, self.sha))
        self.assertEqual(self.git(self.assets, 'branch', '--show-current'), 'developer')
        self.assertEqual((self.assets / '.git/index').read_bytes(), index)

    def test_configure_selects_platform_and_reuses_only_matching_assets_offline(self):
        self.git(self.remote, 'switch', '-c', 'windows')
        (self.remote / 'models/basic/editing.json').write_text('{"platform": "windows"}', encoding='utf-8')
        self.git(self.remote, 'commit', '-am', 'Windows fixture')
        windows_sha = self.git(self.remote, 'rev-parse', 'HEAD')
        windows_app = self.root / 'windows-app'
        windows_app.mkdir()

        self.assertEqual(self.prepare(),
                         (self.assets, self.sha))
        windows_assets, actual = self.prepare(app=windows_app, windows=True)
        self.assertEqual(actual, windows_sha)

        self.git(windows_assets, 'switch', '-c', 'developer')
        catalog = windows_assets / 'catalog.json'
        edited = catalog.read_bytes() + b'\n'
        catalog.write_bytes(edited)
        index = (windows_assets / '.git/index').read_bytes()
        with patch.dict(self.env, GIT_ALLOW_PROTOCOL=''):
            self.assertEqual(self.prepare(),
                             (self.assets, self.sha))
            self.assertEqual(self.prepare(app=windows_app, windows=True),
                             (windows_assets, windows_sha))
            with self.assertRaisesRegex(RuntimeError, 'cannot be reused for[ \n]+macmini'):
                self.prepare(app=windows_app)
            with self.assertRaisesRegex(RuntimeError, 'expected'):
                self.prepare(self.sha, app=windows_app, windows=True)
            self.assertEqual(self.prepare(windows_sha, app=windows_app),
                             (windows_assets, windows_sha))
        self.assertEqual(self.git(windows_assets, 'branch', '--show-current'), 'developer')
        self.assertEqual((windows_assets / '.git/index').read_bytes(), index)
        self.assertEqual(catalog.read_bytes(), edited)

    def test_configure_never_repairs_an_existing_lfs_pointer(self):
        self.prepare()
        pointer = self.git(self.assets, 'show', f'HEAD:{self.lfs_file}') + '\n'
        file = self.assets / self.lfs_file
        file.write_bytes(pointer.encode('utf-8'))
        with self.assertRaisesRegex(RuntimeError, 'not materialized'):
            self.prepare()
        self.assertEqual(file.read_bytes(), pointer.encode('utf-8'))

    def test_runner_switches_between_legacy_and_independent_checkouts_without_fetching_old_pin(self):
        old = '1' * 40
        self.historical_checkout(old)
        with tempfile.TemporaryDirectory(dir=self.root) as scratch:
            # Include the real product dependency so the runner initializer can walk it.
            product = Path(scratch) / 'product'
            product.mkdir()
            self.git(product, 'init', '--initial-branch=main')
            self.git(product, 'config', 'user.name', 'Fixture')
            self.git(product, 'config', 'user.email', 'fixture@example.invalid')
            self.git(product, 'commit', '--allow-empty', '-m', 'Orca fixture')
            self.git(self.app, 'submodule', 'add', product.as_uri(), 'ThirdParty/OrcaSlicer')
            self.git(self.app, 'commit', '-m', 'Add product dependency')
            historical = self.git(self.app, 'rev-parse', 'HEAD')
            self.assertNotEqual(old, self.sha)
            with self.assertRaisesRegex(RuntimeError, 'fetch'):
                self.git(self.app, 'fetch', self.remote.as_uri(), old)
            with patch.dict(os.environ, self.env, clear=True):
                common.prepare_cached_source(self.app, historical, 'windows', consumer=True)
                common.init_source_submodules(self.app)
                self.assertEqual(self.prepare(self.sha), (self.assets, self.sha))
                self.assertEqual(self.git(self.app, 'rev-parse', f'HEAD:{ASSETS_PATH}'), old)

                self.git(self.app, 'update-index', '--force-remove', '--', ASSETS_PATH)
                self.git(self.app, 'config', '--file', '.gitmodules', '--remove-section', f'submodule.{ASSETS_PATH}')
                (self.app / '.gitignore').write_text(f'/{ASSETS_PATH}/\n', encoding='utf-8')
                self.git(self.app, 'add', '.gitmodules', '.gitignore')
                self.git(self.app, 'commit', '-m', 'Decoupled App')
                current = self.git(self.app, 'rev-parse', 'HEAD')
                common.prepare_cached_source(self.app, current, 'windows', consumer=True)
                self.assertFalse((self.assets / '.git').exists())
                common.init_source_submodules(self.app)
                self.assertEqual(self.prepare(self.sha), (self.assets, self.sha))

                self.git(self.app, 'checkout', '--detach', historical)
                common.prepare_cached_source(self.app, historical, 'windows', consumer=True)
                common.init_source_submodules(self.app)
                self.assertEqual(self.prepare(self.sha), (self.assets, self.sha))

    def test_reuses_an_absorbed_legacy_gitfile_without_changing_its_owner(self):
        self.historical_checkout(self.sha)
        self.git(self.app, 'submodule', 'update', '--init', '--checkout', '--', ASSETS_PATH)
        self.git(self.assets, 'lfs', 'install', '--local', '--skip-repo')
        self.git(self.assets, 'lfs', 'pull')
        marker = (self.assets / '.git').read_bytes()
        self.assertEqual(self.prepare(self.sha), (self.assets, self.sha))
        self.assertEqual((self.assets / '.git').read_bytes(), marker)

    def test_dirty_checkout_is_preserved_locally_and_rejected_by_runner_cleanup(self):
        self.prepare(self.sha)
        changed = self.assets / 'catalog.json'
        changed.write_text('edited', encoding='utf-8')
        with self.assertRaisesRegex(RuntimeError, 'JSON'):
            self.prepare(self.sha)
        with patch.dict(os.environ, self.env, clear=True):
            with self.assertRaisesRegex(RuntimeError, 'local changes'):
                common.prepare_cached_source(self.app, self.git(self.app, 'rev-parse', 'HEAD'), 'macos')
        self.assertEqual(changed.read_text(encoding='utf-8'), 'edited')

    def test_refuses_foreign_origin_or_git_dir(self):
        self.prepare(self.sha)
        self.git(self.assets, 'remote', 'set-url', 'origin', self.remote.as_uri())
        with self.assertRaisesRegex(RuntimeError, 'origin does not match'):
            self.prepare(self.sha)
        (self.assets / '.git').rename(self.root / 'foreign-git')
        (self.assets / '.git').write_text(f'gitdir: {self.root / "foreign-git"}\n', encoding='utf-8')
        with self.assertRaisesRegex(RuntimeError, 'outside its expected location'):
            self.prepare(self.sha)

    def test_rejects_junction_before_touching_target(self):
        external = self.root / 'external'
        external.mkdir()
        sentinel = external / 'keep'
        sentinel.write_text('owned elsewhere', encoding='utf-8')
        self.assets.parent.mkdir()
        if os.name == 'nt':
            subprocess.run(['cmd', '/c', 'mklink', '/J', str(self.assets), str(external)], check=True)
            with self.assertRaisesRegex(RuntimeError, 'junction'):
                self.prepare(self.sha)
            with patch.dict(os.environ, self.env, clear=True), self.assertRaisesRegex(RuntimeError, 'junction'):
                common.prepare_cached_source(self.app, self.git(self.app, 'rev-parse', 'HEAD'), 'macos')
        else:
            self.assets.symlink_to(external, target_is_directory=True)
            with self.assertRaisesRegex(RuntimeError, 'symlink'):
                self.prepare(self.sha)
        self.assertEqual(sentinel.read_text(encoding='utf-8'), 'owned elsewhere')

    def test_wrong_sha_or_missing_lfs_object_does_not_publish_checkout(self):
        with self.assertRaisesRegex(RuntimeError, 'fetch'):
            self.prepare('1' * 40)
        self.assertFalse(self.assets.exists())
        objects = [file for file in (self.remote / '.git/lfs/objects').rglob('*') if file.is_file()]
        self.assertTrue(objects)
        for file in objects:
            file.unlink()
        with self.assertRaisesRegex(RuntimeError, r'git lfs|not materialized'):
            self.prepare(self.sha)
        self.assertFalse(self.assets.exists())


if __name__ == '__main__':
    unittest.main()
