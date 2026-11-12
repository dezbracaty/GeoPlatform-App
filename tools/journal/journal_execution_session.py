"""Shared CLI/GUI orchestration. Business verification stays in JournalRunner."""

import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import uuid
from pathlib import Path

from journalsystem import JournalSystem
from journal_runner import JournalRunner, load_journal, finish_result
from session_repository import JournalSessionRepository
from journal_display_profile import (WINDOW_PROFILE, process_environment,
                                     validate_preconditions, wait_for_window)


def _macos_bundle_instance(bundle, deadline):
    """Read LaunchServices identity without launching or changing an application."""
    def query(*arguments):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("Timed out waiting for macOS application registration")
        return subprocess.run(["/usr/bin/lsappinfo", *arguments], check=True,
                              capture_output=True, text=True, timeout=min(2, remaining)).stdout

    found = query("-nonames", "find", "bundlepath=" + str(bundle)).strip()
    applications = re.findall(r"ASN:0x[0-9a-fA-F]+-0x[0-9a-fA-F]+:", found)
    if not applications:
        if found:
            raise RuntimeError("Cannot read macOS application registration")
        return None
    if len(applications) != 1:
        raise RuntimeError("Another instance of the test application is registered")
    output = query("-all", "info", "-only", "pid", "-only", "bundlepath",
                   "-only", "executablepath", applications[0])
    identity = {}
    # -all emits a dictionary (often on one line), rather than the default
    # human-readable summary. Read only the three independently checked fields.
    for match in re.finditer(
            r'(?:^|[,{])\s*"(pid|LSBundlePath|CFBundleExecutablePath)"\s*=\s*("(?:[^"\\]|\\.)*"|[0-9]+)',
            output, re.MULTILINE):
        if match[1] in identity:
            raise RuntimeError("Ambiguous macOS application registration")
        identity[match[1]] = json.loads(match[2])
    if set(identity) != {"pid", "LSBundlePath", "CFBundleExecutablePath"}:
        raise RuntimeError("Incomplete macOS application registration")
    return identity


def _activate_owned_macos_application(process, executable, deadline):
    """Activate only this session's already registered .app child by exact PID."""
    if (sys.platform != "darwin" or executable.parent.name != "MacOS" or
            executable.parent.parent.name != "Contents" or
            executable.parent.parent.parent.suffix != ".app"):
        return None
    bundle = executable.parent.parent.parent

    def check(identity):
        if process.poll() is not None:
            raise RuntimeError("Test application exited before macOS activation completed")
        if (identity is None or identity["pid"] != process.pid or
                Path(identity["LSBundlePath"]).resolve() != bundle or
                Path(identity["CFBundleExecutablePath"]).resolve() != executable):
            raise RuntimeError("macOS test application PID, bundle or executable does not match")

    while True:
        if process.poll() is not None:
            raise RuntimeError("Test application exited while waiting for macOS registration")
        identity = _macos_bundle_instance(bundle, deadline)
        if identity is not None:
            break
        time.sleep(min(0.05, max(0, deadline - time.monotonic())))
    check(identity)
    # Unlike `open -a`, setfront cannot launch a replacement if the child exits
    # between identity verification and activation. It addresses only this PID.
    subprocess.run(["/usr/bin/lsappinfo", "setfront", "#" + str(process.pid)],
                   check=True, capture_output=True, text=True,
                   timeout=max(0.01, min(2, deadline - time.monotonic())))
    check(_macos_bundle_instance(bundle, deadline))
    return {"mechanism": "LaunchServices.setfront", "pid": process.pid,
            "bundlePath": str(bundle)}


def load_application_environment(journal_path):
    """Load optional process setup from the scenario sidecar, not Journal v2."""
    journal_path = Path(journal_path).resolve()
    if journal_path.parent.name != "events":
        return {}
    metadata_path = journal_path.parent.parent / "metadata.json"
    if not metadata_path.is_file():
        return {}
    with metadata_path.open("r", encoding="utf-8") as stream:
        metadata = json.load(stream)
    validate_preconditions(metadata.get("preconditions", {}))
    environment = metadata.get("applicationEnvironment", {})
    if not isinstance(environment, dict):
        raise ValueError("metadata.applicationEnvironment must be an object")
    for key, value in environment.items():
        if (not isinstance(key, str) or not key or "=" in key or "\0" in key or
                not isinstance(value, str) or "\0" in value):
            raise ValueError("metadata.applicationEnvironment requires string keys and values")
    return environment


