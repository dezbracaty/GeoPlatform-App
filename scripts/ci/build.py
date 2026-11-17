"""Use the existing standalone App build/check/package entries."""
from __future__ import annotations
import argparse
import importlib.util
import hashlib
import plistlib
import re
import struct
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile

from mac_regression import regression, package_and_regress
from windows_regression import regression as windows_regression
from common import digest, git_env, init_source_submodules, machine_lock, materialize_sdk, prepare_cached_source, run, text_git, write_json


def cache_value(build, key):
    for line in (build / "CMakeCache.txt").read_text().splitlines():
        if line.startswith(key + ":"):
            return line.split("=", 1)[1]
    raise RuntimeError(f"CMake cache is missing {key}")


def runtime_env(build, env):
    result = dict(env, QT_QPA_PLATFORM="offscreen")
    qt = Path(cache_value(build, "Qt6_DIR"))
    result["PATH"] = str(qt.parents[2] / "bin") + os.pathsep + result.get("PATH", "")
    return result


def source_contract(source):
    spec = importlib.util.spec_from_file_location('selected_app_source_inputs', source / 'cmake/source_inputs.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def validate_sdk(source, slots, sdk_root=None, *, producer_root_sha=None, producer_app_sha=None):
    sdk_root = Path(sdk_root) if sdk_root is not None else source / 'ThirdParty/libs/GPlatformSDK'
    contract = source_contract(source)
    result = {}
    for slot in slots:
        root = sdk_root / slot
        metadata = json.loads((root / 'sdk-build.json').read_text())
        system = 'Windows' if slot.split('/')[0] == 'AMD64' else 'Darwin'
        if (metadata.get('SDK_FORMAT') != 3 or metadata.get('GPLATFORM_SDK_ARCH') != slot.split('/')[0]
                or metadata.get('CMAKE_BUILD_TYPE') != 'Release' or metadata.get('CMAKE_SYSTEM_NAME') != system):
            raise RuntimeError('App requires a matching format-3 SDK')
        contract.verify_sdk_package(root, metadata)
        if any(metadata.get('SOURCE_MODIFIED', {}).values()):
            raise RuntimeError('SDK contains modified producer sources')
        for key, expected in [('SOURCE_STATE', producer_root_sha), ('APP_SOURCE_STATE', producer_app_sha)]:
            state = metadata.get(key, {})
            if (not re.fullmatch('[0-9a-f]{40}', state.get('HEAD', '')) or state.get('DIRTY') is not False
                    or state.get('MERGE_HEAD') or state.get('DIFF_SHA256') != hashlib.sha256(b'').hexdigest()
                    or expected is not None and state.get('HEAD') != expected):
                raise RuntimeError(f'SDK {key} differs from its frozen clean producer')
        result[slot] = metadata
    first = next(iter(result.values()))
    for metadata in result.values():
        for key in ('Qt6_VERSION', 'SOURCE_STATE', 'APP_SOURCE_STATE', 'SOURCE_INPUTS', 'SOURCE_REVISIONS',
                    'DEPENDENCIES', 'UI_BINARY_PACKAGE_VERSION', 'CONSUMER_SOURCE_FORKS'):
            if metadata.get(key) != first.get(key):
                raise RuntimeError(f'SDK architecture slots have different {key}')
    return result


def validate_source_inputs(source, metadata):
    contract = source_contract(source)
    expected = contract.consumer_inputs(next(iter(metadata.values())))
    contract.verify(source / 'ThirdParty', None, expected)


def windows_entry(source, target, cmake_args, env):
    command = ("$ErrorActionPreference = 'Stop'; try { "
               '$global:LASTEXITCODE = 0; '
               '& $env:CI_BUILD_ENTRY -Preset release -Targets $env:CI_BUILD_TARGET '
               '-CMakeArgs @($env:CI_CMAKE_ARGS | ConvertFrom-Json); exit $LASTEXITCODE '
               '} catch { [Console]::Error.WriteLine($_); exit 1 }')
    step_env = dict(env, CI_BUILD_ENTRY=str(source / 'scripts/build.ps1'),
                    CI_BUILD_TARGET=target, CI_CMAKE_ARGS=json.dumps(cmake_args))
    run(['pwsh', '-NoProfile', '-Command', command], cwd=source, env=step_env)


def build_consumer(source, platform, sdk_root, env, *, producer_root_sha=None, producer_app_sha=None):
    """Build/check one explicit SDK consumer. Checkout ownership and locking belong to the caller."""
    source, sdk_root = Path(source).resolve(), Path(sdk_root).resolve()
    slots = ['AMD64/Release'] if platform == 'windows' else ['arm64/Release', 'x86_64/Release']
    metadata = validate_sdk(source, slots, sdk_root, producer_root_sha=producer_root_sha,
                            producer_app_sha=producer_app_sha)
    validate_source_inputs(source, metadata)
    arguments = ['-DGPLATFORM_BUILD_SDK_CHECKS=ON', '-DGPLATFORM_SDK_ROOT=' + str(sdk_root)]
    qt = env.get('Qt6_DIR') or env.get('QT6_DIR')
    if qt:
        arguments.append('-DQt6_DIR=' + qt)
    if platform == 'windows':
        if os.name != 'nt':
            raise RuntimeError('Windows build requires a Windows runner')
        windows_entry(source, 'GPlatform', arguments, env)
        windows_entry(source, 'SDKConsumerExtensionCheck', arguments, env)
        directories = ['build-Release']
        executable = source / 'build-Release/GPlatform.exe'
    elif platform == 'macos':
        if sys.platform != 'darwin':
            raise RuntimeError('Mac build requires a Mac runner')
        run(['arch', '-x86_64', '/usr/bin/true'], env=env)
        directories = []
        for preset, directory in [('arm_release', 'build-arm64-Release'), ('x86_release', 'build-x86_64-Release')]:
            run(['bash', source / 'scripts/build.sh', '--preset', preset, '--configure-only', '--', *arguments], cwd=source, env=env)
            run(['cmake', '--build', '--preset', preset, '--target', 'GPlatform', 'SDKConsumerExtensionCheck'], cwd=source, env=env)
            directories.append(directory)
        executable = source / 'build-arm64-Release/GPlatform.app/Contents/MacOS/GPlatform'
    else:
        raise ValueError('Unknown consumer platform')
    for directory in directories:
        build_dir = source / directory
        if Path(cache_value(build_dir, 'GPLATFORM_SDK_ROOT')).resolve() != sdk_root:
            raise RuntimeError('CMake consumed a different SDK_ROOT')
        run(['ctest', '--test-dir', build_dir, '-R', '^SDKConsumerExtensionCheck$',
             '--no-tests=error', '--output-on-failure'], cwd=source, env=runtime_env(build_dir, env))
    return dict(executable=str(executable), sdk_root=str(sdk_root), metadata=metadata)


def smoke_application(source, executable, output, env):
    """Existing replay lifecycle + one observation, without consuming regression assets."""
    output.mkdir(parents=True, exist_ok=False)
    journal = output / 'startup-observation.json'
    write_json(journal, dict(formatVersion=2, entries=[dict(atMs=0, kind='action',
        payload=dict(code='screenshot.capture', params=dict(filepath='startup.png')))]))
    script = """
import json,sys
from pathlib import Path
sys.path.insert(0,sys.argv[1])
from journal_execution_session import JournalExecutionSession
result=JournalExecutionSession().run(Path(sys.argv[2]),application=Path(sys.argv[3]),
    output_dir=Path(sys.argv[4]),startup_timeout=120,validate=False,log=print)
if not result.get('success') or result.get('applicationKeptOpen') or result.get('cleanupErrors'):
    raise RuntimeError('Owned application startup/observation/cleanup failed: '+str(result.get('error')))
from PIL import Image
with Image.open(Path(sys.argv[4])/'startup.png') as image:
    if image.width < 1 or image.height < 1 or all(a==b for a,b in image.convert('RGB').getextrema()):
        raise RuntimeError('Startup capture is empty or uniform')
"""
    native = dict(env)
    for key in ('QT_QPA_PLATFORM', 'QT_SCALE_FACTOR', 'QT_SCREEN_SCALE_FACTORS', 'QT_DEVICE_PIXEL_RATIO'):
        native.pop(key, None)
    run([sys.executable, '-c', script, source / 'tools/journal', journal, executable, output / 'session'],
        cwd=source, env=native)
    return dict(status='passed', application=str(executable), application_sha256=digest(executable),
                report=str(output / 'session/execution_result.json'), full_regression='not_requested')


def inspect_installer(source, platform, package, output, env):
    """Inspect the actual DMG; on Windows bind the native compiler input and deployed payload."""
    result = dict(status='failed', package_sha256=digest(package), installation_tested=False)
    try:
        if platform == 'macos':
            run(['hdiutil', 'verify', package], env=env)
            attached = plistlib.loads(run(['hdiutil', 'attach', '-readonly', '-nobrowse', '-plist', package], env=env, capture=True))
            devices = [e['dev-entry'] for e in attached['system-entities'] if 'dev-entry' in e]
            try:
                mounts = [Path(e['mount-point']) for e in attached['system-entities'] if 'mount-point' in e]
                bundles = [p for mount in mounts for p in mount.glob('*.app')]
                if len(bundles) != 1:
                    raise RuntimeError('DMG must contain exactly one App bundle')
                bundle = bundles[0]
                original = source / 'build-universal-Release/GPlatform.app'
                spec = importlib.util.spec_from_file_location('installer_bundle_audit', source / 'scripts/macos_bundle.py')
                audit = importlib.util.module_from_spec(spec); spec.loader.exec_module(audit)
                audit.audit_bundle(bundle)
                for relative, path in audit.inventory(original).items():
                    candidate = bundle / relative
                    if path.is_symlink():
                        if not candidate.is_symlink() or os.readlink(path) != os.readlink(candidate):
                            raise RuntimeError('DMG symlink differs from the audited payload')
                    elif not candidate.is_file() or digest(path) != digest(candidate):
                        raise RuntimeError('DMG bytes differ from the audited payload: ' + str(relative))
                if set(audit.inventory(original)) != set(audit.inventory(bundle)):
                    raise RuntimeError('DMG payload inventory differs')
                result['startup'] = smoke_application(source, bundle / 'Contents/MacOS/GPlatform', output / 'startup', env)
                result['payload_check'] = 'mounted_readonly_inventory_hashes_and_universal_dependency_audit'
            finally:
                if devices:
                    run(['hdiutil', 'detach', devices[0]], env=env)
        else:
            with package.open('rb') as stream:
                if stream.read(2) != b'MZ':
                    raise RuntimeError('Inno installer is not a Windows executable')
            executable = source / 'dist/GPlatform.exe'
            with executable.open('rb') as stream:
                if stream.read(2) != b'MZ': raise RuntimeError('Deployed App is not PE')
                stream.seek(0x3c); offset=struct.unpack('<I',stream.read(4))[0]
                stream.seek(offset)
                if stream.read(6) != b'PE\x00\x00\x64\x86':
                    raise RuntimeError('Deployed installer payload is not AMD64')
            if digest(executable) != digest(source / 'build-Release/GPlatform.exe'):
                raise RuntimeError('Installer distribution App differs from the built SDK consumer')
            result['distribution'] = {p.relative_to(source / 'dist').as_posix():digest(p)
                                      for p in (source / 'dist').rglob('*') if p.is_file()}
            result['installer_script_sha256'] = digest(source / 'package/InstallerScript.iss')
            result['startup'] = smoke_application(source, executable, output / 'startup', env)
            result['payload_check'] = 'official_Inno_target_distribution_hashes_and_deployed_AMD64_startup'
            result['not_tested'] = ['installed_registry', 'upgrade_uninstall', 'independent_extraction_of_Inno_payload']
        result['status'] = 'passed'
        return result
    finally:
        write_json(output / 'installer-checks.json', result)


def build(source, platform, app_sha, sdk_sha, test_assets_sha=None, *, run_journal=True, sdk_root=None):
    source = source.resolve()
    if text_git(source, "rev-parse", "HEAD") != app_sha:
        raise RuntimeError("App source SHA changed")
    declared_sdk_sha = text_git(source, 'rev-parse', 'HEAD:ThirdParty/libs/GPlatformSDK')
    if sdk_root is None and declared_sdk_sha != sdk_sha:
        raise RuntimeError('App SDK gitlink differs from prepared SHA')
    output = Path(tempfile.mkdtemp(prefix="app-package-", dir=os.environ["RUNNER_TEMP"]))
    with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as f:
        f.write(f"deliverables={output}\n")
    with open(os.environ["GITHUB_ENV"], "a", encoding="utf-8") as f:
        f.write(f"APP_PACKAGE_ROOT={output}\n")
    env = git_env()
    with machine_lock():
        prepare_cached_source(source, app_sha, platform, consumer=True)
        init_source_submodules(source, consumer=True, external_sdk=sdk_root is not None)
        actual_sdk = materialize_sdk(sdk_root, sdk_sha, platform) if sdk_root is not None else source / 'ThirdParty/libs/GPlatformSDK'
        consumer = build_consumer(source, platform, actual_sdk, env)
        sdk_identity = dict(declared_sdk_sha=declared_sdk_sha, sdk_root=str(actual_sdk),
                            control_sha=text_git(Path(__file__).resolve().parents[2], 'rev-parse', 'HEAD'),
                            run_id=os.environ.get('GITHUB_RUN_ID'), run_attempt=os.environ.get('GITHUB_RUN_ATTEMPT'),
                            sdk_producers={slot: {key: data.get(key) for key in ('SOURCE_STATE', 'APP_SOURCE_STATE')}
                                           for slot, data in consumer['metadata'].items()})
        cmake_args = ['-DGPLATFORM_BUILD_SDK_CHECKS=ON', '-DGPLATFORM_SDK_ROOT=' + str(actual_sdk)]
        if os.environ.get('Qt6_DIR') or os.environ.get('QT6_DIR'):
            cmake_args.append('-DQt6_DIR=' + (os.environ.get('Qt6_DIR') or os.environ['QT6_DIR']))
        if platform == 'windows':
            def entry(target):
                windows_entry(source, target, cmake_args, env)
            for old in (source / "package").glob("*.exe"):
                old.unlink()
            entry("GPlatform_installer")
            packages = list((source / "package").glob("*.exe"))
            if len(packages) != 1:
                raise RuntimeError("Expected exactly one Windows installer")
            package = packages[0]
            architecture = "x64"
        else:
            run([sys.executable, source / "scripts/build_macos_universal.py", "--source-dir", source,
                 "--config", "Release", "--merge-only"], cwd=source, env=env)
            report = json.loads((source / "build-universal-Release/report.json").read_text())
            if report.get("status") != "passed" or report.get("architectures") != ["arm64", "x86_64"]:
                raise RuntimeError("Universal package audit did not pass")
            bundle = source / "build-universal-Release/GPlatform.app"
            regression_output = None
            if run_journal:
                regression_output = Path(tempfile.mkdtemp(prefix="app-regression-", dir=os.environ["RUNNER_TEMP"]))
                with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf8") as stream:
                    stream.write(f"regression_results={regression_output}\n")
                with open(os.environ["GITHUB_ENV"], "a", encoding="utf8") as stream:
                    stream.write(f"APP_REGRESSION_ROOT={regression_output}\n")
            # The established package helper reads the completed, signed App;
            # it does not modify the bundle being exercised by GUI regression.
            spec = importlib.util.spec_from_file_location('selected_macos_bundle', source / 'scripts/macos_bundle.py')
            packaging = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(packaging)
            package = output / 'GPlatform.dmg'
            def make_installer():
                packaging.create_disk_image(bundle, package, output / 'dmg.log')
                if not package.is_file() or not package.stat().st_size:
                    raise RuntimeError('Installer is missing or empty')
                checksum = digest(package)
                (output / (package.name + '.sha256')).write_text(f'{checksum}  {package.name}\n')
                checks = inspect_installer(source, platform, package, output, env)
                return dict(package=package.name, sha256=checksum, checks=checks)
            verify = (lambda: regression(source, bundle, regression_output, env, test_assets_sha)) if run_journal else None
            stages = package_and_regress(make_installer, verify, output / 'mac-stages.json')
            architecture = "universal"
            if regression_output is not None:
                write_json(regression_output / 'regression-result.json', stages['regression'])
            write_json(output / 'build-result.json', dict(app_sha=app_sha, sdk_sha=sdk_sha,
                platform=platform, architecture=architecture, **sdk_identity, sdk_consumer_check='passed',
                installer=stages['installer'], regression=stages['regression'], installation_tested=False))
            if stages['installer']['status'] != 'passed' or (run_journal and stages['regression']['status'] != 'passed'):
                raise RuntimeError('macOS installer or regression failed; both stage results are retained')
            print('macOS installer passed; Journal regression ' + stages['regression']['status'])
            return
        if not package.is_file() or not package.stat().st_size:
            raise RuntimeError("Installer is missing or empty")
        destination = output / package.name
        shutil.copy2(package, destination)
        checksum = digest(destination)
        (output / (destination.name + ".sha256")).write_text(f"{checksum}  {destination.name}\n")
        checks = inspect_installer(source, platform, destination, output, env)
        write_json(output / "build-result.json", dict(app_sha=app_sha, sdk_sha=sdk_sha, checks=checks,
                   platform=platform, architecture=architecture, **sdk_identity, sdk_consumer_check="passed",
                   package=destination.name, sha256=checksum, installation_tested=False,
                   regression=dict(status='pending' if run_journal else 'skipped', reason='run_journal=false' if not run_journal else 'not_started')))
        if run_journal:
            regression_output = Path(tempfile.mkdtemp(prefix='app-regression-', dir=os.environ['RUNNER_TEMP']))
            with open(os.environ['GITHUB_OUTPUT'], 'a', encoding='utf-8') as stream:
                stream.write(f'regression_results={regression_output}\n')
            with open(os.environ['GITHUB_ENV'], 'a', encoding='utf-8') as stream:
                stream.write(f'APP_REGRESSION_ROOT={regression_output}\n')
            result_path = output / 'build-result.json'
            result = json.loads(result_path.read_text(encoding='utf-8'))
            try:
                result['regression'] = windows_regression(source, source / 'build-Release/GPlatform.exe',
                                                          regression_output, env, test_assets_sha)
            except Exception as exc:
                result['regression'] = dict(status='failed', error=str(exc))
                raise
            finally:
                write_json(result_path, result)
                write_json(regression_output / 'regression-result.json', result['regression'])
    print(f"Build and package checks passed: {destination.name}")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source-dir", type=Path, required=True)
    p.add_argument("--platform", choices=("windows", "macos"), required=True)
    p.add_argument("--app-sha", required=True)
    p.add_argument("--sdk-sha", required=True)
    p.add_argument("--sdk-root", type=Path, help="External frozen SDK checkout; default uses the App gitlink")
    p.add_argument('--test-assets-sha', help='Prepared platform TestAssets commit for Journal regression')
    p.add_argument('--run-journal', action=argparse.BooleanOptionalAction, default=True,
                   help='Run this platform Journal regression (default: enabled)')
    a = p.parse_args()
    if a.run_journal and not a.test_assets_sha:
        p.error('--test-assets-sha is required for Journal regression')
    options = dict(run_journal=a.run_journal)
    if a.sdk_root is not None:
        options['sdk_root'] = a.sdk_root
    build(a.source_dir, a.platform, a.app_sha, a.sdk_sha, a.test_assets_sha, **options)


if __name__ == "__main__":
    main()
