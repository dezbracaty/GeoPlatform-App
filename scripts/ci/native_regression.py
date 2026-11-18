"""Maintained full-host native checks used by App platform regression."""
from __future__ import annotations
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import time

from common import git_env, text_git, write_json


def command(args, cwd, env, log, timeout=7200, input_data=None):
    """Retain output without retrying a build or test that fails."""
    print('RUN ' + json.dumps(list(map(str, args))), flush=True)
    with log.open('w', encoding='utf-8') as stream:
        process = subprocess.Popen(list(map(str, args)), cwd=cwd, env=env,
                                   stdout=stream, stderr=subprocess.STDOUT,
                                   stdin=subprocess.PIPE if input_data is not None else None,
                                   start_new_session=os.name != 'nt')
        deadline = time.monotonic() + timeout
        try:
            if input_data is not None:
                process.stdin.write(input_data)
                process.stdin.close()
            while True:
                try:
                    return process.wait(timeout=min(30, max(0.01, deadline - time.monotonic())))
                except subprocess.TimeoutExpired:
                    print(f'Still running: {log.name}; {log.stat().st_size} log bytes', flush=True)
                    if time.monotonic() >= deadline:
                        raise TimeoutError(f'Command budget exhausted: {log}')
        finally:
            if os.name == 'nt':
                if process.poll() is None:
                    subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'], capture_output=True)
            else:
                # Reap this command's group even when its leader already exited.
                # A child can ignore TERM, so leader.wait() alone is insufficient.
                try:
                    os.killpg(process.pid, signal.SIGTERM)
                    until = time.monotonic() + 2
                    while time.monotonic() < until:
                        try:
                            os.killpg(process.pid, 0)
                        except ProcessLookupError:
                            break
                        time.sleep(0.05)
                    else:
                        os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            try:
                process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                if os.name != 'nt':
                    os.killpg(process.pid, signal.SIGKILL)
                else:
                    process.kill()
                process.wait(timeout=15)


def phase(report, name, operation):
    entry = {'name': name, 'status': 'running'}
    report['phases'].append(entry)
    started = time.monotonic()
    try:
        details = operation()
        entry.update(details or {})
        entry.setdefault('returncode', 0)
        entry['status'] = 'passed' if entry['returncode'] == 0 else 'failed'
    except Exception as error:
        entry.update(status='blocked', error=f'{type(error).__name__}: {error}')
    entry['elapsed_seconds'] = round(time.monotonic() - started, 2)
    write_json(Path(report['output']) / 'summary.json', report)
    print(json.dumps(entry), flush=True)
    return entry['status'] == 'passed'


def runtime(source, build, env):
    result = dict(env)
    qt = Path(env['Qt6_DIR']).resolve().parents[2]
    directories = [qt / 'bin', build / 'GPlatform', build]
    directories += sorted(build.glob('sdk-export/**/bin'))
    result['PATH'] = os.pathsep.join(map(str, directories)) + os.pathsep + env.get('PATH', '')
    return result


def windows_native_environment(provenance, env):
    """Use the dependency versions this standalone probe actually linked."""
    dependencies = provenance.get('dependencyCacheValues', {})
    directories = []
    for key, filename in [('OpenCASCADE_DIR', 'TKernel.dll'), ('GMP_LIBRARY', 'libgmp-10.dll')]:
        location = dependencies.get(key)
        if not location:
            raise RuntimeError(f'Native provenance is missing {key}')
        prefix = next((p for p in Path(location).resolve().parents
                       if p.name.lower().startswith('install-')), None)
        if prefix is None:
            raise RuntimeError(f'Cannot locate native dependency installation: {key}')
        candidates = list(prefix.rglob(filename))
        if len(candidates) != 1:
            raise RuntimeError(f'Expected one {filename} in {prefix}, found {len(candidates)}')
        directories.append(candidates[0].parent)
    result = dict(env)
    result['PATH'] = os.pathsep.join(map(str, directories)) + os.pathsep + env.get('PATH', '')
    return result