class JournalExecutionSession:
    def __init__(self, *, cancel_event=None, progress=None, log=None):
        self.cancel_event = cancel_event
        self.progress = progress
        self.log = log

    def run(self, journal_path, *, application=None, host="127.0.0.1", port=0,
            output_dir=None, keep_app=False, startup_timeout=30, progress=None, log=None,
            environment_overrides=None, comparisons=None, validate=True):
        progress = progress if progress is not None else self.progress
        log = log if log is not None else self.log
        journal_path = Path(journal_path).resolve()
        document = load_journal(journal_path)  # Freeze input before launching.
        application_environment = load_application_environment(journal_path)
        if environment_overrides is not None and not application:
            raise ValueError("environment_overrides requires launching a new test application")
        if application_environment and not application:
            raise ValueError("Scenario applicationEnvironment requires launching an application")
        output = Path(output_dir).resolve() if output_dir else None
        if output is not None and output.exists():
            raise FileExistsError(f"Output directory already exists: {output}")
        output_ready = False
        paths = None
        client = JournalSystem(request_timeout=10)
        process = None
        activation = None
        stream = None
        handshake = False
        result = {"results": [], "journalPath": document["journalPath"],
                  "journalSha256": document["journalSha256"],
                  "totalEntries": len(document["entries"])}
        emit = log or (lambda message: None)
        try:
            if self.cancel_event is not None and self.cancel_event.is_set():
                raise InterruptedError("Replay cancelled by caller")
            if application:
                if host != "127.0.0.1":
                    raise ValueError("Launching a local test app requires host 127.0.0.1")
                executable = Path(application).resolve(strict=True)
                if not executable.is_file():
                    raise ValueError("Application must be an executable file, not an .app directory")
                # Reserve an available port; the existing replay identity handshake
                # below also prevents sending commands to a different app instance.
                with socket.socket() as reservation:
                    reservation.bind((host, port))
                    port = reservation.getsockname()[1]
                replay_id = uuid.uuid4().hex
                command = [str(executable), "--ghost-port", str(port),
                           "--journal-replay-id", replay_id]
                inherited = os.environ.copy()
                inherited.pop("GPLATFORM_JOURNAL_PRINTER_FIXTURE", None)
                environment = process_environment(inherited, application_environment,
                                                  environment_overrides=environment_overrides)
                if environment_overrides is not None:
                    result["environmentOverrides"] = dict(environment_overrides)
                environment["GPLATFORM_JOURNAL_SESSION"] = "1"
                # The app owns its Journal location. Buffer startup output until
                # journal.paths is available; never guess it from the caller's cwd.
                stream = tempfile.NamedTemporaryFile(mode="wb", prefix="journal-startup-", suffix=".log", delete=False)
                emit(f"启动独立测试实例，Journal 端口 {port}")
                deadline = time.monotonic() + startup_timeout
                process = subprocess.Popen(command, stdout=stream, stderr=subprocess.STDOUT,
                                           env=environment)
                last_error = "Journal service is not listening"
                while time.monotonic() < deadline:
                    if self.cancel_event is not None and self.cancel_event.is_set():
                        raise InterruptedError("Replay cancelled by caller")
                    if process.poll() is not None:
                        raise RuntimeError(f"Application exited during startup: {process.returncode}")
                    try:
                        client.connect(host, port, timeout=min(0.5, max(0.01, deadline-time.monotonic())))
                        break
                    except OSError as exc:
                        last_error = str(exc)
                        time.sleep(min(0.05, max(0, deadline-time.monotonic())))
                else:
                    raise TimeoutError(f"Application startup timed out: {last_error}")
                activation = _activate_owned_macos_application(process, executable, deadline)
                if activation is not None:
                    emit(f"已激活本会话的 macOS 测试实例，PID {process.pid}")
                response = client.request("journal.replay.begin", {
                    "replayId": replay_id, "resume": True, "windowProfile": WINDOW_PROFILE},
                    timeout=min(3.0, max(0.01, deadline-time.monotonic())))
                if response.get("status") != "ready" or response.get("replayId") != replay_id:
                    raise RuntimeError("Application startup identity/readiness mismatch")
                handshake = True
            else:
                if not port:
                    raise ValueError("Attaching to an existing app requires --port")
                client.connect(host, port)
                # Existing instances enter the same application-owned replay
                # lifecycle as GUI playback, including its restart/ready barrier.
                client.begin_replay(timeout=startup_timeout, window_profile=WINDOW_PROFILE)
                handshake = True
            actual_window = wait_for_window(client)
            paths = client.paths()
            output = JournalSessionRepository(playbacks=paths["playbacks"]).create_playback(output)
            output_ready = True
            emit("Journal 已连接；开始执行文件（不再请求应用重启）")
            result = JournalRunner(client, comparisons=comparisons).run(
                document, output_dir=output, progress=progress, validate=validate,
                cancel_event=self.cancel_event)
            if environment_overrides is not None:
                result["environmentOverrides"] = dict(environment_overrides)
            result["windowProfile"] = WINDOW_PROFILE
            result["actualWindow"] = actual_window
            if activation is not None:
                result["applicationActivation"] = activation
            if handshake:
                client.end_replay()
                handshake = False
        except Exception as exc:
            result.update(success=False, acceptance="failed", error=str(exc))
            emit(f"测试失败：{exc}")
        finally:
            if handshake:
                try:
                    client.end_replay()
                except Exception as exc:
                    result.setdefault("cleanupErrors", []).append(str(exc))
            client.disconnect()
            if process is not None:
                result["applicationExitCode"] = process.poll()
                if process.returncode is not None:
                    result.update(success=False, acceptance="failed",
                                  error=f"Test application exited unexpectedly: {process.returncode}")
                elif not keep_app:
                    # Only this session's child process. Never search/kill user PIDs.
                    try:
                        process.terminate()
                        process.wait(timeout=5)
                    except (OSError, subprocess.TimeoutExpired) as exc:
                        result.setdefault("cleanupErrors", []).append(str(exc))
                result["applicationKeptOpen"] = process.poll() is None
            if not output_ready:
                try:
                    if output is None:
                        output = Path(tempfile.mkdtemp(prefix="journal-startup-failed-"))
                    else:
                        output.mkdir(parents=True, exist_ok=False)
                except OSError as exc:
                    # Do not overwrite an existing/partially written result. Keep
                    # diagnostics even when the chosen destination is unavailable.
                    result["outputError"] = f"{output}: {exc}"
                    output = Path(tempfile.mkdtemp(prefix="journal-startup-failed-"))
                emit(f"测试未正常完成；诊断保存到：{output}")
            if stream:
                stream.close()
                # Copy through an independent descriptor: a retained child may
                # still be writing, so never seek its stdout file descriptor.
                shutil.copyfile(stream.name, output / "application.log")
                if result.get("applicationKeptOpen"):
                    result["applicationLiveLogPath"] = stream.name
                    emit(f"测试实例仍在运行；后续日志：{stream.name}")
                else:
                    # Windows descendants can release an inherited log handle
                    # shortly after the owned application has exited.
                    deadline = time.monotonic() + 5
                    while True:
                        try:
                            Path(stream.name).unlink()
                            break
                        except OSError as exc:
                            if (getattr(exc, "winerror", None) == 32 and
                                    time.monotonic() < deadline):
                                time.sleep(0.02)
                                continue
                            result.setdefault("cleanupErrors", []).append(
                                f"Startup log cleanup: {exc}")
                            result["applicationLiveLogPath"] = stream.name
                            break
                result["applicationLogPath"] = str(output / "application.log")
                # Surface diagnostics without interpreting business data or
                # silently changing the caller's declared verification criteria.
                with (output / "application.log").open("rb") as app_log:
                    result["applicationErrorLines"] = sum(b"[error]" in line.lower() for line in app_log)
                if result["applicationErrorLines"]:
                    emit(f"应用日志有 {result['applicationErrorLines']} 行 error；请检查 {result['applicationLogPath']}")
            if result.get("cleanupErrors"):
                result.update(success=False, acceptance="failed")
            result["sessionPath"] = str(output)
            result["outputPath"] = str(output)
            if application:
                result["applicationPath"] = str(Path(application).resolve())
            if paths is not None:
                result["journalPaths"] = paths
            result["port"] = port
            finish_result(result)
            if paths is not None:
                try:
                    JournalSessionRepository(playbacks=paths['playbacks']).remember_result(output)
                except OSError as exc:
                    emit(f'无法记录最近结果入口：{exc}')
            emit(f"执行结果：{result['acceptance']}；报告：{result['reportPath']}")
        return result
