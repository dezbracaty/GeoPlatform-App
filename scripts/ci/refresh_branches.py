"""Refresh both App workflow branch menus from their actual GitHub repositories."""
import argparse
import base64
import json
import os
from pathlib import Path
import re
import tempfile
import urllib.parse

from common import git, git_env
from github_api import GitHub

PIN = 'Use repository pin'
MODULES = {'sdk_ref': 'ThirdParty/libs/GPlatformSDK', 'orca_ref': 'ThirdParty/OrcaSlicer',
           'smoothui_ref': 'ThirdParty/SmoothUI', 'dock_ref': 'ThirdParty/SmoothUIDock'}
WORKFLOWS = ('.github/workflows/app-windows.yml', '.github/workflows/app-macos.yml')


def repository_name(url):
    if url.startswith('git@github.com:'):
        name = url.split(':', 1)[1]
    else:
        parsed = urllib.parse.urlsplit(url)
        if parsed.hostname != 'github.com':
            raise RuntimeError('Branch menus require GitHub module URLs')
        name = parsed.path.strip('/')
    name = name.removesuffix('.git')
    if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', name):
        raise RuntimeError('Repository must be an owner/name pair')
    return name


def branches(client):
    values = []
    page = 1
    while True:
        result = client.request('GET', f'branches?per_page=100&page={page}')
        values.extend(item['name'] for item in result)
        if len(result) < 100:
            if not values:
                raise RuntimeError(f'No branches found in {client.repository}')
            return sorted(set(values))
        page += 1


def replace_choices(document, name, options, default):
    if default not in options:
        raise RuntimeError(f'Default branch is missing for {name}')
    pattern = re.compile(rf'(?ms)^      {re.escape(name)}:\n.*?(?=^      [a-z_]+:|^permissions:|\Z)')
    match = pattern.search(document)
    if not match:
        raise RuntimeError(f'Missing workflow input {name}')
    block = match.group()
    marker = re.compile(rf'(?ms)^          # branches:{name}:begin\n.*?^          # branches:{name}:end')
    rendered = f'          # branches:{name}:begin\n'
    rendered += ''.join(f'          - {json.dumps(value, ensure_ascii=False)}\n' for value in options)
    rendered += f'          # branches:{name}:end'
    block, count = marker.subn(lambda _: rendered, block)
    if count != 1:
        raise RuntimeError(f'Expected one branch marker for {name}')
    block, count = re.subn(r'^        default: .+$',
                          lambda _: '        default: ' + json.dumps(default, ensure_ascii=False),
                          block, count=1, flags=re.MULTILINE)
    if count != 1:
        raise RuntimeError(f'Missing default for {name}')
    return document[:match.start()] + block + document[match.end():]


def collect(repo, repository, token=None):
    client = GitHub(repository, token)
    default = client.request('GET', '')['default_branch']
    values = {'source_ref': branches(client)}
    if default not in values['source_ref']:
        raise RuntimeError('App default branch was not returned by GitHub')
    metadata = client.request('GET', 'contents/.gitmodules?ref=' + urllib.parse.quote(default, safe=''))
    with tempfile.TemporaryDirectory(prefix='app-branches-') as temporary:
        modules = Path(temporary) / '.gitmodules'
        modules.write_bytes(base64.b64decode(metadata['content']))
        paths = git(repo, 'config', '-f', modules, '--get-regexp', r'^submodule\..*\.path$')
        keys = {line.split(maxsplit=1)[1]: line.split(maxsplit=1)[0][:-5] for line in paths.splitlines()}
        for name, path in MODULES.items():
            if path not in keys:
                raise RuntimeError(f'App default branch does not declare {path}')
            url = git(repo, 'config', '-f', modules, '--get', keys[path] + '.url')
            values[name] = [PIN, *branches(GitHub(repository_name(url), token))]
    return default, values


def refresh(repo, repository, token=None):
    repo = Path(repo).resolve()
    default, values = collect(repo, repository, token)
    changes = {}
    # Finish all remote reads and render both workflows before writing either.
    for name in WORKFLOWS:
        path = repo / name
        original = path.read_text(encoding='utf-8-sig')
        document = original
        for field, options in values.items():
            document = replace_choices(document, field, options, default if field == 'source_ref' else PIN)
        if document != original:
            changes[path] = document
    for path, document in changes.items():
        temporary = path.with_suffix('.yml.tmp')
        temporary.write_text(document, encoding='utf-8', newline='\n')
        temporary.replace(path)
    return list(changes), {name: len(options) - (name != 'source_ref') for name, options in values.items()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--repository', default='dezbracaty/GeoPlatform-App')
    parser.add_argument('--push', action='store_true')
    args = parser.parse_args()
    repo = args.repo.resolve()
    write_env = git_env(os.environ.get('WORKFLOW_TOKEN'))
    if args.push and (not os.environ.get('WORKFLOW_TOKEN') or git(repo, 'status', '--porcelain')):
        raise RuntimeError('Menu publishing requires WORKFLOW_TOKEN and a clean dedicated checkout')
    branch = git(repo, 'branch', '--show-current')
    for attempt in range(3):
        changed, counts = refresh(repo, args.repository)
        print(json.dumps({'changed': bool(changed), 'branches': counts}, ensure_ascii=False))
        if not changed or not args.push:
            break
        if not branch:
            raise RuntimeError('Check out the target branch before publishing menus')
        relative = [path.relative_to(repo).as_posix() for path in changed]
        git(repo, 'add', '--', *relative)
        git(repo, '-c', 'user.name=GPlatform CI', '-c', 'user.email=ci@gplatform.invalid',
            'commit', '-m', 'chore(ci): refresh App branch dropdowns', '--', *relative)
        try:
            git(repo, 'push', 'origin', f'HEAD:refs/heads/{branch}', env=write_env)
            break
        except RuntimeError as error:
            if attempt == 2 or not any(word in str(error).lower() for word in ('rejected', 'fetch first', 'non-fast-forward')):
                raise
            # This is an Actions-only checkout, verified clean before the bot's
            # own commit. Re-render on top of a concurrent author's new commit.
            git(repo, 'fetch', 'origin', f'refs/heads/{branch}', env=write_env)
            git(repo, 'reset', '--hard', 'FETCH_HEAD')
    if os.environ.get('GITHUB_STEP_SUMMARY'):
        with open(os.environ['GITHUB_STEP_SUMMARY'], 'a', encoding='utf-8') as stream:
            stream.write('App branch menus checked against GitHub.\n\n')
            for name, count in counts.items():
                stream.write(f'- `{name}`: {count} remote branches\n')
            stream.write('\nBoth platform menus use the same snapshot.\n')


if __name__ == '__main__':
    main()
