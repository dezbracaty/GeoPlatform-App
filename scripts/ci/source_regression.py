"""Build a fixed full host and run App's maintained platform Journal regression."""
from __future__ import annotations
import argparse
import json
from collections import Counter
import os
from pathlib import Path
import shutil
import sys
import tempfile

from common import (repository_url, digest, git_env, init_host_submodules, machine_lock,
                    prepare_cached_source, require_sha, run, text_git, write_json)
from common import freeze_candidate, app_dependency_repository, resolve_ref
from common import remove_owned_tree
from update_sdk import build_sdk_package, update_sdk
import native_regression as native_checks

ASSET_BRANCHES = {'macos': 'macmini', 'windows': 'windows'}


def prepare(api, app_repository, app_ref, host_repository, host_ref, *, platform='all', sdk_source='source',
            macos_sdk_ref='', windows_sdk_ref='', macos_test_assets_ref='macmini', windows_test_assets_ref='windows'):
    """Freeze the selected App first and resolve only the requested platform dependencies."""
    selected = ['windows', 'macos'] if platform == 'all' else [platform]
    if any(p not in ASSET_BRANCHES for p in selected) or sdk_source not in ('source', 'published'):
        raise ValueError('Invalid regression platform or SDK source')
    if sdk_source == 'source' and (macos_sdk_ref or windows_sdk_ref):
        raise ValueError('SDK refs are forbidden in source mode')
    result = freeze_candidate(api, app_repository, app_ref, host_repository, host_ref)
    assets_repo = app_dependency_repository(api, app_repository, result['app_sha'], 'tests/TestAssets')
    refs = {'macos': macos_test_assets_ref, 'windows': windows_test_assets_ref}
    for name in selected:
        result[name + '_assets_sha'] = resolve_ref(api, assets_repo, refs[name])
    if sdk_source == 'published':
        for name in selected:
            ref = {'macos': macos_sdk_ref, 'windows': windows_sdk_ref}[name] or name
            result[name + '_sdk_sha'] = resolve_ref(api, result['sdk_repository'], ref)
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
prepare_assets(app, sys.argv[2] or None)
'''

APP_CONSUMER = '''
import json, os, sys
from pathlib import Path
p = json.loads(sys.argv[1])
app = Path(p['app'])
sys.path.insert(0, p['ci_dir'])
from build import build_consumer
result = build_consumer(app, p['platform'], Path(p['sdk_root']), dict(os.environ),
                        producer_root_sha=p.get('producer_root_sha'), producer_app_sha=p.get('producer_app_sha'))
Path(p['receipt']).write_text(json.dumps(result), encoding='utf8')
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