def build_environment():
    env = git_env()
    # os.environ is case-insensitive on Windows, dict(os.environ) is not.
    # CPython exposes those copied keys in uppercase; preserve the CMake name.
    qt = os.environ.get('Qt6_DIR') or os.environ.get('QT6_DIR')
    if not qt:
        raise RuntimeError('Runner Qt6_DIR is missing; prepare Qt outside the workflow')
    env['Qt6_DIR'] = qt
    return env


def comparison_verdict(code, details, expected):
    summary = details.get('summary', {})
    return {'returncode': code or int(details.get('status') != 'passed' or
                summary.get('cases') != expected or summary.get('failed', 1) != 0 or
                summary.get('strictFailed', 1) != summary.get('approvedMetadataDifferences', 0)),
            'summary': summary, 'expected_cases': expected, 'setup_error': details.get('setupError'),
            'reference': 'current compiled-platform G-code baseline'}


def architecture(source, arch, output, report, env):
    preset, directory = {'x64': ('release', 'build-Release'),
        'arm64': ('arm_release', 'build-arm64-Release'),
        'x86_64': ('x86_release', 'build-x86_64-Release')}[arch]
    build = source / directory
    output.mkdir()
    arguments = ['-DGPLATFORM_BUILD_TESTS=ON', '-DGPLATFORM_BUILD_SDK_CHECKS=ON',
                 '-DJOURNALSYSTEM_BUILD_TESTS=ON', '-DGPLATFORM_ENABLE_ORCA_NATIVE_REGRESSION=OFF',
                 '-DQt6_DIR=' + env['Qt6_DIR'],
                 '-DPython3_EXECUTABLE=' + sys.executable]
    if os.name == 'nt':
        invocation = ("$ErrorActionPreference = 'Stop'; try { $global:LASTEXITCODE = 0; "
                      "& $env:CI_BUILD_ENTRY -SourceDir $env:CI_SOURCE_DIR -Preset release -Targets all "
                      "-CMakeArgs @($env:CI_CMAKE_ARGS | ConvertFrom-Json); exit $LASTEXITCODE "
                      "} catch { [Console]::Error.WriteLine($_); exit 1 }")
        build_env = dict(env, CI_BUILD_ENTRY=str(Path(__file__).resolve().parents[1] / 'build.ps1'), CI_SOURCE_DIR=str(source), CI_CMAKE_ARGS=json.dumps(arguments))
        build_args = ['pwsh', '-NoProfile', '-Command', invocation]
    else:
        build_env = env
        build_args = ['bash', Path(__file__).resolve().parents[1] / 'build.sh', '--source-dir', source, '--preset', preset, '--configure-only', '--', *arguments]
    def build_tests():
        code = command(build_args, source, build_env, output / 'configure-build.log')
        if code == 0 and os.name != 'nt':
            code = command(['cmake', '--build', '--preset', preset, '--target', 'all'], source, env, output / 'build.log')
        return {'returncode': code}
    built = phase(report, arch + '/build-tests', build_tests)
    test_env = runtime(source, build, env)
    if built:
        def ctest():
            inventory = subprocess.check_output(['ctest', '--test-dir', str(build), '--show-only=json-v1'], env=test_env)
            (output / 'ctest-inventory.json').write_bytes(inventory)
            tests = json.loads(inventory)['tests']
            if not tests:
                raise RuntimeError('CTest registered no tests')
            return {'returncode': command(['ctest', '--test-dir', build, '--output-on-failure',
                '--no-tests=error', '--timeout', '180', '--parallel', '1', '--output-junit', output / 'ctest.xml'],
                source, test_env, output / 'ctest.log'), 'registered_tests': len(tests)}
        phase(report, arch + '/ctest', ctest)
    else:
        phase(report, arch + '/ctest', lambda: (_ for _ in ()).throw(RuntimeError('Test build failed; no stale executable used')))

    # Independent native slicer tests still run if product-test compilation failed.
    native = build / 'ci-orca-native'
    commit = text_git(source / 'App/ThirdParty/OrcaSlicer', 'rev-parse', 'HEAD')
    args = [sys.executable, Path(__file__).with_name('native_child.py'), 'native', '--source', source,
            '--work-dir', native,
            '--source-dir', source / 'App/ThirdParty/OrcaSlicer', '--source-commit', commit,
            '--jobs', env.get('CMAKE_BUILD_PARALLEL_LEVEL', str(os.cpu_count() or 1)), '--command-timeout', '5400']
    if os.name != 'nt':
        args += ['--cmake-arg=-DCMAKE_OSX_ARCHITECTURES=' + arch, '--cmake-arg=-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0']
    native_ok = phase(report, arch + '/native-orca-build', lambda: {'returncode': command(args, source, env, output / 'native-build.log')})
    if (native / 'logs').is_dir():
        shutil.copytree(native / 'logs', output / 'native-logs')
    if (native / 'provenance.json').is_file():
        shutil.copy2(native / 'provenance.json', output / 'native-provenance.json')
    if native_ok:
        provenance = json.loads((native / 'provenance.json').read_text(encoding='utf-8'))
        assets = source / 'App/tests/TestAssets/slicing_regression'
        profile_path = assets / 'candidate-config.json'
        profile = json.loads(profile_path.read_text(encoding='utf-8'))
        # Only bind the tested source. Counts, approved extension values and
        # expected defaults hash stay frozen; incompatible current defaults fail.
        profile['candidateSourceCommit'] = commit
        write_json(output / 'candidate-profile.json', profile)
        for suite, catalog, baseline in [('original48', 'cases.json', 'standard'),
                                          ('extended26', 'cases-extended.json', 'extended')]:
            def compare(suite=suite, catalog=catalog, baseline=baseline):
                destination = output / suite
                # describe() inherits its parent's environment; the existing
                # comparator only supplies its probe_env to later slice calls.
                compare_env = windows_native_environment(provenance, env) if os.name == 'nt' else env
                code = command([sys.executable, source / 'scripts/compare_orca_candidate.py',
                    '--cases', assets / catalog,
                    '--baseline-dir', assets / 'baselines' / baseline,
                    '--extensions', output / 'candidate-profile.json', '--probe', provenance['probe'],
                    '--source-metadata', native / 'provenance.json', '--resource-dir', source / 'App/ThirdParty/OrcaSlicer/resources',
                    '--output', destination, '--repeats', '2', '--timeout', '120'], source, compare_env, output / (suite + '.log'))
                details = json.loads((destination / 'report.json').read_text(encoding='utf-8'))
                expected = len(json.loads((assets / catalog).read_text(encoding='utf-8'))['cases'])
                return comparison_verdict(code, details, expected)
            phase(report, arch + '/' + suite, compare)
    return {'returncode': int(any(p['status'] != 'passed' for p in report['phases']
        if p['name'].startswith(arch + '/') and p['name'] != arch + '/all-stages'))}


