#!/usr/bin/env python3
"""Build two isolated macOS applications, deploy their dependencies, then merge.

The dependency cache intentionally stays single-architecture. Every executable
and library in the resulting app must contain both slices; resources must agree.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile

from macos_bundle import (ARCHITECTURES, BuildError, audit_bundle, capture,
                          deploy_bundle, merge_bundles, run)

ROOT = Path(__file__).resolve().parents[1]


def cache(build):
    values = {}
    path = build / "CMakeCache.txt"
    if path.exists():
        for line in path.read_text().splitlines():
            if line and not line.startswith(("#", "//")) and "=" in line:
                key, value = line.split("=", 1)
                values[key.split(":", 1)[0]] = value
    return values


def main(argv=None, default_source=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, default=default_source or ROOT)
    parser.add_argument("--cmake", default=shutil.which("cmake") or "cmake")
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--arm64-build-dir", type=Path)
    parser.add_argument("--x86_64-build-dir", type=Path)
    parser.add_argument("--qt-dir", type=Path, help="Directory containing Qt6Config.cmake")
    parser.add_argument("--config", choices=("Release", "Debug"), default="Release")
    parser.add_argument("--jobs", type=int, default=os.environ.get("CMAKE_BUILD_PARALLEL_LEVEL", "4"))
    parser.add_argument("--target", action="append", dest="targets", help="Override thin build targets; defaults to the selected project build target")
    parser.add_argument("--merge-only", action="store_true", help="Deploy and merge already built thin apps")
    args = parser.parse_args(argv)
    if platform.system() != "Darwin" or args.jobs < 1:
        parser.error("Run on macOS with a positive job count")
    import fcntl

    source_root = args.source_dir.resolve()
    if not (source_root / "CMakeLists.txt").is_file():
        parser.error(f"Missing project: {source_root}")
    producer = (source_root / "App/CMakeLists.txt").is_file()
    app_source = source_root / "App" if producer else source_root
    targets = args.targets or (["gplatform_build"] if producer else ["GPlatform"])
    work = (args.build_dir or source_root / f"build-universal-{args.config}").resolve()
    if work == source_root or source_root.is_relative_to(work):
        parser.error("Build directory must not contain the source repository")
    work.mkdir(parents=True, exist_ok=True)
    with (work / "build.lock").open("w") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise BuildError(f"Another Universal build is using {work}")
        environment = os.environ.copy()
        environment["CMAKE_BUILD_PARALLEL_LEVEL"] = str(args.jobs)
        bundles = []
        builds = {}
        excluded_plugins = {}
        qt = args.qt_dir
        selected_qt = None
        # Deploy a completed architecture while the other one compiles. The
        # thin build trees, staging bundles and logs are all independent.
        with tempfile.TemporaryDirectory(prefix="assemble-", dir=work) as temporary, \
                ThreadPoolExecutor(max_workers=2) as deploys:
            stage = Path(temporary)
            deployments = []
            for arch in ARCHITECTURES:
                override = getattr(args, arch + "_build_dir")
                build = (override or source_root / f"build-{arch}-{args.config}").resolve()
                if build == source_root or source_root.is_relative_to(build):
                    raise BuildError("Thin build directories must not contain the source repository")
                values = cache(build)
                for key, expected in (("CMAKE_OSX_ARCHITECTURES", arch), ("CMAKE_BUILD_TYPE", args.config),
                                      ("CMAKE_HOME_DIRECTORY", str(source_root))):
                    if values.get(key, expected) != expected:
                        raise BuildError(f"{build}: cached {key} differs; use a separate build directory")
                if not args.merge_only:
                    thin_preset = ("arm" if arch == "arm64" else "x86") + "_" + args.config.lower()
                    command = [args.cmake, "--preset", thin_preset, "-S", source_root, "-B", build]
                    if qt:
                        command.append(f"-DQt6_DIR={qt.resolve()}")
                    run(command, work / f"{arch}-configure.log", environment)
                    run([args.cmake, "--build", build, "--target", *targets, "-j", args.jobs],
                        work / f"{arch}-build.log", environment)
                values = cache(build)
                qt_directory = Path(values["Qt6_DIR"]).resolve()
                if selected_qt is not None and selected_qt != qt_directory:
                    raise BuildError("Both architectures must use the same Qt installation")
                selected_qt = qt_directory
                source = build / ("GPlatform/GPlatform.app" if producer else "GPlatform.app")
                if not source.is_dir():
                    raise BuildError(f"Missing built application: {source}")
                qt_deploy = qt_directory.parents[2] / "bin/macdeployqt"
                deployed = stage / arch / "GPlatform.app"
                deployments.append((arch, deploys.submit(deploy_bundle, source, deployed, build,
                    qt_deploy, work / f"{arch}-deploy.log", environment, app_source / "qml")))
                bundles.append(deployed)
                builds[arch] = str(build)
            for arch, deployment in deployments:
                excluded_plugins[arch] = deployment.result()
            merged = stage / "GPlatform.app"
            removed_rpaths = []
            merge_bundles(*bundles, merged, stage, removed_rpaths)
            run(["codesign", "--force", "--deep", "--sign", "-", merged], work / "codesign.log")
            run(["codesign", "--verify", "--deep", "--strict", merged], work / "codesign-verify.log")
            binaries = audit_bundle(merged)
            output = work / "GPlatform.app"
            if output.exists():
                shutil.rmtree(output)
            merged.rename(output)
            git_head = subprocess.run(["git", "-C", str(source_root), "rev-parse", "HEAD"],
                                      capture_output=True, text=True)
            report = {"formatVersion": 1, "status": "passed", "architectures": list(ARCHITECTURES),
                      "configuration": args.config, "sourceCommit": git_head.stdout.strip() if git_head.returncode == 0 else None,
                      "sourceStatus": capture(["git", "-C", source_root, "status", "--short"]) if git_head.returncode == 0 else None,
                      "sourceDirectory": str(source_root), "cmakeExecutable": str(args.cmake),
                      "jobs": args.jobs, "targets": targets,
                      "thinBuilds": builds, "app": str(output), "binaries": binaries,
                      "qtDirectory": str(selected_qt),
                      "excludedUnusedSqlDrivers": excluded_plugins,
                      "removedExternalLoaderRpaths": removed_rpaths,
                      "qmlStaticArchivesDeployed": False,
                      "runtimeTested": False, "signing": "local ad-hoc; not notarized"}
            (work / "report.json").write_text(json.dumps(report, indent=2) + "\n")
            print(f"Universal application: {output} ({len(binaries)} checked binaries)")


if __name__ == "__main__":
    try:
        main()
    except (BuildError, OSError, subprocess.SubprocessError, KeyError) as error:
        raise SystemExit(str(error))
