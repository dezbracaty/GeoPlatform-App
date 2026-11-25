"""Validate and publish only the selected SDK slots."""
from __future__ import annotations
import argparse
import json
import hashlib
import os
import sys
from pathlib import Path
import shutil
import subprocess
import tempfile
import re
from types import ModuleType
from zipfile import ZipFile

from common import (app_branch_name, branch_ref, checkout_sdk_base, commit_tree, digest, git, git_env,
                    parse_sdk_repository, push_exact_commit, read_remote_head, repository_url, require_sha,
                    run, SDK_DECLARATION, selected_platforms, text_git, write_outputs)

SLOTS = {"windows": ("AMD64/Release",), "macos": ("arm64/Release", "x86_64/Release")}


def prepare_targets(url, app_ref, platform, *, windows_sdk_target='', macos_sdk_target='', env=None):
    """Freeze one expected head per target and a seed commit for new branches."""
    overrides = {'windows': windows_sdk_target, 'macos': macos_sdk_target}
    targets = {}
    for name in selected_platforms(platform):
        branch = overrides[name] or app_branch_name(app_ref)
        if not branch:
            raise ValueError(f'{name}_sdk_target is required to publish from an App SHA')
        branch_ref(branch)
        targets.setdefault(branch, []).append(name)

    plans = []
    seed = None
    for branch, platforms in targets.items():
        base = read_remote_head(url, branch, env=env, allow_missing=True)
        if not base and seed is None:
            seed = read_remote_head(url, None, env=env)
        plans.append(dict(branch=branch, platforms=platforms, base=base, seed='' if base else seed))
    return plans


def build_sdk_package(source, platform, package, env, *, log=None):
    """Produce a source SDK with the official presets and exporter."""
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
        run(['pwsh', '-NoProfile', '-Command', command], cwd=source, log=log,
            env=dict(env, CI_BUILD_ENTRY=str(Path(__file__).resolve().parents[1] / 'build.ps1'), CI_SOURCE_DIR=str(source), CI_CMAKE_ARGS=json.dumps(arguments)))
        builds = ['build-Release']
    elif platform == 'macos':
        builds = []
        for preset, directory in [('arm_release', 'build-arm64-Release'), ('x86_release', 'build-x86_64-Release')]:
            run(['bash', Path(__file__).resolve().parents[1] / 'build.sh', '--source-dir', source, '--preset', preset, '--configure-only', '--', *arguments], cwd=source, env=env, log=log)
            run(['cmake', '--build', '--preset', preset, '--target', 'gplatform_sdk_artifacts'], cwd=source, env=env, log=log)
            builds.append(directory)
    else:
        raise ValueError('Unknown SDK platform')
    for directory in builds:
        run([sys.executable, source / 'cmake/sdk/publish_sdk.py', '--export', source / directory / 'sdk-export',
             '--output', package], cwd=source, env=env, log=log)
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
    validate_slot_manifest(slot)
    contract.verify_sdk_package(slot, metadata)
    return metadata


def validate_slot_manifest(slot):
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


def validate_package_manifest(package, slots):
    manifest_path = package / 'package-manifest.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    expected = {}
    for slot in slots:
        prefix = f'SDK/{slot}/'
        slot_manifest = package / prefix / 'sdk-manifest.json'
        expected[prefix + 'sdk-manifest.json'] = digest(slot_manifest)
        expected.update({prefix + name: sha
                         for name, sha in json.loads(slot_manifest.read_text(encoding='utf-8')).items()})
    actual = {path.relative_to(package).as_posix() for path in (package / 'SDK').rglob('*') if path.is_file()}
    if manifest != expected or actual != set(expected):
        raise RuntimeError('Complete SDK package differs from its package manifest or selected slots')
    return digest(manifest_path)


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


