"""Use the existing standalone App build/check/package entries."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile

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


def validate_sdk(source, slots):
    for slot in slots:
        root = source / "ThirdParty/libs/GPlatformSDK" / slot
        metadata = json.loads((root / "sdk-build.json").read_text())
        if str(metadata.get("SDK_FORMAT")) != "2" or metadata.get("GPLATFORM_SDK_ARCH") != slot.split("/")[0]:
            raise RuntimeError("App requires a matching format-2 SDK")
        manifest = json.loads((root / "sdk-manifest.json").read_text())
        actual = {p.relative_to(root).as_posix() for p in root.rglob("*")
                  if p.is_file() and p != root / "sdk-manifest.json"}
        if actual != set(manifest):
            raise RuntimeError("SDK inventory differs from manifest")
        for name, expected in manifest.items():
            relative = Path(name)
            if relative.is_absolute() or ".." in relative.parts or "\\" in name:
                raise RuntimeError("Unsafe SDK manifest path")
            path = root / relative
            if not path.resolve().is_relative_to(root.resolve()) or digest(path) != expected:
                raise RuntimeError(f"SDK payload hash differs: {name}")


def build(source, platform, app_sha, sdk_sha):
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
        validate_sdk(source, slots)
        cmake_args = ["-DGPLATFORM_BUILD_SDK_CHECKS=ON"]
        if os.environ.get("Qt6_DIR"):
            cmake_args.append("-DQt6_DIR=" + os.environ["Qt6_DIR"])
        if platform == "windows":
            if os.name != "nt":
                raise RuntimeError("Windows build requires a Windows runner")
            def entry(target):
                # -Command plus a parameter array handles PowerShell string[]
                # without concatenating untrusted arguments into executable text.
                command = "& $env:CI_BUILD_ENTRY -Preset release -Targets $env:CI_BUILD_TARGET -CMakeArgs @($env:CI_CMAKE_ARGS | ConvertFrom-Json); exit $LASTEXITCODE"
                step_env = dict(env, CI_BUILD_ENTRY=str(source / "scripts/build.ps1"),
                                CI_BUILD_TARGET=target, CI_CMAKE_ARGS=json.dumps(cmake_args))
                run(["pwsh", "-NoProfile", "-Command", command], cwd=source, env=step_env)
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
                 "--config", "Release", "--merge-only", "--dmg"], cwd=source, env=env)
            report = json.loads((source / "build-universal-Release/report.json").read_text())
            if report.get("status") != "passed" or report.get("architectures") != ["arm64", "x86_64"]:
                raise RuntimeError("Universal package audit did not pass")
            package = source / "build-universal-Release/GPlatform.dmg"
            architecture = "universal"
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
    a = p.parse_args()
    build(a.source_dir, a.platform, a.app_sha, a.sdk_sha)


if __name__ == "__main__":
    main()
