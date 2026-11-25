"""Build a fixed full host and run App's maintained platform Journal regression."""
from __future__ import annotations
import argparse
import hashlib
from importlib.metadata import distributions
import json
from collections import Counter
import os
from pathlib import Path
import platform as runtime_platform
import shutil
import sys
import tempfile

from common import (app_dependency_repository, build_log, digest, freeze_candidate, freeze_published_sdk,
                    git, git_env, init_host_submodules, init_source_submodules, machine_lock, materialize_sdk,
                    prepare_cached_source, require_sha, resolve_ref, run, selected_platforms, text_git, write_json)
from regression_reports import compact_regression_report
import native_regression as native_checks

ASSET_BRANCHES = {'macos': 'macmini', 'windows': 'windows'}


def prepare(api, app_repository, app_ref, host_repository, host_ref, *, platform='all', app_branch='',
            macos_sdk_ref='', windows_sdk_ref='', macos_test_assets_ref='macmini', windows_test_assets_ref='windows'):
    """Freeze the selected App first and resolve only the requested platform dependencies."""
    selected = selected_platforms(platform)
    result = freeze_candidate(api, app_repository, app_ref, host_repository, host_ref)
    assets_repo = app_dependency_repository(api, app_repository, result['app_sha'], 'tests/TestAssets')
    refs = {'macos': macos_test_assets_ref, 'windows': windows_test_assets_ref}
    for name in selected:
        result[name + '_assets_sha'] = resolve_ref(api, assets_repo, refs[name])
    result.update(freeze_published_sdk(api, result['sdk_repository'], app_ref, platform=platform,
                                       app_branch=app_branch, windows_sdk_ref=windows_sdk_ref,
                                       macos_sdk_ref=macos_sdk_ref))
    return result


def verify_inventory(summary, expected):
    """A successful process alone cannot satisfy the all-scene regression gate."""
    cases = summary.get('cases', [])
    planned = summary.get('plannedCases', [])
    if (not expected or len(expected) != len(set(expected)) or summary.get('status') != 'passed'
            or summary.get('runComplete') is not True or summary.get('selectionScope') != 'all'
            or summary.get('candidateCollection') or summary.get('cleanupError') or sorted(planned) != sorted(expected)
            or summary.get('completedCaseCount') != len(expected)
            or len(cases) != len(expected)
            or sorted(case['id'] for case in cases) != sorted(expected)
            or any(case.get('status') != 'passed' for case in cases)):
        raise RuntimeError('Source regression did not pass the complete fixed platform inventory')


# The workflow's frozen CI implementation drives the selected product source.
# Separate interpreters isolate module caches; bundled Journal tools still come
# from the product App, preserving its SDK/protocol and scenario contract.
APP_NATIVE_ASSETS = '''
import sys
from pathlib import Path
app = Path(sys.argv[1])
sys.path.insert(0, sys.argv[3])
from mac_regression import prepare_assets
prepare_assets(app, sys.argv[2])
'''

APP_CONSUMER = '''
import json, os, sys
from pathlib import Path
p = json.loads(sys.argv[1])
app = Path(p['app'])
sys.path.insert(0, p['ci_dir'])
from build import build_consumer
from common import write_json
result = {}
try:
    build_consumer(app, p['platform'], Path(p['sdk_root']), dict(os.environ), result)
finally:
    write_json(p['receipt'], result)
'''

APP_REGRESSION = '''
import json, os, sys
from pathlib import Path
payload = json.loads(sys.argv[1])
app = Path(payload['app'])
sys.path.insert(0, payload['ci_dir'])
if payload['platform'] == 'macos':
    from mac_regression import regression
    result = regression(app, Path(payload['executable']).parents[2], Path(payload['output']),
                        dict(os.environ), payload['assets_sha'])
else:
    from windows_regression import regression
    result = regression(app, Path(payload['executable']), Path(payload['output']),
                        dict(os.environ), payload['assets_sha'])
sys.path.insert(0, str(app / 'tools/journal'))
from session_repository import discover_scenarios
result['expected_cases'] = [path.name for path in discover_scenarios(app / 'tests/TestAssets/scenarios')]
Path(payload['receipt']).write_text(json.dumps(result), encoding='utf-8')
'''


