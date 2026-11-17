"""Small Git/process helpers for the frozen App/SDK workflows."""
from __future__ import annotations

from http.client import HTTPException
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen
from urllib.parse import quote
import base64
import contextlib
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import sys
import subprocess
import time

SHA = re.compile(r"[0-9a-f]{40}")
REPO = re.compile(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+")
SDK_DECLARATION = "cmake/sdk-repository.json"
TEST_ASSETS_PATH = "tests/TestAssets"


API_VERSION = '2026-03-10'

class APIError(RuntimeError):
    def __init__(self, status):
        self.status = status
        super().__init__(f"GitHub request returned HTTP {status}")

class API:
    def __init__(self, token=None):
        self.token = token or os.environ.get("GPLATFORM_WORKFLOW_TOKEN")
        if not self.token:
            raise RuntimeError("GPLATFORM_WORKFLOW_TOKEN is required")

    def request(self, method, path, body=None):
        data = None if body is None else json.dumps(body).encode()
        for attempt in range(3 if method == "GET" else 1):
            request = Request("https://api.github.com" + path, method=method, data=data,
                              headers={"Authorization": f"Bearer {self.token}",
                                       "Accept": "application/vnd.github+json",
                                       "X-GitHub-Api-Version": API_VERSION,
                                       "Content-Type": "application/json"})
            try:
                with urlopen(request, timeout=30) as response:
                    raw = response.read()
                    return response.status, json.loads(raw) if raw else None
            except HTTPError as error:
                if method != "GET" or attempt == 2 or error.code not in (429, 500, 502, 503, 504, 403):
                    raise APIError(error.code) from None
                delay = max(1, int(error.headers.get("Retry-After", 2 ** attempt)))
                if delay > 30:
                    raise RuntimeError("GitHub retry delay exceeds this request budget; no early retry was sent") from None
                time.sleep(delay)
            except (URLError, TimeoutError, OSError, HTTPException, ValueError):
                if method != "GET" or attempt == 2:
                    raise RuntimeError(f"GitHub {method} response unavailable") from None
                time.sleep(2 ** attempt)
        raise RuntimeError("GitHub request exhausted")


def source_app_sha(api, repository, source_sha, app_repository):
    """Validate the host App declaration and retain its original gitlink identity."""
    if not REPO.fullmatch(repository) or not REPO.fullmatch(app_repository):
        raise ValueError("Invalid source or App repository")
    require_sha(source_sha)
    _, item = api.request("GET", f"/repos/{repository}/contents/App?ref={source_sha}")
    if not isinstance(item, dict):
        raise RuntimeError("Root candidate does not contain an App gitlink")
    url = item.get("submodule_git_url", "")
    expected = {f"https://github.com/{app_repository}", f"https://github.com/{app_repository}.git",
                f"git@github.com:{app_repository}.git"}
    if url not in expected:
        raise RuntimeError("Root candidate App gitlink differs from the configured App repository")
    return require_sha(item.get("sha", ""))


def resolve_ref(api, repository, ref):
    repository_url(repository)
    if not ref or ref.startswith(('-', 'refs/')):
        raise ValueError('Expected branch or complete SHA')
    exact = bool(SHA.fullmatch(ref))
    _, item = api.request('GET', f'/repos/{repository}/' +
                         ('commits/' if exact else 'branches/') + quote(ref, safe=''))
    sha = require_sha(item['sha'] if exact else item['commit']['sha'])
    if exact and sha != ref:
        raise RuntimeError('Requested SHA did not resolve exactly')
    return sha


def app_dependency_repository(api, repository, app_sha, path):
    repository_url(repository); require_sha(app_sha)
    _, item = api.request('GET', f'/repos/{repository}/contents/{path}?ref={app_sha}')
    match = re.fullmatch(r'(?:https://github\.com/|git@github\.com:)([^/]+/[^/]+?)(?:\.git)?',
                         item.get('submodule_git_url', ''))
    if not match:
        raise RuntimeError('App dependency GitHub repository declaration missing: ' + path)
    require_sha(item.get('sha', ''))
    repository_url(match[1])
    return match[1]


def parse_sdk_repository(content):
    """One versioned repository address, without a second version/pin mechanism."""
    try:
        declaration = json.loads(content)
        if (not isinstance(declaration, dict) or set(declaration) != {'repository'}
                or not isinstance(declaration['repository'], str)):
            raise ValueError('Expected only repository')
        repository_url(declaration['repository'])
        if any(part in ('.', '..') for part in declaration['repository'].split('/')):
            raise ValueError('SDK repository cannot contain relative path components')
    except (ValueError, TypeError, KeyError) as error:
        raise RuntimeError('Invalid SDK repository declaration: ' + SDK_DECLARATION) from error
    return declaration['repository']


def sdk_repository(api, repository, app_sha):
    repository_url(repository); require_sha(app_sha)
    _, item = api.request('GET', f'/repos/{repository}/contents/{SDK_DECLARATION}?ref={app_sha}')
    if (not isinstance(item, dict) or item.get('type') != 'file'
            or item.get('encoding') != 'base64' or not isinstance(item.get('content'), str)):
        raise RuntimeError('SDK repository declaration must be a base64 file: ' + SDK_DECLARATION)
    try:
        content = base64.b64decode(''.join(item['content'].split()), validate=True).decode('utf-8')
    except (ValueError, UnicodeError) as error:
        raise RuntimeError('Invalid SDK repository declaration encoding') from error
    return parse_sdk_repository(content)


def freeze_candidate(api, app_repository, app_ref, host_repository, host_ref):
    """Freeze independent App/host commits; never replace the selected App by a host pin."""
    app_sha = resolve_ref(api, app_repository, app_ref)
    source_sha = resolve_ref(api, host_repository, host_ref)
    declared = source_app_sha(api, host_repository, source_sha, app_repository)
    return dict(app_sha=app_sha, source_sha=source_sha, declared_app_sha=declared,
                host_repository=host_repository, app_repository=app_repository,
                sdk_repository=sdk_repository(api, app_repository, app_sha))


def require_sha(value):
    if not SHA.fullmatch(value):
        raise ValueError("Expected a complete lowercase commit SHA")
    return value


def repository_url(value):
    if not REPO.fullmatch(value):
        raise ValueError("Expected GitHub owner/repository")
    return f"https://github.com/{value}.git"


def branch_ref(value):
    if value.startswith("-") or value.startswith("refs/") or not value:
        raise ValueError("Expected an existing branch name")
    run(["git", "check-ref-format", f"refs/heads/{value}"], capture=True)
    return f"refs/heads/{value}"


def run(args, *, cwd=None, env=None, data=None, capture=False, attempts=1):
    if attempts < 1:
        raise ValueError("Attempt count must be positive")
    for attempt in range(attempts):
        if attempts > 1:
            print(f"Transfer attempt {attempt + 1}/{attempts}", flush=True)
        result = subprocess.run([str(a) for a in args], cwd=cwd, env=env, input=data,
                                stdout=subprocess.PIPE if capture else None,
                                stderr=subprocess.PIPE if capture and attempts == 1 else None)
        if not result.returncode:
            return result.stdout if capture else None
        if attempt + 1 == attempts:
            # Never print command arguments or environment. Surface captured
            # diagnostics too; Actions has already registered credential masks.
            if capture and result.stderr:
                sys.stderr.write(result.stderr.decode('utf-8', errors='replace'))
                sys.stderr.flush()
            raise RuntimeError(f"{Path(str(args[0])).name} failed (exit {result.returncode})"
                               f" after {attempts} attempt(s)")
        delay = 2 ** attempt
        print(f"Transfer failed (exit {result.returncode}); retrying in {delay}s", flush=True)
        time.sleep(delay)


def git(repo, *args, env=None, data=None, attempts=1):
    return run(["git", "-C", repo, *args], env=env, data=data, capture=True, attempts=attempts)


def text_git(repo, *args, **kwargs):
    return git(repo, *args, **kwargs).decode().strip()


def git_env(secret="GPLATFORM_SOURCE_TOKEN"):
    env = dict(os.environ, GIT_TERMINAL_PROMPT="0", GCM_INTERACTIVE="never",
               GIT_LFS_SKIP_SMUDGE="1")
    # Never fall back to the developer's persistent credential store.
    settings = [("credential.helper", ""), ("core.autocrlf", "false"),
                ("core.eol", "lf"), ("lfs.transfer.maxretries", "3")]
    token = os.environ.get(secret)
    if token:
        header = base64.b64encode(f"x-access-token:{token}".encode()).decode()
        if os.environ.get("GITHUB_ACTIONS") == "true":
            for value in (token, header):
                print(f"::add-mask::{value}", flush=True)
        settings.append(("http.https://github.com/.extraheader", f"AUTHORIZATION: basic {header}"))
    offset = int(env.get("GIT_CONFIG_COUNT", "0"))
    for n, (key, value) in enumerate(settings, offset):
        env[f"GIT_CONFIG_KEY_{n}"] = key
        env[f"GIT_CONFIG_VALUE_{n}"] = value
    env["GIT_CONFIG_COUNT"] = str(offset + len(settings))
    return env


def read_remote_head(url, branch, *, env=None):
    ref = branch_ref(branch)
    entries = run(["git", "ls-remote", "--exit-code", url, ref], env=env, capture=True, attempts=3).decode().splitlines()
    if len(entries) != 1 or entries[0].split()[1] != ref:
        raise RuntimeError("Target branch is missing or ambiguous")
    return require_sha(entries[0].split()[0])




def clone_branch(url, branch, path, *, env=None):
    ref = branch_ref(branch)
    # A failed clone can leave a destination behind. Initialize once and retry
    # only fetch, preserving received objects without deleting any directory.
    path = Path(path)
    path.mkdir()
    git(path, "init", env=env)
    git(path, "remote", "add", "origin", url, env=env)
    git(path, "fetch", "--no-tags", "origin", ref, env=env, attempts=3)
    git(path, "checkout", "--detach", "FETCH_HEAD", env=env)
    return text_git(path, "rev-parse", "HEAD")


def prepare_cached_source(source, sha, platform, *, consumer=False, host=False):
    """Reset the runner's fixed checkout, retaining only preset build caches."""
    source = Path(source).absolute()
    workspace = Path(os.environ["GITHUB_WORKSPACE"]).resolve()
    marker = source / ".git"
    if (marker.is_symlink() or marker.is_junction() or source.is_symlink() or source.is_junction() or source.resolve() != workspace / ("host-source" if host else "source")
            or not marker.is_dir()):
        raise RuntimeError("Refusing to reset a source outside the runner checkout")
    source = source.resolve()
    if (Path(text_git(source, "rev-parse", "--absolute-git-dir")).resolve() != source / ".git"
            or Path(text_git(source, "rev-parse", "--show-toplevel")).resolve() != source
            or text_git(source, "rev-parse", "HEAD") != sha or not re.fullmatch(r"[0-9a-f]{40}", sha)):
        raise RuntimeError("Cached source differs from the fixed source SHA")
    if platform == "windows":
        builds = ["build-Release"]
    elif platform == "macos":
        builds = ["build-arm64-Release", "build-x86_64-Release"]
        if consumer:
            builds.append("build-universal-Release")
    else:
        raise ValueError("Unknown build platform")
    for name in builds:
        path = source / name
        if (path.is_symlink() or path.is_junction() or (path.exists() and not path.is_dir())
                or git(source, "ls-tree", "HEAD", "--", name)):
            raise RuntimeError("Preset cache must be an untracked directory inside source")
    for name in builds:
        cache = source / name / "CMakeCache.txt"
        if not cache.exists():
            continue
        values = {}
        for line in cache.read_text(encoding="utf-8").splitlines():
            if not line.startswith(("//", "#")) and ":" in line and "=" in line:
                key, value = line.split("=", 1)
                values[key.split(":", 1)[0]] = value
        expected = {"CMAKE_HOME_DIRECTORY": str(source), "CMAKE_GENERATOR": "Ninja",
                    "CMAKE_BUILD_TYPE": "Release"}
        if name in ("build-arm64-Release", "build-x86_64-Release"):
            arch = "arm64" if name == "build-arm64-Release" else "x86_64"
            expected.update(GPLATFORM_TARGET_ARCH=arch, CMAKE_OSX_ARCHITECTURES=arch)
        for key in ("Qt6_DIR", "CMAKE_FETCH_CACHE", "CMAKE_TOOLCHAIN_FILE",
                    "CMAKE_C_COMPILER", "CMAKE_CXX_COMPILER", "CMAKE_MAKE_PROGRAM",
                    "CMAKE_OSX_SYSROOT", "CMAKE_OSX_DEPLOYMENT_TARGET"):
            if os.environ.get(key):
                expected[key] = os.environ[key]
        for key, value in expected.items():
            normalize = lambda item: os.path.normcase(os.path.normpath(item))
            if key not in values or normalize(values[key]) != normalize(value):
                raise RuntimeError(f"Preset cache mismatch for {key}: {cache}; clean this owned build cache before changing build inputs")
    env = git_env()
    git(source, "reset", "--hard", "HEAD", env=env)
    git(source, "submodule", "foreach", "--recursive", "git reset --hard HEAD", env=env)
    git(source, "submodule", "foreach", "--recursive", "git clean -ffdx", env=env)
    excludes = [argument for name in builds for argument in ("-e", f"/{name}/")]
    git(source, "clean", "-ffdx", *excludes, env=env)
    # A cached pre-submodule App tree survives git clean because the new
    # index owns App as a gitlink. Remove only this uninitialized owned residue.
    app = source / 'App'
    entry = git(source, 'ls-tree', 'HEAD', '--', 'App')
    if entry.startswith(b'160000 commit '):
        if app.is_symlink() or app.is_junction():
            raise RuntimeError('App cache must be a real directory')
        if app.exists() and not (app / '.git').exists():
            remove_owned_tree(app, source)
    print("Retained source Git objects and preset build caches: " + ", ".join(builds), flush=True)


def _init_pinned_submodules(source, env, *, require_app=False, producer=False,
                            app_sha=None, app_repository=None):
    # The fixed commit's gitlinks are authoritative. .gitmodules may still
    # contain declarations for dependencies removed from the product tree.
    links = {}
    for entry in git(source, "ls-tree", "-rz", "HEAD").split(b"\0"):
        metadata, _, path = entry.partition(b"\t")
        if metadata.startswith(b"160000 commit "):
            links[path.decode()] = metadata.split()[2].decode()
    if producer and 'App' not in links:
        raise RuntimeError('Producer source must contain its pinned App gitlink')
    if producer and app_sha is not None:
        require_sha(app_sha)
        repository_url(app_repository)
    if require_app and set(links) - {TEST_ASSETS_PATH} != {'ThirdParty/OrcaSlicer'}:
        raise RuntimeError('App must contain exactly the Orca gitlink, with optional TestAssets; SDK is explicit')
    if not links:
        return
    if not (Path(source) / ".gitmodules").is_file():
        raise RuntimeError("Fixed source gitlinks have no .gitmodules declarations")
    output = git(source, "config", "-z", "--file", ".gitmodules", "--get-regexp", r"^submodule\..*\.path$")
    declarations = [entry.split(b"\n", 1) for entry in output.split(b"\0") if entry]
    paths = [path.decode() for key, path in declarations]
    if len(paths) != len(set(paths)):
        raise RuntimeError("Duplicate submodule path declarations")
    if set(links) - set(paths):
        raise RuntimeError("Fixed source gitlink is missing its .gitmodules declaration")
    if TEST_ASSETS_PATH in links:
        key = next(key.decode().rsplit('.', 1)[0] + '.update' for key, path in declarations
                   if path.decode() == TEST_ASSETS_PATH)
        if text_git(source, 'config', '--file', '.gitmodules', '--get', key) != 'none':
            raise RuntimeError('Optional TestAssets must declare update=none')
    for path in paths:
        if path.startswith("-") or Path(path).is_absolute() or ".." in Path(path).parts:
            raise ValueError("Invalid submodule path")
        if path not in links:
            print(f"Skipping stale submodule declaration: {path!r}")
            continue
        if path == TEST_ASSETS_PATH:
            # Product builds do not consume test assets. Preserve cached Git/LFS
            # objects while preventing an old initialized pin from dirtying CI.
            git(source, 'submodule', 'deinit', '--force', '--', path, env=env)
            print('Skipping optional pinned TestAssets; initialize explicitly for acceptance')
            continue
        git(source, "submodule", "sync", "--", path, env=env)
        app_owner = producer and path == 'App'
        if app_owner and app_sha is not None:
            key = next(key.decode().rsplit('.', 1)[0] + '.url' for key, value in declarations
                       if value.decode() == path)
            url = text_git(source, 'config', '--file', '.gitmodules', '--get', key)
            if url not in {repository_url(app_repository), repository_url(app_repository).removesuffix('.git'),
                           f'git@github.com:{app_repository}.git'}:
                raise RuntimeError('Host App declaration differs from the selected App repository')
        print(f"Initializing pinned submodule {path!r} at {links[path]}", flush=True)
        run(["git", "-c", "fetch.recurseSubmodules=false", "-C", source, "submodule", "update", "--init",
             "--checkout", "--force", "--depth=1", "--", path], env=env, attempts=3)
        if text_git(Path(source) / path, "rev-parse", "HEAD") != links[path]:
            raise RuntimeError("Initialized submodule differs from its fixed gitlink")
        if app_owner and app_sha is not None and app_sha != links[path]:
            git(Path(source) / path, 'fetch', '--no-tags', '--depth=1', 'origin', app_sha,
                env=env, attempts=3)
            if text_git(Path(source) / path, 'rev-parse', 'FETCH_HEAD^{commit}') != app_sha:
                raise RuntimeError('Fetched App differs from the fixed candidate')
            git(Path(source) / path, 'checkout', '--detach', '--force', app_sha, env=env)
        # A new parent pin may remove a previously initialized child.
        git(Path(source) / path, "clean", "-ffdx", env=env)
        # Each new parent pin supplies its own .gitmodules before children sync.
        child = Path(source) / path
        _init_pinned_submodules(child, env, require_app=app_owner)
        git(child, 'lfs', 'install', '--local', env=env)
        git(child, 'lfs', 'pull', env=env)


def init_source_submodules(source):
    """Initialize App source inputs; binary SDK selection is always explicit."""
    _init_pinned_submodules(source, git_env(), require_app=True)


def init_host_submodules(source, app_sha, app_repository):
    """Initialize a host's pinned forks and the independently frozen App candidate."""
    require_sha(app_sha); repository_url(app_repository)
    _init_pinned_submodules(source, git_env(), producer=True,
                            app_sha=app_sha, app_repository=app_repository)


def materialize_sdk(root, sha, platform):
    """Materialize only the frozen platform's LFS payload; never follow a branch."""
    root = Path(root).resolve()
    if (not re.fullmatch(r'[0-9a-f]{40}', sha)
            or Path(text_git(root, 'rev-parse', '--show-toplevel')).resolve() != root
            or text_git(root, 'rev-parse', 'HEAD') != sha
            or text_git(root, 'status', '--porcelain', '--untracked-files=no')):
        raise RuntimeError('SDK checkout differs from the frozen clean commit')
    slots = {'windows': 'AMD64/Release/**', 'macos': 'arm64/Release/**,x86_64/Release/**'}
    if platform not in slots:
        raise ValueError('Unknown SDK platform')
    run(['git', '-C', root, 'lfs', 'pull', '--include', slots[platform], '--exclude', ''],
        env=git_env(), attempts=3)
    if text_git(root, 'rev-parse', 'HEAD') != sha or text_git(root, 'status', '--porcelain', '--untracked-files=no'):
        raise RuntimeError('SDK changed while materializing its fixed payload')
    return root


def digest(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def _retry_readonly(function, filename, error, *, windows=None):
    is_windows = os.name == "nt" if windows is None else windows
    if not is_windows or not isinstance(error, PermissionError):
        raise error
    mode = os.lstat(filename).st_mode
    if stat.S_ISLNK(mode) or mode & stat.S_IWRITE:
        raise error
    # Windows Python 3.12 does not support chmod(follow_symlinks=False).
    # The lstat check above rejects links before adjusting the readonly bit.
    os.chmod(filename, mode | stat.S_IWRITE)
    function(filename)


def remove_owned_tree(path, parent, *, require_git=False):
    path, parent = Path(path), Path(parent).resolve()
    if path.is_symlink() or path.resolve().parent != parent:
        raise RuntimeError("Refusing to clean a directory outside its task parent")
    if not path.exists():
        return
    if require_git and not (path / ".git").exists():
        raise RuntimeError("Refusing to clean a directory without its task checkout marker")
    shutil.rmtree(path, onexc=_retry_readonly)


def write_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(value, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def write_outputs(value, result=None):
    if result:
        write_json(result, value)
    output = os.environ.get("GITHUB_OUTPUT")
    if output:
        with open(output, "a", encoding="utf-8") as f:
            for key, item in value.items():
                rendered = json.dumps(item, separators=(",", ":")) if not isinstance(item, str) else item
                if "\n" in rendered or "\r" in rendered:
                    raise ValueError("Multiline workflow output rejected")
                f.write(f"{key}={rendered}\n")
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as f:
            f.write("\n```json\n" + json.dumps(value, indent=2) + "\n```\n")


def commit_tree(repo, tree, base, message, *, env=None):
    env = dict(env or os.environ)
    env.update(GIT_AUTHOR_NAME="GPlatform CI", GIT_AUTHOR_EMAIL="ci@gplatform.invalid",
               GIT_COMMITTER_NAME="GPlatform CI", GIT_COMMITTER_EMAIL="ci@gplatform.invalid")
    return text_git(repo, "commit-tree", tree, "-p", require_sha(base),
                    data=(message + "\n").encode(), env=env)


def push_exact_commit(repo, url, branch, base, commit, *, env=None, lfs=False):
    ref = branch_ref(branch)
    if read_remote_head(url, branch, env=env) != base:
        raise RuntimeError("Target branch changed; start a new update")
    if commit == base:
        return
    parents = text_git(repo, "rev-list", "--parents", "-n", "1", require_sha(commit)).split()
    if parents != [commit, base]:
        raise RuntimeError("Only a direct fast-forward child of the expected base may be pushed")
    if lfs:
        run(["git", "-C", repo, "lfs", "push", "origin", commit], env=env, attempts=3)
    for attempt in range(3):
        try:
            # The exact lease is a server-side old-SHA comparison. The direct
            # parent check above forbids all history-replacing updates.
            git(repo, "push", f"--force-with-lease={ref}:{base}", "origin", f"{commit}:{ref}", env=env)
        except RuntimeError:
            current = read_remote_head(url, branch, env=env)
            if current == commit:
                return
            if current != base:
                raise RuntimeError("Concurrent update or uncertain push result")
            if attempt == 2:
                raise
            time.sleep(2 ** attempt)
        else:
            if read_remote_head(url, branch, env=env) != commit:
                raise RuntimeError("Remote advanced before update could be confirmed")
            return


@contextlib.contextmanager
def machine_lock(timeout=1800):
    path = Path(os.environ.get("GPLATFORM_MACHINE_BUILD_LOCK",
                               str(Path.home() / ".cache/gplatform/build.lock")))
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a+b") as f:
        if os.name == "nt":
            import msvcrt
            if path.stat().st_size == 0:
                f.write(b"0"); f.flush()
            def acquire():
                f.seek(0); msvcrt.locking(f.fileno(), msvcrt.LK_NBLCK, 1)
            def release():
                f.seek(0); msvcrt.locking(f.fileno(), msvcrt.LK_UNLCK, 1)
        else:
            import fcntl
            def acquire():
                fcntl.flock(f.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            def release():
                fcntl.flock(f.fileno(), fcntl.LOCK_UN)
        deadline = time.monotonic() + timeout
        while True:
            try:
                acquire()
                break
            except OSError:
                if time.monotonic() >= deadline:
                    raise RuntimeError("Timed out waiting for the shared machine build lock")
                time.sleep(2)
        try:
            yield
        finally:
            release()
