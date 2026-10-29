"""CI transport and bookkeeping. Product build/package rules remain in their existing entries."""
import base64
import contextlib
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import time

# Native Windows terminals can use GBK while CMake emits Unicode status symbols.
# Keep the original bytes in log files and avoid aborting a build while echoing them.
for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, 'reconfigure'):
        _stream.reconfigure(errors='replace')


def read_json(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def write_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    temporary.replace(path)


def run(args, cwd=None, *, env=None, log=None, capture=False):
    args = [str(arg) for arg in args]
    # Never print command arguments: Git may carry an authorization header.
    print(f'Running {Path(args[0]).name}' + (f' (log: {log})' if log else ''), flush=True)
    if capture:
        result = subprocess.run(args, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if result.returncode:
            details = result.stderr.decode('utf-8', errors='replace').strip()
            raise RuntimeError(f'{Path(args[0]).name} failed ({result.returncode}): {details}')
        return result.stdout.decode('utf-8', errors='replace').strip()
    if log:
        Path(log).parent.mkdir(parents=True, exist_ok=True)
        with Path(log).open('ab') as output:
            with subprocess.Popen(args, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT) as process:
                for line in process.stdout:
                    output.write(line)
                    output.flush()
                    print(line.decode('utf-8', errors='replace'), end='', flush=True)
                status = process.wait()
        if status:
            raise RuntimeError(f'{Path(args[0]).name} failed ({status}); see {log}')
    else:
        subprocess.run(args, cwd=cwd, env=env, check=True)


def git(repo, *args, env=None):
    # Fetching and initializing dependencies are repeatable when the proxy drops
    # TLS. Content-addressed LFS uploads are repeatable too. Keep reference
    # mutations such as push/commit outside this transport retry.
    retryable = args and (args[0] in ('clone', 'fetch', 'submodule')
                          or args[:2] in (('lfs', 'pull'), ('lfs', 'push')))
    for attempt in range(3):
        try:
            return run(['git', '-C', repo, *args], env=env, capture=True)
        except RuntimeError as error:
            transient = str(error).lower().rstrip().endswith(': eof') or any(message in str(error).lower() for message in
                            ('unexpected eof', 'connection reset', 'tls connect error',
                             'ssl connect error', 'schannel: next initializesecuritycontext',
                             'operation too slow', 'timed out', 'http/2 stream', 'error: 502'))
            if not retryable or not transient or attempt == 2:
                raise
            print('Git transport interrupted; retrying dependency transfer...', flush=True)
            time.sleep(2 ** (attempt + 1))


def git_env(token=None):
    env = dict(os.environ, GIT_TERMINAL_PROMPT='0', GCM_INTERACTIVE='never', GIT_LFS_SKIP_SMUDGE='1')
    env.setdefault('GIT_HTTP_LOW_SPEED_LIMIT', '1024')
    env.setdefault('GIT_HTTP_LOW_SPEED_TIME', '60')
    if token:
        count = int(env.get('GIT_CONFIG_COUNT', '0'))
        env[f'GIT_CONFIG_KEY_{count}'] = 'http.https://github.com/.extraheader'
        encoded = base64.b64encode(f'x-access-token:{token}'.encode()).decode()
        env[f'GIT_CONFIG_VALUE_{count}'] = f'AUTHORIZATION: basic {encoded}'
        env['GIT_CONFIG_COUNT'] = str(count + 1)
    return env


def github_credential(explicit, *, use_git_credentials=False):
    if explicit:
        return explicit
    if not use_git_credentials:
        return None
    result = subprocess.run(['git', 'credential', 'fill'],
                            input='protocol=https\nhost=github.com\n\n', text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=git_env())
    if not result.returncode:
        values = dict(line.split('=', 1) for line in result.stdout.splitlines() if '=' in line)
        if values.get('password'):
            return values['password']
    gh = shutil.which('gh')
    if gh:
        # An unattended runner may not have the Git helper's terminal/keychain
        # context. Reuse the account's explicit CLI login entirely in memory.
        cli_env = git_env()
        # The repository-scoped Actions token must not shadow the opted-in
        # local login needed to read private source submodules.
        cli_env.pop('GH_TOKEN', None)
        cli_env.pop('GITHUB_TOKEN', None)
        result = subprocess.run([gh, 'auth', 'token', '--hostname', 'github.com'], text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=cli_env)
        if not result.returncode and result.stdout.strip():
            return result.stdout.strip()
    details = result.stderr.strip()
    raise RuntimeError('Runner cannot read a noninteractive Git or GitHub CLI login; configure the workflow token'
                       + (f' ({details})' if details else ''))


@contextlib.contextmanager
def lock(path, *, timeout=0):
    """OS lock is released after a crash; no stale directory lock to remove manually."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('a+b') as stream:
        # Reading byte zero is forbidden while another Windows process holds it.
        if os.fstat(stream.fileno()).st_size == 0:
            stream.write(b'0')
            stream.flush()
        stream.seek(0)
        if os.name == 'nt':
            import msvcrt
        else:
            import fcntl
        deadline = time.monotonic() + timeout
        waiting = False
        while True:
            try:
                if os.name == 'nt':
                    msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
                else:
                    fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
                break
            except OSError:
                if time.monotonic() >= deadline:
                    raise
                if not waiting:
                    print('Waiting for the active build pipeline to finish...', flush=True)
                    waiting = True
                time.sleep(2)
        try:
            yield
        finally:
            stream.seek(0)
            if os.name == 'nt':
                msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(stream, fcntl.LOCK_UN)


def remove_under(path, parent):
    path, parent = Path(path).resolve(), Path(parent).resolve()
    if path == parent or not path.is_relative_to(parent) or path.is_symlink():
        raise RuntimeError(f'Refusing to remove outside the CI child directory: {path}')
    if path.exists():
        shutil.rmtree(path)


def packager(source):
    spec = importlib.util.spec_from_file_location('gplatform_existing_packager', Path(source) / 'cmake/sdk/publish_sdk.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def verify_files(root, manifest, digest):
    root = Path(root).resolve()
    for name, expected in manifest.items():
        path = (root / name).resolve()
        if not path.is_relative_to(root) or not path.is_file() or digest(path) != expected:
            raise RuntimeError(f'Missing, changed or unsafe artifact: {name}')


def build_environment(config):
    env = dict(os.environ, Qt6_DIR=config['qt6_dir'], CMAKE_BUILD_PARALLEL_LEVEL=str(config.get('jobs', 4)))
    cache = Path(config['fetch_cache'])
    if config.get('platform') == 'macos':
        if config.get('developer_dir'):
            env['DEVELOPER_DIR'] = config['developer_dir']
    env['CMAKE_FETCH_CACHE'] = str(cache)
    return env


def build_entry(automation, source, build, config, *, target=None, cmake_arg=None, log=None, arch=None):
    """Invoke the existing project build entry for the configured platform."""
    if config.get('platform') == 'macos':
        if arch not in ('arm64', 'x86_64'):
            raise RuntimeError('macOS builds require one explicit SDK architecture')
        preset = ('arm' if arch == 'arm64' else 'x86') + '_release'
        env = build_environment(config)
        args = ['bash', Path(automation) / 'scripts/build.sh', '--source-dir', source,
                '--preset', preset, '--cmake', config['cmake'], '--configure-only', '--',
                '-B', build, f"-DQt6_DIR={config['qt6_dir']}", f"-DCMAKE_FETCH_CACHE={env['CMAKE_FETCH_CACHE']}",
                f'-DPython3_EXECUTABLE={sys.executable}']
        if cmake_arg:
            args.append(cmake_arg)
        run(args, env=env, log=log)
        default = 'gplatform_build' if (Path(source) / 'App/CMakeLists.txt').is_file() else 'GPlatform'
        run([config['cmake'], '--build', build, '--target', target or default,
             '--parallel', config.get('jobs', 4)], env=env, log=log)
        return
    args = ['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'RemoteSigned', '-File',
            Path(automation) / 'scripts/build.ps1', '-Preset', 'release', '-SourceDir', source,
            '-BuildDirectory', build, '-CMake', config['cmake']]
    if config.get('vs_root'):
        args += ['-VsRoot', config['vs_root']]
    if target:
        args += ['-Targets', target]
    if cmake_arg:
        args += ['-CMakeArgs', cmake_arg]
    run(args, env=build_environment(config), log=log)


def merge_universal(source, binary, config, *, log=None, dmg=False):
    """Deploy and merge already-built thin applications through the existing product entry."""
    args = [sys.executable, Path(source) / 'scripts/build_macos_universal.py',
            '--source-dir', source, '--cmake', config['cmake'], '--build-dir', Path(binary) / 'universal',
            '--arm64-build-dir', Path(binary) / 'arm64', '--x86_64-build-dir', Path(binary) / 'x86_64',
            '--qt-dir', config['qt6_dir'], '--jobs', config.get('jobs', 4), '--merge-only']
    if dmg:
        args.append('--dmg')
    try:
        run(args, env=build_environment(config), log=log)
    finally:
        if log:
            for diagnostic in (Path(binary) / 'universal').glob('*.log'):
                shutil.copy2(diagnostic, Path(log).parent / f'{Path(log).stem}-{diagnostic.name}')


def machine_config(path):
    config = read_json(path)
    config.setdefault('platform', 'windows')
    if config['platform'] not in ('windows', 'macos'):
        raise RuntimeError('platform must be windows or macos')
    for key in ('root', 'cmake', 'qt6_dir'):
        if not config.get(key) or not Path(config[key]).is_absolute():
            raise RuntimeError(f'machine.json requires an absolute {key}')
    if not (Path(config['qt6_dir']) / 'Qt6Config.cmake').is_file():
        raise RuntimeError('qt6_dir must contain Qt6Config.cmake')
    if not Path(config['cmake']).is_file():
        raise RuntimeError('Configured CMake executable does not exist')
    config.setdefault('jobs', 4)
    if not isinstance(config['jobs'], int) or config['jobs'] < 1:
        raise RuntimeError('jobs must be a positive integer')
    # Existing CMake contracts additionally check compiler identity inside each cache.
    qt_version = (Path(config['qt6_dir']) / 'Qt6ConfigVersion.cmake').read_text(encoding='utf-8')
    identity = json.dumps([config['cmake'], config['qt6_dir'], config.get('vs_root'), qt_version], sort_keys=True)
    if config['platform'] == 'macos':
        if platform.system() != 'Darwin':
            raise RuntimeError('macOS machine configuration requires a macOS host')
        if config.get('developer_dir') and not Path(config['developer_dir']).is_absolute():
            raise RuntimeError('developer_dir must be an absolute Apple developer directory')
        env = dict(os.environ)
        if config.get('developer_dir'):
            env['DEVELOPER_DIR'] = config['developer_dir']
        identity += json.dumps([run(command, env=env, capture=True) for command in (
            ['xcrun', '--find', 'clang++'], ['xcrun', 'clang++', '--version'],
            ['xcrun', '--show-sdk-path'], ['xcrun', '--show-sdk-version'], [config['cmake'], '--version'])])
    for parent in Path(config['cmake']).parents:
        toolset = parent / 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt'
        if toolset.is_file():
            identity += toolset.read_text(encoding='utf-8').strip()
            break
    config['toolchain'] = hashlib.sha256(identity.encode()).hexdigest()[:16]
    config.setdefault('fetch_cache', str(Path(config['root']) / 'cache' / config['toolchain']))
    if not Path(config['fetch_cache']).is_absolute():
        raise RuntimeError('fetch_cache must be an absolute path')
    return config


def now():
    return time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())
