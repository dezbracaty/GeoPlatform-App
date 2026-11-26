"""Build a fixed full host and run App's maintained platform Journal regression."""
from __future__ import annotations
import argparse
import json
from collections import Counter
import os
from pathlib import Path
import sys

from common import (TEST_ASSETS_PATH, asset_path, build_log, check_runner_test_assets, digest,
                    freeze_candidate, freeze_published_sdk,
                    git_env, init_host_submodules, init_source_submodules, machine_lock, materialize_sdk,
                    prepare_cached_source, require_sha, resolve_ref, run, selected_platforms,
                    test_assets_checkout, test_assets_repository, text_git, write_json)
from regression_reports import compact_regression_report
import native_regression as native_checks

ASSET_BRANCHES = {'macos': 'macmini', 'windows': 'windows'}


def prepare(api, app_repository, app_ref, host_repository, host_ref, *, platform='all', app_branch='',
            macos_sdk_ref='', windows_sdk_ref='', macos_test_assets_ref='macmini', windows_test_assets_ref='windows'):
    """Freeze the selected App first and resolve only the requested platform dependencies."""
    selected = selected_platforms(platform)
    result = freeze_candidate(api, app_repository, app_ref, host_repository, host_ref)
    repository = result['assets_repository'] = test_assets_repository()
    refs = {'macos': macos_test_assets_ref, 'windows': windows_test_assets_ref}
    for name in selected:
        result[name + '_assets_sha'] = resolve_ref(api, repository, refs[name])
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
assets = Path(payload['assets_path'])
sys.path.insert(0, payload['ci_dir'])
if payload['platform'] == 'macos':
    from mac_regression import regression
    result = regression(app, Path(payload['executable']).parents[2], Path(payload['output']),
                        dict(os.environ), assets=assets, assets_sha=payload['assets_sha'])
else:
    from windows_regression import regression
    result = regression(app, Path(payload['executable']), Path(payload['output']),
                        dict(os.environ), assets=assets, assets_sha=payload['assets_sha'])
sys.path.insert(0, str(app / 'tools/journal'))
from session_repository import discover_scenarios
result['expected_cases'] = [path.name for path in discover_scenarios(assets / 'scenarios')]
Path(payload['receipt']).write_text(json.dumps(result), encoding='utf-8')
'''


def regress(source, platform, source_sha, assets_sha, output, *, app, app_sha, app_repository,
            sdk_root, sdk_sha, sdk_repository, assets_ref=None, assets_repository=None):
    source, app, output = Path(source).resolve(), Path(app).resolve(), Path(output).resolve()
    require_sha(source_sha); require_sha(assets_sha); require_sha(app_sha); require_sha(sdk_sha)
    if platform not in ASSET_BRANCHES:
        raise ValueError('Unknown source regression platform')
    output.mkdir(parents=True, exist_ok=False)
    report = dict(status='failed', source_sha=source_sha, app_sha=app_sha, app_repository=app_repository, platform=platform,
                  assets_repository=assets_repository, assets_ref=assets_ref or ASSET_BRANCHES[platform],
                  assets_sha=assets_sha, assets_path=str(source / 'App' / TEST_ASSETS_PATH),
                  sdk_source='published', sdk_repository=sdk_repository, phases=[], output=str(output))
    try:
        repository = test_assets_repository()
        report['assets_repository'] = assets_repository or repository
        if report['assets_repository'] != repository:
            raise RuntimeError('Prepared TestAssets repository differs from the workflow declaration')
        # Automatic screenshot verification requires Pillow.
        run([sys.executable, '-c', 'import PIL'], env=dict(os.environ))
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
            transfer_env = git_env()
            run(['git', '-C', source, 'lfs', 'pull', '--include', 'Core/tests/**', '--exclude', ''],
                env=transfer_env, attempts=3)
            # CTest uses the App pinned by the host. Journal reads the assets
            # in that same checkout while using the standalone App's tools.
            host_app = source / 'App'
            assets, actual = test_assets_checkout(host_app, f'-DGPLATFORM_TEST_ASSETS_SHA={assets_sha}', env=transfer_env)
            if actual != assets_sha or assets != asset_path(host_app):
                raise RuntimeError('Native TestAssets checkout differs from the prepared path or SHA')
            report.update(native_assets_path=str(assets), native_assets_sha=actual)
            env = native_checks.build_environment()
            for arch in (['x64'] if platform == 'windows' else ['arm64', 'x86_64']):
                check = native_checks.architecture(source, arch, output / arch, report, env, assets_sha=actual)
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
            if asset_path(host_app) != assets:
                raise RuntimeError('Journal TestAssets path differs from the native checkout')
            journal_assets_sha = check_runner_test_assets(host_app, assets, env=transfer_env)
            if journal_assets_sha != assets_sha:
                raise RuntimeError('Journal TestAssets checkout differs from the prepared SHA')
            report.update(journal_assets_path=str(assets), journal_assets_sha=journal_assets_sha)
            payload = dict(app=str(app), ci_dir=ci_dir, platform=platform, executable=str(executable),
                           output=str(output / 'journals'), assets_sha=journal_assets_sha, assets_path=str(assets),
                           receipt=str(output / 'journal-receipt.json'))
            run([sys.executable, '-c', APP_REGRESSION, json.dumps(payload)],
                cwd=source, env=dict(os.environ))
            receipt = json.loads((output / 'journal-receipt.json').read_text(encoding='utf-8'))
            if (receipt.get('status') != 'passed' or receipt.get('assets_sha') != journal_assets_sha
                    or receipt.get('assets_path') != str(assets)):
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
    parser.add_argument('--assets-repository', required=True)
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
            sdk_sha=args.sdk_sha, sdk_repository=args.sdk_repository, assets_ref=args.assets_ref,
            assets_repository=args.assets_repository)


if __name__ == '__main__':
    sys.exit(main())
