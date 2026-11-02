"""Real SDK payload validation, without claiming an actual platform build."""
import json
import os
import shutil
import subprocess
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts/ci"))
from build import cache_value, validate_sdk, validate_source_inputs, windows_entry
from common import digest


class AppSDKChecks(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.source = Path(self.temp.name)
        self.slots = ["arm64/Release", "x86_64/Release"]
        (self.source / 'cmake').mkdir()
        shutil.copy2(Path(__file__).resolve().parents[2] / 'cmake/source_inputs.py', self.source / 'cmake/source_inputs.py')
        for slot in self.slots:
            root = self.source / "ThirdParty/libs/GPlatformSDK" / slot
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
        validate_sdk(self.source, self.slots)

    def test_second_architecture_corruption_fails(self):
        (self.source / "ThirdParty/libs/GPlatformSDK/x86_64/Release/lib/test.a").write_bytes(b"corrupt")
        with self.assertRaisesRegex(RuntimeError, "missing, changed or outside"):
            validate_sdk(self.source, self.slots)

    def test_missing_or_extra_payload_fails(self):
        root = self.source / "ThirdParty/libs/GPlatformSDK/arm64/Release"
        (root / "extra.a").write_bytes(b"undeclared")
        with self.assertRaisesRegex(RuntimeError, "complete package manifest"):
            validate_sdk(self.source, self.slots)

    def test_sdk_symlink_cannot_read_outside_slot(self):
        root = self.source / "ThirdParty/libs/GPlatformSDK/arm64/Release"
        outside = self.source / "outside.a"
        outside.write_bytes(b"external")
        (root / "lib/test.a").unlink()
        (root / "lib/test.a").symlink_to(outside)
        self.manifest(root)
        with self.assertRaisesRegex(RuntimeError, "missing, changed or outside"):
            validate_sdk(self.source, self.slots)

    def test_wrong_architecture_fails(self):
        root = self.source / "ThirdParty/libs/GPlatformSDK/x86_64/Release"
        (root / "sdk-build.json").write_text(json.dumps({"SDK_FORMAT":3,"GPLATFORM_SDK_ARCH":"arm64"}))
        self.manifest(root)
        with self.assertRaisesRegex(RuntimeError, "matching format"):
            validate_sdk(self.source, self.slots)

    def test_consumer_checks_real_orca_and_never_requires_private_ui(self):
        from build import source_contract
        orca = self.source / 'ThirdParty/OrcaSlicer'
        orca.mkdir()
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
            root = self.source / 'ThirdParty/libs/GPlatformSDK' / slot
            metadata = json.loads((root / 'sdk-build.json').read_text())
            metadata['SOURCE_INPUTS']['OrcaSlicer'] = observed
            metadata['SOURCE_REVISIONS']['OrcaSlicer'] = observed['revision']
            (root / 'sdk-build.json').write_text(json.dumps(metadata))
            self.manifest(root)
        metadata = validate_sdk(self.source, self.slots)
        validate_source_inputs(self.source, metadata)
        self.assertFalse((self.source / 'ThirdParty/SmoothUI').exists())
        (orca / 'payload.cpp').write_text('changed')
        with self.assertRaisesRegex(RuntimeError, 'managed content'):
            validate_source_inputs(self.source, metadata)

    def test_backslash_manifest_escape_is_rejected(self):
        root = self.source / 'ThirdParty/libs/GPlatformSDK/arm64/Release'
        # On POSIX this can be a literal filename: still reject the Windows escape.
        (root / '..\\outside').write_bytes(b'bad')
        self.manifest(root)
        with self.assertRaisesRegex(RuntimeError, 'outside the package'):
            validate_sdk(self.source, self.slots)

    def test_old_sdk_and_incomplete_provenance_are_rejected(self):
        root = self.source / "ThirdParty/libs/GPlatformSDK/arm64/Release"
        original = json.loads((root / 'sdk-build.json').read_text())
        for field, value in [('SDK_FORMAT', 2), ('CONSUMER_SOURCE_FORKS', ['OrcaSlicer', 'SmoothUI']),
                             ('SOURCE_INPUTS', {'OrcaSlicer': {'revision': 'only-one'}})]:
            metadata = dict(original, **{field: value})
            (root / 'sdk-build.json').write_text(json.dumps(metadata))
            self.manifest(root)
            with self.assertRaises(RuntimeError):
                validate_sdk(self.source, self.slots)

    def test_cross_architecture_contract_mismatch_is_rejected(self):
        root = self.source / "ThirdParty/libs/GPlatformSDK/x86_64/Release"
        metadata = json.loads((root / 'sdk-build.json').read_text())
        metadata['Qt6_VERSION'] = '6.8.0'
        (root / 'sdk-build.json').write_text(json.dumps(metadata))
        self.manifest(root)
        with self.assertRaisesRegex(RuntimeError, 'differ'):
            validate_sdk(self.source, self.slots)

    def test_corrupt_ui_contract_is_rejected_after_manifest_update(self):
        root = self.source / "ThirdParty/libs/GPlatformSDK/arm64/Release"
        (root / 'ui-package.json').write_text('{}')
        self.manifest(root)
        with self.assertRaisesRegex(RuntimeError, 'contract digest'):
            validate_sdk(self.source, self.slots)

    def test_missing_qt_cache_is_an_explicit_failure(self):
        build = self.source / "build-arm64-Release"
        build.mkdir()
        (build / "CMakeCache.txt").write_text("Qt6_DIR:PATH=/toolchain/lib/cmake/Qt6\n")
        self.assertEqual(cache_value(build,"Qt6_DIR"),"/toolchain/lib/cmake/Qt6")
        with self.assertRaisesRegex(RuntimeError, "missing"):
            cache_value(build,"MISSING")


class WindowsEntryChecks(unittest.TestCase):
    def test_cmake_arguments_and_paths_are_environment_data(self):
        source = Path('/source with spaces')
        arguments = ['-DNAME=value with spaces', '-DGPLATFORM_BUILD_SDK_CHECKS=ON']
        with patch('build.run') as execute:
            windows_entry(source, 'SDKConsumerExtensionCheck', arguments, {'BASELINE': 'preserve'})
        args, kwargs = execute.call_args
        self.assertEqual(args[0][:3], ['pwsh', '-NoProfile', '-Command'])
        self.assertEqual(kwargs['cwd'], source)
        self.assertEqual(kwargs['env']['BASELINE'], 'preserve')
        self.assertEqual(kwargs['env']['CI_BUILD_ENTRY'], str(source / 'scripts/build.ps1'))
        self.assertEqual(json.loads(kwargs['env']['CI_CMAKE_ARGS']), arguments)
        self.assertNotIn(str(source), args[0][-1])
        self.assertNotIn(arguments[0], args[0][-1])

    @unittest.skipUnless(shutil.which('pwsh'), 'Native PowerShell is exercised on the Windows CI runner')
    def test_real_powershell_preserves_pre_native_failure_native_exit_and_success(self):
        with tempfile.TemporaryDirectory(prefix='App CI entry ') as temporary:
            source = Path(temporary)
            script = source / 'scripts/build.ps1'
            script.parent.mkdir()
            parameters = '[CmdletBinding()] param([string]$Preset, [string[]]$Targets, [string[]]$CMakeArgs)\n'
            environment = dict(os.environ, CI_TEST_PYTHON=sys.executable)
            for body, code in [("throw 'failure before any native invocation'", 1),
                               ('& $env:CI_TEST_PYTHON -c "import sys; sys.exit(7)"', 7),
                               ("if ($Targets[0] -ne 'target with spaces' -or $CMakeArgs[0] -ne '-DNAME=value with spaces') { throw 'array transport failed' }; & $env:CI_TEST_PYTHON -c 'pass'", 0)]:
                script.write_text(parameters + body)
                if code:
                    with self.assertRaisesRegex(RuntimeError, f'exit {code}'):
                        windows_entry(source, 'target with spaces', ['-DNAME=value with spaces'], environment)
                else:
                    windows_entry(source, 'target with spaces', ['-DNAME=value with spaces'], environment)


if __name__ == "__main__":
    unittest.main()