def regress(source, platform, source_sha, assets_sha, output, *, app_sha, app_repository, sdk_source='source', sdk_root=None, sdk_sha=None, sdk_repository=None, assets_ref=None):
    source, output = Path(source).resolve(), Path(output).resolve()
    require_sha(source_sha); require_sha(assets_sha); require_sha(app_sha)
    if platform not in ASSET_BRANCHES:
        raise ValueError('Unknown source regression platform')
    if sdk_source not in ('source', 'published'):
        raise ValueError('Invalid SDK source')
    if sdk_source == 'published' and (sdk_root is None or not sdk_sha):
        raise ValueError('Published mode requires a frozen SDK checkout and SHA')
    if sdk_source == 'source' and (sdk_root is not None or sdk_sha):
        raise ValueError('Source mode cannot consume a published SDK')
    output.mkdir(parents=True, exist_ok=False)
    report = dict(status='failed', source_sha=source_sha, app_sha=app_sha, app_repository=app_repository, platform=platform,
                  assets_ref=assets_ref or ASSET_BRANCHES[platform], assets_sha=assets_sha, sdk_source=sdk_source, phases=[], output=str(output))
    package = None
    try:
        # Automatic screenshot verification requires Pillow. Optional review
        # hints and schema tooling must not block the formal Journal gate.
        run([sys.executable, '-c', 'import PIL, yaml'], env=dict(os.environ))
        with machine_lock():
            prepare_cached_source(source, source_sha, platform, host=True)
            init_host_submodules(source, app_sha, app_repository)
            run(['git', '-C', source, 'lfs', 'pull', '--include', 'Core/tests/**', '--exclude', ''], env=git_env(), attempts=3)
            app = source / 'App'
            ci_root = Path(__file__).resolve().parents[2]
            ci_dir = str(ci_root / 'scripts/ci')
            if text_git(app, 'rev-parse', 'HEAD') != app_sha:
                raise RuntimeError('Actual App differs from the fixed candidate')
            report['declared_app_sha'] = text_git(source, 'rev-parse', source_sha + ':App')
            report['control_sha'] = text_git(Path(__file__).resolve().parents[2], 'rev-parse', 'HEAD')
            # Native tests and GUI regression consume the same frozen platform
            # assets. Restore the source pin before SDK production/consumption
            # so platform baseline selection cannot dirty producer provenance.
            run([sys.executable, '-c', APP_NATIVE_ASSETS, str(app), assets_sha, ci_dir], cwd=source, env=dict(os.environ))
            report['native_assets_sha'] = assets_sha
            # Checks belong to App; these are controller contract tests, not a
            # second business-regression runner or a replacement for Journal.
            run([sys.executable, '-m', 'unittest', 'discover', '-s', ci_root / 'tests/ci', '-v'],
                cwd=ci_root, env=dict(os.environ))
            env = native_checks.build_environment()
            for arch in (['x64'] if platform == 'windows' else ['arm64', 'x86_64']):
                check = native_checks.architecture(source, arch, output / arch, report, env)
                if check['returncode']:
                    raise RuntimeError('Required native regression checks failed: ' + arch)
            run([sys.executable, '-c', APP_NATIVE_ASSETS, str(app), '', ci_dir], cwd=source, env=dict(os.environ))
            if sdk_source == 'source':
                # The exporter stages beside this directory. A nested report
                # path can exceed Windows MAX_PATH even for valid source files.
                package = Path(tempfile.mkdtemp(prefix='sdk-package-', dir=os.environ.get('RUNNER_TEMP'))).resolve()
                report['sdk_package'] = dict(path=str(package), retained=True)
                build_sdk_package(source, platform, package, env)
                receipt = update_sdk(source, source_sha, platform, package,
                                     repository_url(sdk_repository), app_sha=app_sha, publish=False)
                report['sdk'] = dict(receipt, package_manifest_sha256=digest(package / 'package-manifest.json'))
                metadata = ['package-manifest.json'] + [f'SDK/{slot}/{name}'
                    for slot in receipt['selected_slots'] for name in ('sdk-build.json', 'sdk-manifest.json')]
                report['sdk_package']['metadata'] = {}
                for name in metadata:
                    destination = output / 'sdk-package' / name
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(package / name, destination)
                    report['sdk_package']['metadata'][name] = digest(destination)
                selected_sdk = package / 'SDK'
            else:
                require_sha(sdk_sha)
                selected_sdk = Path(sdk_root).resolve()
                # App's helper checks exact Git identity and LFS before the consumer.
                run([sys.executable, '-c', "import sys;sys.path.insert(0,sys.argv[1]);from common import materialize_sdk;materialize_sdk(sys.argv[2],sys.argv[3],sys.argv[4])",
                     ci_dir, selected_sdk, sdk_sha, platform], env=dict(os.environ))
                report['sdk'] = dict(sdk_sha=sdk_sha, sdk_root=str(selected_sdk))
            consumer_payload = dict(app=str(app), ci_dir=ci_dir, platform=platform, sdk_root=str(selected_sdk),
                producer_root_sha=source_sha, producer_app_sha=report['app_sha'],
                receipt=str(output / 'consumer-receipt.json'))
            run([sys.executable, '-c', APP_CONSUMER, json.dumps(consumer_payload)], cwd=source, env=dict(os.environ))
            consumer = json.loads((output / 'consumer-receipt.json').read_text(encoding='utf8'))
            report['consumer'] = consumer
            executable = Path(consumer['executable'])
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
            if package is not None:
                try:
                    remove_owned_tree(package, package.parent)
                except Exception as error:
                    report['cleanupError'] = f'{type(error).__name__}: {error}'
                    raise
                report['sdk_package']['retained'] = False
            report['status'] = 'passed'
    except Exception as error:
        report['status'] = 'failed'
        report['error'] = f'{type(error).__name__}: {error}'
        raise
    finally:
        write_json(output / 'source-regression.json', report)
        write_json(output / 'summary.json', report)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', type=Path, required=True)
    parser.add_argument('--source-sha', required=True)
    parser.add_argument('--app-sha', required=True)
    parser.add_argument('--app-repository', required=True)
    parser.add_argument('--assets-sha', required=True)
    parser.add_argument('--assets-ref')
    parser.add_argument('--platform', choices=ASSET_BRANCHES, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--sdk-source', choices=['source', 'published'], default='source')
    parser.add_argument('--sdk-root', type=Path)
    parser.add_argument('--sdk-sha')
    parser.add_argument('--sdk-repository', required=True)
    args = parser.parse_args()
    delegated = native_checks.windows_environment(script=__file__)
    if delegated is not None:
        return delegated
    regress(args.source_dir, args.platform, args.source_sha, args.assets_sha, args.output,
            app_sha=args.app_sha, app_repository=args.app_repository, sdk_source=args.sdk_source, sdk_root=args.sdk_root, sdk_sha=args.sdk_sha, sdk_repository=args.sdk_repository, assets_ref=args.assets_ref)


if __name__ == '__main__':
    sys.exit(main())
