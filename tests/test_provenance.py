"""A missing nested dependency must not inherit its parent's repository identity."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from acceptance import git_identity


class RepositoryIdentityChecks(unittest.TestCase):
    def test_uninitialized_dependency_is_unknown_while_actual_repository_is_identified(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / '\u9879\u76ee'
            root.mkdir()
            def git(*args):
                return subprocess.check_output(['git', '-C', str(root), *args],
                                               stderr=subprocess.DEVNULL, text=True).strip()
            git('init', '--quiet')
            (root / 'tracked.txt').write_text('repository fixture', encoding='utf-8')
            git('add', 'tracked.txt')
            git('-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid',
                '-c', 'commit.gpgsign=false', 'commit', '--quiet', '--no-verify', '-m', 'fixture')
            empty_sdk = root / 'ThirdParty/libs/GPlatformSDK'
            empty_sdk.mkdir(parents=True)
            self.assertEqual(git_identity(root), dict(commit=git('rev-parse', 'HEAD'), dirty=False))
            self.assertEqual(git_identity(empty_sdk), dict(commit=None, dirty=None))


if __name__ == '__main__':
    unittest.main()
