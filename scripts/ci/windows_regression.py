"""Run the maintained acceptance entry on the deployed Windows App."""
from __future__ import annotations
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import subprocess
import sys

from common import digest, text_git, write_json
from mac_regression import prepare_assets


def require_desktop():
    if sys.platform != 'win32':
        raise RuntimeError('Windows Journal regression requires Windows')
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    session = wintypes.DWORD()
    if not kernel.ProcessIdToSessionId(os.getpid(), ctypes.byref(session)) or session.value == 0:
        raise RuntimeError('Run the Windows runner in a logged-in desktop session, not Session 0')
    user = ctypes.WinDLL('user32', use_last_error=True)
    user.OpenInputDesktop.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
    user.OpenInputDesktop.restype = wintypes.HANDLE
    user.SwitchDesktop.argtypes = (wintypes.HANDLE,)
    user.CloseDesktop.argtypes = (wintypes.HANDLE,)
    desktop = user.OpenInputDesktop(0, False, 0x0100)
    if not desktop:
        raise RuntimeError('Windows regression requires an accessible, unlocked input desktop')
    try:
        if not user.SwitchDesktop(desktop):
            raise RuntimeError('Windows regression requires an unlocked desktop')
    finally:
        user.CloseDesktop(desktop)


def regression(source, executable, output, env, test_assets_sha):
    source, executable, output = Path(source), Path(executable), Path(output)
    require_desktop()
    from PIL import Image
    if not executable.is_file():
        raise RuntimeError('Windows regression executable is missing from the deployed App')
    assets_sha = prepare_assets(source, test_assets_sha)
    output.mkdir(parents=True, exist_ok=True)
    tools = source / 'tools/journal'
    write_json(output / 'regression-inputs.json', dict(application=str(executable),
        application_sha256=digest(executable), app_sha=text_git(source, 'rev-parse', 'HEAD'),
        assets_sha=assets_sha, journalTools={name: digest(tools / name) for name in
        ('journal_runner.py', 'journal_execution_session.py', 'journal_display_profile.py')}))
    scenes = sorted((source / 'tests/TestAssets/scenarios').glob('*/metadata.json'))
    pending = []
    for metadata in scenes:
        declaration = json.loads(metadata.read_text(encoding='utf-8'))
        review = declaration.get('baselineReview', {})
        if review.get('platform') != 'Windows amd64' or review.get('status') != 'approved':
            pending.append(metadata.parent.name)
        directory = declaration.get('preconditions', {}).get('applicationDirectory')
        if (directory and Path(directory).resolve() != executable.parent.resolve()
                and not declaration.get('deploymentResources')):
            raise RuntimeError(f'Windows DB baseline deployment directory differs for '
                               f'{metadata.parent.name}: expected {directory}; actual {executable.parent}')
    if not scenes or pending:
        raise RuntimeError(f'Windows standards require reviewed baselines before CI: {pending}')
    native = dict(env)
    native.pop('QT_QPA_PLATFORM', None)
    native['PYTHONIOENCODING'] = 'utf-8'
    # run_acceptance owns the explicit Windows child-process DPR=1 policy.
    command = [sys.executable, source / 'tools/run_acceptance.py', '--application', executable,
               '--assets', source / 'tests/TestAssets', '--cases', 'all',
               '--output', output / 'run', '--exports', output / 'exports']
    with (output / 'regression.log').open('w', encoding='utf-8') as log:
        completed = subprocess.run([str(x) for x in command], cwd=source, env=native,
                                   stdout=log, stderr=subprocess.STDOUT)
    path = output / 'run/summary.json'
    summary = json.loads(path.read_text(encoding='utf-8')) if path.is_file() else {}
    if (completed.returncode != 0 or summary.get('status') != 'passed'
            or not summary.get('runComplete') or summary.get('selectionScope') != 'all'
            or summary.get('candidateCollection') or not summary.get('cases')):
        raise RuntimeError(f'Windows regression did not pass: exit {completed.returncode}; see {output}')
    names = [scene.parent.name for scene in scenes]
    if (summary.get('plannedCases') != names or summary.get('completedCaseCount') != len(names)
            or sorted(case['id'] for case in summary['cases']) != names
            or any(case.get('status') != 'passed' for case in summary['cases'])):
        raise RuntimeError('Windows regression did not pass every scene in the fixed asset inventory')
    for case in summary['cases']:
        result = output / 'run' / case['id'] / 'execution_result.json'
        execution = json.loads(result.read_text(encoding='utf-8'))
        window = execution.get('actualWindow', {})
        if window.get('devicePixelRatio') != 1 or window.get('logicalSize') != [1382, 954]:
            raise RuntimeError(f"Windows regression display profile differs: {case['id']}")
        for step in execution.get('results', []):
            if step['kind'] == 'verify.screenshot':
                actual = result.parent / step['evidence'][1]
                with Image.open(actual) as capture:
                    if capture.size != (1382, 954):
                        raise RuntimeError(f'Windows screenshot size differs: {actual}')
    return dict(status='passed', assets_sha=assets_sha, case_count=summary['completedCaseCount'])
