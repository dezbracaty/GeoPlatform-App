"""Run the maintained App regression entry against the audited macOS delivery bundle."""
from __future__ import annotations
from concurrent.futures import ThreadPoolExecutor
import getpass
import json
import os
import re
from pathlib import Path
import subprocess
import sys

from common import git_env, run, text_git, write_json


def prepare_assets(source, selected_sha=None):
    """Initialize the declared repository, then consume the independently prepared test commit."""
    source = Path(source)
    expected = text_git(source, 'rev-parse', 'HEAD:tests/TestAssets')
    if len(expected) != 40:
        raise RuntimeError('Regression requires a fixed TestAssets gitlink')
    env = git_env()
    run(['git', '-C', source, 'submodule', 'sync', '--', 'tests/TestAssets'], env=env)
    run(['git', '-C', source, 'submodule', 'update', '--init', '--checkout', '--', 'tests/TestAssets'],
        env=env, attempts=3)
    assets = source / 'tests/TestAssets'
    if text_git(assets, 'rev-parse', 'HEAD') != expected:
        raise RuntimeError('TestAssets differs from the App fixed gitlink')
    selected_sha = selected_sha or expected
    if re.fullmatch('[0-9a-f]{40}', selected_sha) is None:
        raise ValueError('TestAssets must be pinned to a complete prepared SHA')
    if selected_sha != expected:
        run(['git', '-C', assets, 'fetch', '--depth=1', 'origin', selected_sha], env=env, attempts=3)
        run(['git', '-C', assets, 'checkout', '--detach', selected_sha], env=env)
    if text_git(assets, 'rev-parse', 'HEAD') != selected_sha:
        raise RuntimeError('TestAssets differs from the prepared regression SHA')
    run(['git', '-C', assets, 'lfs', 'pull'], env=env, attempts=3)
    return selected_sha


def regression(source, bundle, output, env, test_assets_sha=None):
    source, bundle, output = Path(source), Path(bundle), Path(output)
    if sys.platform != 'darwin':
        raise RuntimeError('macOS GUI regression requires macOS')
    console_user = subprocess.check_output(['/usr/bin/stat', '-f', '%Su', '/dev/console'], text=True).strip()
    if console_user != getpass.getuser():
        raise RuntimeError('Run the Mac mini runner in the logged-in GUI user session')
    tools = source / 'tools/journal'
    if not (tools / 'journal_execution_session.py').is_file():
        raise RuntimeError('The selected App commit is missing its bundled tools/journal')
    # Importing the comparator here checks required automatic-verification dependencies.
    run([sys.executable, '-c', 'import PIL'], env=env)
    assets_sha = prepare_assets(source, test_assets_sha)
    executable = bundle / 'Contents/MacOS/GPlatform'
    if not executable.is_file():
        raise RuntimeError('Regression executable is missing from the audited App bundle')
    output.mkdir(parents=True, exist_ok=True)
    paths = [tools / name for name in ('journal_runner.py', 'journal_execution_session.py', 'journal_display_profile.py')]
    from common import digest
    write_json(output / 'regression-inputs.json', dict(application=str(executable), assets_sha=assets_sha,
               app_sha=text_git(source, 'rev-parse', 'HEAD'),
               journalTools={path.name: digest(path) for path in paths}))
    # SDK checks use offscreen, but real Journal GUI regression must use the native desktop.
    native_env = dict(env)
    for key in ('QT_QPA_PLATFORM', 'QT_SCALE_FACTOR', 'QT_SCREEN_SCALE_FACTORS', 'QT_DEVICE_PIXEL_RATIO',
                'QT_AUTO_SCREEN_SCALE_FACTOR', 'QT_FONT_DPI', 'QT_ENABLE_HIGHDPI_SCALING',
                'QT_SCALE_FACTOR_ROUNDING_POLICY'):
        native_env.pop(key, None)
    command = [sys.executable, source / 'tools/run_acceptance.py', '--application', executable,
               '--journal-tools', tools, '--assets', source / 'tests/TestAssets', '--cases', 'all',
               '--output', output / 'run', '--exports', output / 'exports']
    with (output / 'regression.log').open('w', encoding='utf8') as log:
        result = subprocess.run([str(arg) for arg in command], cwd=source, env=native_env,
                                stdout=log, stderr=subprocess.STDOUT)
    summary_path = output / 'run/summary.json'
    summary = json.loads(summary_path.read_text(encoding='utf8')) if summary_path.is_file() else {}
    if result.returncode != 0 or summary.get('status') != 'passed' or not summary.get('runComplete'):
        raise RuntimeError(f"Mac mini regression did not pass: exit {result.returncode}; see {output}")
    return dict(status='passed', assets_sha=assets_sha, case_count=summary['completedCaseCount'])


def package_and_regress(package, verify, result_path):
    """Both children start after deployment; a failure never skips the other child's evidence."""
    results = {}
    with ThreadPoolExecutor(max_workers=2) as pool:
        pending = {name: pool.submit(operation) for name, operation in [('installer', package), ('regression', verify)]}
        for name, future in pending.items():
            try:
                value = future.result()
                results[name] = dict(value or {})
                results[name].setdefault('status', 'passed')
            except Exception as exc:
                results[name] = dict(status='failed', error=str(exc))
    write_json(result_path, results)
    return results
