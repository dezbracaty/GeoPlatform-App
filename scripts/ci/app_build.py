"""Build the standalone App against its recorded binary SDK; never produce an SDK."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import sys
import traceback

from common import (build_entry, build_environment, git, git_env, lock, machine_config,
                    merge_universal, now, read_json, remove_under, run, verify_files, write_json)

AUTOMATION = Path(__file__).resolve().parents[2]
PIN = 'Use repository pin'
from source_cache import seed_submodules, record_verified_inputs

SDK = 'ThirdParty/libs/GPlatformSDK'
FORKS = {'orca_ref': 'ThirdParty/OrcaSlicer', 'smoothui_ref': 'ThirdParty/SmoothUI',
         'dock_ref': 'ThirdParty/SmoothUIDock', 'sdk_ref': SDK}


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def architectures(platform):
    return ('arm64', 'x86_64') if platform == 'macos' else ('AMD64',)


def verify_sdk(source, platform):
    metadata = {}
    for arch in architectures(platform):
        slot = Path(source) / SDK / arch / 'Release'
        info = read_json(slot / 'sdk-build.json')
        if (info.get('SDK_FORMAT') != 2 or info.get('CMAKE_BUILD_TYPE') != 'Release'
                or info.get('CMAKE_SYSTEM_NAME') != ('Darwin' if platform == 'macos' else 'Windows')
                or info.get('GPLATFORM_SDK_ARCH') != arch):
            raise RuntimeError(f'SDK does not contain the required {platform}/{arch}/Release package')
        verify_files(slot, read_json(slot / 'sdk-manifest.json'), digest)
        metadata[arch] = info
    if platform == 'macos':
        for key in ('Qt6_VERSION', 'SOURCE_STATE', 'SOURCE_INPUTS', 'SOURCE_REVISIONS', 'DEPENDENCIES'):
            if metadata['arm64'].get(key) != metadata['x86_64'].get(key):
                raise RuntimeError(f'The two macOS SDK slots have different {key}')
    return metadata


def record_selected_inputs(source, metadata):
    return record_verified_inputs(source, metadata)


def prepare_source(source, config):
    env = git_env(os.environ.get('SOURCE_TOKEN') or os.environ.get('GITHUB_TOKEN'))
    if Path(git(source, 'rev-parse', '--show-toplevel')).resolve() != source.resolve():
        raise RuntimeError('App source must be its own isolated Git checkout')
    if git(source, 'status', '--porcelain', '--ignore-submodules=all'):
        raise RuntimeError('App source must be clean before dependency preparation')
    base = git(source, 'rev-parse', 'HEAD')
    entries = git(source, 'ls-files', '--stage').splitlines()
    modules = [line.split('\t', 1)[1] for line in entries if line.startswith('160000 ')]
    if not set(FORKS.values()).issubset(modules):
        raise RuntimeError('App must declare its SDK and three source submodules')
    git(source, 'submodule', 'sync', '--recursive', env=env)
    seed_submodules(source, modules, config, env)
    git(source, 'submodule', 'update', '--init', '--recursive', '--depth', '1', '--', *modules, env=env)
    refs = {}
    for field, path in FORKS.items():
        selected = os.environ.get('INPUT_' + field.upper(), '')
        if selected and selected != PIN:
            ref = 'refs/heads/' + selected
            git(source, 'check-ref-format', ref)
            git(source / path, 'fetch', '--no-tags', 'origin', ref, env=env)
            git(source / path, 'checkout', '--detach', 'FETCH_HEAD', env=env)
            refs[field] = selected
        elif field != 'sdk_ref' and os.environ.get('INPUT_DEPENDENCIES', 'locked') == 'latest':
            git(source / path, 'config', 'remote.origin.fetch', '+refs/heads/*:refs/remotes/origin/*')
            git(source, 'submodule', 'update', '--remote', '--checkout', '--', path, env=env)
        if path != SDK:
            git(source / path, 'submodule', 'update', '--init', '--recursive', '--depth', '1', env=env)
    revisions = {}
    for path in modules:
        directory = source / path
        if path == SDK:
            includes = ','.join(f'{arch}/Release/**' for arch in architectures(config['platform']))
            git(directory, 'lfs', 'pull', f'--include={includes}', '--exclude=', env=env)
        else:
            nested = [directory]
            for line in git(directory, 'submodule', 'status', '--recursive', env=env).splitlines():
                if line and line[0] in '+U':
                    raise RuntimeError(f'Unresolved nested App submodule: {line}')
                if line and not line.startswith('-'):
                    nested.append(directory / line.strip().split()[1])
            for child in nested:
                git(child, 'lfs', 'pull', env=env)
                git(child, 'lfs', 'fsck', env=env)
                if git(child, 'status', '--porcelain', env=env):
                    raise RuntimeError(f'Modified source submodule: {child}')
        revisions[path] = git(directory, 'rev-parse', 'HEAD')
    git(source, 'add', '--', *modules)
    if git(source, 'diff', '--cached', '--name-only'):
        git(source, '-c', 'user.name=GPlatform CI', '-c', 'user.email=ci@gplatform.invalid',
            'commit', '-m', 'ci: record selected App dependencies')
    return {'base_commit': base, 'commit': git(source, 'rev-parse', 'HEAD'),
            'submodules': revisions, 'explicit_refs': refs,
            'dependencies': os.environ.get('INPUT_DEPENDENCIES', 'locked')}


def completed(directory, platform):
    directory = Path(directory)
    saved = read_json(directory / 'build-manifest.json')
    if (saved.get('kind') != 'standalone-app' or saved.get('status') != 'built' or saved.get('platform') != platform
            or saved.get('task') != 'installer' or not saved.get('artifacts')
            or saved.get('delivery') not in ('artifacts', 'release')):
        raise RuntimeError('Resume requires a completed standalone App installer for this platform')
    verify_files(directory / 'deliverables', saved['artifacts'], digest)
    return saved


def prepare(platform):
    default = Path.home() / 'gp-app-ci' if platform == 'macos' else Path('C:/gp-app-ci')
    root = Path(os.environ.get('GP_APP_CI_ROOT') or default).resolve()
    config = machine_config(root / 'config/machine.json')
    if config['platform'] != platform or Path(config['root']).resolve() != root:
        raise RuntimeError('App runner machine.json must match the selected platform and CI root')
    resume = os.environ.get('INPUT_RESUME', '')
    run_id = os.environ['GITHUB_RUN_ID'] + '-' + os.environ['GITHUB_RUN_ATTEMPT']
    if resume:
        if not re.fullmatch(r'\d+-\d+', resume):
            raise RuntimeError('resume_run must be a previous run-id-attempt')
        run_id = resume
    directory = root / 'runs' / run_id
    delivery = os.environ.get('INPUT_DELIVERY') or 'artifacts'
    if resume:
        delivery = completed(directory, platform)['delivery']
    elif delivery == 'release':
        if os.environ.get('INPUT_TASK', 'installer') != 'installer':
            raise RuntimeError('Release requires task=installer')
        if not re.fullmatch(r'\d+\.\d+\.\d+', os.environ.get('INPUT_VERSION', '')):
            raise RuntimeError('Release requires the existing numeric project version')
    with open(os.environ['GITHUB_ENV'], 'a', encoding='utf-8') as stream:
        stream.write(f'GP_APP_CI_ROOT={root}\nGP_RUN={directory}\nGP_DELIVERY={delivery}\n')


def build(config_path, source, directory):
    config = machine_config(config_path)
    source, directory, root = source.resolve(), directory.resolve(), Path(config['root']).resolve()
    if not source.is_relative_to(root) or not directory.is_relative_to(root / 'runs') or directory == root / 'runs':
        raise RuntimeError('App source and run must be isolated inside the App CI root')
    if source.is_relative_to(directory) or directory.is_relative_to(source):
        raise RuntimeError('App source and run directory must be separate')
    path = directory / 'build-manifest.json'
    if path.exists():
        raise RuntimeError('Run already exists; use resume_run or start a new run')
    logs, deliverables = directory / 'logs', directory / 'deliverables'
    deliverables.mkdir(parents=True)
    task = os.environ.get('INPUT_TASK') or 'installer'
    delivery = os.environ.get('GP_DELIVERY') or 'artifacts'
    if (task not in ('installer', 'build') or delivery not in ('artifacts', 'release')
            or os.environ.get('INPUT_DEPENDENCIES', 'locked') not in ('locked', 'latest')):
        raise RuntimeError('Unsupported App build task or delivery')
    manifest = {'format': 1, 'kind': 'standalone-app', 'status': 'building', 'platform': config['platform'],
                'started_at': now(), 'task': task, 'delivery': delivery, 'run_id': directory.name,
                'repository': os.environ.get('GITHUB_REPOSITORY', 'dezbracaty/GeoPlatform-App'),
                'requested_ref': os.environ.get('INPUT_REF', ''), 'artifacts': {}}
    write_json(path, manifest)
    try:
        version = re.search(r'project\(GPlatform\s+VERSION\s+(\d+\.\d+\.\d+)\)',
                            (source / 'CMakeLists.txt').read_text(), re.IGNORECASE)
        if not version:
            raise RuntimeError('App CMakeLists.txt must declare its project version')
        manifest['version'] = version[1]
        requested = os.environ.get('INPUT_VERSION', '')
        if requested and requested != version[1]:
            raise RuntimeError('Requested version differs from the existing App project version')
        manifest['source'] = prepare_source(source, config)
        metadata = verify_sdk(source, config['platform'])
        manifest['source']['commit'] = record_selected_inputs(source, metadata)
        manifest['sdk_commit'] = manifest['source']['submodules'][SDK]
        manifest['sdk_metadata'] = metadata
        git(source, 'bundle', 'create', str(directory / 'source.bundle'), 'HEAD')
        manifest['source_bundle_sha256'] = digest(directory / 'source.bundle')
        identity = str(source) + config['toolchain'] + os.environ.get('INPUT_REF', '')
        binary = root / 'build' / hashlib.sha256(identity.encode()).hexdigest()[:20]
        if os.environ.get('INPUT_CLEAN') == 'true':
            remove_under(binary, root / 'build')
        manifest['build_directory'] = str(binary)
        write_json(path, manifest)
        env = dict(build_environment(config), QT_QPA_PLATFORM='offscreen')
        qt_bin = Path(config['qt6_dir']).parents[2] / 'bin'
        env['PATH'] = str(qt_bin) + os.pathsep + env.get('PATH', '')
        for arch in architectures(config['platform']):
            thin = binary / arch
            build_entry(AUTOMATION, source, thin, config,
                        arch=arch if config['platform'] == 'macos' else None,
                        cmake_arg='-DGPLATFORM_BUILD_SDK_CHECKS=ON', log=logs / f'{arch}-build.log')
            build_entry(AUTOMATION, source, thin, config, target='SDKConsumerExtensionCheck',
                        arch=arch if config['platform'] == 'macos' else None,
                        cmake_arg='-DGPLATFORM_BUILD_SDK_CHECKS=ON', log=logs / f'{arch}-checks-build.log')
            run([Path(config['cmake']).with_name('ctest' + ('.exe' if os.name == 'nt' else '')),
                 '--test-dir', thin, '-R', '^SDKConsumerExtensionCheck$', '--no-tests=error', '--output-on-failure'],
                env=env, log=logs / f'{arch}-checks.log')
        if config['platform'] == 'macos':
            merge_universal(source, binary, config, dmg=task == 'installer', log=logs / 'universal.log')
            report = read_json(binary / 'universal/report.json')
            if report.get('status') != 'passed' or report.get('architectures') != ['arm64', 'x86_64']:
                raise RuntimeError('Standalone App Universal audit did not pass')
            shutil.copyfile(binary / 'universal/report.json', directory / 'universal-manifest.json')
            if task == 'installer':
                shutil.copy2(binary / 'universal/GPlatform.dmg', deliverables /
                             f'GPlatform-{version[1]}-macos-universal-{directory.name}.dmg')
        elif task == 'installer':
            for old in (source / 'package').glob('*.exe'):
                old.unlink()
            build_entry(AUTOMATION, source, binary / 'AMD64', config, target='GPlatform_installer',
                        cmake_arg=f"-DISCC_EXECUTABLE={config['iscc']}" if config.get('iscc') else None,
                        log=logs / 'installer.log')
            installers = list((source / 'package').glob('*.exe'))
            if len(installers) != 1:
                raise RuntimeError('Expected exactly one Windows installer')
            shutil.copy2(installers[0], deliverables / installers[0].name)
        manifest['artifacts'] = {item.name: digest(item) for item in deliverables.iterdir() if item.is_file()}
        if task == 'installer' and len(manifest['artifacts']) != 1:
            raise RuntimeError('Expected exactly one installer artifact')
        (directory / 'checksums.sha256').write_text(''.join(
            f'{value}  {name}\n' for name, value in sorted(manifest['artifacts'].items())), encoding='utf-8')
        manifest.update(status='built', finished_at=now())
    except BaseException as error:
        manifest.update(status='failed', error=str(error), finished_at=now())
        raise
    finally:
        write_json(path, manifest)
        if os.environ.get('GITHUB_STEP_SUMMARY'):
            with open(os.environ['GITHUB_STEP_SUMMARY'], 'a', encoding='utf-8') as stream:
                stream.write(f"Standalone App: **{manifest['status']}**\n\n")
                stream.write(f"Source: `{manifest.get('source', {}).get('commit', '')}`\n\n")
                stream.write(f"SDK: `{manifest.get('sdk_commit', '')}`\n\n")
                for name in manifest['artifacts']:
                    stream.write(f'- `{name}`\n')


def download(directory):
    directory = directory.resolve()
    manifest = read_json(directory / 'build-manifest.json')
    destination = directory / 'download'
    if destination.is_symlink():
        raise RuntimeError('Download staging directory must not be a symlink')
    remove_under(destination, directory)
    ready = manifest.get('task') == 'installer'
    if ready:
        manifest = completed(directory, manifest['platform'])
        suffix = '.dmg' if manifest['platform'] == 'macos' else '.exe'
        if len(manifest['artifacts']) != 1 or any(Path(name).name != name or not name.endswith(suffix)
                                                for name in manifest['artifacts']):
            raise RuntimeError('Download requires exactly one platform installer')
        destination.mkdir()
        for name in manifest['artifacts']:
            os.link(directory / 'deliverables' / name, destination / name)
        (destination / 'checksums.sha256').write_text(''.join(
            f'{value}  {name}\n' for name, value in sorted(manifest['artifacts'].items())), encoding='utf-8')
    if os.environ.get('GITHUB_OUTPUT'):
        with open(os.environ['GITHUB_OUTPUT'], 'a', encoding='utf-8') as stream:
            stream.write(f'ready={str(ready).lower()}\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation', choices=('prepare', 'build', 'download'))
    parser.add_argument('--platform', choices=('macos', 'windows'))
    parser.add_argument('--config', type=Path)
    parser.add_argument('--source', type=Path)
    parser.add_argument('--run-dir', type=Path)
    args = parser.parse_args()
    if args.operation == 'prepare':
        prepare(args.platform)
    elif args.operation == 'download':
        download(args.run_dir)
    else:
        config = read_json(args.config)
        with lock(Path(config.get('machine_lock') or Path(config['root']) / 'pipeline.lock'), timeout=7200):
            build(args.config, args.source, args.run_dir)


if __name__ == '__main__':
    try:
        main()
    except Exception:
        traceback.print_exc()
        sys.exit(1)