def windows_environment(*, script=None):
    if os.name != 'nt' or os.environ.get('VSCMD_ARG_TGT_ARCH') == 'x64':
        return None
    # Use already-installed VS, without installing tools or changing the service.
    vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
    root = os.environ.get('GPLATFORM_VS_ROOT') or subprocess.check_output([str(vswhere), '-latest', '-products', '*',
        '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip()
    command_text = '''$ErrorActionPreference = 'Stop'
$dev = Join-Path $env:CI_VS_ROOT 'Common7/Tools/VsDevCmd.bat'
if ($dev -match '[%"\\r\\n]') { throw 'Unsupported VS path' }
$lines = & $env:ComSpec /d /c "`"$dev`" -arch=x64 -host_arch=x64 >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'VS environment failed' }
foreach ($line in $lines) { if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') } }
& $env:CI_PYTHON $env:CI_REGRESSION_SCRIPT @($env:CI_REGRESSION_ARGS | ConvertFrom-Json)
exit $LASTEXITCODE'''
    env = dict(os.environ, CI_VS_ROOT=root, CI_PYTHON=sys.executable,
               CI_REGRESSION_SCRIPT=str(Path(script or __file__).resolve()), CI_REGRESSION_ARGS=json.dumps(sys.argv[1:]))
    return wait_windows_tree(subprocess.Popen(['pwsh', '-NoProfile', '-Command', command_text], env=env))


def wait_windows_tree(process):
    try:
        return process.wait()
    finally:
        if process.poll() is None:
            subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'], capture_output=True)
            process.wait(timeout=15)
