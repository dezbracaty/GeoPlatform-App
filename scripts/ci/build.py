"""Package an independent App against the selected published SDK."""
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

from common import (digest, git_env, init_source_submodules, machine_lock, materialize_sdk,
                    prepare_cached_source, require_sha, run,
                    runner_report_path, text_git, write_json, write_outputs)


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


def validate_sdk(source, slots, sdk_root):
    sdk_root = Path(sdk_root)
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
        for key in ('SOURCE_STATE', 'APP_SOURCE_STATE'):
            state = metadata.get(key, {})
            if (not re.fullmatch('[0-9a-f]{40}', state.get('HEAD', '')) or state.get('DIRTY') is not False
                    or state.get('MERGE_HEAD') or state.get('DIFF_SHA256') != hashlib.sha256(b'').hexdigest()):
                raise RuntimeError(f'SDK {key} does not identify a clean producer')
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


def windows_entry(source, targets, cmake_args, env):
    command = ("$ErrorActionPreference = 'Stop'; try { "
               '$global:LASTEXITCODE = 0; '
               '& $env:CI_BUILD_ENTRY -SourceDir $env:CI_SOURCE_DIR -Preset release -Targets @($env:CI_BUILD_TARGETS | ConvertFrom-Json) '
               '-CMakeArgs @($env:CI_CMAKE_ARGS | ConvertFrom-Json); exit $LASTEXITCODE '
               '} catch { [Console]::Error.WriteLine($_); exit 1 }')
    step_env = dict(env, CI_BUILD_ENTRY=str(Path(__file__).resolve().parents[1] / 'build.ps1'),
                    CI_SOURCE_DIR=str(source), CI_BUILD_TARGETS=json.dumps(targets), CI_CMAKE_ARGS=json.dumps(cmake_args))
    run(['pwsh', '-NoProfile', '-Command', command], cwd=source, env=step_env)


