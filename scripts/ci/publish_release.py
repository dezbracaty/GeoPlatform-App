"""Publish the current attempt's checked installers as an App prerelease."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import tempfile

from common import digest, run


def collect_assets(root, staging, app_sha, sdk_sha, run_journal):
    assets = []
    regression = ''
    for platform, extension, architecture in [('windows', '.exe', 'x64'), ('macos', '.dmg', 'universal')]:
        directory = root / platform
        metadata_path = directory / 'build-result.json'
        metadata = json.loads(metadata_path.read_text(encoding='utf-8'))
        if any(metadata.get(key) != value for key, value in {
                'app_sha': app_sha, 'sdk_sha': sdk_sha, 'platform': platform,
                'architecture': architecture, 'sdk_consumer_check': 'passed'}.items()):
            raise RuntimeError(f'{platform} build does not match the successful fixed inputs')
        installer = metadata.get('installer', {}) if platform == 'macos' else metadata
        if platform == 'macos':
            regression = metadata.get('regression', {}).get('status')
            if installer.get('status') != 'passed' or regression != ('passed' if run_journal else 'skipped'):
                raise RuntimeError('macOS installer or enabled Journal regression did not pass')
        name = installer.get('package', '')
        if (not name or Path(name).name != name or '\\' in name or '\n' in name
                or '#' in name or Path(name).suffix.lower() != extension):
            raise RuntimeError(f'{platform} installer has an invalid package name')
        package = directory / name
        checksum = directory / (name + '.sha256')
        for path in [metadata_path, package, checksum]:
            if path.resolve().parent != directory.resolve() or not path.is_file() or not path.stat().st_size:
                raise RuntimeError(f'{platform} installer evidence is missing or outside its artifact')
        expected = installer.get('sha256', '')
        if (not re.fullmatch('[0-9a-f]{64}', expected) or digest(package) != expected
                or checksum.read_text(encoding='utf-8').strip() != f'{expected}  {name}'):
            raise RuntimeError(f'{platform} installer checksum does not match its evidence')
        report = staging / f'build-result-{platform}.json'
        shutil.copyfile(metadata_path, report)
        assets.extend([package, checksum, report])
    if len({path.name for path in assets}) != len(assets):
        raise RuntimeError('Release asset names must be unique across platforms')
    return assets, regression


def publish(root, *, repository, app_sha, sdk_sha, workflow_sha, run_id, attempt,
            run_journal, server_url='https://github.com'):
    if (not all(re.fullmatch('[0-9a-f]{40}', sha) for sha in [app_sha, sdk_sha, workflow_sha])
            or not re.fullmatch('[1-9][0-9]*', run_id) or not re.fullmatch('[1-9][0-9]*', attempt)):
        raise ValueError('Publishing requires fixed source SHAs and the current workflow identity')
    tag = f'build-{run_id}-{attempt}'
    workflow_url = f'{server_url}/{repository}/actions/runs/{run_id}/attempts/{attempt}'
    with tempfile.TemporaryDirectory(prefix='app-release-') as temporary:
        staging = Path(temporary)
        assets, regression = collect_assets(root, staging, app_sha, sdk_sha, run_journal)
        expected = {path.name: (path.stat().st_size, 'sha256:' + digest(path)) for path in assets}
        notes = staging / 'notes.md'
        notes.write_text(f"""Development prerelease; manual acceptance is still required.

App SHA: `{app_sha}`
SDK SHA: `{sdk_sha}`
Workflow SHA: `{workflow_sha}`
macOS Journal regression: **{regression}**

[Build and check results]({workflow_url})

After acceptance, this same release can be marked as a full release without rebuilding its installers.
""", encoding='utf-8')
        def gh(*args, capture=False):
            return run(['gh', *args, '--repo', repository], capture=capture)
        gh('release', 'create', tag, '--target', app_sha, '--draft', '--prerelease', '--latest=false',
           '--title', f'Development build {app_sha[:8]} ({run_id}.{attempt})', '--notes-file', notes)
        gh('release', 'upload', tag, *assets)
        response = json.loads(run(['gh', 'api', f'repos/{repository}/releases/tags/{tag}'], capture=True))
        uploaded = {asset['name']: (asset['size'], asset.get('digest')) for asset in response['assets']
                    if asset.get('state') == 'uploaded'}
        if (response.get('target_commitish') != app_sha or response.get('tag_name') != tag
                or not response.get('draft') or not response.get('prerelease') or uploaded != expected):
            raise RuntimeError('Release draft or uploaded assets do not match the checked installers; draft retained')
        gh('release', 'edit', tag, '--draft=false', '--prerelease', '--latest=false')
        return response['html_url']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--artifacts', type=Path, required=True)
    args = parser.parse_args()
    url = publish(args.artifacts, repository=os.environ['GITHUB_REPOSITORY'],
                  app_sha=os.environ['APP_SHA'], sdk_sha=os.environ['SDK_SHA'],
                  workflow_sha=os.environ['GITHUB_WORKFLOW_SHA'], run_id=os.environ['GITHUB_RUN_ID'],
                  attempt=os.environ['GITHUB_RUN_ATTEMPT'], run_journal=os.environ['RUN_JOURNAL'] == 'true',
                  server_url=os.environ.get('GITHUB_SERVER_URL', 'https://github.com'))
    with open(os.environ['GITHUB_STEP_SUMMARY'], 'a', encoding='utf-8') as stream:
        stream.write(f'## App prerelease\n\n[Download installers]({url})\n')
    print(url)


if __name__ == '__main__':
    main()
