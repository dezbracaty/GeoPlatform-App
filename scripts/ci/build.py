"""Use the existing standalone App build/check/package entries."""
from __future__ import annotations
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile

from mac_regression import regression, package_and_regress
from common import digest, git_env, init_source_submodules, machine_lock, prepare_cached_source, run, text_git, write_json


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


def validate_sdk(source, slots):
    contract = source_contract(source)
    result = {}
    for slot in slots:
        root = source / 'ThirdParty/libs/GPlatformSDK' / slot
        metadata = json.loads((root / 'sdk-build.json').read_text())
        system = 'Windows' if slot.split('/')[0] == 'AMD64' else 'Darwin'
        if (metadata.get('SDK_FORMAT') != 3 or metadata.get('GPLATFORM_SDK_ARCH') != slot.split('/')[0]
                or metadata.get('CMAKE_BUILD_TYPE') != 'Release' or metadata.get('CMAKE_SYSTEM_NAME') != system):
            raise RuntimeError('App requires a matching format-3 SDK')
        contract.verify_sdk_package(root, metadata)
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


def build(source, platform, app_sha, sdk_sha, test_assets_sha=None):
    source = source.resolve()
    if text_git(source, "rev-parse", "HEAD") != app_sha:
        raise RuntimeError("App source SHA changed")
    if text_git(source, "rev-parse", "HEAD:ThirdParty/libs/GPlatformSDK") != sdk_sha:
        raise RuntimeError("App SDK gitlink differs from prepared SHA")
    slots = ["AMD64/Release"] if platform == "windows" else ["arm64/Release", "x86_64/Release"]
    output = Path(tempfile.mkdtemp(prefix="app-package-", dir=os.environ["RUNNER_TEMP"]))
    with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as f:
        f.write(f"deliverables={output}\n")
    with open(os.environ["GITHUB_ENV"], "a", encoding="utf-8") as f:
        f.write(f"APP_PACKAGE_ROOT={output}\n")
    env = git_env()
    with machine_lock():
        prepare_cached_source(source, app_sha, platform, consumer=True)
        init_source_submodules(source, consumer=True)
        metadata = validate_sdk(source, slots)
        validate_source_inputs(source, metadata)
        cmake_args = ["-DGPLATFORM_BUILD_SDK_CHECKS=ON"]
        if os.environ.get("Qt6_DIR"):
            cmake_args.append("-DQt6_DIR=" + os.environ["Qt6_DIR"])
        if platform == "windows":
            if os.name != "nt":
                raise RuntimeError("Windows build requires a Windows runner")
            def entry(target):
                windows_entry(source, target, cmake_args, env)
            entry("GPlatform")
            entry("SDKConsumerExtensionCheck")
            run(["ctest", "--test-dir", source / "build-Release", "-R", "^SDKConsumerExtensionCheck$",
                 "--no-tests=error", "--output-on-failure"], cwd=source,
                env=runtime_env(source / "build-Release", env))
            for old in (source / "package").glob("*.exe"):
                old.unlink()
            entry("GPlatform_installer")
            packages = list((source / "package").glob("*.exe"))
            if len(packages) != 1:
                raise RuntimeError("Expected exactly one Windows installer")
            package = packages[0]
            architecture = "x64"
        else:
            if sys.platform != "darwin":
                raise RuntimeError("Mac build requires a Mac runner")
            run(["arch", "-x86_64", "/usr/bin/true"], env=env)
            for preset, directory in (("arm_release", "build-arm64-Release"), ("x86_release", "build-x86_64-Release")):
                run(["bash", source / "scripts/build.sh", "--preset", preset, "--configure-only", "--", *cmake_args],
                    cwd=source, env=env)
                run(["cmake", "--build", "--preset", preset, "--target", "GPlatform", "SDKConsumerExtensionCheck"],
                    cwd=source, env=env)
                run(["ctest", "--test-dir", source / directory, "-R", "^SDKConsumerExtensionCheck$",
                     "--no-tests=error", "--output-on-failure"], cwd=source,
                    env=runtime_env(source / directory, env))
            run([sys.executable, source / "scripts/build_macos_universal.py", "--source-dir", source,
                 "--config", "Release", "--merge-only"], cwd=source, env=env)
            report = json.loads((source / "build-universal-Release/report.json").read_text())
            if report.get("status") != "passed" or report.get("architectures") != ["arm64", "x86_64"]:
                raise RuntimeError("Universal package audit did not pass")
            bundle = source / "build-universal-Release/GPlatform.app"
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
                return dict(package=package.name, sha256=checksum)
            stages = package_and_regress(make_installer,
                lambda: regression(source, bundle, regression_output, env, test_assets_sha), output / 'mac-stages.json')
            architecture = "universal"
            write_json(regression_output / 'regression-result.json', stages['regression'])
            write_json(output / 'build-result.json', dict(app_sha=app_sha, sdk_sha=sdk_sha,
                platform=platform, architecture=architecture, sdk_consumer_check='passed',
                installer=stages['installer'], regression=stages['regression'], installation_tested=False))
            if any(item['status'] != 'passed' for item in stages.values()):
                raise RuntimeError('macOS installer or regression failed; both stage results are retained')
            print('macOS installer and regression passed')
            return
        if not package.is_file() or not package.stat().st_size:
            raise RuntimeError("Installer is missing or empty")
        destination = output / package.name
        shutil.copy2(package, destination)
        checksum = digest(destination)
        (output / (destination.name + ".sha256")).write_text(f"{checksum}  {destination.name}\n")
        write_json(output / "build-result.json", dict(app_sha=app_sha, sdk_sha=sdk_sha,
                   platform=platform, architecture=architecture, sdk_consumer_check="passed",
                   package=destination.name, sha256=checksum, installation_tested=False))
    print(f"Build and package checks passed: {destination.name}")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source-dir", type=Path, required=True)
    p.add_argument("--platform", choices=("windows", "macos"), required=True)
    p.add_argument("--app-sha", required=True)
    p.add_argument("--sdk-sha", required=True)
    p.add_argument('--test-assets-sha', help='Prepared TestAssets commit for macOS regression')
    a = p.parse_args()
    if a.platform == 'macos' and not a.test_assets_sha:
        p.error('--test-assets-sha is required for macOS regression')
    build(a.source_dir, a.platform, a.app_sha, a.sdk_sha, a.test_assets_sha)


if __name__ == "__main__":
    main()
