#!/usr/bin/env python3
"""Prepare the independent TestAssets checkout used by native tests and Journal."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import tempfile

ASSETS_REPOSITORY = 'dezbracaty/GeoPlatform-TestAssets'
ASSETS_PATH = 'tests/TestAssets'
FULL_SHA = re.compile(r'[0-9a-f]{40}')


def git(path, *args, env=None):
    command = ['git', '-C', str(path), *args]
    env = dict(os.environ if env is None else env)
    for key in ('GIT_DIR', 'GIT_COMMON_DIR', 'GIT_WORK_TREE', 'GIT_INDEX_FILE',
                'GIT_OBJECT_DIRECTORY', 'GIT_ALTERNATE_OBJECT_DIRECTORIES'):
        env.pop(key, None)
    result = subprocess.run(command, env=env, capture_output=True, encoding='utf-8', errors='replace')
    if result.returncode:
        raise RuntimeError(f'Git {args[0]} failed in {path}: {result.stderr.strip() or result.stdout.strip()}')
    return result.stdout.strip()


def require_real_path(path):
    """Reject links and Windows reparse points before Git or filesystem operations."""
    try:
        info = path.lstat()
    except FileNotFoundError:
        return
    if (stat.S_ISLNK(info.st_mode)
            or getattr(info, 'st_file_attributes', 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT):
        raise RuntimeError(f'TestAssets path must not be a symlink or junction: {path}')


def asset_path(app):
    require_real_path(app)
    path = app / ASSETS_PATH
    for directory in (path.parent, path):
        require_real_path(directory)
        if directory.exists() and not directory.is_dir():
            raise RuntimeError(f'TestAssets path must be a directory: {directory}')
    return path


def checkout_head(app, path, *, env=None):
    """Verify the repository, including the exact legacy absorbed Git directory."""
    marker = path / '.git'
    require_real_path(marker)
    if not (marker.is_dir() or marker.is_file()):
        raise RuntimeError(f'Refusing to use a directory without a TestAssets checkout: {path}')
    top = Path(git(path, 'rev-parse', '--show-toplevel', env=env)).resolve()
    git_dir = Path(git(path, 'rev-parse', '--absolute-git-dir', env=env))
    common = Path(git(path, 'rev-parse', '--path-format=absolute', '--git-common-dir', env=env))
    expected = marker
    if marker.is_file():
        if Path(git(app, 'rev-parse', '--show-toplevel', env=env)).resolve() != app:
            raise RuntimeError(f'TestAssets checkout has no owning App repository: {path}')
        app_git = Path(git(app, 'rev-parse', '--absolute-git-dir', env=env)).resolve()
        expected = app_git / 'modules' / ASSETS_PATH
    if top != path or git_dir.resolve() != expected or common.resolve() != expected:
        raise RuntimeError(f'TestAssets Git directory is outside its expected location: {path}')
    name = ASSETS_REPOSITORY
    origins = {f'https://github.com/{name}', f'https://github.com/{name}.git',
               f'git@github.com:{name}.git', f'ssh://git@github.com/{name}.git'}
    if git(path, 'config', '--local', '--get-all', 'remote.origin.url', env=env) not in origins:
        raise RuntimeError(f'TestAssets origin does not match {name}: {path}')
    head = git(path, 'rev-parse', 'HEAD^{commit}', env=env)
    if not FULL_SHA.fullmatch(head):
        raise RuntimeError(f'TestAssets HEAD must be a complete commit SHA: {path}')
    return head


def require_clean(path, *, env=None):
    env = dict(os.environ if env is None else env, GIT_OPTIONAL_LOCKS='0')
    if git(path, 'status', '--porcelain', '--untracked-files=all', env=env):
        raise RuntimeError(f'TestAssets has local changes: {path}')
    if any(entry[0].islower() or entry[0] == 'S'
           for entry in git(path, 'ls-files', '-v', '-z', env=env).split('\0') if entry):
        raise RuntimeError(f'TestAssets index has assume-unchanged or skip-worktree entries: {path}')


def asset_entry(root, name):
    relative = Path(name)
    if not relative.parts or relative.anchor or '..' in relative.parts:
        raise ValueError(f'Invalid TestAssets catalog path: {name!r}')
    path = root
    for part in relative.parts:
        path = path / part
        require_real_path(path)
    return path


def check_catalog(path):
    catalog = json.loads(asset_entry(path, 'catalog.json').read_text(encoding='utf-8'))
    if catalog['schemaVersion'] != 1 or catalog['scenarioRoot'] != 'scenarios' or not catalog['modelCatalogs']:
        raise ValueError('Unrecognized TestAssets catalog')
    files = [catalog['scenarioSchema'], catalog['baselineManifest'],
             *(item['path'] for item in catalog['modelCatalogs']),
             'models/basic/editing.json', 'models/imported/printer-preset-switch/111.3mf',
             'slicing_regression/cases.json', 'slicing_regression/cases-extended.json']
    for name in files:
        if not asset_entry(path, name).is_file():
            raise RuntimeError(f'Required TestAssets file is missing: {name}')
    if not any(journal.is_file() for journal in asset_entry(path, 'scenarios').glob('*/events/Journal.json')):
        raise RuntimeError('TestAssets contains no active Journal files')


def missing_lfs(path, *, env=None):
    files = json.loads(git(path, 'lfs', 'ls-files', '--json', 'HEAD', env=env))['files']
    if not files:
        raise RuntimeError('TestAssets contains no Git LFS files')
    missing = []
    for item in files:
        file = asset_entry(path, item['name'])
        if not item['checkout'] or not file.is_file() or file.stat().st_size != item['size']:
            missing.append(item['name'])
    return missing


def check_assets(path, env, *, materialize=False):
    git(path, 'lfs', 'fsck', '--pointers', 'HEAD', env=env)
    missing = missing_lfs(path, env=env)
    if missing and materialize:
        if not git(path, 'config', '--default', '', '--get', 'filter.lfs.clean', env=env):
            git(path, 'lfs', 'install', '--local', '--skip-repo', env=env)
        git(path, 'lfs', 'pull', '--include=', '--exclude=', 'origin', env=env)
        missing = missing_lfs(path, env=env)
    if missing:
        raise RuntimeError(f'TestAssets Git LFS materialization failed: {missing[0]}')
    check_catalog(path)


def populate(path, ref, head, env):
    selected = ref
    branch = None if FULL_SHA.fullmatch(ref) else f'refs/heads/{ref}'
    if branch:
        git(path, 'check-ref-format', branch, env=env)
        selected, _, observed = git(path, 'ls-remote', '--exit-code', '--refs', 'origin', branch, env=env).partition('\t')
        if not FULL_SHA.fullmatch(selected) or observed != branch:
            raise RuntimeError('TestAssets branch did not resolve to a complete commit SHA')
    if selected != head:
        depth = ('--depth=1',) if head is None else ()
        git(path, 'fetch', '--no-tags', '--no-recurse-submodules', '--no-auto-maintenance',
            '--no-write-fetch-head', *depth, 'origin', selected, env=env)
        if git(path, 'rev-parse', f'{selected}^{{commit}}', env=env) != selected:
            raise RuntimeError('TestAssets fetch did not return the selected commit')
        git(path, 'checkout', '--detach', '--no-overwrite-ignore', selected, env=env)
    check_assets(path, env, materialize=True)
    actual = git(path, 'rev-parse', 'HEAD^{commit}', env=env)
    if actual != selected:
        raise RuntimeError('TestAssets checkout differs from the selected commit')
    if branch:
        git(path, 'update-ref', f'refs/remotes/origin/{ref}', selected, env=env)
    return actual


def check_configure_ref(path, head, sha, platform_branch, env):
    expected = sha
    if not sha:
        try:
            expected = git(path, 'rev-parse', '--verify',
                           f'refs/remotes/origin/{platform_branch}^{{commit}}', env=env)
        except RuntimeError:
            expected = None
    if expected != head:
        ref = sha or platform_branch
        raise RuntimeError(f'Existing TestAssets checkout at {path} has HEAD {head}; '
                           f'cannot reuse it for {ref}. Run scripts/prepare_test_assets.py '
                           f'--ref {ref} explicitly after saving local changes.')


def prepare_assets(app, ref, *, env=None, configure_default=None):
    """Use normal developer Git credentials unless the runner supplies its job environment."""
    selected = ref or configure_default
    if configure_default is not None and ref and not FULL_SHA.fullmatch(ref):
        raise ValueError('GPLATFORM_TEST_ASSETS_SHA must be a complete lowercase commit SHA')
    if not selected or selected.startswith(('-', 'refs/')):
        raise ValueError('Expected a TestAssets branch or complete lowercase commit SHA')
    app = Path(app).absolute()
    require_real_path(app)
    app = app.resolve()
    path = asset_path(app)
    env = dict(os.environ if env is None else env, GIT_LFS_SKIP_SMUDGE='1')
    git(app, 'lfs', 'version', env=env)
    if path.exists() and any(path.iterdir()):
        head = checkout_head(app, path, env=env)
        if configure_default is not None:
            check_configure_ref(path, head, ref, configure_default, env)
            check_assets(path, env)
            return path, head
        require_clean(path, env=env)
        return path, populate(path, selected, head, env)
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.test-assets-', dir=path.parent) as temporary:
        stage = Path(temporary) / 'checkout'
        stage.mkdir()
        git(stage, 'init', '--quiet', env=env)
        git(stage, 'remote', 'add', 'origin', f'https://github.com/{ASSETS_REPOSITORY}.git', env=env)
        git(stage, 'lfs', 'install', '--local', '--skip-repo', env=env)
        actual = populate(stage, selected, None, env)
        asset_path(app)
        if path.exists():
            path.rmdir()
        stage.rename(path)
    return path, actual


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ref', help='TestAssets branch or complete commit SHA')
    parser.add_argument('--configure-default', choices=('macmini', 'windows'), help=argparse.SUPPRESS)
    options = parser.parse_args()
    if not options.ref and not options.configure_default:
        parser.error('--ref is required')
    try:
        path, sha = prepare_assets(Path(__file__).resolve().parents[1], options.ref,
                                   configure_default=options.configure_default)
    except (OSError, RuntimeError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, f'TestAssets preparation failed: {error}\n')
    if options.configure_default:
        print(f'{sha} at {path}')
    else:
        print(f'TestAssets {ASSETS_REPOSITORY} {options.ref} {sha} at {path}')


if __name__ == '__main__':
    main()
