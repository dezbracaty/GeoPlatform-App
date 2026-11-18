"""Real Git checkout of a selected App newer than the host's declared gitlink."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts/ci'))
from common import init_host_submodules


class AppCandidateCheckout(unittest.TestCase):
    def git(self, directory, *arguments):
        return subprocess.check_output(['git', '-C', str(directory), *arguments], stderr=subprocess.PIPE).decode().strip()

    def repository(self, path):
        path.mkdir()
        self.git(path, 'init', '-q')
        self.git(path, 'config', 'user.name', 'CI fixture')
        self.git(path, 'config', 'user.email', 'ci@example.invalid')
        return path

    def commit(self, repository):
        self.git(repository, 'add', '--ignore-removal', '.')
        self.git(repository, 'commit', '-qm', 'fixture source')
        return self.git(repository, 'rev-parse', 'HEAD')

    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name).resolve()
        orca = self.repository(root / 'orca')
        (orca / 'source.cpp').write_text('old Orca')
        old_orca = self.commit(orca)
        (orca / 'source.cpp').write_text('selected Orca')
        self.orca_sha = self.commit(orca)
        app = self.repository(root / 'app')
        (app / 'payload').write_text('old App')
        (app / '.gitmodules').write_text(
            f'[submodule "orca"]\npath = ThirdParty/OrcaSlicer\nurl = {orca.as_uri()}\n'
        )
        self.git(app, 'update-index', '--add', '--cacheinfo', '160000', old_orca, 'ThirdParty/OrcaSlicer')
        self.old_app = self.commit(app)
        (app / 'payload').write_text('selected App')
        self.git(app, 'update-index', '--cacheinfo', '160000', self.orca_sha, 'ThirdParty/OrcaSlicer')
        self.selected_app = self.commit(app)
        self.host = self.repository(root / 'source')
        (self.host / 'host.cpp').write_text('fixed host')
        (self.host / '.gitmodules').write_text(
            '[submodule "App"]\npath = App\nurl = https://github.com/org/App.git\n')
        self.git(self.host, 'update-index', '--add', '--cacheinfo', '160000', self.old_app, 'App')
        self.host_sha = self.commit(self.host)
        environment = patch.dict(os.environ, {
            'GIT_CONFIG_COUNT': '1',
            'GIT_CONFIG_KEY_0': 'url.' + app.as_uri() + '.insteadOf',
            'GIT_CONFIG_VALUE_0': 'https://github.com/org/App.git',
            'GIT_ALLOW_PROTOCOL': 'file', 'GPLATFORM_SOURCE_TOKEN': '',
        })
        environment.start()
        self.addCleanup(environment.stop)

    def test_checkout_uses_exact_app_and_its_forks_without_changing_host_commit(self):
        init_host_submodules(self.host, self.selected_app, 'org/App')
        self.assertEqual(self.git(self.host / 'App', 'rev-parse', 'HEAD'), self.selected_app)
        self.assertEqual((self.host / 'App/payload').read_text(), 'selected App')
        self.assertEqual(self.git(self.host / 'App/ThirdParty/OrcaSlicer', 'rev-parse', 'HEAD'), self.orca_sha)
        self.assertEqual(self.git(self.host, 'rev-parse', 'HEAD'), self.host_sha)
        self.assertEqual(self.git(self.host, 'rev-parse', 'HEAD:App'), self.old_app)
        self.assertEqual(self.git(self.host, 'status', '--porcelain', '--', '.', ':(exclude)App'), '')
        self.assertFalse((self.host / 'App/ThirdParty/libs/GPlatformSDK/.git').exists())

    def test_wrong_repository_is_rejected_before_checkout(self):
        with self.assertRaisesRegex(RuntimeError, 'selected App repository'):
            init_host_submodules(self.host, self.selected_app, 'other/App')
        self.assertFalse((self.host / 'App/.git').exists())
        self.assertEqual(self.git(self.host, 'rev-parse', 'HEAD'), self.host_sha)

    def test_moving_ref_is_rejected_before_any_checkout(self):
        with self.assertRaisesRegex(ValueError, 'complete lowercase commit SHA'):
            init_host_submodules(self.host, 'feature/moving', 'org/App')
        self.assertFalse((self.host / 'App/.git').exists())


if __name__ == '__main__':
    unittest.main()
