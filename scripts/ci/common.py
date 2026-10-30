"""Read-only source checkout and native process helpers for App CI."""
from __future__ import annotations
import base64
import contextlib
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import stat
import subprocess
import time

SDK_PATH = "ThirdParty/libs/GPlatformSDK"

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
            # Transfer stderr streams directly; Actions masks credentials before
            # the child starts. Local commands retain the original capture policy.
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

def prepare_cached_source(source, sha, platform, *, consumer=False):
    """Reset the runner's fixed checkout, retaining only preset build caches."""
    source = Path(source).absolute()
    workspace = Path(os.environ["GITHUB_WORKSPACE"]).resolve()
    marker = source / ".git"
    if (marker.is_symlink() or marker.is_junction() or source.is_symlink() or source.is_junction() or source.resolve() != workspace / "source"
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
    print("Retained source Git objects and preset build caches: " + ", ".join(builds), flush=True)


def _init_pinned_submodules(source, env, *, skip=None, require_sdk=False):
    # The fixed commit's gitlinks are authoritative. .gitmodules may still
    # contain declarations for dependencies removed from the product tree.
    links = {}
    for entry in git(source, "ls-tree", "-rz", "HEAD").split(b"\0"):
        metadata, _, path = entry.partition(b"\t")
        if metadata.startswith(b"160000 commit "):
            links[path.decode()] = metadata.split()[2].decode()
    if require_sdk and SDK_PATH not in links:
        raise RuntimeError("App SDK gitlink is missing from the fixed source commit")
    if not links:
        return
    if not (Path(source) / ".gitmodules").is_file():
        raise RuntimeError("Fixed source gitlinks have no .gitmodules declarations")
    output = git(source, "config", "-z", "--file", ".gitmodules", "--get-regexp", r"^submodule\..*\.path$")
    paths = [entry.split(b"\n", 1)[1].decode() for entry in output.split(b"\0") if entry]
    if len(paths) != len(set(paths)):
        raise RuntimeError("Duplicate submodule path declarations")
    if set(links) - set(paths):
        raise RuntimeError("Fixed source gitlink is missing its .gitmodules declaration")
    for path in paths:
        if path.startswith("-") or Path(path).is_absolute() or ".." in Path(path).parts:
            raise ValueError("Invalid submodule path")
        if path not in links:
            print(f"Skipping stale submodule declaration: {path!r}")
            continue
        if path == skip:
            # Producer never downloads SDK inputs; drop an old initialized worktree
            # while preserving its Git/LFS object store for later consumers.
            git(source, "submodule", "deinit", "--force", "--", path, env=env)
            continue
        git(source, "submodule", "sync", "--", path, env=env)
        print(f"Initializing pinned submodule {path!r} at {links[path]}", flush=True)
        run(["git", "-C", source, "submodule", "update", "--init",
             "--checkout", "--force", "--depth=1", "--", path], env=env, attempts=3)
        if text_git(Path(source) / path, "rev-parse", "HEAD") != links[path]:
            raise RuntimeError("Initialized submodule differs from its fixed gitlink")
        # A new parent pin may remove a previously initialized child.
        git(Path(source) / path, "clean", "-ffdx", env=env)
        # Each new parent pin supplies its own .gitmodules before children sync.
        _init_pinned_submodules(Path(source) / path, env)


def init_source_submodules(source, *, consumer=False):
    env = git_env()
    _init_pinned_submodules(source, env, skip=None if consumer else f"App/{SDK_PATH}",
                           require_sdk=consumer)
    if consumer:
        sdk = SDK_PATH
        slot = "AMD64/Release" if os.name == "nt" else None
        include = f"{slot}/**" if slot else "arm64/Release/**,x86_64/Release/**"
        run(["git", "-C", Path(source) / sdk, "lfs", "pull", "--include", include,
             "--exclude", ""], env=env)


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
