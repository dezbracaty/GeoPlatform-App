"""Git-content build gating, atomic menus and SDK/download integrity contracts."""
import contextlib
import hashlib
import io
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch

APP = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(APP / 'scripts/ci'))
import app_build
import change_detection as changes
import common
import refresh_branches as menus


class AppCITests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='app-ci-check-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.output = contextlib.redirect_stdout(io.StringIO())
        self.output.__enter__()
        self.addCleanup(self.output.__exit__, None, None, None)
        common.run(['git', 'init', '-b', 'main', self.root], capture=True)
        common.git(self.root, 'config', 'user.name', 'Test Author')
        common.git(self.root, 'config', 'user.email', 'test@example.invalid')
        for name in menus.WORKFLOWS:
            destination = self.root / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(APP / name, destination)
        (self.root / 'main.cpp').write_text('int main() {}\n')
        self.base = self.commit('ordinary baseline commit')

    def commit(self, message):
        common.git(self.root, 'add', '-A')
        common.git(self.root, 'commit', '-m', message)
        return common.git(self.root, 'rev-parse', 'HEAD')

    def update_menus(self):
        for name in menus.WORKFLOWS:
            path = self.root / name
            text = menus.replace_choices(path.read_text(), 'source_ref', ['main', 'new-branch'], 'main')
            path.write_text(text)

    def test_menu_only_push_skips_build_without_any_commit_message_convention(self):
        self.update_menus()
        head = self.commit('any text a person or a robot might use')
        self.assertFalse(changes.requires_build(self.root, self.base, head))

    def test_source_and_mixed_menu_source_pushes_require_build(self):
        self.update_menus()
        (self.root / 'main.cpp').write_text('int main() { return 1; }\n')
        self.assertTrue(changes.requires_build(self.root, self.base, self.commit('update')))

    def test_workflow_execution_changes_and_injected_yaml_require_build(self):
        path = self.root / menus.WORKFLOWS[0]
        original = path.read_text()
        for value in (original.replace('ubuntu-24.04', 'ubuntu-22.04'),
                      original.replace('# branches:source_ref:begin\n',
                                       '# branches:source_ref:begin\n          other_yaml: true\n')):
            path.write_text(value)
            head = self.commit('workflow change')
            self.assertTrue(changes.requires_build(self.root, self.base, head))

    def test_new_branch_and_dependency_gitlink_changes_require_build(self):
        self.assertTrue(changes.requires_build(self.root, '0' * 40, self.base))
        common.git(self.root, 'update-index', '--add', '--cacheinfo',
                   '160000,' + '1' * 40 + ',ThirdParty/libs/GPlatformSDK')
        common.git(self.root, 'commit', '-m', 'SDK update')
        self.assertTrue(changes.requires_build(self.root, self.base, common.git(self.root, 'rev-parse', 'HEAD')))

    def test_menu_render_failure_changes_neither_platform(self):
        originals = {name: (self.root / name).read_bytes() for name in menus.WORKFLOWS}
        invalid = self.root / menus.WORKFLOWS[1]
        invalid.write_text(invalid.read_text().replace('# branches:sdk_ref:end', '# missing marker'))
        baseline = {name: (self.root / name).read_bytes() for name in menus.WORKFLOWS}
        values = {'source_ref': ['main', 'new-branch'], **{key: [menus.PIN, 'main'] for key in menus.MODULES}}
        with patch.object(menus, 'collect', return_value=('main', values)):
            with self.assertRaises(RuntimeError):
                menus.refresh(self.root, 'example/App')
        self.assertEqual(baseline, {name: (self.root / name).read_bytes() for name in menus.WORKFLOWS})
        invalid.write_bytes(originals[menus.WORKFLOWS[1]])
        with patch.object(menus, 'collect', return_value=('main', values)):
            changed, counts = menus.refresh(self.root, 'example/App')
        self.assertEqual(len(changed), 2)
        self.assertEqual(counts['sdk_ref'], 1)

    def test_sdk_manifest_rejects_corrupt_files_before_compilation(self):
        slot = self.root / app_build.SDK / 'AMD64/Release'
        slot.mkdir(parents=True)
        common.write_json(slot / 'sdk-build.json', {'SDK_FORMAT': 2, 'CMAKE_BUILD_TYPE': 'Release',
                          'CMAKE_SYSTEM_NAME': 'Windows', 'GPLATFORM_SDK_ARCH': 'AMD64'})
        library = slot / 'library.lib'
        library.write_bytes(b'compiled SDK')
        common.write_json(slot / 'sdk-manifest.json', {'library.lib': app_build.digest(library)})
        self.assertIn('AMD64', app_build.verify_sdk(self.root, 'windows'))
        library.write_bytes(b'corrupted SDK')
        with self.assertRaisesRegex(RuntimeError, 'changed'):
            app_build.verify_sdk(self.root, 'windows')

    def test_download_contains_only_installer_and_its_checksum_and_resume_checks_platform(self):
        directory = self.root / 'runs/123-1'
        deliverables = directory / 'deliverables'
        deliverables.mkdir(parents=True)
        installer = deliverables / 'GPlatform.dmg'
        installer.write_bytes(b'verified installer')
        manifest = {'kind': 'standalone-app', 'status': 'built', 'platform': 'macos',
                    'task': 'installer', 'delivery': 'artifacts',
                    'artifacts': {installer.name: app_build.digest(installer)}}
        common.write_json(directory / 'build-manifest.json', manifest)
        app_build.download(directory)
        self.assertEqual({p.name for p in (directory / 'download').iterdir()},
                         {'GPlatform.dmg', 'checksums.sha256'})
        self.assertEqual((directory / 'download/checksums.sha256').read_text(),
                         f'{app_build.digest(installer)}  GPlatform.dmg\n')
        with self.assertRaises(RuntimeError):
            app_build.completed(directory, 'windows')
        installer.write_bytes(b'changed')
        with self.assertRaisesRegex(RuntimeError, 'changed'):
            app_build.completed(directory, 'macos')


if __name__ == '__main__':
    unittest.main()