def build_consumer(source, platform, sdk_root, env, receipt):
    """Build/check one explicit SDK consumer. Checkout ownership and locking belong to the caller."""
    source, sdk_root = Path(source).resolve(), Path(sdk_root).resolve()
    slots = ['AMD64/Release'] if platform == 'windows' else ['arm64/Release', 'x86_64/Release']
    metadata = validate_sdk(source, slots, sdk_root)
    receipt.update(sdk_root=str(sdk_root), metadata=metadata)
    validate_source_inputs(source, metadata)
    arguments = ['-DGPLATFORM_BUILD_TESTS=OFF', '-DGPLATFORM_BUILD_SDK_CHECKS=ON',
                 '-DGPLATFORM_SDK_ROOT=' + str(sdk_root)]
    qt = env.get('Qt6_DIR') or env.get('QT6_DIR')
    if qt:
        arguments.append('-DQt6_DIR=' + qt)
    if platform == 'windows':
        if os.name != 'nt':
            raise RuntimeError('Windows build requires a Windows runner')
        windows_entry(source, ['GPlatform', 'SDKConsumerExtensionCheck'], arguments, env)
        directories = ['build-Release']
        executable = source / 'build-Release/GPlatform.exe'
    elif platform == 'macos':
        if sys.platform != 'darwin':
            raise RuntimeError('Mac build requires a Mac runner')
        run(['arch', '-x86_64', '/usr/bin/true'], env=env)
        directories = []
        for preset, directory in [('arm_release', 'build-arm64-Release'), ('x86_release', 'build-x86_64-Release')]:
            run(['bash', Path(__file__).resolve().parents[1] / 'build.sh', '--source-dir', source, '--preset', preset, '--configure-only', '--', *arguments], cwd=source, env=env)
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
    receipt['executable'] = str(executable)


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
    output_dir=Path(sys.argv[4]),startup_timeout=120,validate=False,native_window=True,log=print)
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
    tools = Path(__file__).resolve().parents[2] / 'tools/journal'
    run([sys.executable, '-c', script, tools, journal, executable, output / 'session'],
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
                spec = importlib.util.spec_from_file_location('installer_bundle_audit', Path(__file__).resolve().parents[1] / 'macos_bundle.py')
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


def build(source, platform, app_sha, sdk_root, sdk_sha):
    source = source.resolve()
    require_sha(app_sha)
    require_sha(sdk_sha)
    output = runner_report_path(dict(os.environ, PLATFORM=platform), "installer-results")
    output.mkdir(parents=True, exist_ok=False)
    package_dir = output / "package"
    report_path = output / "build-result.json"
    report = dict(status="failed", platform=platform, app_sha=app_sha, sdk_source="published",
                  run_id=os.environ.get("GITHUB_RUN_ID"), run_attempt=os.environ.get("GITHUB_RUN_ATTEMPT"),
                  installation_tested=False)
    try:
        report["control_sha"] = text_git(Path(__file__).resolve().parents[2], "rev-parse", "HEAD")
        env = git_env()
        with machine_lock():
            prepare_cached_source(source, app_sha, platform, consumer=True)
            init_source_submodules(source)
            if text_git(source, "rev-parse", "HEAD") != app_sha:
                raise RuntimeError("App source SHA changed")
            actual_sdk = materialize_sdk(sdk_root, sdk_sha, platform)
            report.update(sdk_root=str(actual_sdk), sdk_sha=text_git(actual_sdk, "rev-parse", "HEAD"))
            consumer = {}
            try:
                build_consumer(source, platform, actual_sdk, env, consumer)
            finally:
                if "metadata" in consumer:
                    report["sdk_producers"] = {
                        slot: {key: data[key] for key in ("SOURCE_STATE", "APP_SOURCE_STATE")}
                        for slot, data in consumer["metadata"].items()
                    }
            report["sdk_consumer_check"] = "passed"
            package_dir.mkdir()
            if platform == "windows":
                cmake_args = ["-DGPLATFORM_BUILD_TESTS=OFF", "-DGPLATFORM_BUILD_SDK_CHECKS=ON",
                              "-DGPLATFORM_SDK_ROOT=" + str(actual_sdk)]
                qt = env.get("Qt6_DIR") or env.get("QT6_DIR")
                if qt:
                    cmake_args.append("-DQt6_DIR=" + qt)
                for old in (source / "package").glob("*.exe"):
                    old.unlink()
                windows_entry(source, ["GPlatform_installer"], cmake_args, env)
                packages = list((source / "package").glob("*.exe"))
                if len(packages) != 1:
                    raise RuntimeError("Expected exactly one Windows installer")
                package = package_dir / packages[0].name
                shutil.copy2(packages[0], package)
                report["architecture"] = "x64"
            else:
                scripts = Path(__file__).resolve().parents[1]
                run([sys.executable, scripts / "build_macos_universal.py", "--source-dir", source,
                     "--config", "Release", "--merge-only", "--sdk-root", actual_sdk], cwd=source, env=env)
                universal = source / "build-universal-Release"
                audit = json.loads((universal / "report.json").read_text())
                if audit.get("status") != "passed" or audit.get("architectures") != ["arm64", "x86_64"]:
                    raise RuntimeError("Universal package audit did not pass")
                spec = importlib.util.spec_from_file_location("selected_macos_bundle", scripts / "macos_bundle.py")
                packaging = importlib.util.module_from_spec(spec)
                spec.loader.exec_module(packaging)
                package = package_dir / "GPlatform.dmg"
                packaging.create_disk_image(universal / "GPlatform.app", package, output / "dmg.log")
                report["architecture"] = "universal"
            if not package.is_file() or not package.stat().st_size:
                raise RuntimeError("Installer is missing or empty")
            checks = inspect_installer(source, platform, package, output, env)
            checksum = checks["package_sha256"]
            checksum_path = package.with_suffix(package.suffix + ".sha256")
            checksum_path.write_text(f"{checksum}  {package.name}\n", encoding="utf-8")
        check_summary = {key: checks[key] for key in ("status", "payload_check", "startup", "not_tested",
                                                      "installer_script_sha256") if key in checks}
        if "distribution" in checks:
            check_summary["distribution_files"] = len(checks["distribution"])
        report.update(status="passed", package=package.name, sha256=checksum, checks=check_summary)
    except Exception as error:
        report["error"] = f"{type(error).__name__}: {error}"
        raise
    finally:
        write_json(report_path, report)
        files = dict(report=report_path.as_posix())
        if report["status"] == "passed":
            files.update(package=package.as_posix(), checksum=checksum_path.as_posix())
        write_outputs(files)
    print(f"Build and package checks passed: {package.name}")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source-dir", type=Path, required=True)
    p.add_argument("--platform", choices=("windows", "macos"), required=True)
    p.add_argument("--app-sha", required=True)
    p.add_argument("--sdk-root", type=Path, required=True)
    p.add_argument("--sdk-sha", required=True)
    a = p.parse_args()
    build(a.source_dir, a.platform, a.app_sha, a.sdk_root, a.sdk_sha)


if __name__ == "__main__":
    main()