def source_contract(source, source_sha, app_sha, *, require_clean):
    if text_git(source, 'rev-parse', 'HEAD') != source_sha:
        raise RuntimeError('Source checkout differs from requested SHA')
    if require_clean and text_git(source, 'status', '--porcelain', '--untracked-files=no', '--ignore-submodules=all'):
        raise RuntimeError('SDK source checkout has tracked changes')
    app = Path(source) / 'App'
    require_sha(app_sha)
    pinned(source, source_sha, 'App')
    if text_git(app, 'rev-parse', 'HEAD') != app_sha:
        raise RuntimeError('App checkout differs from the fixed App candidate')
    if require_clean and text_git(app, 'status', '--porcelain', '--untracked-files=no', '--ignore-submodules=all'):
        raise RuntimeError('SDK App source checkout has tracked changes')
    path = 'cmake/source_inputs.py'
    module = ModuleType('selected_source_inputs')
    module.__file__ = str(app / path)
    exec(compile(git(app, 'show', f'{app_sha}:{path}'), module.__file__, 'exec'), module.__dict__)
    return module


def validate_source(source, sha, app_sha, metadata, contract):
    empty = hashlib.sha256(b'').hexdigest()
    app_state = metadata.get('APP_SOURCE_STATE', {})
    if (app_state.get('HEAD') != app_sha or app_state.get('DIRTY') is not False
            or app_state.get('MERGE_HEAD') or app_state.get('DIFF_SHA256') != empty
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


def validate_package(source, source_sha, platform, package, url, *, app_sha, require_clean=True):
    require_sha(source_sha)
    contract = source_contract(source, source_sha, app_sha, require_clean=require_clean)
    app = Path(source) / 'App'
    declared = parse_sdk_repository(text_git(app, 'show', f'{app_sha}:{SDK_DECLARATION}'))
    if repository_url(declared) != url:
        raise RuntimeError("SDK target URL differs from the pinned source declaration")
    slots = SLOTS[platform]
    package = Path(package)
    metadata_by_slot = {}
    for name in slots:
        metadata = validate_slot(package / "SDK" / name, source_sha, contract)
        validate_source(source, source_sha, app_sha, metadata, contract)
        metadata_by_slot[name] = metadata
    first = next(iter(metadata_by_slot.values()))
    for metadata in metadata_by_slot.values():
        for key in ('Qt6_VERSION', 'SOURCE_STATE', 'APP_SOURCE_STATE', 'SOURCE_INPUTS',
                    'SOURCE_REVISIONS', 'DEPENDENCIES', 'UI_BINARY_PACKAGE_VERSION', 'CONSUMER_SOURCE_FORKS'):
            if metadata.get(key) != first.get(key):
                raise RuntimeError(f'SDK architecture slots have different {key}')
    return dict(source_sha=source_sha, app_sha=app_sha, declared_app_sha=pinned(source, source_sha, 'App'),
                platform=platform, selected_slots=list(slots), published=False, sdk_sha='', updated_slots=[], changed=False,
                package_manifest_sha256=validate_package_manifest(package, slots),
                slot_manifests={name: digest(package / 'SDK' / name / 'sdk-manifest.json') for name in slots})


def gh_git_env():
    env = git_env(secret=None)
    for name in tuple(env):
        if name.startswith('GIT_TRACE') or name in ('GH_TOKEN', 'GITHUB_TOKEN', 'GH_DEBUG', 'DEBUG',
                                                    'GIT_CURL_VERBOSE', 'GIT_CONFIG_PARAMETERS'):
            env.pop(name)
    offset = int(env['GIT_CONFIG_COUNT'])
    env.update({'GH_HOST': 'github.com', 'GH_PROMPT_DISABLED': '1',
                'GIT_CONFIG_COUNT': str(offset + 1),
                f'GIT_CONFIG_KEY_{offset}': 'credential.https://github.com.helper',
                f'GIT_CONFIG_VALUE_{offset}': '!gh auth git-credential'})
    return env


def gh_json(path, env):
    return json.loads(run(['gh', 'api', '--hostname', 'github.com', path], env=env, capture=True, attempts=3))


def successful_build_artifacts(repo, run_id, attempt, platforms, env):
    base = f'repos/{repo}/actions/runs/{run_id}'
    info = gh_json(f'{base}/attempts/{attempt}', env)
    if (info.get('id') != run_id or info.get('run_attempt') != attempt
            or info.get('path') != '.github/workflows/build-sdk.yml' or info.get('event') != 'workflow_dispatch'
            or info['repository']['full_name'].casefold() != repo.casefold()):
        raise RuntimeError('Requested run/attempt is not the selected repository Build SDK')
    if (info.get('status'), info.get('conclusion')) != ('completed', 'success'):
        raise RuntimeError(f"Build SDK {run_id}/{attempt}: {info.get('status')} ({info.get('conclusion')})")
    return info, successful_platform_artifacts(repo, run_id, attempt, platforms, env)


def successful_platform_artifacts(repo, run_id, attempt, platforms, env):
    base = f'repos/{repo}/actions/runs/{run_id}'
    jobs = gh_json(f'{base}/attempts/{attempt}/jobs?per_page=100', env)['jobs']
    artifacts = gh_json(f'{base}/artifacts?per_page=100', env)['artifacts']
    selected = {}
    for platform in platforms:
        name = f'sdk-{platform}-{run_id}-{attempt}'
        matching_jobs = [job for job in jobs if job['name'] == f'sdk-build-{platform}']
        if len(matching_jobs) != 1:
            raise RuntimeError(f'Expected one Build SDK job for {platform}')
        job = matching_jobs[0]
        steps = {step['name']: step['conclusion'] for step in job['steps']}
        if (job['run_id'] != run_id or job['run_attempt'] != attempt
                or (job['status'], job['conclusion']) != ('completed', 'success')
                or steps.get('Build and export selected SDK') != 'success'
                or steps.get('Upload SDK delivery evidence') != 'success'):
            raise RuntimeError(f'Build SDK build/upload did not succeed for {platform}')
        matches = [item for item in artifacts if item['name'] == name]
        if len(matches) != 1 or matches[0]['expired']:
            raise RuntimeError(f'Expected one available artifact: {name}')
        artifact = matches[0]
        if not re.fullmatch(r'sha256:[0-9a-f]{64}', artifact.get('digest') or ''):
            raise RuntimeError(f'Artifact has no GitHub SHA256 digest: {name}')
        selected[platform] = artifact
    return selected


def download_artifact(repo, artifact, package, env):
    package.mkdir()
    path = package / 'download.zip'
    endpoint = f"repos/{repo}/actions/artifacts/{artifact['id']}/zip"
    with path.open('wb') as stream:
        subprocess.run(['gh', 'api', '--hostname', 'github.com', endpoint], env=env, stdout=stream,
                       stderr=subprocess.DEVNULL, check=True)
    if 'sha256:' + digest(path) != artifact['digest']:
        raise RuntimeError(f"GitHub artifact digest differs: {artifact['name']}")
    with ZipFile(path) as archive:
        archive.extractall(package)
    path.unlink()
    if {entry.name for entry in package.iterdir()} != {'SDK', 'package-manifest.json', 'source-receipt.json'}:
        raise RuntimeError(f"Artifact does not contain the complete SDK and receipts: {artifact['name']}")


def verify_downloaded_package(source, source_sha, app_sha, platform, package, url):
    receipt = json.loads((package / 'source-receipt.json').read_text(encoding='utf-8'))
    expected = validate_package(source, source_sha, platform, package, url, app_sha=app_sha, require_clean=False)
    if any(receipt.get(key) != value for key, value in expected.items()):
        raise RuntimeError(f'Downloaded {platform} SDK receipt does not match the verified package')
    return receipt


def publish_run(args, result):
    source = args.source_dir.resolve()
    source_sha, app_sha = require_sha(args.source_sha), require_sha(args.app_sha)
    repo = args.app_repository
    repository_url(repo)
    platforms = selected_platforms(args.platform)
    env = gh_git_env()
    info, artifacts = successful_build_artifacts(repo, args.run, args.attempt, platforms, env)
    result.update(app_repository=repo, app_repository_id=info['repository']['id'],
                  workflow_sha=info['head_sha'], artifacts={}, branches={})
    declared = parse_sdk_repository(text_git(source / 'App', 'show', f'{app_sha}:{SDK_DECLARATION}'))
    url = repository_url(declared)
    result['sdk_repository'] = declared
    plans = prepare_targets(url, args.app_branch, args.platform, windows_sdk_target=args.windows_sdk_target,
                            macos_sdk_target=args.macos_sdk_target, env=env)
    with tempfile.TemporaryDirectory(prefix='sdk-downloads-') as directory:
        downloads = Path(directory)
        for platform, artifact in artifacts.items():
            package = downloads / platform
            download_artifact(repo, artifact, package, env)
            receipt = verify_downloaded_package(source, source_sha, app_sha, platform, package, url)
            result['artifacts'][platform] = dict(id=artifact['id'], name=artifact['name'],
                                                 digest=artifact['digest'], receipt=receipt)
        for plan in plans:
            slots = {slot: downloads / platform / 'SDK' / slot
                     for platform in plan['platforms'] for slot in SLOTS[platform]}
            result['branches'][plan['branch']] = publish_slots(url, plan['branch'], plan['base'], plan['seed'],
                                                              slots, source_sha, app_sha, env=env)


def publish_slots(url, branch, base, seed, slots, source_sha, app_sha, *, env=None):
    """Commit all collected slots in one guarded target-branch update."""
    if env is None:
        env = git_env(secret=None)
    parent = require_sha(base or seed)
    if read_remote_head(url, branch, env=env, allow_missing=True) != base:
        raise RuntimeError("SDK expected base changed")
    with tempfile.TemporaryDirectory(prefix="ci-sdk-") as temporary:
        checkout = Path(temporary) / "sdk"
        if checkout_sdk_base(url, parent, checkout, env=env) != parent:
            raise RuntimeError("SDK changed while preparing checkout")
        git(checkout, "lfs", "install", "--local", env=env)
        destinations = {name: safe_destination(checkout, name) for name in slots}
        for name, slot in slots.items():
            destination = destinations[name]
            if destination.exists():
                shutil.rmtree(destination)
            shutil.copytree(slot, destination, symlinks=True)
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
        old_tree = text_git(checkout, "rev-parse", f"{parent}^{{tree}}")
        commit = base if base and tree == old_tree else commit_tree(
            checkout, tree, parent, f"Update SDK from Root {source_sha} App {app_sha}", env=env)
        push_exact_commit(checkout, url, branch, base, commit, env=env, lfs=tree != old_tree, seed=seed)
    return dict(target=branch, sdk_sha=commit, published=True, updated_slots=list(slots),
                changed=commit != base, expected_base_sha=base, seed_sha=seed)


def main():
    p = argparse.ArgumentParser(description='Publish complete SDK artifacts from one successful Build SDK run attempt.')
    p.add_argument("--source-dir", type=Path, required=True)
    p.add_argument("--source-sha", required=True)
    p.add_argument("--app-sha", required=True)
    p.add_argument("--app-repository", default="dezbracaty/GeoPlatform-App")
    p.add_argument("--run", type=int, required=True)
    p.add_argument("--attempt", type=int, required=True)
    p.add_argument("--platform", choices=("windows", "macos", "all"), required=True)
    p.add_argument("--app-branch", default="", help="Default SDK target for the selected platforms")
    p.add_argument("--windows-sdk-target", default="")
    p.add_argument("--macos-sdk-target", default="")
    p.add_argument("--result", type=Path, required=True)
    a = p.parse_args()
    if a.run < 1 or a.attempt < 1:
        p.error('Run and attempt must be positive')
    result = dict(status='failed', run_id=a.run, attempt=a.attempt, source_sha=a.source_sha, app_sha=a.app_sha)
    try:
        publish_run(a, result)
        result['status'] = 'passed'
    except Exception as error:
        result['error'] = str(error)
        raise
    finally:
        write_outputs(result, a.result)


if __name__ == "__main__":
    main()
