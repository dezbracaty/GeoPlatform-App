"""Read-only source checkout and native process helpers for App CI."""
from __future__ import annotations
import base64
import contextlib
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import time

SDK_PATH = "ThirdParty/libs/GPlatformSDK"

def run(args, *, cwd=None, env=None, data=None, capture=False):
    result = subprocess.run([str(a) for a in args], cwd=cwd, env=env, input=data,
                            stdout=subprocess.PIPE if capture else None,
                            stderr=subprocess.PIPE if capture else None)
    if result.returncode:
        # Do not expose credential headers or arbitrary subprocess stderr.
        raise RuntimeError(f"{Path(str(args[0])).name} failed (exit {result.returncode})")
    return result.stdout if capture else None

def git(repo, *args, env=None, data=None):
    return run(["git", "-C", repo, *args], env=env, data=data, capture=True)

def text_git(repo, *args, **kwargs):
    return git(repo, *args, **kwargs).decode().strip()

def git_env(secret="GPLATFORM_SOURCE_TOKEN"):
    env = dict(os.environ, GIT_TERMINAL_PROMPT="0", GCM_INTERACTIVE="never",
               GIT_LFS_SKIP_SMUDGE="1")
    # Never fall back to the developer's persistent credential store.
    settings = [("credential.helper", ""), ("core.autocrlf", "false"),
                ("core.eol", "lf")]
    token = os.environ.get(secret)
    if token:
        header = base64.b64encode(f"x-access-token:{token}".encode()).decode()
        settings.append(("http.https://github.com/.extraheader", f"AUTHORIZATION: basic {header}"))
    offset = int(env.get("GIT_CONFIG_COUNT", "0"))
    for n, (key, value) in enumerate(settings, offset):
        env[f"GIT_CONFIG_KEY_{n}"] = key
        env[f"GIT_CONFIG_VALUE_{n}"] = value
    env["GIT_CONFIG_COUNT"] = str(offset + len(settings))
    return env

def init_source_submodules(source, *, consumer=False):
    env = git_env()
    output = text_git(source, "config", "--file", ".gitmodules", "--get-regexp", r"^submodule\..*\.path$")
    paths = [line.split(" ", 1)[1] for line in output.splitlines()]
    sdk = SDK_PATH if consumer else f"App/{SDK_PATH}"
    for path in paths:
        if path == sdk and not consumer:
            continue
        if path.startswith("-") or Path(path).is_absolute() or ".." in Path(path).parts:
            raise ValueError("Invalid submodule path")
        git(source, "submodule", "update", "--init", "--recursive", "--", path, env=env)
    if consumer:
        slot = "AMD64/Release" if os.name == "nt" else None
        include = f"{slot}/**" if slot else "arm64/Release/**,x86_64/Release/**"
        git(Path(source) / sdk, "lfs", "pull", "--include", include, "--exclude", "", env=env)

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
