"""Run the maintained App regression entry against the audited macOS delivery bundle."""
from __future__ import annotations
import getpass
import json
import os
import re
from pathlib import Path
import subprocess
import sys
import http.cookiejar
import secrets
import signal
import time
import urllib.request
import urllib.error
import uuid

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


def server_regression(source, executable, output, env, *, timeout=14400):
    """Own a normal Server process, submit one all-ID task, retain both attempt stages."""
    output = Path(output)
    tools = Path(source) / 'tools/journal'
    sys.path.insert(0, str(tools))
    from session_repository import discover_scenarios
    ids = [path.name for path in discover_scenarios(Path(source) / 'tests/TestAssets/scenarios')]
    if not ids:
        raise RuntimeError('No active Journal scenarios')
    code = f'{secrets.randbelow(1000000):06d}'
    # The CLI owns its existing repository lock and shutdown contract. SIGBREAK
    # becomes the same KeyboardInterrupt on a Windows-owned console group.
    # run_path does not add the script directory as a normal Python script launch does.
    wrapper = ("import runpy,signal,sys; from pathlib import Path; "
               "signal.signal(getattr(signal,'SIGBREAK',signal.SIGTERM),signal.default_int_handler); "
               "sys.argv=sys.argv[1:]; sys.path.insert(0,str(Path(sys.argv[0]).resolve().parent)); "
               "runpy.run_path(sys.argv[0],run_name='__main__')")
    command = [sys.executable, '-u', '-c', wrapper, tools / 'journal_server.py',
               '--application', executable, '--app-root', source, '--assets', Path(source) / 'tests/TestAssets',
               '--output-root', output / 'run', '--exports', output / 'exports',
               '--port', '0', '--listen', '127.0.0.1', '--token', code, '--no-browser']
    opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()),
                                        urllib.request.ProxyHandler({}))
    base = None
    def request(path, payload=None):
        data = None if payload is None else json.dumps(payload).encode()
        req = urllib.request.Request(base + path, data=data, headers={'Content-Type': 'application/json'})
        with opener.open(req, timeout=15) as response:
            return json.load(response)
    def operation(path, payload=None):
        return request(path, dict(payload or {}, commandId=uuid.uuid4().hex))
    task_id = None
    last_progress = None
    last_heartbeat = time.monotonic()
    run_requested = False
    completed = False
    with (output / 'regression.log').open('w', encoding='utf8') as log:
        child = subprocess.Popen(list(map(str, command)), cwd=source, env=env, stdout=log,
                                 stderr=subprocess.STDOUT,
                                 creationflags=subprocess.CREATE_NEW_PROCESS_GROUP if os.name == 'nt' else 0)
        deadline = time.monotonic() + timeout
        try:
            while base is None:
                if child.poll() is not None:
                    raise RuntimeError('Journal Server startup failed; inspect regression.log')
                match = re.search(r'http://127[.]0[.]0[.]1:([0-9]+)', (output / 'regression.log').read_text(encoding='utf8'))
                if match:
                    base = match[0]
                    break
                if time.monotonic() >= min(deadline, deadline - timeout + 60):
                    raise TimeoutError('Journal Server did not become ready')
                time.sleep(.2)
            print('Journal Server ready: ' + base, flush=True)
            request('/api/login', {'token': code})
            run_requested = True
            operation('/api/run', {'cases': ids})
            while True:
                state = request('/api/state')
                task = state['task']
                if task_id is None:
                    task_id = task['id']
                if task.get('id') != task_id:
                    raise RuntimeError('Server task identity changed')
                write_json(output / 'server-state.json', state)
                progress = (task['state'], task.get('stage'), task.get('caseIndex'), task.get('case'))
                if progress != last_progress or time.monotonic() - last_heartbeat >= 30:
                    print('Journal ' + json.dumps({key:task.get(key) for key in
                        ('state','stage','caseIndex','total','case','step','totalSteps')}, ensure_ascii=False), flush=True)
                    last_progress, last_heartbeat = progress, time.monotonic()
                if task['state'] in ('completed', 'failed', 'cancelled'):
                    completed = True
                    directory = Path(task['output']).resolve()
                    if directory.parent != (output / 'run').resolve():
                        raise RuntimeError('Server returned a result outside owned staging')
                    summary = json.loads((directory / 'summary.json').read_text(encoding='utf8'))
                    if (task['state'] != 'completed' or summary.get('status') != 'passed'
                            or not summary.get('runComplete') or summary.get('cleanupError')
                            or summary.get('selectionScope') != 'all' or summary.get('candidateCollection')
                            or sorted(summary.get('plannedCases', [])) != sorted(ids)
                            or summary.get('completedCaseCount') != len(ids)
                            or sorted(c['id'] for c in summary.get('cases', [])) != sorted(ids)
                            or any(c.get('status') != 'passed' for c in summary.get('cases', []))):
                        raise RuntimeError(f'Journal Server full regression did not pass; see {directory}')
                    return summary, directory
                if child.poll() is not None:
                    raise RuntimeError('Journal Server exited before the task completed')
                if time.monotonic() >= deadline:
                    raise TimeoutError('Journal Server regression budget exhausted')
                time.sleep(1)
        finally:
            if child.poll() is None:
                if base and run_requested and not completed and request('/api/state')['task']['state'] in ('running', 'paused', 'cancelling'):
                    operation('/api/cancel')
                    until = time.monotonic() + 60
                    while time.monotonic() < until:
                        if request('/api/state')['task']['state'] in ('completed', 'failed', 'cancelled'):
                            break
                        time.sleep(.25)
                    else:
                        raise RuntimeError('Server cancellation did not finish; owned Server retained for cleanup')
                child.send_signal(signal.CTRL_BREAK_EVENT if os.name == 'nt' else signal.SIGINT)
                child.wait(timeout=30)
            if child.returncode != 0 and completed:
                raise RuntimeError(f'Journal Server cleanup failed: exit {child.returncode}')


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
    summary, directory = server_regression(source, executable, output, native_env)
    return dict(status='passed', assets_sha=assets_sha, case_count=summary['completedCaseCount'],
                summary_path=str(directory / 'summary.json'))
