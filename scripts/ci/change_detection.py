"""Decide from Git content whether a push needs a build, ignoring generated menus."""
import argparse
import json
import os
from pathlib import Path
import re

from common import git, git_env

WORKFLOWS = frozenset(('.github/workflows/app-macos.yml', '.github/workflows/app-windows.yml',
                       '.github/workflows/macos-build.yml', '.github/workflows/windows-build.yml'))


def normalize_menus(document):
    """Remove only strictly validated choice data, preserving executable YAML."""
    names = re.findall(r'^          # branches:([a-z_]+):begin$', document, re.MULTILINE)
    if not names or len(set(names)) != len(names):
        raise ValueError('Missing or duplicate generated branch menus')
    for name in names:
        field = re.compile(rf'(?ms)^      {name}:\n.*?(?=^      [a-z_]+:|^permissions:|\Z)')
        match = field.search(document)
        if not match:
            raise ValueError('Missing generated input')
        block = match.group()
        marker = re.compile(rf'(?ms)^          # branches:{name}:begin\n(.*?)^          # branches:{name}:end$')
        choice = marker.search(block)
        if not choice:
            raise ValueError('Malformed generated block')
        options = []
        for line in choice[1].splitlines():
            if not line.startswith('          - '):
                raise ValueError('Generated block contains other YAML')
            value = json.loads(line[len('          - '):])
            if not isinstance(value, str):
                raise ValueError('Generated choice must be a JSON string')
            options.append(value)
        default = re.search(r'^        default: (.+)$', block, re.MULTILINE)
        if not default or json.loads(default[1]) not in options:
            raise ValueError('Invalid generated default')
        block = marker.sub(lambda _: f'          # branches:{name}:begin\n          # branches:{name}:end', block)
        block = re.sub(r'^        default: .+$', '        default: "__generated_branch_default__"',
                       block, count=1, flags=re.MULTILINE)
        document = document[:match.start()] + block + document[match.end():]
    return document


def requires_build(repo, before, after):
    if not re.fullmatch(r'[0-9a-f]{40}', before or '') or set(before) == {'0'}:
        return True
    if not re.fullmatch(r'[0-9a-f]{40}', after or ''):
        raise RuntimeError('Push head must be a full commit SHA')
    try:
        changed = git(repo, 'diff', '--name-only', '--no-renames', before, after, '--').splitlines()
        if not changed:
            return False
        for name in changed:
            if name not in WORKFLOWS:
                return True
            old = git(repo, 'show', f'{before}:{name}')
            new = git(repo, 'show', f'{after}:{name}')
            if normalize_menus(old) != normalize_menus(new):
                return True
        return False
    except (RuntimeError, ValueError, json.JSONDecodeError):
        # A missing base, deleted workflow or unrecognized edit must be built.
        return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--before', default=os.environ.get('PUSH_BEFORE', ''))
    parser.add_argument('--after', default=os.environ.get('PUSH_AFTER', ''))
    args = parser.parse_args()
    required = True
    if os.environ.get('GITHUB_EVENT_NAME') == 'push':
        for commit in (args.before, args.after):
            if re.fullmatch(r'[0-9a-f]{40}', commit or '') and set(commit) != {'0'}:
                try:
                    git(args.repo, 'cat-file', '-e', commit + '^{commit}')
                except RuntimeError:
                    try:
                        git(args.repo, 'fetch', '--no-tags', '--depth', '1', 'origin', commit,
                            env=git_env(os.environ.get('GITHUB_TOKEN')))
                    except RuntimeError:
                        pass
        required = requires_build(args.repo, args.before, args.after)
    if os.environ.get('GITHUB_OUTPUT'):
        with open(os.environ['GITHUB_OUTPUT'], 'a', encoding='utf-8') as stream:
            stream.write(f'build_required={str(required).lower()}\n')
    print('Build required.' if required else 'Only generated branch choices changed; build is not required.')


if __name__ == '__main__':
    main()
