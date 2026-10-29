"""Publish a completed App installer to this App repository's immutable Release."""
import argparse
import os
from pathlib import Path
import re
import urllib.parse
import urllib.request

from app_build import completed, digest
from common import git, git_env, read_json, run, write_json
from github_api import GitHub


def publish(directory, token):
    directory = Path(directory).resolve()
    manifest = read_json(directory / 'build-manifest.json')
    manifest = completed(directory, manifest['platform'])
    if manifest['delivery'] != 'release' or not token:
        raise RuntimeError('This run must request Release delivery with an App write token')
    repository = manifest['repository']
    if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repository):
        raise RuntimeError('Unsafe App repository name')
    if digest(directory / 'source.bundle') != manifest['source_bundle_sha256']:
        raise RuntimeError('Saved App source bundle has changed')
    work = directory / 'publish/source'
    env = git_env(token)
    if not work.exists():
        work.parent.mkdir(parents=True, exist_ok=True)
        run(['git', 'clone', directory / 'source.bundle', work], env=env)
    commit = manifest['source']['commit']
    if git(work, 'rev-parse', 'HEAD') != commit:
        raise RuntimeError('Saved release source differs from the completed build')
    git(work, 'remote', 'set-url', 'origin', f'https://github.com/{repository}.git')
    tag = 'v' + manifest['version'] + ('-macos-universal' if manifest['platform'] == 'macos' else '')
    api = GitHub(repository, token)
    marker = 'GeoPlatform-App CI run: ' + manifest['run_id']
    release = api.request('GET', 'releases/tags/' + urllib.parse.quote(tag, safe=''), missing=True)
    if release and marker not in release.get('body', ''):
        raise RuntimeError('The version Release belongs to another run')
    state_path = directory / 'publication.json'
    state = read_json(state_path) if state_path.exists() else {}
    if release and not release['draft']:
        if state.get('assets_ready') and state.get('release_id') == release['id']:
            return
        raise RuntimeError('An existing public App Release cannot be replaced')
    existing = git(work, 'ls-remote', 'origin', 'refs/tags/' + tag, env=env).split()
    if existing and existing[0] != commit:
        raise RuntimeError('The release tag points to another App commit')
    if not existing:
        git(work, 'push', 'origin', f'{commit}:refs/tags/{tag}', env=env)
    if not release:
        release = api.request('POST', 'releases', {'tag_name': tag, 'name': tag, 'draft': True,
                                                 'body': marker + '\n\nApp: ' + commit + '\nSDK: ' + manifest['sdk_commit']})
    state.update(release_id=release['id'])
    write_json(state_path, state)
    assets = [directory / 'deliverables' / name for name in sorted(manifest['artifacts'])]
    checksums = directory / 'checksums.sha256'
    checksums.write_text(''.join(f'{digest(path)}  {path.name}\n' for path in assets), encoding='utf-8')
    assets.append(checksums)
    remote = api.request('GET', f"releases/{release['id']}/assets?per_page=100")
    for path in assets:
        expected = 'sha256:' + digest(path)
        found = next((item for item in remote if item['name'] == path.name), None)
        if found:
            if found.get('digest') != expected or found.get('size') != path.stat().st_size:
                raise RuntimeError(f'Existing release asset differs: {path.name}')
            continue
        url = f"https://uploads.github.com/repos/{repository}/releases/{release['id']}/assets?name=" + urllib.parse.quote(path.name)
        request = urllib.request.Request(url, data=path.read_bytes(), method='POST',
            headers={'Authorization': 'Bearer ' + token, 'Content-Type': 'application/octet-stream',
                     'Accept': 'application/vnd.github+json', 'User-Agent': 'GeoPlatform-App-CI'})
        try:
            import json
            with urllib.request.urlopen(request, timeout=180) as response:
                uploaded = json.load(response)
        except OSError:
            raise RuntimeError('App release asset transfer interrupted; use resume_run to recover') from None
        if uploaded.get('digest') != expected or uploaded.get('size') != path.stat().st_size:
            raise RuntimeError(f'Release integrity check failed: {path.name}')
    state['assets_ready'] = True
    write_json(state_path, state)
    release = api.request('PATCH', f"releases/{release['id']}", {'draft': False,
        'make_latest': 'false' if manifest['platform'] == 'macos' else 'true'})
    state.update(released=True, release_url=release['html_url'])
    write_json(state_path, state)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run-dir', type=Path, required=True)
    args = parser.parse_args()
    publish(args.run_dir, os.environ.get('GITHUB_TOKEN'))
