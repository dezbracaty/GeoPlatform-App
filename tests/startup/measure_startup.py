"""Measure the real application's existing Ready barrier without replay mode.

Run after a standard preset configure/build. For a post-reboot sample, run this
before opening GPlatform and use --runs 1 --label post-reboot-first-launch.
This tool never clears system caches or claims that a fresh process has cold IO.
Only child processes created by this tool are terminated.
"""

import argparse
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import selectors
import socket
import statistics
import subprocess
import time


TIMING = re.compile(r"\[STARTUP TIMING\] (.*?) - elapsed: (\d+)ms, total: (\d+)ms")
STAGE = re.compile(r"\[STARTUP\] stage=(\w+) elapsedMs=(\d+)")


def running_instances(application):
    output = subprocess.check_output(["ps", "-axo", "pid=,comm="], text=True)
    return [line.strip() for line in output.splitlines()
            if len(line.strip().split(None, 1)) == 2
            and line.strip().split(None, 1)[1] == str(application)]


def measure(application, directory, timeout, dyld_statistics):
    instances = running_instances(application)
    if instances:
        raise RuntimeError("Close the existing application first: " + "; ".join(instances))
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    environment = os.environ.copy()
    # Keep normal saved presets, route and startup behavior; replay mode and
    # JOURNAL_SESSION would change those and invalidate this baseline.
    for key in ("GPLATFORM_JOURNAL_SESSION", "GPLATFORM_JOURNAL_PRINTER_FIXTURE"):
        environment.pop(key, None)
    environment["GP_LOG_CONSOLE"] = "1"
    if dyld_statistics:
        environment["DYLD_PRINT_STATISTICS"] = "1"
    result = {"port": port, "checkpointsMs": {}, "stagesMs": {}, "presetDetailsMs": {},
              "applicationStates": []}
    directory.mkdir()
    command = [str(application), "--ghost-port", str(port)]
    result["command"] = command
    process = None
    with (directory / "application.log").open("wb") as log, selectors.DefaultSelector() as selector:
        launched_wall = time.time_ns() / 1e9
        launched = time.monotonic()
        result["launchedAt"] = datetime.fromtimestamp(launched_wall).isoformat()
        try:
            process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                       env=environment)
            result["pid"] = process.pid
            selector.register(process.stdout, selectors.EVENT_READ)
            pending = b""
            ready = False
            deadline = launched + timeout
            while not ready:
                if time.monotonic() >= deadline:
                    raise TimeoutError("Application did not reach its real Ready barrier")
                if process.poll() is not None:
                    raise RuntimeError(f"Application exited before Ready: {process.returncode}")
                for key, _ in selector.select(min(0.1, max(0, deadline - time.monotonic()))):
                    chunk = os.read(key.fileobj.fileno(), 65536)
                    log.write(chunk)
                    log.flush()
                    pending += chunk
                    while b"\n" in pending:
                        raw, pending = pending.split(b"\n", 1)
                        line = raw.decode("utf-8", errors="replace")
                        match = TIMING.search(line)
                        if match:
                            name, _, total = match.groups()
                            result["checkpointsMs"][name] = int(total)
                            if name == "Log system initialized":
                                result["beforeMainObservedUpperBoundMs"] = round(
                                    (time.monotonic() - launched) * 1000 - int(total), 3)
                            if name == "Application ready":
                                result["launchToReadyObservedMs"] = round((time.monotonic() - launched) * 1000, 3)
                                ready = True
                        match = STAGE.search(line)
                        if match:
                            result["stagesMs"][match[1]] = int(match[2])
                        match = re.search(r"\[STARTUP PRESETS\] backend initialized elapsedMs=(\d+)", line)
                        if match:
                            result["presetDetailsMs"]["backendInitialization"] = int(match[1])
                        match = re.search(r"\[STARTUP PRESETS\] configuration restored totalMs=(\d+)", line)
                        if match:
                            result["presetDetailsMs"]["backendAndConfiguration"] = int(match[1])
                        match = re.search(r"\[STARTUP STATE\] applicationState=(\d+) elapsedMs=(\d+)", line)
                        if match:
                            result["applicationStates"].append({"state": int(match[1]),
                                                                "elapsedMs": int(match[2])})
                        if "[STARTUP] failed" in line:
                            raise RuntimeError(line)
            stages = result["stagesMs"]
            names = ("Presets", "Modules", "Services", "UiShell", "Scene", "FirstFrame", "Ready")
            result["stageDurationsMs"] = {
                f"{left}->{right}": stages[right] - stages[left]
                for left, right in zip(names, names[1:]) if left in stages and right in stages}
            result["success"] = True
        except Exception as error:
            result.update(success=False, error=str(error))
        finally:
            if process is not None:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)
                        result.update(success=False, cleanupError="Owned child required SIGKILL")
                result["exitCode"] = process.returncode
                log.write(process.stdout.read())
                process.stdout.close()
    (directory / "result.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--application", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--label", choices=("process-restart", "post-reboot-first-launch"),
                        default="process-restart")
    parser.add_argument("--timeout", type=float, default=60)
    parser.add_argument("--dyld-statistics", action="store_true")
    args = parser.parse_args()
    if platform.system() != "Darwin":
        parser.error("This launch observer currently supports macOS only")
    application = args.application.resolve()
    if not application.is_file() or args.runs < 1 or args.timeout <= 0:
        parser.error("Use an existing executable, positive run count and timeout")
    if args.label == "post-reboot-first-launch" and args.runs != 1:
        parser.error("A post-reboot first launch is one sample; collect repeats separately")
    if running_instances(application):
        parser.error("Close the existing application before measuring")
    args.output.mkdir(parents=True, exist_ok=False)
    report = {
        "application": str(application), "label": args.label,
        "labelIsOperatorSupplied": True, "systemCachesCleared": False,
        "beforeMainTiming": "Monotonic parent observation minus first internal checkpoint; includes process creation and console delivery latency",
        "readyTiming": "Monotonic parent observation; includes console delivery latency",
        "cleanup": "SIGTERM only for owned measurement children, after Ready",
        "platform": platform.platform(), "machine": platform.machine(),
        "bootTime": subprocess.check_output(["sysctl", "-n", "kern.boottime"], text=True).strip(),
        "uptime": subprocess.check_output(["uptime"], text=True).strip(),
        "dyldStatistics": args.dyld_statistics, "runs": [],
    }
    for index in range(args.runs):
        result = measure(application, args.output / f"run-{index + 1:02}", args.timeout,
                         args.dyld_statistics)
        report["runs"].append(result)
        print(json.dumps({"run": index + 1, "success": result["success"],
                          "launchToReadyObservedMs": result.get("launchToReadyObservedMs"),
                          "error": result.get("error")}), flush=True)
        (args.output / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
        if not result["success"]:
            return 1
        if index + 1 < args.runs:
            time.sleep(2)
    # Read the executable only AFTER sampling, so hashing cannot prewarm it.
    digest = hashlib.sha256()
    with application.open("rb") as binary:
        for chunk in iter(lambda: binary.read(1024 * 1024), b""):
            digest.update(chunk)
    report["applicationSha256AfterSampling"] = digest.hexdigest()
    values = [run["launchToReadyObservedMs"] for run in report["runs"]]
    report["summaryMs"] = {"median": statistics.median(values), "min": min(values), "max": max(values)}
    (args.output / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps(report["summaryMs"]), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
