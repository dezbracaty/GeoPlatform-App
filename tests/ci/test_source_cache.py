"""Real Git checks for cache independence and authoritative dependency pins."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts/ci'))
from common import git, git_env, write_json
from source_cache import seed_snapshot, seed_submodules


class SourceCacheTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='source cache ')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.config = {'root': str(self.root)}
        self.env = dict(git_env(), GIT_ALLOW_PROTOCOL='file:https')

    def repository(self, path):
        path.mkdir(parents=True)
        git(path, 'init', env=self.env)
        git(path, 'config', 'user.name', 'Fixture', env=self.env)
        git(path, 'config', 'user.email', 'fixture@example.invalid', env=self.env)
        return path

    def commit(self, repo, text):
        (repo / 'input.txt').write_text(text)
        git(repo, 'add', '.', env=self.env)
        git(repo, 'commit', '-m', text, env=self.env)
        return git(repo, 'rev-parse', 'HEAD', env=self.env)

    def fixture(self):
        origin = self.repository(self.root / 'fork.git')
        self.commit(origin, 'old')
        cache = self.root / 'runner/_work/GPlatform/GPlatform/source/App/ThirdParty/OrcaSlicer'
        cache.parent.mkdir(parents=True)
        subprocess.run(['git', 'clone', str(origin), str(cache)], env=self.env, check=True, capture_output=True)
        pin = self.commit(origin, 'new authoritative pin')
        source = self.repository(self.root / 'fresh')
        module = 'ThirdParty/OrcaSlicer'
        (source / '.gitmodules').write_text(f'[submodule "orca"]\npath = {module}\nurl = {origin}\n')
        git(source, 'add', '.gitmodules', env=self.env)
        git(source, 'update-index', '--add', '--cacheinfo', f'160000,{pin},{module}', env=self.env)
        git(source, 'commit', '-m', 'pin', env=self.env)
        return source, module, cache, origin, pin

    def test_cache_survives_retention_and_missing_pinned_commit_is_fetched(self):
        source, module, cache, origin, pin = self.fixture()
        (cache / 'input.txt').write_text('uncommitted developer change')
        seed_submodules(source, [module], self.config, self.env)
        self.assertFalse((source / module / '.git/objects/info/alternates').exists())
        shutil.rmtree(cache)
        git(source, 'submodule', 'update', '--init', '--depth', '1', '--', module, env=self.env)
        self.assertEqual(git(source / module, 'rev-parse', 'HEAD', env=self.env), pin)
        self.assertEqual((source / module / 'input.txt').read_text(), 'new authoritative pin')
        git(source / module, 'fsck', '--no-dangling', env=self.env)

    def test_wrong_remote_cache_is_not_used(self):
        source, module, cache, origin, pin = self.fixture()
        git(cache, 'remote', 'set-url', 'origin', str(self.root / 'unrelated'), env=self.env)
        seed_submodules(source, [module], self.config, self.env)
        self.assertFalse((source / module / '.git').exists())

    def test_already_pinned_cache_and_interrupted_clone_materialize_files(self):
        source, module, cache, origin, pin = self.fixture()
        old = git(cache, 'rev-parse', 'HEAD', env=self.env)
        git(source, 'update-index', '--cacheinfo', f'160000,{old},{module}', env=self.env)
        git(source, 'commit', '-m', 'pin already in cache', env=self.env)
        seed_submodules(source, [module], self.config, self.env)
        git(source, 'submodule', 'update', '--init', '--', module, env=self.env)
        self.assertEqual((source / module / 'input.txt').read_text(), 'old')
        self.assertEqual(git(source / module, 'status', '--porcelain', env=self.env), '')
        (source / module / 'input.txt').unlink()
        seed_submodules(source, [module], self.config, self.env)
        self.assertEqual((source / module / 'input.txt').read_text(), 'old')

    def test_source_snapshot_is_bound_to_its_saved_digest(self):
        origin = self.repository(self.root / 'original')
        head = self.commit(origin, 'snapshot')
        directory = self.root / 'runs/1-1'
        directory.mkdir(parents=True)
        bundle = directory / 'source.bundle'
        git(origin, 'bundle', 'create', bundle, 'HEAD', env=self.env)
        manifest = {'status': 'built', 'source_path': str(origin),
                    'source_bundle_sha256': hashlib.sha256(bundle.read_bytes()).hexdigest()}
        write_json(directory / 'build-manifest.json', manifest)
        destination = self.root / 'source'
        seed_snapshot(destination, self.config, self.env)
        self.assertEqual(git(destination, 'rev-parse', 'HEAD', env=self.env), head)
        bundle.write_bytes(bundle.read_bytes() + b'corruption')
        with self.assertRaisesRegex(RuntimeError, 'checksum'):
            seed_snapshot(destination, self.config, self.env)


if __name__ == '__main__':
    unittest.main()
