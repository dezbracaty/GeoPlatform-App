"""Validate and publish only the selected SDK slots."""
from __future__ import annotations
import argparse
import json
import hashlib
import importlib.util
import sys
from pathlib import Path
import shutil
import tempfile
import re

from common import (clone_branch, commit_tree, digest, git, git_env, push_exact_commit,
                    read_remote_head, repository_url, require_sha, SDK_DECLARATION, parse_sdk_repository,
                    text_git, write_outputs, run)

SLOTS = {"windows": ("AMD64/Release",), "macos": ("arm64/Release", "x86_64/Release")}


def build_sdk_package(source, platform, package, env):
    """Official preset producer/export used by both SDK publication and source regression."""
    source, package = Path(source).resolve(), Path(package).resolve()
    arguments = ['-DGPLATFORM_BUILD_TESTS=OFF', '-DGPLATFORM_BUILD_ORCA_NATIVE_PROBES=OFF']
    qt = env.get('Qt6_DIR') or env.get('QT6_DIR')
    if qt:
        arguments.append('-DQt6_DIR=' + qt)
    if platform == 'windows':
        command = ("$ErrorActionPreference = 'Stop'; try { $global:LASTEXITCODE = 0; "
                   "& $env:CI_BUILD_ENTRY -SourceDir $env:CI_SOURCE_DIR -Preset release -Targets gplatform_sdk_artifacts "
                   "-CMakeArgs @($env:CI_CMAKE_ARGS | ConvertFrom-Json); exit $LASTEXITCODE "
                   "} catch { [Console]::Error.WriteLine($_); exit 1 }")
        run(['pwsh', '-NoProfile', '-Command', command], cwd=source,
            env=dict(env, CI_BUILD_ENTRY=str(Path(__file__).resolve().parents[1] / 'build.ps1'), CI_SOURCE_DIR=str(source), CI_CMAKE_ARGS=json.dumps(arguments)))
        builds = ['build-Release']
    elif platform == 'macos':
        builds = []
        for preset, directory in [('arm_release', 'build-arm64-Release'), ('x86_release', 'build-x86_64-Release')]:
            run(['bash', Path(__file__).resolve().parents[1] / 'build.sh', '--source-dir', source, '--preset', preset, '--configure-only', '--', *arguments], cwd=source, env=env)
            run(['cmake', '--build', '--preset', preset, '--target', 'gplatform_sdk_artifacts'], cwd=source, env=env)
            builds.append(directory)
    else:
        raise ValueError('Unknown SDK platform')
    for directory in builds:
        run([sys.executable, source / 'cmake/sdk/publish_sdk.py', '--export', source / directory / 'sdk-export',
             '--output', package], cwd=source, env=env)
    return package


def is_sdk_binary(path):
    """Match the producer's library/object locations, excluding shared assets."""
    suffix = path.suffix.lower()
    if path.parts[0] == "objects":
        return suffix in {".o", ".obj"}
    return path.parts[0] == "lib" and (suffix in {".a", ".lib", ".dylib", ".dll", ".so"}
                                      or ".so." in path.name)


def validate_slot(slot, source_sha, contract):
    metadata = json.loads((slot / "sdk-build.json").read_text())
    state = metadata.get("SOURCE_STATE", {})
    if (metadata.get("SDK_FORMAT") != 3 or state.get("HEAD") != source_sha
            or state.get("DIRTY") is not False or state.get("MERGE_HEAD")
            or any(metadata.get("SOURCE_MODIFIED", {}).values())):
        raise RuntimeError("SDK must come from the fixed, clean format-3 source")
    architecture = slot.parent.name
    expected_system = "Windows" if architecture == "AMD64" else "Darwin"
    if (metadata.get("GPLATFORM_SDK_ARCH") != architecture
            or metadata.get("CMAKE_SYSTEM_NAME") != expected_system
            or metadata.get("CMAKE_BUILD_TYPE") != "Release"):
        raise RuntimeError("SDK platform, architecture or configuration differs")
    manifest = json.loads((slot / "sdk-manifest.json").read_text())
    if not isinstance(manifest, dict) or not manifest:
        raise RuntimeError("SDK manifest is empty")
    actual = set()
    for path in slot.rglob("*"):
        if path.is_symlink() and not path.resolve().is_relative_to(slot.resolve()):
            raise RuntimeError("SDK symlink escapes its slot")
        if path.is_file() and path != slot / "sdk-manifest.json":
            actual.add(path.relative_to(slot).as_posix())
    if actual != set(manifest):
        raise RuntimeError("SDK file inventory differs from manifest")
    for name, expected in manifest.items():
        rel = Path(name)
        if rel.is_absolute() or ".." in rel.parts or "\\" in name or not name:
            raise RuntimeError("Unsafe SDK manifest path")
        if digest(slot / rel) != expected:
            raise RuntimeError(f"SDK payload hash differs: {name}")
    contract.verify_sdk_package(slot, metadata)
    return metadata


