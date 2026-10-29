"""Real Git checks for cache independence and authoritative dependency pins."""
import hashlib
import os
from pathlib import Path
import shutil
import stat
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
        # Let Git quote Windows paths and spaces using its own config syntax.
        git(source, 'config', '--file', '.gitmodules', 'submodule.orca.path', module, env=self.env)
        git(source, 'config', '--file', '.gitmodules', 'submodule.orca.url', str(origin), env=self.env)
        git(source, 'add', '.gitmodules', env=self.env)
        git(source, 'update-index', '--add', '--cacheinfo', f'160000,{pin},{module}', env=self.env)
        git(source, 'commit', '-m', 'pin', env=self.env)
        return source, module, cache, origin, pin

    def test_cache_survives_retention_and_missing_pinned_commit_is_fetched(self):
        source, module, cache, origin, pin = self.fixture()
        (cache / 'input.txt').write_text('uncommitted developer change')
        seed_submodules(source, [module], self.config, self.env)
        self.assertFalse((source / module / '.git/objects/info/alternates').exists())
        # Local Git objects are read-only on Windows. Retention must really
        # remove this fixture cache before proving the checkout is independent.
        self.assertTrue(cache.resolve().is_relative_to(self.root))
        def remove_readonly(function, path, error):
            if not isinstance(error[1], PermissionError):
                raise error[1]
            os.chmod(path, stat.S_IREAD | stat.S_IWRITE)
            function(path)
        shutil.rmtree(cache, onerror=remove_readonly)
        self.assertFalse(cache.exists())
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

    def test_windows_checkout_policy_matches_producer_bytes_and_normalizes_clean_old_cache(self):
        source, module, cache, origin, pin = self.fixture()
        pin = self.commit(origin, 'line one\nline two\n')
        git(source, 'update-index', '--cacheinfo', f'160000,{pin},{module}', env=self.env)
        git(source, 'commit', '-m', 'text input', env=self.env)
        seed_submodules(source, [module], self.config, self.env)
        git(source, 'submodule', 'update', '--init', '--', module, env=self.env)
        text = source / module / 'input.txt'
        self.assertEqual(text.read_bytes(), b'line one\r\nline two\r\n')
        git(source / module, 'config', 'core.autocrlf', 'false', env=self.env)
        git(source / module, '-c', 'core.autocrlf=false', 'read-tree', '--empty', env=self.env)
        git(source / module, '-c', 'core.autocrlf=false', 'reset', '--hard', 'HEAD', env=self.env)
        self.assertEqual(text.read_bytes(), b'line one\nline two\n')
        seed_submodules(source, [module], self.config, self.env)
        self.assertEqual(text.read_bytes(), b'line one\r\nline two\r\n')
        self.assertEqual(git(source / module, 'status', '--porcelain', env=self.env), '')

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

    def test_normalized_cache_preserves_file_timestamp_on_reuse(self):
        source, module, cache, origin, pin = self.fixture()
        seed_submodules(source, [module], self.config, self.env)
        git(source, 'submodule', 'update', '--init', '--', module, env=self.env)
        text = source / module / 'input.txt'
        timestamp = 946684800000000000
        os.utime(text, ns=(timestamp, timestamp))
        before = text.stat().st_mtime_ns
        seed_submodules(source, [module], self.config, self.env)
        self.assertEqual(text.stat().st_mtime_ns, before)
        self.assertEqual(text.read_text(), 'new authoritative pin')


if __name__ == '__main__':
    unittest.main()
