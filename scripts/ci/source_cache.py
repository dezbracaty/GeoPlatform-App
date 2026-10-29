"""Seed isolated checkouts from local CI repositories; remote gitlinks remain authoritative."""
import argparse
import hashlib
import os
from pathlib import Path

from common import git, git_env, read_json, run


def roots(config):
    values = [Path(config['root'])]
    if config.get('machine_lock'):
        values.append(Path(config['machine_lock']).parent)
    return list(dict.fromkeys(p.resolve() for p in values))


def sources(config):
    found = []
    for root in roots(config):
        found.extend([root / 'runner/_work/GPlatform/GPlatform/source',
                      root / 'runner/_work/GeoPlatform-App/GeoPlatform-App/source'])
        for manifest in sorted((root / 'runs').glob('*/build-manifest.json'),
                               key=lambda p: p.stat().st_mtime, reverse=True):
            data = read_json(manifest)
            if data.get('source_path'):
                found.append(Path(data['source_path']))
            found.extend(manifest.parent / 'publish' / name for name in ('remote-app', 'source'))
    return list(dict.fromkeys(p.resolve() for p in found if p.is_dir()))


def identity(url):
    return url.rstrip('/').removesuffix('.git').casefold()


def seed_submodules(source, paths, config, env):
    # macOS already has warm checkouts; keep its established build path intact.
    if config.get('platform', 'windows') != 'windows' or not config.get('root'):
        return
    candidates = sources(config)
    for relative in paths:
        target = source / relative
        if target.exists() and any(target.iterdir()):
            continue
        entries = git(source, 'config', '-f', '.gitmodules', '--get-regexp',
                      r'^submodule\..*\.path$', env=env).splitlines()
        key = next(line.split()[0][:-5] for line in entries if line.split(maxsplit=1)[1] == relative)
        url = git(source, 'config', '-f', '.gitmodules', '--get', key + '.url', env=env)
        name = Path(relative).name
        seeded = False
        for cached_source in candidates:
            for prefix in ('App/ThirdParty', 'ThirdParty', 'ThirdParty/libs'):
                cached = cached_source / prefix / name
                if cached.resolve() == target.resolve() or not (cached / '.git').exists():
                    continue
                try:
                    remote = git(cached, 'remote', 'get-url', 'origin', env=env)
                except RuntimeError:
                    continue
                if identity(remote) != identity(url):
                    continue
                print(f'Reusing local Git objects for {relative}', flush=True)
                target.parent.mkdir(parents=True, exist_ok=True)
                # Local clones own their object files (possibly hardlinked), with
                # no alternates that depend on retention of a previous run.
                run(['git', 'clone', '--no-checkout', cached, target], env=env)
                git(target, 'remote', 'set-url', 'origin', url, env=env)
                seeded = True
                break
            if seeded:
                break


def seed_snapshot(source, config, env):
    source = source.resolve()
    if not source.is_relative_to(Path(config['root']).resolve()):
        raise RuntimeError('Source cache destination must be inside the CI root')
    for root in roots(config):
        manifests = sorted((root / 'runs').glob('*/build-manifest.json'),
                           key=lambda p: p.stat().st_mtime, reverse=True)
        for path in manifests:
            data = read_json(path)
            bundle = path.parent / 'source.bundle'
            if (data.get('status') != 'built' or not data.get('source_path')
                    or data.get('source_shallow_boundaries')
                    or not bundle.is_file() or not data.get('source_bundle_sha256')):
                continue
            with bundle.open('rb') as stream:
                if hashlib.file_digest(stream, 'sha256').hexdigest() != data['source_bundle_sha256']:
                    raise RuntimeError('Local source snapshot checksum differs from its completed build')
            source.mkdir(parents=True, exist_ok=True)
            if not (source / '.git').exists():
                run(['git', 'init', source], env=env)
            repository = os.environ.get('GITHUB_REPOSITORY') or data.get('repository', 'dezbracaty/GPlatform')
            git(source, 'config', 'remote.origin.url', 'https://github.com/' + repository, env=env)
            git(source, 'fetch', '--no-tags', bundle, 'HEAD:refs/cache/source-snapshot', env=env)
            git(source, 'checkout', '--force', '--detach', 'refs/cache/source-snapshot', env=env)
            print('Restored verified local source snapshot; checkout will fetch the selected remote commit.', flush=True)
            return
    print('No completed local source snapshot; checkout will fetch from the remote.', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--config', type=Path, required=True)
    args = parser.parse_args()
    seed_snapshot(args.source, read_json(args.config), git_env())
