"""Real Git/LFS tests; all writes stay inside TemporaryDirectory."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from types import SimpleNamespace
from zipfile import ZipFile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts/ci"))
import common
from common import digest, git, git_env, text_git
from update_sdk import (SLOTS, download_artifact, gh_git_env, prepare_targets, publish_run, publish_slots,
                        successful_build_artifacts, validate_package)
import hashlib
import importlib.util
from fs_fixture import directory_link


class Updates(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="ci-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.env = dict(os.environ, GIT_AUTHOR_NAME="Test", GIT_AUTHOR_EMAIL="test@example.invalid",
                        GIT_COMMITTER_NAME="Test", GIT_COMMITTER_EMAIL="test@example.invalid",
                        GIT_ALLOW_PROTOCOL="file", GIT_LFS_SKIP_SMUDGE="1")
        self.patch = patch.dict(os.environ, self.env)
        self.patch.start(); self.addCleanup(self.patch.stop)
        self.sdk = self.repo("sdk")
        for arch in ("AMD64", "arm64", "x86_64"):
            self.write(self.sdk, f"{arch}/Release/sdk-build.json", json.dumps({"SDK_FORMAT": 2}))
            self.write(self.sdk, f"{arch}/Release/old.txt", "old")
        self.write(self.sdk, ".gitattributes", "* -text\n*.a filter=lfs diff=lfs merge=lfs -text\n")
        self.commit(self.sdk)
        self.sdk_url = self.bare(self.sdk)
        self.app = self.repo("app")
        for path in (".github/workflows/build-installer.yml", "scripts/ci/owned.py", "tests/ci/owned.py", "docs/ci.md"):
            self.write(self.app, path, "owned")
        self.write(self.app, "removed.txt", "obsolete")
        self.commit(self.app)
        self.app_url = self.bare(self.app)
        self.orca = self.repo('orca')
        self.write(self.orca, 'source.cpp', 'managed Orca')
        self.commit(self.orca)
        self.ui = self.repo('ui')
        self.write(self.ui, 'source.cpp', 'managed UI')
        self.commit(self.ui)
        self.kd = self.repo('kd')
        self.write(self.kd, 'source.cpp', 'managed docking')
        self.commit(self.kd)
        self.dock = self.repo('dock')
        self.write(self.dock, 'source.cpp', 'managed dock')
        git(self.dock, 'submodule', 'add', str(self.kd), 'KDDockWidgets')
        self.commit(self.dock)
        app_helper = Path(__file__).resolve().parents[2] / 'cmake/source_inputs.py'
        shutil.copyfile(app_helper, self.write(self.app, 'cmake/source_inputs.py', ''))
        git(self.app, 'submodule', 'add', str(self.orca), 'ThirdParty/OrcaSlicer')
        self.declared_sdk_url = 'https://github.com/fixture/SDK.git'
        self.write(self.app, common.SDK_DECLARATION, json.dumps({'repository': 'fixture/SDK'}))
        git(self.app, 'add', '.')
        git(self.app, 'commit', '-m', 'App owns Orca and declares the SDK repository')
        # Keep the production URL check real; only Git transport maps to a local bare remote.
        index = int(os.environ.get('GIT_CONFIG_COUNT', '0'))
        os.environ.update({f'GIT_CONFIG_KEY_{index}': f'url.{self.sdk_url}.insteadOf',
                           f'GIT_CONFIG_VALUE_{index}': self.declared_sdk_url,
                           'GIT_CONFIG_COUNT': str(index + 1)})
        self.app_sha = text_git(self.app, 'rev-parse', 'HEAD')
        git(self.app, 'push', self.app_url, 'HEAD:refs/heads/feature/gplatform-ui')
        self.source = self.repo('source')
        git(self.source, 'submodule', 'add', str(self.app), 'App')
        git(self.source, 'submodule', 'add', str(self.ui), 'ThirdParty/SmoothUI')
        git(self.source, 'submodule', 'add', str(self.dock), 'ThirdParty/SmoothUIDock')
        self.commit(self.source)
        common._init_pinned_submodules(self.source, common.git_env(), producer=True)
        self.source_sha = text_git(self.source, 'rev-parse', 'HEAD')
        spec = importlib.util.spec_from_file_location('sdk_source_inputs', app_helper)
        self.contract = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.contract)
        self.roots = {fork: self.source / 'ThirdParty' / fork for fork in
                      ('SmoothUI', 'SmoothUIDock', 'SmoothUIDock/KDDockWidgets')}
        self.roots['OrcaSlicer'] = self.source / 'App/ThirdParty/OrcaSlicer'
        inventory = {fork: self.contract.managed_files(root) for fork, root in self.roots.items()}
        self.inputs = self.contract.summary(self.contract.record(self.source / 'App/ThirdParty', inventory, self.roots))
        self.initial_sdk = text_git(self.sdk, "rev-parse", "HEAD")
        for branch in ('windows', 'macos'):
            git(self.sdk_url, 'update-ref', f'refs/heads/{branch}', self.initial_sdk)
        self.packages = self.root / "packages"
        for arch in ("AMD64", "arm64", "x86_64"):
            self.make_slot(arch)

    def repo(self, name):
        path = self.root / name
        common.run(["git", "init", "-b", "feature/gplatform-ui", path], capture=True)
        return path

    def write(self, root, name, value):
        p = root / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(value)
        return p

    def commit(self, repo):
        git(repo, "add", "-A")
        git(repo, "commit", "-m", "fixture")

    def bare(self, repo):
        path = self.root / (repo.name + ".git")
        common.run(["git", "clone", "--bare", repo, path], capture=True)
        return path.as_posix()

    def make_slot(self, arch):
        slot = self.packages / 'SDK' / arch / 'Release'
        self.write(slot, 'lib/test.a', f'binary payload {arch}')
        self.write(slot, 'share/renderer/monkey.obj', 'o monkey\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n')
        self.write(slot, 'lib/cmake/notes.source', 'ordinary SDK metadata, not a shared library\n')
        modules, resources = {}, {}
        for uri in ('SmoothUI', 'SmoothUI.Controls', 'SmoothUI.impl', 'SmoothUIDock'):
            relative = 'qml/' + uri.replace('.', '/') + '/Fixture.qml'
            file = self.write(slot, relative, 'import QtQuick\nItem {}\n')
            modules[uri] = {'path': uri.replace('.', '/')}
            resources[':/qt/qml/' + uri.replace('.', '/') + '/Fixture.qml'] = {'path': relative, 'sha256': digest(file)}
        ui_contract = self.write(slot, 'ui-package.json', json.dumps({'schema': 1, 'link_mode': 'STATIC',
                                                                   'modules': modules, 'resources': resources}))
        empty = hashlib.sha256(b'').hexdigest()
        self.write(slot, 'sdk-build.json', json.dumps({
            'SDK_FORMAT': 3, 'UI_BINARY_PACKAGE_VERSION': 1, 'CONSUMER_SOURCE_FORKS': ['OrcaSlicer'],
            'GPLATFORM_SDK_ARCH': arch, 'CMAKE_SYSTEM_NAME': 'Windows' if arch == 'AMD64' else 'Darwin',
            'CMAKE_BUILD_TYPE': 'Release', 'Qt6_VERSION': '6.9.3',
            'SOURCE_STATE': {'HEAD': self.source_sha, 'DIRTY': False, 'MERGE_HEAD': '', 'DIFF_SHA256': empty},
            'APP_SOURCE_STATE': {'HEAD': self.app_sha, 'DIRTY': False, 'DIFF_SHA256': empty},
            'SOURCE_INPUTS': self.inputs, 'SOURCE_REVISIONS': {fork: record['revision'] for fork, record in self.inputs.items()},
            'SOURCE_MODIFIED': {fork: False for fork in self.inputs},
            'DEPENDENCIES': {'SMOOTHUI_BUILD_STATIC_LIB': 'ON'}, 'UI_PACKAGE_SHA256': digest(ui_contract)}))
        self.manifest(slot)
        return slot

    def manifest(self, slot):
        value = {p.relative_to(slot).as_posix(): digest(p) for p in slot.rglob("*")
                 if p.is_file() and p.name != "sdk-manifest.json"}
        self.write(slot, "sdk-manifest.json", json.dumps(value))

    def export_fixture(self, platform):
        package = self.root / f'export-{platform}'
        if package.exists():
            shutil.rmtree(package)
        for slot in SLOTS[platform]:
            shutil.copytree(self.packages / 'SDK' / slot, package / 'SDK' / slot, symlinks=True)
        manifest = {p.relative_to(package).as_posix(): digest(p) for p in package.rglob('*') if p.is_file()}
        self.write(package, 'package-manifest.json', json.dumps(manifest))
        return package

    def sdk_update(self, platform="windows", base=None):
        package = self.export_fixture(platform)
        receipt = validate_package(self.source, self.source_sha, platform, package,
                                   self.declared_sdk_url, app_sha=self.app_sha)
        slots = {name: package / 'SDK' / name for name in receipt['selected_slots']}
        return publish_slots(self.declared_sdk_url, platform, base or self.initial_sdk, '',
                             slots, self.source_sha, self.app_sha)

    def upload_fixture(self, platform, artifact_id):
        package = self.export_fixture(platform)
        receipt = validate_package(self.source, self.source_sha, platform, package,
                                   self.declared_sdk_url, app_sha=self.app_sha)
        self.write(package, 'source-receipt.json', json.dumps(receipt))
        zipped = self.root / f'{artifact_id}.zip'
        with ZipFile(zipped, 'w') as archive:
            for file in package.rglob('*'):
                if file.is_file():
                    archive.write(file, file.relative_to(package).as_posix())
        return dict(id=artifact_id, name=f'sdk-{platform}-123-2', expired=False, digest='sha256:' + digest(zipped))

    def run_info(self):
        return dict(id=123, run_attempt=2, event='workflow_dispatch', path='.github/workflows/build-sdk.yml',
                    status='completed', conclusion='success', head_sha=self.app_sha,
                    repository=dict(full_name='fixture/App', id=17))

    def publish_inputs(self):
        return SimpleNamespace(source_dir=self.source, source_sha=self.source_sha, app_sha=self.app_sha,
                               platform='all', app_branch='feature/shared', windows_sdk_target='', macos_sdk_target='',
                               app_repository='fixture/App', run=123, attempt=2)

    def run_responses(self, artifacts):
        steps = [dict(name=name, conclusion='success')
                 for name in ('Build and export selected SDK', 'Upload SDK delivery evidence')]
        jobs = [dict(name=f'sdk-build-{platform}', run_id=123, run_attempt=2,
                     status='completed', conclusion='success', steps=steps)
                for platform in ('windows', 'macos')]
        base = 'repos/fixture/App/actions/runs/123'
        return {base + '/attempts/2': self.run_info(), base + '/attempts/2/jobs?per_page=100': {'jobs': jobs},
                base + '/artifacts?per_page=100': {'artifacts': artifacts}}

    def test_prepare_freezes_one_new_target_and_publishes_both_platforms_once(self):
        plan, = prepare_targets(self.declared_sdk_url, 'feature/shared', 'all')
        self.assertEqual(plan, dict(branch='feature/shared', platforms=['windows', 'macos'],
                                    base='', seed=self.initial_sdk))
        args = self.publish_inputs()
        windows = self.upload_fixture('windows', 11)
        responses = self.run_responses([windows])
        with patch('update_sdk.gh_json', side_effect=lambda path, env: responses[path]):
            with self.assertRaisesRegex(RuntimeError, 'one available artifact'):
                publish_run(args, {})
        self.assertEqual(common.read_remote_head(self.sdk_url, 'feature/shared', allow_missing=True), '')

        macos = self.upload_fixture('macos', 22)
        responses = self.run_responses([windows, macos])
        run = subprocess.run

        def download(argv, **kwargs):
            if argv[0] != 'gh':
                return run(argv, **kwargs)
            artifact_id = argv[-1].split('/')[-2]
            kwargs['stdout'].write((self.root / f'{artifact_id}.zip').read_bytes())

        # Local publishing can validate frozen commits even while CI files are edited.
        (self.source / '.gitmodules').write_text('local host edit')
        (self.source / 'App/cmake/source_inputs.py').write_text('local app edit')
        result = {}
        with (patch('update_sdk.gh_json', side_effect=lambda path, env: responses[path]),
              patch('update_sdk.subprocess.run', side_effect=download),
              patch('update_sdk.push_exact_commit', wraps=common.push_exact_commit) as push):
            publish_run(args, result)
        push.assert_called_once()
        sha = common.read_remote_head(self.sdk_url, 'feature/shared')
        delivery = result['branches']['feature/shared']
        self.assertEqual(delivery['sdk_sha'], sha)
        self.assertEqual(delivery['updated_slots'], ['AMD64/Release', 'arm64/Release', 'x86_64/Release'])
        self.assertEqual(text_git(self.sdk_url, 'rev-list', '--count', f'{self.initial_sdk}..{sha}'), '1')
        for slot in delivery['updated_slots']:
            self.assertTrue(git(self.sdk_url, 'show', f'{sha}:{slot}/lib/test.a').startswith(b'version https://git-lfs.'))

    def test_prepare_requires_targets_only_for_selected_platforms_and_preserves_expected_head(self):
        with self.assertRaisesRegex(ValueError, 'macos_sdk_target'):
            prepare_targets(self.declared_sdk_url, self.app_sha, 'all', windows_sdk_target='windows')
        plan, = prepare_targets(self.declared_sdk_url, self.app_sha, 'windows',
                                windows_sdk_target='windows', macos_sdk_target='unused')
        self.assertEqual(plan, dict(branch='windows', platforms=['windows'], base=self.initial_sdk, seed=''))

    def test_first_publication_refuses_concurrent_creation(self):
        plan, = prepare_targets(self.declared_sdk_url, 'feature/race', 'macos')
        git(self.sdk_url, 'update-ref', 'refs/heads/feature/race', self.initial_sdk)
        slots = {name: self.packages / 'SDK' / name for name in SLOTS['macos']}
        with self.assertRaisesRegex(RuntimeError, 'expected base changed'):
            publish_slots(self.declared_sdk_url, plan['branch'], plan['base'], plan['seed'], slots,
                          self.source_sha, self.app_sha)
        self.assertEqual(common.read_remote_head(self.sdk_url, 'feature/race'), self.initial_sdk)

    def test_run_metadata_blocks_incomplete_wrong_attempt_and_failed_build(self):
        info = self.run_info()
        for changed in (dict(status='in_progress', conclusion=None), dict(run_attempt=1),
                        dict(path='.github/workflows/run-regression.yml')):
            with patch('update_sdk.gh_json', return_value=dict(info, **changed)) as request:
                with self.assertRaises(RuntimeError):
                    successful_build_artifacts('fixture/App', 123, 2, ['windows'], {})
                request.assert_called_once()
        responses = self.run_responses([])
        responses['repos/fixture/App/actions/runs/123/attempts/2/jobs?per_page=100']['jobs'][0]['steps'] = [
            dict(name='Build and export selected SDK', conclusion='failure')]
        with patch('update_sdk.gh_json', side_effect=lambda path, env: responses[path]):
            with self.assertRaisesRegex(RuntimeError, 'build/upload did not succeed'):
                successful_build_artifacts('fixture/App', 123, 2, ['windows'], {})

    def test_local_git_uses_gh_helper_without_token_or_trace_environment(self):
        with patch.dict(os.environ, GH_TOKEN='fixture', GITHUB_TOKEN='fixture', GIT_TRACE_CURL='1'):
            env = gh_git_env()
        self.assertNotIn('GH_TOKEN', env)
        self.assertNotIn('GITHUB_TOKEN', env)
        self.assertNotIn('GIT_TRACE_CURL', env)
        index = int(env['GIT_CONFIG_COUNT']) - 1
        self.assertEqual(env[f'GIT_CONFIG_KEY_{index}'], 'credential.https://github.com.helper')
        self.assertEqual(env[f'GIT_CONFIG_VALUE_{index}'], '!gh auth git-credential')

    def test_download_rejects_wrong_digest_before_extracting(self):
        artifact = dict(self.upload_fixture('windows', 11), digest='sha256:' + '0' * 64)
        package = self.root / 'download'
        with patch('update_sdk.subprocess.run',
                   side_effect=lambda args, **kw: kw['stdout'].write((self.root / '11.zip').read_bytes())):
            with self.assertRaisesRegex(RuntimeError, 'artifact digest differs'):
                download_artifact('fixture/App', artifact, package, {})
        self.assertFalse((package / 'SDK').exists())

    def test_clone_retries_fetch_without_recreating_checkout(self):
        original = subprocess.run
        failures = []
        checkout = self.root / "retry-clone"
        expected = text_git(self.app, "rev-parse", "HEAD")

        def interrupted(args, **kwargs):
            if "fetch" in args and len(failures) < 2:
                self.assertTrue((checkout / ".git").is_dir())
                failures.append(args)
                return subprocess.CompletedProcess(args, 128)
            return original(args, **kwargs)

        with patch.object(common.subprocess, "run", side_effect=interrupted):
            with patch.object(common.time, "sleep") as sleep:
                sha = common.checkout_sdk_base(self.app_url, expected, checkout, env=git_env())
        self.assertEqual(len(failures), 2)
        self.assertEqual(sleep.call_count, 2)
        self.assertEqual(sha, expected)
        self.assertEqual((checkout / "removed.txt").read_text(), "obsolete")

    def test_windows_changes_one_slot_lfs_and_repeat_is_noop(self):
        result = self.sdk_update()
        self.assertTrue(result["changed"])
        sha = result["sdk_sha"]
        for arch in ("arm64", "x86_64"):
            self.assertEqual(text_git(self.sdk_url, "rev-parse", f"{sha}:{arch}"),
                             text_git(self.sdk_url, "rev-parse", f"{self.initial_sdk}:{arch}"))
        self.assertNotIn("old.txt", text_git(self.sdk_url, "ls-tree", "-r", "--name-only", sha + ":AMD64"))
        pointer = git(self.sdk_url, "show", f"{sha}:AMD64/Release/lib/test.a")
        self.assertTrue(pointer.startswith(b"version https://git-lfs.github.com/spec/v1"))
        checkout = self.root / "consumer"
        common.checkout_sdk_base(self.sdk_url, sha, checkout, env=git_env())
        git(checkout, "lfs", "install", "--local", env=git_env())
        git(checkout, "lfs", "pull", "--include", "AMD64/Release/**", "--exclude", "", env=git_env())
        self.assertEqual((checkout / "AMD64/Release/lib/test.a").read_bytes(), b"binary payload AMD64")
        for relative in ("share/renderer/monkey.obj", "lib/cmake/notes.source"):
            payload = (self.packages / "SDK/AMD64/Release" / relative).read_bytes()
            self.assertEqual(git(self.sdk_url, "show", f"{sha}:AMD64/Release/{relative}"), payload)
            self.assertEqual((checkout / "AMD64/Release" / relative).read_bytes(), payload)
        repeated = self.sdk_update(base=sha)
        self.assertFalse(repeated["changed"])
        self.assertEqual(repeated["sdk_sha"], sha)

    def test_mac_commits_two_slots_and_preserves_windows(self):
        result = self.sdk_update("macos")
        self.assertEqual(text_git(self.sdk_url, "rev-parse", f'{result["sdk_sha"]}:AMD64'),
                         text_git(self.sdk_url, "rev-parse", f"{self.initial_sdk}:AMD64"))
        self.assertEqual(result["updated_slots"], ["arm64/Release", "x86_64/Release"])
        for arch in ("arm64", "x86_64"):
            for relative in ("share/renderer/monkey.obj", "lib/cmake/notes.source"):
                payload = (self.packages / "SDK" / arch / "Release" / relative).read_bytes()
                self.assertEqual(git(self.sdk_url, "show", f'{result["sdk_sha"]}:{arch}/Release/{relative}'), payload)

    def test_compiler_object_requires_lfs_then_round_trips_real_payload(self):
        slot = self.packages / "SDK/AMD64/Release"
        payload = b"\x00\x01compiler object payload\x00"
        self.write(slot, "objects/compiler.obj", "placeholder").write_bytes(payload)
        self.manifest(slot)
        with self.assertRaisesRegex(RuntimeError, "not covered by LFS.*objects/compiler.obj"):
            self.sdk_update()
        self.assertEqual(common.read_remote_head(self.sdk_url, "windows"), self.initial_sdk)
        shared_library = b"\x7fELF versioned shared-library payload"
        self.write(slot, "lib/libsample.so.1", "placeholder").write_bytes(shared_library)
        self.manifest(slot)
        # Only the temporary fixture gains LFS rules for these exported binaries.
        with (self.sdk / ".gitattributes").open("a") as stream:
            stream.write("**/objects/*.obj filter=lfs diff=lfs merge=lfs -text\n")
            stream.write("**/lib/*.so.* filter=lfs diff=lfs merge=lfs -text\n")
        self.commit(self.sdk)
        git(self.sdk, "push", self.sdk_url, "HEAD:refs/heads/windows")
        self.initial_sdk = text_git(self.sdk, "rev-parse", "HEAD")
        for branch in ('windows', 'macos'):
            git(self.sdk_url, 'update-ref', f'refs/heads/{branch}', self.initial_sdk)
        result = self.sdk_update()
        pointer = git(self.sdk_url, "show", f'{result["sdk_sha"]}:AMD64/Release/objects/compiler.obj')
        self.assertTrue(pointer.startswith(b"version https://git-lfs.github.com/spec/v1\n"))
        checkout = self.root / "object-consumer"
        common.checkout_sdk_base(self.sdk_url, result["sdk_sha"], checkout, env=git_env())
        git(checkout, "lfs", "install", "--local", env=git_env())
        git(checkout, "lfs", "pull", "--include", "AMD64/Release/**", "--exclude", "", env=git_env())
        self.assertEqual((checkout / "AMD64/Release/objects/compiler.obj").read_bytes(), payload)
        pointer = git(self.sdk_url, "show", f'{result["sdk_sha"]}:AMD64/Release/lib/libsample.so.1')
        self.assertTrue(pointer.startswith(b"version https://git-lfs.github.com/spec/v1\n"))
        self.assertEqual((checkout / "AMD64/Release/lib/libsample.so.1").read_bytes(), shared_library)

    def test_mac_second_payload_failure_pushes_neither(self):
        (self.packages / "SDK/x86_64/Release/lib/test.a").write_bytes(b"corrupt")
        with self.assertRaises(RuntimeError):
            self.sdk_update("macos")
        self.assertEqual(common.read_remote_head(self.sdk_url, "windows"), self.initial_sdk)

    def test_wrong_sdk_repository_is_rejected_before_any_write(self):
        other = self.root / 'wrong-sdk.git'
        common.run(['git', 'clone', '--bare', self.sdk_url, other], capture=True)
        with self.assertRaisesRegex(RuntimeError, 'target URL differs'):
            validate_package(self.source, self.source_sha, 'windows', self.export_fixture('windows'),
                       str(other), app_sha=self.app_sha)
        self.assertEqual(common.read_remote_head(str(other), 'windows'), self.initial_sdk)

    def test_cross_batch_insertion_is_rejected(self):
        first = self.sdk_update()
        slot = self.packages / "SDK/AMD64/Release"
        (slot / "lib/test.a").write_bytes(b"different batch")
        self.manifest(slot)
        inserted = self.sdk_update(base=first["sdk_sha"])
        with self.assertRaisesRegex(RuntimeError, "expected base changed"):
            self.sdk_update("windows", base=first["sdk_sha"])
        self.assertEqual(common.read_remote_head(self.sdk_url, "windows"), inserted["sdk_sha"])

    def test_artifacts_default_never_reads_clones_or_pushes_the_sdk_target(self):
        before = text_git(self.app_url, 'for-each-ref', '--format=%(refname) %(objectname)')
        with patch('update_sdk.read_remote_head', side_effect=AssertionError('no target read')), \
             patch('update_sdk.checkout_sdk_base', side_effect=AssertionError('no target clone')), \
             patch('update_sdk.push_exact_commit', side_effect=AssertionError('no push')):
            value = validate_package(self.source, self.source_sha, 'windows', self.export_fixture('windows'),
                               self.declared_sdk_url, app_sha=self.app_sha)
        self.assertFalse(value['published'])
        self.assertEqual(text_git(self.app_url, 'for-each-ref', '--format=%(refname) %(objectname)'), before)
        self.assertEqual(common.read_remote_head(self.sdk_url, 'windows'), self.initial_sdk)

    def test_unused_sdk_wrong_pin_and_dirty_bytes_do_not_become_producer_inputs(self):
        sdk = self.source / 'App' / 'unused-sdk'
        if sdk.exists():
            sdk.rmdir()
        common.run(['git', 'clone', self.sdk_url, sdk], capture=True, env=git_env())
        self.write(sdk, 'cache.txt', 'wrong SDK pin')
        self.commit(sdk)
        wrong_pin = text_git(sdk, 'rev-parse', 'HEAD')
        self.write(sdk, 'cache.txt', 'dirty unused SDK')
        git(sdk, 'remote', 'set-url', 'origin', str(self.root / 'unavailable-sdk'))
        value = validate_package(self.source, self.source_sha, 'windows', self.export_fixture('windows'),
                           self.declared_sdk_url, app_sha=self.app_sha)
        self.assertFalse(value['published'])
        self.assertEqual(text_git(sdk, 'rev-parse', 'HEAD'), wrong_pin)
        self.assertEqual((sdk / 'cache.txt').read_text(), 'dirty unused SDK')

    def test_root_app_and_ui_changes_cannot_pass_clean_source_validation(self):
        for repo, relative in [(self.source, '.gitmodules'), (self.source / 'App', 'removed.txt'),
                               (self.source / 'ThirdParty/SmoothUI', 'source.cpp')]:
            file = repo / relative
            previous = file.read_bytes()
            file.write_bytes(previous + b'changed')
            with self.assertRaisesRegex(RuntimeError, 'tracked changes'):
                self.sdk_update()
            file.write_bytes(previous)
        app = self.source / 'App'
        git(app, 'checkout', '--detach', 'HEAD~1')
        with self.assertRaisesRegex(RuntimeError, 'fixed App candidate'):
            self.sdk_update()
        self.assertEqual(common.read_remote_head(self.sdk_url, 'windows'), self.initial_sdk)

    def test_explicit_app_commit_can_advance_without_rewriting_the_host_gitlink(self):
        app = self.source / 'App'
        declared = self.app_sha
        self.write(app, 'selected-app.txt', 'new selected App source')
        self.commit(app)
        selected = text_git(app, 'rev-parse', 'HEAD')
        self.assertNotEqual(selected, declared)
        slot = self.packages / 'SDK/AMD64/Release'
        metadata = json.loads((slot / 'sdk-build.json').read_text())
        metadata['APP_SOURCE_STATE']['HEAD'] = selected
        self.write(slot, 'sdk-build.json', json.dumps(metadata)); self.manifest(slot)
        result = validate_package(self.source, self.source_sha, 'windows', self.export_fixture('windows'),
                            self.declared_sdk_url, app_sha=selected)
        self.assertEqual(result['app_sha'], selected)
        self.assertEqual(result['declared_app_sha'], declared)
        self.assertEqual(text_git(self.source, 'rev-parse', 'HEAD'), self.source_sha)
        self.assertEqual(text_git(self.source, 'rev-parse', self.source_sha + ':App'), declared)
        with self.assertRaisesRegex(RuntimeError, 'fixed App candidate'):
            validate_package(self.source, self.source_sha, 'windows', self.export_fixture('windows'),
                       self.declared_sdk_url, app_sha=declared)
        self.write(app, 'selected-app.txt', 'dirty mutation')
        with self.assertRaisesRegex(RuntimeError, 'tracked changes'):
            validate_package(self.source, self.source_sha, 'windows', self.export_fixture('windows'),
                       self.declared_sdk_url, app_sha=selected)

    def test_sdk3_contract_app_provenance_and_managed_content_are_required(self):
        slot = self.packages / 'SDK/AMD64/Release'
        original = (slot / 'sdk-build.json').read_text()
        for field, value in [('SDK_FORMAT', 2), ('APP_SOURCE_STATE', {'HEAD': '0' * 40, 'DIRTY': False}),
                             ('SOURCE_INPUTS', {}), ('UI_PACKAGE_SHA256', '0' * 64)]:
            metadata = json.loads(original)
            metadata[field] = value
            self.write(slot, 'sdk-build.json', json.dumps(metadata));self.manifest(slot)
            with self.assertRaises(RuntimeError):
                self.sdk_update()
            self.assertEqual(common.read_remote_head(self.sdk_url, 'windows'), self.initial_sdk)
        self.write(slot, 'sdk-build.json', original);self.manifest(slot)
        (self.source / 'App/ThirdParty/OrcaSlicer/source.cpp').write_text('dirty managed source')
        with self.assertRaisesRegex(RuntimeError, 'managed content'):
            self.sdk_update()

    def test_macos_contracts_must_match_before_any_slot_is_written(self):
        slot = self.packages / 'SDK/x86_64/Release'
        metadata = json.loads((slot / 'sdk-build.json').read_text());metadata['Qt6_VERSION'] = 'other'
        self.write(slot, 'sdk-build.json', json.dumps(metadata));self.manifest(slot)
        with self.assertRaisesRegex(RuntimeError, 'architecture slots'):
            self.sdk_update('macos')
        self.assertEqual(common.read_remote_head(self.sdk_url, 'windows'), self.initial_sdk)

    def exercise_lfs_object_push_retry(self, succeed):
        git(self.sdk, 'lfs', 'install', '--local', env=git_env())
        git(self.sdk, 'remote', 'add', 'origin', self.sdk_url)
        payload = 'real retry-test LFS payload'
        self.write(self.sdk, 'AMD64/Release/lib/retry.a', payload)
        self.commit(self.sdk)
        commit = text_git(self.sdk, 'rev-parse', 'HEAD')
        original = subprocess.run
        lfs_attempts, ref_pushes = [], []
        def transport(argv, **kwargs):
            if len(argv) > 4 and argv[3:5] == ['lfs', 'push']:
                lfs_attempts.append(list(argv))
                if len(lfs_attempts) < 3 or not succeed:
                    return original([sys.executable, '-c',
                                     "import sys; print('simulated locks transport EOF', file=sys.stderr); sys.exit(56)"],
                                    **kwargs)
            if len(argv) > 3 and argv[3] == 'push':
                self.assertEqual(len(lfs_attempts), 3)
                self.assertEqual(common.read_remote_head(self.sdk_url, 'windows'), self.initial_sdk)
                ref_pushes.append(list(argv))
            return original(argv, **kwargs)
        with patch.object(common.subprocess, 'run', side_effect=transport), patch.object(common.time, 'sleep') as sleep:
            if succeed:
                common.push_exact_commit(self.sdk, self.sdk_url, 'windows',
                                         self.initial_sdk, commit, env=git_env(), lfs=True)
            else:
                with self.assertRaisesRegex(RuntimeError, r'exit 56.*after 3 attempt'):
                    common.push_exact_commit(self.sdk, self.sdk_url, 'windows',
                                             self.initial_sdk, commit, env=git_env(), lfs=True)
        self.assertEqual(len(lfs_attempts), 3)
        self.assertEqual([call.args for call in sleep.call_args_list], [(1,), (2,)])
        self.assertEqual(common.read_remote_head(self.sdk_url, 'windows'),
                         commit if succeed else self.initial_sdk)
        self.assertEqual(len(ref_pushes), 1 if succeed else 0)
        if succeed:
            checkout = self.root / 'retry-consumer'
            common.run(['git', 'clone', '--branch', 'windows', self.sdk_url, checkout], env=git_env(), capture=True)
            git(checkout, 'lfs', 'install', '--local', env=git_env())
            git(checkout, 'lfs', 'pull', '--include', 'AMD64/Release/**', '--exclude', '', env=git_env())
            self.assertEqual((checkout / 'AMD64/Release/lib/retry.a').read_text(), payload)

    def test_lfs_object_push_retries_before_advancing_the_ref(self):
        self.exercise_lfs_object_push_retry(True)

    def test_exhausted_lfs_object_push_never_advances_the_ref(self):
        self.exercise_lfs_object_push_retry(False)

    def test_push_lost_response_confirms_same_commit(self):
        tree = text_git(self.app, "rev-parse", "HEAD^{tree}")
        base = text_git(self.app, "rev-parse", "HEAD")
        commit = common.commit_tree(self.app, tree, base, "new commit")
        git(self.app, "remote", "add", "origin", self.app_url)
        original = common.git
        def lost(repo, *args, **kwargs):
            result = original(repo, *args, **kwargs)
            if args and args[0] == "push":
                raise RuntimeError("response lost")
            return result
        with patch.object(common, "git", side_effect=lost):
            common.push_exact_commit(self.app, self.app_url, "feature/gplatform-ui", base, commit)
        self.assertEqual(common.read_remote_head(self.app_url, "feature/gplatform-ui"), commit)



    def test_symlink_ancestor_rejected_without_deleting_external_files(self):
        outside = self.root / 'outside'
        sentinel = self.write(outside, 'Release/sentinel.txt', 'preserve')
        original = common.checkout_sdk_base
        def redirected(url, ref, checkout, **kwargs):
            base = original(url, ref, checkout, **kwargs)
            shutil.rmtree(checkout / 'AMD64')
            directory_link(checkout / 'AMD64', outside)
            return base
        with patch('update_sdk.checkout_sdk_base', side_effect=redirected):
            with self.assertRaisesRegex(RuntimeError, 'ancestor'):
                self.sdk_update()
        self.assertEqual(sentinel.read_text(), 'preserve')
        self.assertEqual(common.read_remote_head(self.sdk_url, 'windows'), self.initial_sdk)

    def test_inside_checkout_alias_cannot_redirect_a_platform_slot(self):
        from update_sdk import safe_destination
        checkout = self.root / 'destination'
        target = checkout / 'arm64/Release'
        target.mkdir(parents=True)
        sentinel = target / 'preserve-mac'
        sentinel.write_text('other platform')
        directory_link(checkout / 'AMD64', target.parent)
        with self.assertRaisesRegex(RuntimeError, 'ancestor'):
            safe_destination(checkout, 'AMD64/Release')
        self.assertEqual(sentinel.read_text(), 'other platform')

    def test_unchanged_binary_without_lfs_attributes_is_rejected(self):
        self.write(self.sdk, ".gitattributes", "* -text\n")
        shutil.rmtree(self.sdk / "AMD64/Release")
        shutil.copytree(self.packages / "SDK/AMD64/Release", self.sdk / "AMD64/Release")
        self.commit(self.sdk)
        git(self.sdk, "push", self.sdk_url, "HEAD:refs/heads/windows")
        self.initial_sdk = text_git(self.sdk, "rev-parse", "HEAD")
        for branch in ('windows', 'macos'):
            git(self.sdk_url, 'update-ref', f'refs/heads/{branch}', self.initial_sdk)
        # This repeats identical payload bytes, so the .a is absent from diff.
        with self.assertRaisesRegex(RuntimeError, "not covered by LFS"):
            self.sdk_update()
        self.assertEqual(common.read_remote_head(self.sdk_url, "windows"), self.initial_sdk)

    def test_branch_deleted_between_precheck_and_push_is_not_recreated(self):
        base = text_git(self.app, "rev-parse", "HEAD")
        tree = text_git(self.app, "rev-parse", "HEAD^{tree}")
        commit = common.commit_tree(self.app, tree, base, "next")
        git(self.app, "remote", "add", "origin", self.app_url)
        original = common.git
        def deletion(repo, *args, **kwargs):
            if args and args[0] == "push":
                original(self.app_url, "update-ref", "-d", "refs/heads/feature/gplatform-ui")
            return original(repo, *args, **kwargs)
        with patch.object(common, "git", side_effect=deletion), self.assertRaises(RuntimeError):
            common.push_exact_commit(self.app, self.app_url, "feature/gplatform-ui", base, commit)
        result = subprocess.run(["git", "-C", self.app_url, "show-ref", "--verify",
                                 "refs/heads/feature/gplatform-ui"], capture_output=True)
        self.assertNotEqual(result.returncode, 0)

    def test_non_fast_forward_commit_rejected_before_push(self):
        base = text_git(self.app, "rev-parse", "HEAD")
        tree = text_git(self.app, "rev-parse", "HEAD^{tree}")
        unrelated = text_git(self.app, "commit-tree", tree, data=b"unrelated\n", env=self.env)
        with self.assertRaisesRegex(RuntimeError, "direct fast-forward"):
            common.push_exact_commit(self.app, self.app_url, "feature/gplatform-ui", base, unrelated)

if __name__ == "__main__":
    unittest.main()