def ci_tools_key(ci_root):
    # These are tests of the checked-out workflow implementation. Its asset
    # gitlink is neither initialized nor used by the local test fixtures.
    entries = git(ci_root, 'ls-tree', '-rz', 'HEAD').split(b'\0')
    source = b'\0'.join(entry for entry in entries
                        if not entry.endswith(b'\ttests/TestAssets') or not entry.startswith(b'160000 '))
    tools = {}
    for name in ('bash', 'cmake', 'ctest', 'git', 'git-lfs', 'ninja', 'node', 'pwsh'):
        path = shutil.which(name)
        if path:
            path = Path(path).resolve()
            info = path.stat()
            tools[name] = [str(path), info.st_size, info.st_mtime_ns]
    environment = {name: os.environ.get(name) for name in ('Qt6_DIR', 'QT6_DIR', 'VSCMD_VER',
                   'VCToolsVersion', 'WindowsSDKVersion', 'GIT_CONFIG_GLOBAL', 'GIT_CONFIG_SYSTEM',
                   'GIT_CONFIG_NOSYSTEM')}
    runtime = dict(os=runtime_platform.platform(), python=[sys.executable, sys.version],
                   packages=sorted((p.metadata.get('Name', ''), p.version) for p in distributions()),
                   tools=tools, environment=environment)
    return hashlib.sha256(source + b'\0' + json.dumps(runtime, sort_keys=True).encode()).hexdigest()


def check_ci_tools(ci_root, build, report):
    """Run changed tool contracts after Journal; retain only a successful receipt."""
    cache = build / 'ci-tools-passed.json'
    if cache.is_symlink() or cache.is_junction() or cache.exists() and not cache.is_file():
        raise RuntimeError('CI tools receipt must be a real file')
    if text_git(ci_root, 'status', '--porcelain', '--untracked-files=no', '--ignore-submodules=all'):
        raise RuntimeError('CI implementation checkout has local source changes')
    key = ci_tools_key(ci_root)
    try:
        receipt = json.loads(cache.read_text(encoding='utf-8'))
    except (FileNotFoundError, ValueError):
        receipt = {}
    if (isinstance(receipt, dict) and receipt.get('key') == key and receipt.get('status') == 'passed'
            and isinstance(receipt.get('control_sha'), str) and len(receipt['control_sha']) == 40):
        report['ci_tools'] = dict(receipt, reused=True)
        print(f"Reused passed CI tools checks from {receipt['control_sha']}", flush=True)
        return
    receipt = dict(key=key, status='failed', control_sha=report['control_sha'])
    report['ci_tools'] = dict(receipt, reused=False)
    run([sys.executable, '-m', 'unittest', 'discover', '-s', ci_root / 'tests/ci', '-v'],
        cwd=ci_root, env=dict(os.environ))
    receipt['status'] = 'passed'
    with tempfile.TemporaryDirectory(prefix='.ci-tools-', dir=build) as directory:
        staged = Path(directory) / cache.name
        write_json(staged, receipt)
        staged.replace(cache)
    report['ci_tools'] = dict(receipt, reused=False)


