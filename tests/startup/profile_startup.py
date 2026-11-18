"""Collect Qt's QML creation/binding trace from the real macOS application.

Use the standard preset build first. GP_STARTUP_PROFILE enables the application's
opt-in profiling services and native library markers only for this owned child.
This recording has profiler overhead and is not a normal launch benchmark.
"""

import argparse
import json
import os
from pathlib import Path
import selectors
import socket
import subprocess
import sys
import time
import xml.etree.ElementTree as ET


def available_port():
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        return reservation.getsockname()[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--application", type=Path, required=True)
    parser.add_argument("--qmlprofiler", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=60)
    args = parser.parse_args()
    if sys.platform != "darwin":
        parser.error("This pipe observer currently supports macOS only")
    application, profiler = args.application.resolve(), args.qmlprofiler.resolve()
    if not application.is_file() or not profiler.is_file() or args.timeout <= 0:
        parser.error("Use existing executables and a positive timeout")
    running = subprocess.check_output(["ps", "-axo", "pid=,comm="], text=True)
    if any(line.strip().split(None, 1)[-1] == str(application)
           for line in running.splitlines()):
        parser.error("Close the existing application before recording")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    trace = output / "startup.qtd"
    command = [str(profiler), "--interactive", "--port", str(available_port()),
               "--include", "creating,compiling,binding,handlingsignal,javascript",
               "--output", str(output / "exit.qtd"), str(application),
               "--ghost-port", str(available_port())]
    environment = os.environ.copy()
    environment.update(GP_STARTUP_PROFILE="1", GP_LOG_CONSOLE="1")
    for key in ("GPLATFORM_JOURNAL_SESSION", "GPLATFORM_JOURNAL_PRINTER_FIXTURE"):
        environment.pop(key, None)
    result = {"command": command, "ready": False, "traceFlushed": False,
              "normalBenchmark": False}
    deadline = time.monotonic() + args.timeout
    process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, env=environment)
    with (output / "application.log").open("wb") as log, selectors.DefaultSelector() as selector:
        selector.register(process.stdout, selectors.EVENT_READ)
        pending = b""
        try:
            while time.monotonic() < deadline and process.poll() is None:
                for key, _ in selector.select(0.1):
                    chunk = os.read(key.fileobj.fileno(), 65536)
                    log.write(chunk)
                    log.flush()
                    pending += chunk
                    while b"\n" in pending:
                        line, pending = pending.split(b"\n", 1)
                        if b"[STARTUP TIMING] Application ready -" in line and not result["ready"]:
                            result["ready"] = True
                            process.stdin.write(("f " + str(trace) + "\n").encode())
                            process.stdin.flush()
                if result["ready"] and trace.exists():
                    try:
                        tree = ET.parse(trace)
                        if tree.findall(".//range"):
                            result["traceFlushed"] = True
                            break
                    except ET.ParseError:
                        pass
            if not result["traceFlushed"]:
                result["error"] = "No complete nonempty QML trace before timeout/process exit"
        except Exception as error:
            result["error"] = str(error)
        finally:
            if process.poll() is None:
                try:
                    # qmlprofiler owns the application and documents q as
                    # terminating its target. Never address other user PIDs.
                    process.stdin.write(b"q\n")
                    process.stdin.flush()
                    process.wait(timeout=10)
                except (OSError, subprocess.TimeoutExpired):
                    process.terminate()
                    process.wait(timeout=5)
            log.write(process.stdout.read())
            process.stdout.close()
            process.stdin.close()
    result["exitCode"] = process.returncode
    result["success"] = result["ready"] and result["traceFlushed"] and not result.get("error")
    (output / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    return 0 if result["success"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