def safe_destination(checkout, name):
    current = checkout
    for part in Path(name).parts:
        current = current / part
        if current.is_symlink() or current.is_junction() or (current.exists() and not current.is_dir()):
            raise RuntimeError("SDK destination or ancestor is not a real directory")
    if not current.resolve().is_relative_to(checkout.resolve()):
        raise RuntimeError("SDK destination escapes its checkout")
    return current


def pinned(repo, sha, path):
    entry = text_git(repo, 'ls-tree', sha, '--', path).split()
    if len(entry) != 4 or entry[:2] != ['160000', 'commit']:
        raise RuntimeError(f'Source dependency is not a pinned gitlink: {path}')
    return entry[2]


def source_contract(source, source_sha, app_sha):
    if text_git(source, 'rev-parse', 'HEAD') != source_sha:
        raise RuntimeError('Source checkout differs from requested SHA')
    if text_git(source, 'status', '--porcelain', '--untracked-files=no', '--ignore-submodules=all'):
        raise RuntimeError('SDK source checkout has tracked changes')
    app = Path(source) / 'App'
    require_sha(app_sha)
    pinned(source, source_sha, 'App')
    if text_git(app, 'rev-parse', 'HEAD') != app_sha:
        raise RuntimeError('App checkout differs from the fixed App candidate')
    if text_git(app, 'status', '--porcelain', '--untracked-files=no', '--ignore-submodules=all'):
        raise RuntimeError('SDK App source checkout has tracked changes')
    spec = importlib.util.spec_from_file_location('selected_source_inputs', app / 'cmake/source_inputs.py')
    module = importlib.util.module_from_spec(spec)
    bytecode = sys.dont_write_bytecode
    try:
        sys.dont_write_bytecode = True
        spec.loader.exec_module(module)
    finally:
        sys.dont_write_bytecode = bytecode
    return app_sha, module


def validate_source(source, sha, app_sha, metadata, contract):
    empty = hashlib.sha256(b'').hexdigest()
    app_state = metadata.get('APP_SOURCE_STATE', {})
    if (app_state.get('HEAD') != app_sha or app_state.get('DIRTY') is not False
            or app_state.get('DIFF_SHA256') != empty
            or metadata.get('SOURCE_STATE', {}).get('DIFF_SHA256') != empty):
        raise RuntimeError('SDK provenance differs from the fixed clean Root/App source')
    source = Path(source)
    expected = {fork: pinned(source, sha, 'ThirdParty/' + fork) for fork in ('SmoothUI', 'SmoothUIDock')}
    expected['OrcaSlicer'] = pinned(source / 'App', app_sha, 'ThirdParty/OrcaSlicer')
    expected['SmoothUIDock/KDDockWidgets'] = pinned(source / 'ThirdParty/SmoothUIDock',
                                                   expected['SmoothUIDock'], 'KDDockWidgets')
    if metadata.get('SOURCE_REVISIONS') != expected or set(metadata.get('SOURCE_MODIFIED', {})) != set(expected):
        raise RuntimeError('SDK metadata revisions differ from pinned source forks')
    roots = {fork: source / 'ThirdParty' / fork for fork in expected}
    roots['OrcaSlicer'] = source / 'App/ThirdParty/OrcaSlicer'
    for fork, root in roots.items():
        if text_git(root, 'status', '--porcelain', '--untracked-files=no'):
            raise RuntimeError(f'{fork} managed content has tracked changes')
    contract.verify(source / 'App/ThirdParty', None, metadata['SOURCE_INPUTS'], roots=roots)