def regress(source, platform, source_sha, assets_sha, output, *, app, app_sha, app_repository,
            sdk_root, sdk_sha, sdk_repository, assets_ref=None):
    source, app, output = Path(source).resolve(), Path(app).resolve(), Path(output).resolve()
    require_sha(source_sha); require_sha(assets_sha); require_sha(app_sha); require_sha(sdk_sha)
    if platform not in ASSET_BRANCHES:
        raise ValueError('Unknown source regression platform')
    output.mkdir(parents=True, exist_ok=False)
    report = dict(status='failed', source_sha=source_sha, app_sha=app_sha, app_repository=app_repository, platform=platform,
                  assets_ref=assets_ref or ASSET_BRANCHES[platform], assets_sha=assets_sha,
                  sdk_source='published', sdk_repository=sdk_repository, phases=[], output=str(output))
    try:
        # Automatic screenshot verification requires Pillow. Optional review
        # hints and schema tooling must not block the formal Journal gate.
        run([sys.executable, '-c', 'import PIL, yaml'], env=dict(os.environ))
        with machine_lock():
            prepare_cached_source(source, source_sha, platform, host=True)
            init_host_submodules(source, host_platform=platform)
            prepare_cached_source(app, app_sha, platform, consumer=True)
            init_source_submodules(app)
            ci_root = Path(__file__).resolve().parents[2]
            ci_dir = str(ci_root / 'scripts/ci')
            if text_git(app, 'rev-parse', 'HEAD') != app_sha:
                raise RuntimeError('Actual App differs from the fixed candidate')
            report['declared_app_sha'] = text_git(source, 'rev-parse', source_sha + ':App')
            report['control_sha'] = text_git(ci_root, 'rev-parse', 'HEAD')
            selected_sdk = materialize_sdk(sdk_root, sdk_sha, platform)
            report['sdk'] = dict(sdk_sha=text_git(selected_sdk, 'rev-parse', 'HEAD'), sdk_root=str(selected_sdk))
            run(['git', '-C', source, 'lfs', 'pull', '--include', 'Core/tests/**', '--exclude', ''], env=git_env(), attempts=3)
            # Native CTest uses the App and forks pinned by the chosen host.
            # Both checkouts use the same frozen platform assets for their tests.
            run([sys.executable, '-c', APP_NATIVE_ASSETS, str(source / 'App'), assets_sha, ci_dir], cwd=source, env=dict(os.environ))
            report['native_assets_sha'] = assets_sha
            env = native_checks.build_environment()
            for arch in (['x64'] if platform == 'windows' else ['arm64', 'x86_64']):
                check = native_checks.architecture(source, arch, output / arch, report, env)
                if check['returncode']:
                    raise RuntimeError('Required native regression checks failed: ' + arch)
            consumer_receipt = output / 'consumer-receipt.json'
            consumer_payload = dict(app=str(app), ci_dir=ci_dir, platform=platform, sdk_root=str(selected_sdk),
                                    receipt=str(consumer_receipt))
            try:
                with build_log(output, 'sdk-consumer') as log:
                    run([sys.executable, '-c', APP_CONSUMER, json.dumps(consumer_payload)],
                        cwd=source, env=dict(os.environ), log=log)
            finally:
                if consumer_receipt.is_file():
                    report['consumer'] = json.loads(consumer_receipt.read_text(encoding='utf8'))
            executable = Path(report['consumer']['executable'])
            if not executable.is_file():
                raise RuntimeError('The SDK consumer did not deploy its expected executable')
            report.update(application=str(executable), application_sha256=digest(executable))
            payload = dict(app=str(app), ci_dir=ci_dir, platform=platform, executable=str(executable),
                           output=str(output / 'journals'), assets_sha=assets_sha,
                           receipt=str(output / 'journal-receipt.json'))
            # App's prepare_assets applies git_env itself. Forward the original
            # job environment so its multi-valued Authorization header is added once.
            run([sys.executable, '-c', APP_REGRESSION, json.dumps(payload)],
                cwd=source, env=dict(os.environ))
            receipt = json.loads((output / 'journal-receipt.json').read_text(encoding='utf-8'))
            if receipt.get('status') != 'passed' or receipt.get('assets_sha') != assets_sha:
                raise RuntimeError('Platform regression receipt differs from the prepared assets')
            summary_path = Path(receipt['summary_path']).resolve()
            if not summary_path.is_relative_to(output / 'journals/run'):
                raise RuntimeError('Journal receipt is outside owned staging')
            summary = json.loads(summary_path.read_text(encoding='utf-8'))
            verify_inventory(summary, receipt['expected_cases'])
            report.update(case_count=len(receipt['expected_cases']),
                initial_counts=dict(Counter((c.get('attempts') or [c])[0].get('status') for c in summary['cases'])),
                final_counts=dict(Counter(c.get('status') for c in summary['cases'])),
                serial_recheck_cases=sum(len(c.get('attempts', [])) > 1 for c in summary['cases']))
            build = app / ('build-Release' if platform == 'windows' else 'build-arm64-Release')
            check_ci_tools(ci_root, build, report)
            report['status'] = 'passed'
    except KeyboardInterrupt:
        report['interrupted'] = True
        raise
    except Exception as error:
        report['status'] = 'failed'
        report['error'] = f'{type(error).__name__}: {error}'
        raise
    finally:
        write_json(output / 'source-regression.json', report)
        write_json(output / 'summary.json', report)
        try:
            compact_regression_report(output)
        except (OSError, RuntimeError, ValueError) as error:
            print(f'Could not finish pruning regression evidence: {error}', file=sys.stderr, flush=True)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', type=Path, required=True)
    parser.add_argument('--app-dir', type=Path, required=True)
    parser.add_argument('--source-sha', required=True)
    parser.add_argument('--app-sha', required=True)
    parser.add_argument('--app-repository', required=True)
    parser.add_argument('--assets-sha', required=True)
    parser.add_argument('--assets-ref')
    parser.add_argument('--platform', choices=ASSET_BRANCHES, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--sdk-root', type=Path, required=True)
    parser.add_argument('--sdk-sha', required=True)
    parser.add_argument('--sdk-repository', required=True)
    args = parser.parse_args()
    delegated = native_checks.windows_environment(script=__file__)
    if delegated is not None:
        return delegated
    regress(args.source_dir, args.platform, args.source_sha, args.assets_sha, args.output,
            app=args.app_dir, app_sha=args.app_sha, app_repository=args.app_repository, sdk_root=args.sdk_root,
            sdk_sha=args.sdk_sha, sdk_repository=args.sdk_repository, assets_ref=args.assets_ref)


if __name__ == '__main__':
    sys.exit(main())
