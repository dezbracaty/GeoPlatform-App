"""Real SDK payload validation, without claiming an actual platform build."""
import json
import hashlib
import os
import shutil
import subprocess
from pathlib import Path
import sys
import tempfile
from contextlib import nullcontext
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts/ci"))
from build import cache_value, main, smoke_application, validate_sdk, validate_source_inputs, windows_entry
from common import digest
from fs_fixture import directory_link


class AppSDKChecks(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.source = Path(self.temp.name)
        self.slots = ["arm64/Release", "x86_64/Release"]
        (self.source / 'cmake').mkdir()
        shutil.copy2(Path(__file__).resolve().parents[2] / 'cmake/source_inputs.py', self.source / 'cmake/source_inputs.py')
        for slot in self.slots:
            root = self.source / "selected-sdk" / slot
            (root / "lib").mkdir(parents=True)
            (root / "lib/test.a").write_bytes(b"actual payload")
            modules, resources = {}, {}
            for uri in ('SmoothUI', 'SmoothUI.Controls', 'SmoothUI.impl', 'SmoothUIDock'):
                module_path = uri.replace('.', '/')
                relative = 'qml/' + module_path + '/Fixture.qml'
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('import QtQuick\nItem {}\n')
                resources[':/qt/qml/' + module_path + '/Fixture.qml'] = {'path': relative, 'sha256': digest(path)}
                modules[uri] = {'path': module_path, 'qml_count': 1}
            contract = root / 'ui-package.json'
            contract.write_text(json.dumps({'schema': 1, 'link_mode': 'STATIC', 'modules': modules, 'resources': resources}))
            forks = ('SmoothUI', 'SmoothUIDock', 'SmoothUIDock/KDDockWidgets', 'OrcaSlicer')
            (root / "sdk-build.json").write_text(json.dumps({
                "SDK_FORMAT": 3, "GPLATFORM_SDK_ARCH": slot.split("/")[0],
                "CMAKE_BUILD_TYPE": "Release", "CMAKE_SYSTEM_NAME": "Darwin", "Qt6_VERSION": "6.9.3",
                "SOURCE_STATE": dict(HEAD='a'*40, DIRTY=False, DIFF_SHA256=hashlib.sha256(b'').hexdigest()),
                "APP_SOURCE_STATE": dict(HEAD='b'*40, DIRTY=False, DIFF_SHA256=hashlib.sha256(b'').hexdigest()),
                "UI_BINARY_PACKAGE_VERSION": 1, "CONSUMER_SOURCE_FORKS": ['OrcaSlicer'],
                "SOURCE_INPUTS": {fork: {'revision': 'revision-' + fork} for fork in forks},
                "SOURCE_REVISIONS": {fork: 'revision-' + fork for fork in forks},
                "DEPENDENCIES": {'SMOOTHUI_BUILD_STATIC_LIB': 'ON'}, "UI_PACKAGE_SHA256": digest(contract)}))
            self.manifest(root)

    def manifest(self, root):
        entries = {p.relative_to(root).as_posix():digest(p) for p in root.rglob("*")
                   if p.is_file() and p != root / "sdk-manifest.json"}
        (root / "sdk-manifest.json").write_text(json.dumps(entries))

    def test_both_sdk_payloads_validate(self):
        validate_sdk(self.source, self.slots, self.source/'selected-sdk')

    def test_explicit_sdk_root_cannot_silently_fall_back_to_declared_pin(self):
        external=self.source/'external-sdk'
        shutil.move(self.source/'selected-sdk',external)
        validate_sdk(self.source,self.slots,external)
        with self.assertRaises(TypeError):validate_sdk(self.source,self.slots)
        (external/'arm64/Release/lib/test.a').write_bytes(b'corrupt selected package')
        with self.assertRaises(RuntimeError):validate_sdk(self.source,self.slots,external)

    def test_published_sdk_with_dirty_or_missing_provenance_is_rejected(self):
        root=self.source/'selected-sdk/arm64/Release'
        data=json.loads((root/'sdk-build.json').read_text())
        data['SOURCE_STATE']['DIRTY']=True
        (root/'sdk-build.json').write_text(json.dumps(data));self.manifest(root)
        with self.assertRaisesRegex(RuntimeError,'clean producer'):validate_sdk(self.source, self.slots, self.source/'selected-sdk')
        data.pop('SOURCE_STATE')
        (root/'sdk-build.json').write_text(json.dumps(data));self.manifest(root)
        with self.assertRaisesRegex(RuntimeError,'clean producer'):validate_sdk(self.source, self.slots, self.source/'selected-sdk')

    def test_second_architecture_corruption_fails(self):
        (self.source / "selected-sdk/x86_64/Release/lib/test.a").write_bytes(b"corrupt")
        with self.assertRaisesRegex(RuntimeError, "missing, changed or outside"):
            validate_sdk(self.source, self.slots, self.source/'selected-sdk')

    def test_missing_or_extra_payload_fails(self):
        root = self.source / "selected-sdk/arm64/Release"
        (root / "extra.a").write_bytes(b"undeclared")
        with self.assertRaisesRegex(RuntimeError, "complete package manifest"):
            validate_sdk(self.source, self.slots, self.source/'selected-sdk')

    def test_sdk_symlink_cannot_read_outside_slot(self):
        root = self.source / "selected-sdk/arm64/Release"
        outside = self.source / "outside.a"
        outside.write_bytes(b"external")
        if os.name == 'nt':
            # A real directory junction needs no symlink privilege. Keep the
            # original manifest reference to lib/test.a as the attack input.
            external_directory = self.source / 'external-sdk-library'
            external_directory.mkdir()
            outside = external_directory / 'test.a'
            outside.write_bytes((root / 'lib/test.a').read_bytes())
            (root / 'lib/test.a').unlink()
            (root / 'lib').rmdir()
            directory_link(root / 'lib', external_directory)
            self.assertFalse((root / 'lib/test.a').resolve().is_relative_to(root.resolve()))
            rejection = 'complete package manifest|missing, changed or outside'
        else:
            (root / "lib/test.a").unlink()
            (root / "lib/test.a").symlink_to(outside)
            self.manifest(root)
            rejection = 'missing, changed or outside'
        sentinel = outside.read_bytes()
        with self.assertRaisesRegex(RuntimeError, rejection):
            validate_sdk(self.source, self.slots, self.source/'selected-sdk')
        self.assertEqual(outside.read_bytes(), sentinel)

    def test_wrong_architecture_fails(self):
        root = self.source / "selected-sdk/x86_64/Release"
        (root / "sdk-build.json").write_text(json.dumps({"SDK_FORMAT":3,"GPLATFORM_SDK_ARCH":"arm64"}))
        self.manifest(root)
        with self.assertRaisesRegex(RuntimeError, "matching format"):
            validate_sdk(self.source, self.slots, self.source/'selected-sdk')

    def test_consumer_checks_real_orca_and_never_requires_private_ui(self):
        from build import source_contract
        orca = self.source / 'ThirdParty/OrcaSlicer'
        orca.mkdir(parents=True)
        def git(*args):
            subprocess.check_call(['git', '-C', str(orca), *args], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        git('init', '-q')
        git('config', 'user.name', 'CI fixture')
        git('config', 'user.email', 'fixture@example.invalid')
        (orca / 'payload.cpp').write_text('original')
        git('add', '.')
        git('commit', '-qm', 'managed Orca')
        contract = source_contract(self.source)
        observed = contract.summary(contract.record(self.source / 'ThirdParty', {'OrcaSlicer': ['payload.cpp']}))['OrcaSlicer']
        for slot in self.slots:
            root = self.source / 'selected-sdk' / slot
            metadata = json.loads((root / 'sdk-build.json').read_text())
            metadata['SOURCE_INPUTS']['OrcaSlicer'] = observed
            metadata['SOURCE_REVISIONS']['OrcaSlicer'] = observed['revision']
            (root / 'sdk-build.json').write_text(json.dumps(metadata))
            self.manifest(root)
        metadata = validate_sdk(self.source, self.slots, self.source/'selected-sdk')
        validate_source_inputs(self.source, metadata)
        self.assertFalse((self.source / 'ThirdParty/SmoothUI').exists())
        (orca / 'payload.cpp').write_text('changed')
        with self.assertRaisesRegex(RuntimeError, 'managed content'):
            validate_source_inputs(self.source, metadata)

    def test_backslash_manifest_escape_is_rejected(self):
        root = self.source / 'selected-sdk/arm64/Release'
        if os.name == 'nt':
            # Backslashes are separators on Windows, so inject the invalid
            # serialized key instead of accidentally creating an outside file.
            outside = root.parent / 'outside'
            outside.write_bytes(b'preserve')
            manifest = json.loads((root / 'sdk-manifest.json').read_text())
            manifest['..\\outside'] = digest(outside)
            (root / 'sdk-manifest.json').write_text(json.dumps(manifest))
            self.assertIn('..\\outside', json.loads((root / 'sdk-manifest.json').read_text()))
            with self.assertRaisesRegex(RuntimeError, 'complete package manifest|outside the package'):
                validate_sdk(self.source, self.slots, self.source/'selected-sdk')
            self.assertEqual(outside.read_bytes(), b'preserve')
        else:
            # POSIX permits a literal backslash: exercise the lexical guard too.
            (root / '..\\outside').write_bytes(b'bad')
            self.manifest(root)
            with self.assertRaisesRegex(RuntimeError, 'outside the package'):
                validate_sdk(self.source, self.slots, self.source/'selected-sdk')

    def test_old_sdk_and_incomplete_provenance_are_rejected(self):
        root = self.source / "selected-sdk/arm64/Release"
        original = json.loads((root / 'sdk-build.json').read_text())
        for field, value in [('SDK_FORMAT', 2), ('CONSUMER_SOURCE_FORKS', ['OrcaSlicer', 'SmoothUI']),
                             ('SOURCE_INPUTS', {'OrcaSlicer': {'revision': 'only-one'}})]:
            metadata = dict(original, **{field: value})
            (root / 'sdk-build.json').write_text(json.dumps(metadata))
            self.manifest(root)
            with self.assertRaises(RuntimeError):
                validate_sdk(self.source, self.slots, self.source/'selected-sdk')

    def test_cross_architecture_contract_mismatch_is_rejected(self):
        root = self.source / "selected-sdk/x86_64/Release"
        metadata = json.loads((root / 'sdk-build.json').read_text())
        metadata['Qt6_VERSION'] = '6.8.0'
        (root / 'sdk-build.json').write_text(json.dumps(metadata))
        self.manifest(root)
        with self.assertRaisesRegex(RuntimeError, 'differ'):
            validate_sdk(self.source, self.slots, self.source/'selected-sdk')

    def test_corrupt_ui_contract_is_rejected_after_manifest_update(self):
        root = self.source / "selected-sdk/arm64/Release"
        (root / 'ui-package.json').write_text('{}')
        self.manifest(root)
        with self.assertRaisesRegex(RuntimeError, 'contract digest'):
            validate_sdk(self.source, self.slots, self.source/'selected-sdk')

    def test_missing_qt_cache_is_an_explicit_failure(self):
        build = self.source / "build-arm64-Release"
        build.mkdir()
        (build / "CMakeCache.txt").write_text("Qt6_DIR:PATH=/toolchain/lib/cmake/Qt6\n")
        self.assertEqual(cache_value(build,"Qt6_DIR"),"/toolchain/lib/cmake/Qt6")
        with self.assertRaisesRegex(RuntimeError, "missing"):
            cache_value(build,"MISSING")


class WindowsEntryChecks(unittest.TestCase):
    @unittest.skipUnless(os.name == 'nt' and shutil.which('pwsh'), 'Windows native build entry')
    def test_native_entry_selects_first_cmake_on_path_and_honors_explicit_path(self):
        entry = Path(__file__).resolve().parents[2] / 'scripts/build.ps1'
        with tempfile.TemporaryDirectory(prefix='App CMake selection ') as temporary:
            root = Path(temporary)
            log = root / 'calls.txt'
            tools = []
            for name in ('first', 'second'):
                directory = root / name
                directory.mkdir()
                tool = directory / 'ci-cmake.cmd'
                # Record native invocations without configuring or compiling a fixture project.
                tool.write_text(f'@echo {name} %*>>"%CI_CMAKE_CALLS%"\n@exit /b 0\n')
                tools.append(tool)
            env = dict(os.environ, CI_CMAKE_CALLS=str(log),
                       PATH=os.pathsep.join([str(tool.parent) for tool in tools] + [os.environ['PATH']]))
            for command, selected in [('ci-cmake.cmd', 'first'), (str(tools[1]), 'second')]:
                with self.subTest(command=command):
                    log.unlink(missing_ok=True)
                    result = subprocess.run(['pwsh', '-NoProfile', '-File', str(entry),
                                             '-CMake', command, '-Targets', 'GPlatform'],
                                            env=env, capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertEqual(log.read_text().splitlines(),
                                     [f'{selected} --preset release',
                                      f'{selected} --build --preset release --target GPlatform'])
            log.unlink()
            result = subprocess.run(['pwsh', '-NoProfile', '-File', str(entry),
                                     '-CMake', str(root / 'missing-cmake.exe')],
                                    env=env, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('CMake was not found', result.stderr)
            self.assertFalse(log.exists())

    def test_cmake_arguments_and_paths_are_environment_data(self):
        source = Path('/source with spaces')
        arguments = ['-DNAME=value with spaces', '-DGPLATFORM_BUILD_SDK_CHECKS=ON']
        with patch('build.run') as execute:
            targets = ['GPlatform', 'SDKConsumerExtensionCheck']
            windows_entry(source, targets, arguments, {'BASELINE': 'preserve'})
        args, kwargs = execute.call_args
        self.assertEqual(args[0][:3], ['pwsh', '-NoProfile', '-Command'])
        self.assertEqual(kwargs['cwd'], source)
        self.assertEqual(kwargs['env']['BASELINE'], 'preserve')
        self.assertEqual(kwargs['env']['CI_BUILD_ENTRY'], str(Path(__file__).resolve().parents[2] / 'scripts/build.ps1'))
        self.assertEqual(json.loads(kwargs['env']['CI_BUILD_TARGETS']), targets)
        self.assertEqual(json.loads(kwargs['env']['CI_CMAKE_ARGS']), arguments)
        self.assertNotIn(str(source), args[0][-1])
        self.assertNotIn(arguments[0], args[0][-1])

    @unittest.skipUnless(shutil.which('pwsh'), 'Native PowerShell is exercised on the Windows CI runner')
    def test_real_powershell_preserves_pre_native_failure_native_exit_and_success(self):
        with tempfile.TemporaryDirectory(prefix='App CI entry ') as temporary:
            source = Path(temporary) / 'selected product'
            source.mkdir()
            control = Path(temporary) / 'fixed control'
            script = control / 'scripts/build.ps1'
            script.parent.mkdir(parents=True)
            (script.parent / 'ci').mkdir()
            fixed_module = script.parent / 'ci/build.py'
            parameters = '[CmdletBinding()] param([string]$SourceDir, [string]$Preset, [string[]]$Targets, [string[]]$CMakeArgs)\n'
            environment = dict(os.environ, CI_TEST_PYTHON=sys.executable)
            for body, code in [("throw 'failure before any native invocation'", 1),
                               ('& $env:CI_TEST_PYTHON -c "import sys; sys.exit(7)"', 7),
                               ("if ($Targets.Count -ne 2 -or $Targets[0] -ne 'target with spaces' -or $Targets[1] -ne 'second-target' -or $CMakeArgs[0] -ne '-DNAME=value with spaces') { throw 'array transport failed' }; & $env:CI_TEST_PYTHON -c 'pass'", 0)]:
                script.write_text(parameters + body)
                if code:
                    with patch('build.__file__', str(fixed_module)), self.assertRaisesRegex(RuntimeError, f'exit {code}'):
                        windows_entry(source, ['target with spaces', 'second-target'], ['-DNAME=value with spaces'], environment)
                else:
                    with patch('build.__file__', str(fixed_module)):
                        windows_entry(source, ['target with spaces', 'second-target'], ['-DNAME=value with spaces'], environment)


class SDKSelectionChecks(unittest.TestCase):
    def test_platform_lfs_materialization_never_follows_a_branch_or_other_platform(self):
        from common import materialize_sdk
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary).resolve();sha='a'*40
            for platform,include in [('windows','AMD64/Release/**'),('macos','arm64/Release/**,x86_64/Release/**')]:
                with self.subTest(platform=platform),patch('common.text_git',side_effect=[str(root),sha,'',sha,'']),patch('common.run') as run:
                    self.assertEqual(materialize_sdk(root,sha,platform),root)
                    self.assertEqual(run.call_args.args[0],['git','-C',root,'lfs','pull','--include',include,'--exclude',''])
    def test_wrong_identity_dirty_checkout_and_mid_transfer_change_fail_closed(self):
        from common import materialize_sdk
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary).resolve();sha='a'*40
            for replies in [[str(root),'b'*40],[str(root),sha,' M lib.dll']]:
                with self.subTest(replies=replies),patch('common.text_git',side_effect=replies),patch('common.run') as run:
                    with self.assertRaisesRegex(RuntimeError,'frozen clean commit'):materialize_sdk(root,sha,'windows')
                    run.assert_not_called()
            with patch('common.text_git',side_effect=[str(root),sha,'','b'*40]),patch('common.run'):
                with self.assertRaisesRegex(RuntimeError,'changed while materializing'):materialize_sdk(root,sha,'windows')
            with patch('common.text_git') as git,patch('common.run') as run:
                with self.assertRaisesRegex(RuntimeError,'frozen clean commit'):materialize_sdk(root,'moving-branch','windows')
                git.assert_not_called();run.assert_not_called()


class InstallerPayloadChecks(unittest.TestCase):
    def fixture(self, root, *, architecture=0x8664, deployed=b''):
        import struct
        payload=bytearray(96);payload[:2]=b'MZ';struct.pack_into('<I',payload,0x3c,64)
        payload[64:68]=b'PE\x00\x00';struct.pack_into('<H',payload,68,architecture)
        for name in ['dist/GPlatform.exe','build-Release/GPlatform.exe']:
            p=root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(bytes(payload)+deployed)
        script=root/'package/InstallerScript.iss';script.parent.mkdir();script.write_text('[Files]\n')
        installer=root/'GPlatform.exe';installer.write_bytes(b'MZInno-fixture')
        return installer

    def test_windows_checks_actual_amd64_distribution_and_binds_startup_identity(self):
        from build import inspect_installer
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);package=self.fixture(root)
            with patch('build.smoke_application',return_value={'status':'passed'}) as smoke:
                result=inspect_installer(root,'windows',package,root/'checks',{})
            self.assertEqual(result['status'],'passed')
            self.assertFalse(result['installation_tested'])
            self.assertEqual(result['distribution']['GPlatform.exe'],digest(root/'dist/GPlatform.exe'))
            self.assertIn('independent_extraction_of_Inno_payload',result['not_tested'])
            self.assertEqual(smoke.call_args.args[1],root/'dist/GPlatform.exe')

    def test_windows_x86_or_stale_distribution_stops_before_startup(self):
        from build import inspect_installer
        for fault in ['x86','stale']:
            with self.subTest(fault=fault),tempfile.TemporaryDirectory() as temporary:
                root=Path(temporary);package=self.fixture(root,architecture=0x14c if fault=='x86' else 0x8664)
                if fault=='stale':(root/'build-Release/GPlatform.exe').write_bytes(b'different product')
                with patch('build.smoke_application') as smoke,self.assertRaises(RuntimeError):
                    inspect_installer(root,'windows',package,root/'checks',{})
                smoke.assert_not_called()
                self.assertEqual(json.loads((root/'checks/installer-checks.json').read_text())['status'],'failed')


class InstallerSelectionChecks(unittest.TestCase):
    def test_startup_uses_control_tools_and_keeps_native_display(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / 'selected-app'
            executable = root / 'GPlatform.exe'
            executable.write_bytes(b'fixture')
            with patch('build.run') as invoke:
                smoke_application(source, executable, root / 'startup', {'QT_SCALE_FACTOR': '2'})
            command = invoke.call_args.args[0]
            self.assertEqual(command[3], Path(__file__).resolve().parents[2] / 'tools/journal')
            self.assertIn('validate=False,native_window=True', command[2])
            self.assertNotIn('QT_SCALE_FACTOR', invoke.call_args.kwargs['env'])

    def test_installer_reports_failures_and_exposes_only_checked_packages(self):
        from build import build
        sdk_sha, app_sha = 'a' * 40, 'b' * 40
        for platform in ('windows', 'macos'):
            for failure in (None, 'SDK materialization failed', 'consumer build failed', 'installer check failed'):
                with self.subTest(platform=platform, failure=failure), tempfile.TemporaryDirectory() as temporary:
                    base = Path(temporary).resolve()
                    workspace, runner = base / 'work space', base / 'runner-temp'
                    source = workspace / 'source'
                    sdk = workspace / 'sdk'
                    sdk.mkdir(parents=True)
                    (sdk / 'sentinel').write_bytes(b'published SDK')
                    runner.mkdir()
                    (source / 'package').mkdir(parents=True)
                    universal = source / 'build-universal-Release'
                    universal.mkdir()
                    (universal / 'report.json').write_text(json.dumps(
                        dict(status='passed', architectures=['arm64', 'x86_64'])), encoding='utf-8')
                    result_root = base if platform == 'windows' else workspace
                    result = result_root / 'installer-results/123-2' / platform
                    outputs = base / 'github-output'
                    env = dict(GITHUB_WORKSPACE=str(workspace), RUNNER_TEMP=str(runner),
                               GITHUB_RUN_ID='123', GITHUB_RUN_ATTEMPT='2', GITHUB_OUTPUT=str(outputs))

                    def create_disk_image(bundle, package, log):
                        package.write_bytes(b'checked package')
                        log.write_text('diagnostic', encoding='utf-8')

                    def inspect(source, platform, package, output, env):
                        self.assertFalse(outputs.exists())
                        (output / 'startup.png').write_bytes(b'local diagnostic')
                        if failure == 'installer check failed':
                            raise RuntimeError(failure)
                        checks = dict(status='passed', package_sha256=digest(package))
                        if platform == 'windows':
                            checks['distribution'] = {'GPlatform.exe': 'fixture hash'}
                        (output / 'installer-checks.json').write_text(json.dumps(checks), encoding='utf-8')
                        return checks

                    packager = SimpleNamespace(create_disk_image=create_disk_image)

                    producers = {'SOURCE_STATE': {'HEAD': 'd' * 40},
                                 'APP_SOURCE_STATE': {'HEAD': 'e' * 40}}
                    metadata = {'fixture/Release': producers}

                    def consume_sdk(source, platform, sdk_root, env, receipt):
                        receipt['metadata'] = metadata
                        if failure == 'consumer build failed':
                            raise RuntimeError(failure)

                    with (
                        patch.dict(os.environ, env, clear=True),
                        patch('build.text_git', side_effect=['c' * 40, app_sha, sdk_sha]),
                        patch('build.machine_lock', return_value=nullcontext()),
                        patch('build.prepare_cached_source') as prepare,
                        patch('build.init_source_submodules') as initialize,
                        patch('build.materialize_sdk', return_value=sdk,
                              side_effect=RuntimeError(failure) if failure == 'SDK materialization failed' else None) as materialize,
                        patch('build.build_consumer', side_effect=consume_sdk) as consume,
                        patch('build.windows_entry', side_effect=lambda *args: (
                            source / 'package' / 'GPlatform.exe').write_bytes(b'checked package')) as installer_entry,
                        patch('build.run'),
                        patch('build.importlib.util.spec_from_file_location',
                              return_value=SimpleNamespace(loader=SimpleNamespace(exec_module=lambda module: None))),
                        patch('build.importlib.util.module_from_spec', return_value=packager),
                        patch('build.inspect_installer', side_effect=inspect),
                    ):
                        if failure:
                            with self.assertRaisesRegex(RuntimeError, failure):
                                build(source, platform, app_sha, sdk, sdk_sha)
                        else:
                            build(source, platform, app_sha, sdk, sdk_sha)
                        prepare.assert_called_once_with(source, app_sha, platform, consumer=True)
                        initialize.assert_called_once_with(source)
                        materialize.assert_called_once_with(sdk, sdk_sha, platform)
                        if failure == 'SDK materialization failed':
                            consume.assert_not_called()
                        else:
                            actual_source, selected_platform, actual_sdk, _, _ = consume.call_args.args
                            self.assertEqual((actual_source, selected_platform, actual_sdk),
                                             (source, platform, sdk))
                        if installer_entry.called:
                            self.assertIn('-DGPLATFORM_BUILD_TESTS=OFF', installer_entry.call_args.args[2])

                    values = dict(line.split('=', 1) for line in outputs.read_text(encoding='utf-8').splitlines())
                    report_path = result / 'build-result.json'
                    self.assertEqual(values['report'], report_path.as_posix())
                    report = json.loads(report_path.read_text(encoding='utf-8'))
                    self.assertEqual((report['app_sha'], report['sdk_source']), (app_sha, 'published'))
                    self.assertEqual(report.get('sdk_sha'), None if failure == 'SDK materialization failed' else sdk_sha)
                    if failure != 'SDK materialization failed':
                        self.assertEqual(report['sdk_producers'], {'fixture/Release': producers})
                    self.assertEqual((sdk / 'sentinel').read_bytes(), b'published SDK')
                    package = result / 'package' / ('GPlatform.exe' if platform == 'windows' else 'GPlatform.dmg')
                    if failure:
                        self.assertEqual(set(values), {'report'})
                        self.assertEqual(report['status'], 'failed')
                        self.assertIn(failure, report['error'])
                        self.assertEqual(package.exists(), failure == 'installer check failed')
                    else:
                        self.assertEqual(set(values), {'report', 'package', 'checksum'})
                        self.assertEqual(report['status'], 'passed')
                        self.assertEqual(values['package'], package.as_posix())
                        self.assertEqual(Path(values['checksum']).read_text(encoding='utf-8').split()[0],
                                         digest(package))
                        self.assertNotIn('distribution', report['checks'])
                        if platform == 'windows':
                            self.assertEqual(report['checks']['distribution_files'], 1)
                            full_checks = json.loads((result / 'installer-checks.json').read_text(encoding='utf-8'))
                            self.assertEqual(full_checks['distribution'], {'GPlatform.exe': 'fixture hash'})
                    self.assertEqual((result / 'startup.png').exists(), failure not in ('SDK materialization failed', 'consumer build failed'))

    def test_packaging_requires_the_frozen_app_and_sdk(self):
        arguments = ['build.py', '--source-dir', 'source', '--platform', 'macos',
                     '--app-sha', 'b' * 40, '--sdk-root', 'sdk', '--sdk-sha', 'a' * 40]
        with patch('sys.argv', arguments), patch('build.build') as execute:
            main()
        execute.assert_called_once_with(Path('source'), 'macos', 'b' * 40, Path('sdk'), 'a' * 40)

    def test_missing_sdk_and_retired_source_or_regression_flags_fail_before_build(self):
        import contextlib, io
        base = ['build.py', '--source-dir', 'source', '--platform', 'macos', '--app-sha', 'b' * 40]
        sdk = ['--sdk-root', 'sdk', '--sdk-sha', 'a' * 40]
        for extra in [[], sdk[:2], sdk[2:], sdk + ['--source-sha', 'c' * 40],
                      sdk + ['--run-journal'], sdk + ['--test-assets-sha', 'c' * 40]]:
            with self.subTest(extra=extra), patch('sys.argv', base + extra), patch('build.build') as execute, contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:
                    main()
                self.assertEqual(error.exception.code, 2)
                execute.assert_not_called()


if __name__ == "__main__":
    unittest.main()