def update_sdk(source, source_sha, platform, packages, url, expected_base=None, *, app_sha, publish=False):
    require_sha(source_sha)
    app_sha, contract = source_contract(source, source_sha, app_sha)
    app = Path(source) / 'App'
    declared = parse_sdk_repository(text_git(app, 'show', f'{app_sha}:{SDK_DECLARATION}'))
    if repository_url(declared) != url:
        raise RuntimeError("SDK target URL differs from the pinned source declaration")
    slots = SLOTS[platform]
    branch = platform
    metadata_by_slot = {}
    for name in slots:
        metadata = validate_slot(Path(packages) / "SDK" / name, source_sha, contract)
        validate_source(source, source_sha, app_sha, metadata, contract)
        metadata_by_slot[name] = metadata
    first = next(iter(metadata_by_slot.values()))
    for metadata in metadata_by_slot.values():
        for key in ('Qt6_VERSION', 'SOURCE_STATE', 'APP_SOURCE_STATE', 'SOURCE_INPUTS',
                    'SOURCE_REVISIONS', 'DEPENDENCIES', 'UI_BINARY_PACKAGE_VERSION', 'CONSUMER_SOURCE_FORKS'):
            if metadata.get(key) != first.get(key):
                raise RuntimeError(f'SDK architecture slots have different {key}')
    receipt = dict(source_sha=source_sha, app_sha=app_sha, declared_app_sha=pinned(source, source_sha, 'App'), platform=platform,
                   selected_slots=list(slots), published=False,
                   slot_manifests={name: digest(Path(packages) / 'SDK' / name / 'sdk-manifest.json') for name in slots})
    if not publish:
        return dict(receipt, sdk_sha='', updated_slots=[], changed=False)
    env = git_env("GPLATFORM_PUBLISH_TOKEN")
    base = expected_base or read_remote_head(url, branch, env=env)
    require_sha(base)
    if read_remote_head(url, branch, env=env) != base:
        raise RuntimeError("SDK expected base changed")
    with tempfile.TemporaryDirectory(prefix="ci-sdk-") as temporary:
        checkout = Path(temporary) / "sdk"
        if clone_branch(url, branch, checkout, env=env) != base:
            raise RuntimeError("SDK changed while preparing checkout")
        git(checkout, "lfs", "install", "--local", env=env)
        destinations = {name: safe_destination(checkout, name) for name in slots}
        for name in slots:
            destination = destinations[name]
            if destination.exists():
                shutil.rmtree(destination)
            shutil.copytree(Path(packages) / "SDK" / name, destination, symlinks=True)
        git(checkout, "add", "-A", "--", *slots, env=env)
        changed = git(checkout, "diff", "--cached", "--name-only", "-z").decode().split("\0")
        if any(p and not any(p.startswith(slot + "/") for slot in slots) for p in changed):
            raise RuntimeError("SDK update escaped selected slots")
        binaries = [p.relative_to(checkout).as_posix() for name in slots
                    for p in (checkout / name).rglob("*") if p.is_file() and
                    is_sdk_binary(p.relative_to(checkout / name))]
        for path in binaries:
            if not (checkout / path).exists():
                continue
            attrs = git(checkout, "check-attr", "-z", "filter", "--", path).split(b"\0")
            if len(attrs) < 3 or attrs[2] != b"lfs":
                raise RuntimeError(f"SDK binary is not covered by LFS: {path}")
            pointer = git(checkout, "show", f":{path}")
            match = re.fullmatch(rb"version https://git-lfs.github.com/spec/v1\noid sha256:([0-9a-f]{64})\nsize ([0-9]+)\n", pointer)
            payload = checkout / path
            if (not match or match[1].decode() != digest(payload)
                    or int(match[2]) != payload.stat().st_size):
                raise RuntimeError(f"SDK staged LFS pointer differs from payload: {path}")
        tree = text_git(checkout, "write-tree")
        old_tree = text_git(checkout, "rev-parse", f"{base}^{{tree}}")
        commit = base if tree == old_tree else commit_tree(
            checkout, tree, base, f"Update {platform} SDK from Root {source_sha} App {app_sha}", env=env)
        push_exact_commit(checkout, url, branch, base, commit, env=env, lfs=commit != base)
    return dict(receipt, sdk_sha=commit, published=True,
                updated_slots=list(slots), changed=commit != base, expected_base_sha=base)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source-dir", type=Path, required=True)
    p.add_argument("--source-sha", required=True)
    p.add_argument("--app-sha", required=True)
    p.add_argument("--platform", choices=SLOTS, required=True)
    p.add_argument("--package-root", type=Path, required=True)
    p.add_argument("--sdk-repository", required=True)
    p.add_argument("--expected-base-sha")
    p.add_argument("--publish", action="store_true", help="Explicitly publish selected slots; default only validates artifacts")
    p.add_argument("--result", type=Path, required=True)
    a = p.parse_args()
    result = update_sdk(a.source_dir, a.source_sha, a.platform, a.package_root,
                        repository_url(a.sdk_repository), a.expected_base_sha, app_sha=a.app_sha, publish=a.publish)
    write_outputs(result, a.result)


if __name__ == "__main__":
    main()
