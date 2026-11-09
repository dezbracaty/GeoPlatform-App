import base64
import hashlib
import importlib.util
import json
import sys
import tempfile
import unittest
import os
import subprocess
import time
import socket
import shutil
from html.parser import HTMLParser
from urllib.parse import unquote, urlparse
from unittest.mock import Mock, patch
from pathlib import Path
from contextlib import contextmanager


TOOLS = Path(__file__).resolve().parents[2] / "tools/journal"
sys.path.insert(0, str(TOOLS))

from journal_runner import (  # noqa: E402
    DB_SERIALIZATION_FORMAT,
    JournalFormatError,
    JournalRunner,
    compare_serialized_data,
    load_journal,
    write_journal,
    write_report,
    write_result,
)
from journalsystem.journalsystem import JournalSystem  # noqa: E402
from session_repository import JournalSessionRepository  # noqa: E402
from journal_display_profile import WINDOW_PROFILE  # noqa: E402


class FakeSocket:
    def __init__(self, chunks=()):
        self.chunks = list(chunks)
        self.sent = bytearray()
        self.timeout = None

    def recv(self, _size):
        return self.chunks.pop(0) if self.chunks else b""

    def sendall(self, data):
        self.sent.extend(data)

    def gettimeout(self):
        return self.timeout

    def settimeout(self, timeout):
        self.timeout = timeout

    def close(self):
        pass


def frame(document):
    payload = json.dumps(document, separators=(",", ":")).encode("utf-8")
    return str(len(payload)).encode("ascii") + b":" + payload


class RunnerClient:
    def __init__(self, root):
        self.client = object()
        self.root = Path(root)
        self.recording_session = self.root / "recordings" / "session_20260904_120000_123"
        self.playbacks = self.root / "playbacks"
        self.calls = []
        self.recording = False
        self.recording_id = "test-recording"
        self.db_data = {
            "dbInstanceId": "42",
            "serializationFormat": DB_SERIALIZATION_FORMAT,
            "serialized": base64.b64encode(b"complete-db-state-v1").decode("ascii"),
        }

    @staticmethod
    def normalize_db_id(value):
        return JournalSystem.normalize_db_id(value)

    @staticmethod
    def entry(kind, payload, at_ms=0, sequence=0):
        return JournalSystem.entry(kind, payload, at_ms, sequence)

    def window_info(self, window):
        return {"objectName": "gplatform.main", "logicalSize": [1382, 954],
                "devicePixelRatio": 1, "availableLogicalSize": [1920, 1080]}

    def start_recording(self, record_mouse_moves=True, *, window_profile=None):
        self.calls.append(("start", record_mouse_moves))
        self.recording = True
        return {"recordingId": self.recording_id}

    def recording_status(self):
        return {"recording": self.recording, "recordingId": self.recording_id}

    def stop_recording(self):
        self.calls.append("stop")
        self.recording = False
        return {
            "entries": [self.entry(
                "action", {"code": "existing.action", "params": {}}, 5, 1)],
        }

    def serialize_db(self, db_id):
        self.calls.append(("db", str(db_id)))
        return dict(self.db_data, atMs=10, sequence=2)

    def paths(self):
        return {"recordings": str(self.root / "recordings"),
                "playbacks": str(self.playbacks)}

    def begin_replay(self, timeout=30.0, *, window_profile=None):
        self.calls.append("begin")

    def end_replay(self):
        self.calls.append("end")

    def execute_entry(self, entry):
        self.calls.append(("execute", entry["kind"]))
        return {"entry": entry}

    def invoke_action(self, code, params=None):
        entry = self.entry("action", {"code": code, "params": params or {}})
        self.execute_entry(entry)
        return {"recorded": True, "entry": entry}


@contextmanager
def restart_probe_directory():
    with tempfile.TemporaryDirectory() as temporary:
        try:
            yield temporary
        finally:
            # The restarted Windows child inherits this handle. Its final
            # evidence event can arrive just before OS process teardown.
            # Require actual handle release; do not ignore cleanup failures.
            if os.name == "nt":
                log = Path(temporary) / "application.log"
                deadline = time.monotonic() + 5
                while log.exists():
                    try:
                        log.unlink()
                    except PermissionError:
                        if time.monotonic() >= deadline:
                            raise
                        time.sleep(0.02)
                    else:
                        break


class JournalPythonClientCheck(unittest.TestCase):
    def test_macos_activation_waits_for_owned_registration_without_launching(self):
        from journal_execution_session import _activate_owned_macos_application
        executable = Path("/Applications/Test App.app/Contents/MacOS/Test").resolve()
        process = Mock(pid=123)
        process.poll.return_value = None
        identity = ('{ "CFBundleExecutablePath"="/Applications/Test App.app/Contents/MacOS/Test", '
                    '"LSASN"=ASN:0x0-0x123:, "LSBundlePath"="/Applications/Test App.app", "pid"=123 }\n---\n')
        replies = ["", "ASN:0x0-0x123:", identity, "", "ASN:0x0-0x123:", identity]
        commands = []
        def run(command, **kwargs):
            commands.append(command)
            return subprocess.CompletedProcess(command, 0, stdout=replies.pop(0))
        with patch("journal_execution_session.sys.platform", "darwin"), \
                patch("journal_execution_session.subprocess.run", side_effect=run), \
                patch("journal_execution_session.subprocess.Popen") as launch, \
                patch("journal_execution_session.time.sleep"):
            result = _activate_owned_macos_application(process, executable, time.monotonic() + 10)
        self.assertEqual(result["pid"], 123)
        self.assertEqual(result["mechanism"], "LaunchServices.setfront")
        self.assertEqual([c for c in commands if "setfront" in c],
                         [["/usr/bin/lsappinfo", "setfront", "#123"]])
        self.assertTrue(all(c[0] == "/usr/bin/lsappinfo" for c in commands))
        self.assertTrue(all("-all" in c for c in commands if "info" in c))
        launch.assert_not_called()


    def test_macos_activation_rejects_ambiguous_or_wrong_identity_before_activation(self):
        from journal_execution_session import _activate_owned_macos_application
        executable = Path("/Applications/Test.app/Contents/MacOS/Test").resolve()
        process = Mock(pid=123)
        process.poll.return_value = None
        cases = [
            ["ASN:0x0-0x123: ASN:0x0-0x456:"],
            ["ASN:0x0-0x123:", '"pid"=456\n"LSBundlePath"="/Applications/Test.app"\n"CFBundleExecutablePath"="/Applications/Test.app/Contents/MacOS/Test"'],
            ["ASN:0x0-0x123:", '"pid"=123\n"LSBundlePath"="/Applications/Other.app"\n"CFBundleExecutablePath"="/Applications/Test.app/Contents/MacOS/Test"'],
            ["ASN:0x0-0x123:", '"pid"=123\n"LSBundlePath"="/Applications/Test.app"\n"CFBundleExecutablePath"="/Applications/Test.app/Contents/MacOS/Other"'],
            ["ASN:0x0-0x123:", '"pid"=123'],
            ["unexpected registration output"],
        ]
        for replies in cases:
            with self.subTest(replies=replies):
                def run(command, **kwargs):
                    self.assertNotIn("setfront", command)
                    return subprocess.CompletedProcess(command, 0, stdout=replies.pop(0))
                with patch("journal_execution_session.sys.platform", "darwin"), \
                        patch("journal_execution_session.subprocess.run", side_effect=run):
                    with self.assertRaises(RuntimeError):
                        _activate_owned_macos_application(process, executable, time.monotonic() + 10)


    def test_macos_activation_child_exit_or_timeout_never_launches_replacement(self):
        from journal_execution_session import _activate_owned_macos_application
        executable = Path("/Applications/Test.app/Contents/MacOS/Test").resolve()
        identity = ('"pid"=123\n"LSBundlePath"="/Applications/Test.app"\n'
                    '"CFBundleExecutablePath"="/Applications/Test.app/Contents/MacOS/Test"\n')
        for polls in ([5], [None, 5], [None, None, 5]):
            with self.subTest(polls=polls):
                process = Mock(pid=123)
                process.poll.side_effect = polls
                commands = []
                def run(command, **kwargs):
                    commands.append(command)
                    output = identity if "info" in command else "ASN:0x0-0x123:" if "find" in command else ""
                    return subprocess.CompletedProcess(command, 0, stdout=output)
                with patch("journal_execution_session.sys.platform", "darwin"), \
                        patch("journal_execution_session.subprocess.run", side_effect=run):
                    with self.assertRaises(RuntimeError):
                        _activate_owned_macos_application(process, executable, time.monotonic() + 10)
                self.assertTrue(all(c[0] == "/usr/bin/lsappinfo" for c in commands))
        process = Mock(pid=123)
        process.poll.return_value = None
        with patch("journal_execution_session.sys.platform", "darwin"), \
                patch("journal_execution_session.subprocess.run") as run:
            with self.assertRaises(TimeoutError):
                _activate_owned_macos_application(process, executable, time.monotonic() - 1)
            run.assert_not_called()


    def test_activation_leaves_other_platforms_and_unbundled_executables_unchanged(self):
        from journal_execution_session import _activate_owned_macos_application
        for platform, executable in [("linux", "/tmp/Test.app/Contents/MacOS/Test"),
                                     ("win32", "/tmp/Test.app/Contents/MacOS/Test"),
                                     ("darwin", "/tmp/Test")]:
            with self.subTest(platform=platform):
                with patch("journal_execution_session.sys.platform", platform), \
                        patch("journal_execution_session.subprocess.run") as run:
                    self.assertIsNone(_activate_owned_macos_application(Mock(), Path(executable), 0))
                    run.assert_not_called()


    def test_macos_activation_rechecks_unique_identity_after_setfront(self):
        from journal_execution_session import _activate_owned_macos_application
        executable = Path("/Applications/Test.app/Contents/MacOS/Test").resolve()
        process = Mock(pid=123)
        process.poll.return_value = None
        identity = ('"pid"=123\n"LSBundlePath"="/Applications/Test.app"\n'
                    '"CFBundleExecutablePath"="/Applications/Test.app/Contents/MacOS/Test"\n')
        for after in ("", "ASN:0x0-0x123: ASN:0x0-0x456:"):
            replies = ["ASN:0x0-0x123:", identity, "", after]
            with self.subTest(after=after), \
                    patch("journal_execution_session.sys.platform", "darwin"), \
                    patch("journal_execution_session.subprocess.run", side_effect=lambda command, **kwargs:
                          subprocess.CompletedProcess(command, 0, stdout=replies.pop(0))):
                with self.assertRaises(RuntimeError):
                    _activate_owned_macos_application(process, executable, time.monotonic() + 10)


    def test_execution_retains_activation_evidence_and_original_readiness_handshake(self):
        from journal_execution_session import JournalExecutionSession
        with tempfile.TemporaryDirectory() as temporary:
            client = RunnerClient(temporary)
            journal = write_journal(Path(temporary) / "source.json", [client.entry("action", {"code": "existing.action"})])
            client.connect = Mock()
            client.disconnect = Mock()
            client.request = Mock(side_effect=lambda method, params, **kwargs:
                                  {"status": "ready", "replayId": params["replayId"]})
            process = Mock(returncode=None)
            process.poll.return_value = None
            process.wait.side_effect = lambda **kwargs: setattr(process.poll, "return_value", -15)
            activation = {"mechanism": "LaunchServices.setfront", "pid": 123}
            with patch("journal_execution_session.JournalSystem", return_value=client), \
                    patch("journal_execution_session.subprocess.Popen", return_value=process), \
                    patch("journal_execution_session._activate_owned_macos_application", return_value=activation):
                result = JournalExecutionSession().run(journal, application=sys.executable)
            self.assertEqual(result["applicationActivation"], activation)
            saved = json.loads((Path(result["outputPath"]) / "execution_result.json").read_text(encoding="utf-8"))
            self.assertEqual(saved["applicationActivation"], activation)
            self.assertEqual(client.request.call_args.args[0], "journal.replay.begin")
            self.assertTrue(result["success"])


    def test_execution_uses_one_source_read_even_if_file_changes_during_connect(self):
        from journal_execution_session import JournalExecutionSession
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            client = RunnerClient(root)
            journal = write_journal(root / "source.json", [
                client.entry("action", {"code": "original"})])
            original = journal.read_bytes()
            def connect(*args):
                write_journal(journal, [client.entry("action", {"code": "changed"})])
            client.connect = Mock(side_effect=connect)
            client.disconnect = Mock()
            client.execute_entry = Mock(return_value={})
            read_bytes = Path.read_bytes
            reads = []
            def read(path):
                if path == journal:
                    reads.append(path)
                return read_bytes(path)
            with patch("journal_execution_session.JournalSystem", return_value=client), \
                    patch.object(Path, "read_bytes", read):
                summary = JournalExecutionSession().run(journal, port=9999)
            self.assertEqual(reads, [journal])
            self.assertEqual(client.execute_entry.call_args.args[0]["payload"]["code"], "original")
            saved = json.loads((Path(summary["outputPath"]) / "execution_result.json").read_text(encoding="utf-8"))
            self.assertTrue(saved["success"])
            self.assertEqual(saved["journalSha256"], hashlib.sha256(original).hexdigest())
            self.assertNotEqual(saved["journalSha256"], hashlib.sha256(journal.read_bytes()).hexdigest())
            self.assertFalse((Path(summary["outputPath"]) / "session_result.json").exists())

    def test_cleanup_failure_has_one_consistent_result_and_report(self):
        from journal_execution_session import JournalExecutionSession
        with tempfile.TemporaryDirectory() as temporary:
            client = RunnerClient(temporary)
            journal = write_journal(Path(temporary) / "source.json", [
                client.entry("action", {"code": "existing.action"})])
            client.connect = Mock()
            client.disconnect = Mock()
            client.request = Mock(side_effect=lambda method, params, **kwargs:
                                  {"status": "ready", "replayId": params["replayId"]})
            client.end_replay = Mock(side_effect=ConnectionError("cleanup failed"))
            process = Mock(returncode=None)
            process.poll.return_value = None
            process.wait.side_effect = lambda **kwargs: setattr(process.poll, "return_value", -15)
            with patch("journal_execution_session.JournalSystem", return_value=client), \
                    patch("journal_execution_session.subprocess.Popen", return_value=process):
                summary = JournalExecutionSession().run(journal, application=sys.executable)
            output = Path(summary["outputPath"])
            saved = json.loads((output / "execution_result.json").read_text(encoding="utf-8"))
            self.assertEqual(saved["results"][0]["status"], "passed")
            self.assertFalse(summary["success"])
            self.assertFalse(saved["success"])
            self.assertEqual(saved["acceptance"], "failed")
            report = (output / "verification_report.html").read_text(encoding="utf-8")
            self.assertIn("Execution: failed", report)
            self.assertIn("cleanup failed", report)
            self.assertFalse((output / "session_result.json").exists())

    def test_startup_log_cleanup_waits_for_sharing_but_preserves_real_failures(self):
        from journal_execution_session import JournalExecutionSession
        for failures, winerror, expected_calls, success in [
                (1, 32, 2, True), (100, 32, 2, False), (1, 5, 1, False)]:
            with self.subTest(failures=failures, winerror=winerror), tempfile.TemporaryDirectory() as temporary:
                client = RunnerClient(temporary)
                journal = write_journal(Path(temporary) / "source.json", [
                    client.entry("action", {"code": "existing.action"})])
                client.connect = Mock()
                client.disconnect = Mock()
                client.request = Mock(side_effect=lambda method, params, **kwargs:
                                      {"status": "ready", "replayId": params["replayId"]})
                process = Mock(returncode=None)
                process.poll.return_value = None
                process.wait.side_effect = lambda **kwargs: setattr(process.poll, "return_value", -15)
                clock = [0]
                logs = []
                unlink = Path.unlink
                def remove_log(path, *args, **kwargs):
                    logs.append(path)
                    if len(logs) <= failures:
                        error = PermissionError("startup log locked")
                        error.winerror = winerror
                        raise error
                    return unlink(path, *args, **kwargs)
                with patch("journal_execution_session.JournalSystem", return_value=client), \
                        patch("journal_execution_session.subprocess.Popen", return_value=process), \
                        patch("journal_execution_session.time.monotonic", side_effect=lambda: clock[0]), \
                        patch("journal_execution_session.time.sleep", side_effect=lambda _: clock.__setitem__(0, 6)), \
                        patch.object(Path, "unlink", remove_log):
                    summary = JournalExecutionSession().run(journal, application=sys.executable)
                try:
                    self.assertEqual(len(logs), expected_calls)
                    self.assertEqual(summary["success"], success)
                    saved = json.loads((Path(summary["outputPath"]) / "execution_result.json").read_text(encoding="utf-8"))
                    self.assertEqual(saved["success"], success)
                    if success:
                        self.assertFalse(logs[0].exists())
                    else:
                        self.assertEqual(saved["acceptance"], "failed")
                        self.assertIn("startup log locked", " ".join(saved["cleanupErrors"]))
                        self.assertTrue(Path(summary["applicationLiveLogPath"]).is_file())
                        self.assertIn("startup log locked", Path(summary["reportPath"]).read_text(encoding="utf-8"))
                finally:
                    if logs[0].exists():
                        logs[0].unlink()

    def test_missing_input_target_fails_without_a_ui_assertion(self):
        with tempfile.TemporaryDirectory() as temporary:
            client = RunnerClient(temporary)
            client.execute_entry = Mock(side_effect=RuntimeError("UI target not found: missing"))
            journal = write_journal(Path(temporary) / "source.json", [
                client.entry("input.mouse", {"target": "missing", "event": "press"}, sequence=1),
                client.entry("action", {"code": "must.not.execute"}, sequence=2)])
            result = JournalRunner(client).play(journal, restart=False)
            self.assertFalse(result["success"])
            self.assertEqual(result["completedEntries"], 1)
            self.assertIn("not found", result["results"][0]["message"])
            client.execute_entry.assert_called_once()

    def test_steps_keep_source_snapshot_and_persist_cleanup_failure(self):
        with tempfile.TemporaryDirectory() as temporary:
            client = RunnerClient(temporary)
            runner = JournalRunner(client)
            journal = write_journal(Path(temporary) / "source.json", [
                client.entry("action", {"code": "first"}, sequence=1),
                client.entry("action", {"code": "second"}, sequence=2)])
            source_hash = hashlib.sha256(journal.read_bytes()).hexdigest()
            client.execute_entry = Mock(return_value={})
            first = runner.step_journal(journal)
            self.assertFalse(first["complete"])
            write_journal(journal, [client.entry("action", {"code": "changed"})])
            with patch.object(client, "end_replay", side_effect=RuntimeError("step cleanup failed")):
                second = runner.step_journal(journal)
            self.assertTrue(second["complete"])
            self.assertIsNone(runner._step_state)
            self.assertEqual([call.args[0]["payload"]["code"]
                              for call in client.execute_entry.call_args_list], ["first", "second"])
            summary = second["summary"]
            saved = json.loads((Path(summary["outputPath"]) / "execution_result.json").read_text(encoding="utf-8"))
            self.assertFalse(saved["success"])
            self.assertEqual(saved["completedEntries"], 2)
            self.assertEqual(saved["journalSha256"], source_hash)
            self.assertIn("step cleanup failed", Path(summary["reportPath"]).read_text(encoding="utf-8"))

    def test_repository_has_one_layout_and_one_playback_naming_rule(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            repository = JournalSessionRepository(root / "recordings", root / "playbacks", root / "cases")
            old = root / "recordings/session_old"
            write_journal(old / "Journal.json", [JournalSystem.entry("action", {"code": "x"})])
            self.assertIsNone(repository.describe_session(old, "录制")["journal_file"])
            regular = root / "recordings/session_current"
            write_journal(regular / "events/Journal.json", [JournalSystem.entry("action", {"code": "x"})])
            repository.save_metadata(regular, {"name": "case", "description": "same metadata"})
            listed = next(item for item in repository.list_sessions() if item["path"] == regular)
            external = repository.describe_session(regular, "外部")
            for key in ("name", "journal_file", "mtime", "metadata", "error"):
                self.assertEqual(listed[key], external[key])
            for _ in range(2):
                output = repository.create_playback()
                self.assertRegex(output.name, r"^session_\d{8}_\d{6}_\d{6}_[a-f0-9]{6}$")
                self.assertTrue((output / "screenshots").is_dir())
                self.assertIsNone(repository.describe_session(output, "回放")["journal_file"])
            with self.assertRaises(FileExistsError):
                repository.create_playback(output)

    def test_screenshot_action_uses_session_directory_and_reports_artifact(self):
        with tempfile.TemporaryDirectory() as temporary:
            client = RunnerClient(temporary)
            entry = client.entry("action", {"code": "screenshot.capture",
                                          "params": {"filepath": "screenshots/all.png", "notify": False}})
            journal = write_journal(Path(temporary) / "case.json", [entry])
            def execute(dispatched):
                self.assertEqual(dispatched["kind"], "action")
                self.assertEqual(dispatched["payload"]["code"], "screenshot.capture")
                self.assertFalse(dispatched["payload"]["params"]["notify"])
                Path(dispatched["payload"]["params"]["filepath"]).write_bytes(b"test-image")
            client.execute_entry = Mock(side_effect=execute)
            result = JournalRunner(client).play(journal, restart=False)
            actual = (Path(result["outputPath"]) / "screenshots/all.png").resolve()
            self.assertEqual(actual.read_bytes(), b"test-image")
            self.assertEqual(result["results"][0]["evidence"], [actual])
            self.assertEqual(result["acceptance"], "not_verified")
            self.assertEqual(load_journal(journal)["entries"][0]["payload"]["params"]["filepath"],
                             "screenshots/all.png")
            self.assertIn('href="screenshots/all.png"', Path(result["reportPath"]).read_text(encoding="utf-8"))

    def test_file_import_resolves_relative_fixture_from_journal_directory(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            events = root / "scenario" / "events"
            fixture = root / "scenario" / "assets" / "cube.stl"
            fixture.parent.mkdir(parents=True)
            fixture.write_text("solid cube\nendsolid cube\n", encoding="utf-8")
            client = RunnerClient(temporary)
            journal = write_journal(events / "Journal.json", [client.entry(
                "action", {"code": "file.import",
                           "params": {"filePath": "../assets/cube.stl"}})])
            client.execute_entry = Mock(return_value={})

            result = JournalRunner(client).play(journal, restart=False)

            self.assertTrue(result["success"])
            dispatched = client.execute_entry.call_args.args[0]
            self.assertEqual(dispatched["payload"]["params"]["filePath"],
                             str(fixture.resolve()))
            self.assertEqual(load_journal(journal)["entries"][0]
                             ["payload"]["params"]["filePath"],
                             "../assets/cube.stl")

    def test_repository_replays_use_existing_portable_import_fixtures(self):
        # Guard the shipped catalog, rather than letting a developer's local
        # Downloads path survive until an end-to-end replay on another machine.
        repository = Path(__file__).resolve().parents[2]
        scenarios = sorted((repository / "tests/TestAssets/scenarios").glob("*/events/Journal.json"))
        self.assertTrue(scenarios)
        for scenario in scenarios:
            for entry in load_journal(scenario)["entries"]:
                payload = entry["payload"]
                if entry["kind"] != "action" or payload.get("code") != "file.import":
                    continue
                path = payload.get("params", {}).get("filePath", "")
                with self.subTest(scenario=scenario.parent.parent.name, path=path):
                    self.assertTrue(path)
                    self.assertFalse(Path(path).is_absolute() or path.startswith("/") or ":" in path or "\\" in path)
                    fixture = (scenario.parent / path).resolve()
                    self.assertTrue(fixture.is_relative_to(repository))
                    self.assertTrue(fixture.is_file(), f"missing fixture: {fixture}")

    def test_file_import_rejects_missing_relative_fixture_before_dispatch(self):
        with tempfile.TemporaryDirectory() as temporary:
            client = RunnerClient(temporary)
            journal = write_journal(Path(temporary) / "events" / "Journal.json", [
                client.entry("action", {"code": "file.import",
                             "params": {"filePath": "../assets/missing.stl"}})])
            client.execute_entry = Mock(return_value={})

            result = JournalRunner(client).play(journal, restart=False)

            self.assertEqual(result["acceptance"], "failed")
            self.assertIn("Import fixture does not exist",
                          result["results"][0]["message"])
            client.execute_entry.assert_not_called()

    def test_screenshot_action_missing_file_is_failure(self):
        with tempfile.TemporaryDirectory() as temporary:
            client = RunnerClient(temporary)
            journal = write_journal(Path(temporary) / "case.json", [client.entry(
                "action", {"code": "screenshot.capture", "params": {"filepath": "screenshots/all.png"}})])
            result = JournalRunner(client).play(journal, restart=False)
            self.assertEqual(result["acceptance"], "failed")
            self.assertIn("did not save an image", result["results"][0]["message"])

    def test_screenshot_action_does_not_accept_or_overwrite_stale_image(self):
        with tempfile.TemporaryDirectory() as temporary:
            client = RunnerClient(temporary)
            existing = Path(temporary) / "existing.png"
            existing.write_bytes(b"previous-evidence")
            journal = write_journal(Path(temporary) / "case.json", [client.entry(
                "action", {"code": "screenshot.capture", "params": {"filepath": str(existing)}})])
            result = JournalRunner(client).play(journal, restart=False)
            self.assertEqual(result["acceptance"], "failed")
            self.assertEqual(existing.read_bytes(), b"previous-evidence")
            self.assertNotIn(("execute", "action"), client.calls)

    def test_saved_json_validates_db_captures_data_and_does_not_restart(self):
        with tempfile.TemporaryDirectory() as temporary:
            client = RunnerClient(temporary)
            journal = write_journal(Path(temporary) / "case.json", [
                client.entry("verify.db", {"dbInstanceId": "42",
                             "serializationFormat": DB_SERIALIZATION_FORMAT,
                             "expectedSerialized": client.db_data["serialized"]}, sequence=1),
                client.entry("capture.db", {"dbInstanceId": "42"}, sequence=2)])
            source = journal.read_bytes()
            result = JournalRunner(client).play(journal, restart=False, output_dir=Path(temporary) / "pass")
            self.assertEqual(result["acceptance"], "passed")
            self.assertEqual(result["completedEntries"], 2)
            self.assertNotIn("begin", client.calls)
            self.assertNotIn("end", client.calls)
            raw = Path(result["results"][1]["evidence"][0]).read_bytes()
            self.assertEqual(raw, base64.b64decode(client.db_data["serialized"]))
            client.db_data["serialized"] = base64.b64encode(b"changed").decode("ascii")
            failed = JournalRunner(client).play(journal, restart=False, output_dir=Path(temporary) / "fail")
            self.assertEqual(failed["acceptance"], "failed")
            self.assertEqual(failed["completedEntries"], 1)
            self.assertTrue(Path(failed["results"][0]["evidence"][0]).exists())
            self.assertTrue((Path(failed["outputPath"]) / "execution_result.json").exists())
            for summary in (result, failed):
                output = Path(summary["outputPath"])
                self.assertFalse((output / "Journal.json").exists())
                saved = json.loads((output / "execution_result.json").read_text(encoding="utf-8"))
                self.assertEqual(saved["outputPath"], ".")
                self.assertEqual(saved["reportPath"], "verification_report.html")
                for step in saved["results"]:
                    for evidence in step["evidence"]:
                        self.assertFalse(Path(evidence).is_absolute())
                        self.assertTrue((output / evidence).exists())
                self.assertEqual(saved["journalPath"], str(journal.resolve()))
                self.assertEqual(saved["journalSha256"], hashlib.sha256(source).hexdigest())
            self.assertEqual(journal.read_bytes(), source)

    def test_result_references_survive_session_rename_move_and_copy(self):
        class Links(HTMLParser):
            def __init__(self):
                super().__init__()
                self.urls = []

            def handle_starttag(self, tag, attrs):
                if tag == "a":
                    self.urls.append(dict(attrs)["href"])

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            session = root / "session_original"
            (session / "screenshots").mkdir(parents=True)
            (session / "reports").mkdir()
            names = ["screenshots/实际 #1%.png", "screenshots/diff & result.png",
                     "reports/db_actual.bin", "reports/db_expected.bin", "reports/db_comparison.json"]
            for name in names:
                (session / name).write_bytes(b"fixture-evidence")
            (session / "application.log").write_text("application log")
            baseline = root / "外部基线 #1.png"
            baseline.write_bytes(b"external-baseline")
            journal = write_journal(root / "source.json", [JournalSystem.entry(
                "action", {"code": "existing.action"})])
            report = session / "verification_report.html"
            results = [{"index": 1, "kind": "verify.screenshot", "atMs": 0, "status": "passed",
                        "evidence": [session / name for name in names] + [baseline]}]
            summary = {"outputPath": str(session), "sessionPath": str(session),
                       "reportPath": str(report), "applicationLogPath": str(session / "application.log"),
                       "journalPath": str(journal), "results": results}
            write_report(report, dict(summary, success=True, acceptance="passed"))
            for filename in ("execution_result.json",):
                write_result(session / filename, summary)
            # Persistence must not mutate the live response used by GUI/CLI.
            self.assertEqual(summary["reportPath"], str(report))
            self.assertEqual(results[0]["evidence"][0], session / names[0])
            original_files = {name: (session / name).read_bytes() for name in (
                "verification_report.html", "execution_result.json")}

            def check(directory):
                self.assertFalse((directory / "Journal.json").exists())
                self.assertFalse((directory / baseline.name).exists())
                for filename, original in original_files.items():
                    self.assertEqual((directory / filename).read_bytes(), original)
                parser = Links()
                html = (directory / "verification_report.html").read_text(encoding="utf-8")
                parser.feed(html)
                self.assertIn("external: ", html)
                self.assertEqual(len(parser.urls), len(names) + 1)
                for url, name in zip(parser.urls[:-1], names):
                    parsed = urlparse(url)
                    self.assertEqual((parsed.scheme, parsed.query, parsed.fragment), ("", "", ""))
                    self.assertEqual(unquote(parsed.path), name)
                    self.assertTrue((directory / unquote(parsed.path)).is_file())
                self.assertEqual(parser.urls[-1], baseline.as_uri())
                for filename in ("execution_result.json",):
                    saved = json.loads((directory / filename).read_text(encoding="utf-8"))
                    self.assertEqual(saved["outputPath"], ".")
                    self.assertEqual(saved["sessionPath"], ".")
                    self.assertEqual(saved["journalPath"], str(journal))
                    self.assertEqual(saved["results"][0]["evidence"], names + [str(baseline)])
                    for key in ("reportPath", "applicationLogPath"):
                        self.assertFalse(Path(saved[key]).is_absolute())
                        self.assertTrue((directory / saved[key]).is_file())

            renamed = JournalSessionRepository().rename(session, "重命名 # 测试")
            check(renamed)
            moved = root / "moved" / renamed.name
            moved.parent.mkdir()
            shutil.move(str(renamed), moved)
            check(moved)
            copied = root / "copy" / moved.name
            shutil.copytree(moved, copied)
            check(copied)

    def test_persisted_relative_references_and_external_live_log_are_preserved(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            session = root / "session_result"
            session.mkdir()
            summary = {"outputPath": ".", "reportPath": "verification_report.html",
                       "applicationLiveLogPath": str(root / "live.log"),
                       "results": [{"evidence": ["screenshots/截图 #1.png"]}]}
            write_result(session / "execution_result.json", summary)
            self.assertEqual(json.loads((session / "execution_result.json").read_text(encoding="utf-8")), summary)

    def test_capture_only_is_not_acceptance_and_ui_assertions_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            client = RunnerClient(temporary)
            journal = write_journal(Path(temporary) / "capture.json", [
                client.entry("capture.db", {"dbInstanceId": "42"})])
            self.assertEqual(JournalRunner(client).play(journal, restart=False)["acceptance"], "not_verified")
            for expected, tolerance in (({"text": "old UI assertion"}, 0),):
                bad = write_journal(Path(temporary) / "bad.json", [client.entry(
                    "verify.ui", {"target": "slider", "expected": expected, "absoluteTolerance": tolerance})])
                with self.assertRaises(JournalFormatError):
                    load_journal(bad)

    def test_application_environment_only_comes_from_a_scenario_sidecar(self):
        from journal_execution_session import load_application_environment
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            scenario = root / "printer_case"
            events = scenario / "events"
            events.mkdir(parents=True)
            journal = write_journal(events / "Journal.json", [
                JournalSystem.entry("action", {"code": "existing.action"})])
            (scenario / "metadata.json").write_text(json.dumps({
                "applicationEnvironment": {
                    "GPLATFORM_JOURNAL_PRINTER_FIXTURE": "connected",
                }
            }), encoding="utf-8")
            self.assertEqual(load_application_environment(journal), {
                "GPLATFORM_JOURNAL_PRINTER_FIXTURE": "connected",
            })

            standalone = write_journal(root / "Journal.json", [
                JournalSystem.entry("action", {"code": "existing.action"})])
            self.assertEqual(load_application_environment(standalone), {})

            (scenario / "metadata.json").write_text(json.dumps({
                "applicationEnvironment": ["not", "an", "object"]
            }), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "must be an object"):
                load_application_environment(journal)

    def test_attached_session_enters_and_leaves_application_replay(self):
        from journal_execution_session import JournalExecutionSession
        with tempfile.TemporaryDirectory() as temporary:
            journal = write_journal(Path(temporary) / "case.json", [
                JournalSystem.entry("action", {"code": "existing.action"})])
            client = Mock()
            client.window_info.return_value = RunnerClient.window_info(None, {})
            client.paths.return_value = {"recordings": str(Path(temporary) / "recordings"),
                                         "playbacks": str(Path(temporary) / "playbacks")}
            with patch("journal_execution_session.JournalSystem", return_value=client), \
                    patch("journal_execution_session.subprocess.Popen") as launch, \
                    patch("journal_execution_session.JournalRunner") as runner:
                runner.return_value.run.return_value = {
                    "journalPath": str(journal), "journalSha256": hashlib.sha256(journal.read_bytes()).hexdigest(),
                    "totalEntries": 1, "outputPath": str(Path(temporary) / "run"),
                    "results": [{"index": 1, "atMs": 0, "kind": "action", "status": "passed"}]}
                result = JournalExecutionSession().run(journal, port=9999,
                    startup_timeout=45, output_dir=Path(temporary) / "run")
                self.assertTrue(result["success"])
                client.begin_replay.assert_called_once_with(timeout=45, window_profile=WINDOW_PROFILE)
                client.end_replay.assert_called_once()
                launch.assert_not_called()

    def test_session_uses_existing_identity_handshake_and_only_owns_its_child(self):
        from journal_execution_session import JournalExecutionSession
        with tempfile.TemporaryDirectory() as temporary:
            journal = write_journal(Path(temporary) / "case.json", [
                JournalSystem.entry("action", {"code": "existing.action"})])
            client = Mock()
            client.window_info.return_value = RunnerClient.window_info(None, {})
            client.paths.return_value = {"recordings": str(Path(temporary) / "recordings"),
                                         "playbacks": str(Path(temporary) / "playbacks")}
            client.request.side_effect = lambda method, params, **kwargs: {"status": "ready", "replayId": params["replayId"]}
            process = Mock(returncode=None)
            process.poll.return_value = None
            process.wait.side_effect = lambda **kwargs: setattr(process.poll, "return_value", -15)
            with patch("journal_execution_session.JournalSystem", return_value=client), \
                    patch("journal_execution_session.subprocess.Popen", return_value=process) as launch, \
                    patch("journal_execution_session.JournalRunner") as runner:
                def run(_journal, *, output_dir, progress, cancel_event):
                    self.assertTrue(output_dir.is_dir())
                    return {"journalPath": _journal["journalPath"],
                            "journalSha256": _journal["journalSha256"], "totalEntries": 1,
                            "outputPath": str(output_dir),
                            "results": [{"index": 1, "atMs": 0, "kind": "action", "status": "passed"}]}
                runner.return_value.run.side_effect = run
                result = JournalExecutionSession().run(journal, application=sys.executable,
                                                 output_dir=Path(temporary) / "run")
                self.assertTrue(result["success"])
                command = launch.call_args.args[0]
                self.assertIn("--journal-replay-id", command)
                self.assertEqual(launch.call_args.kwargs["env"]["GPLATFORM_JOURNAL_SESSION"],
                                 "1")
                self.assertTrue(client.request.call_args.args[1]["resume"])
                client.begin_replay.assert_not_called()
                runner.return_value.play.assert_not_called()
                process.terminate.assert_called_once()
                process.kill.assert_not_called()
                self.assertFalse(result["applicationKeptOpen"])
                self.assertEqual(Path(result["applicationLogPath"]).parent, Path(result["outputPath"]))
                saved = json.loads((Path(result["sessionPath"]) / "execution_result.json").read_text(encoding="utf-8"))
                self.assertEqual(saved["applicationLogPath"], "application.log")
                self.assertEqual(saved["sessionPath"], ".")

    def test_session_applies_scenario_environment_to_its_child(self):
        from journal_execution_session import JournalExecutionSession
        with tempfile.TemporaryDirectory() as temporary:
            scenario = Path(temporary) / "printer_case"
            events = scenario / "events"
            events.mkdir(parents=True)
            journal = write_journal(events / "Journal.json", [
                JournalSystem.entry("action", {"code": "existing.action"})])
            (scenario / "metadata.json").write_text(json.dumps({
                "applicationEnvironment": {
                    "GPLATFORM_JOURNAL_PRINTER_FIXTURE": "connected",
                }
            }), encoding="utf-8")
            client = Mock()
            client.window_info.return_value = RunnerClient.window_info(None, {})
            client.paths.return_value = {
                "recordings": str(Path(temporary) / "recordings"),
                "playbacks": str(Path(temporary) / "playbacks"),
            }
            client.request.side_effect = lambda method, params, **kwargs: {
                "status": "ready", "replayId": params["replayId"]}
            process = Mock(returncode=None)
            process.poll.return_value = None
            process.wait.side_effect = lambda **kwargs: setattr(
                process.poll, "return_value", -15)
            with patch("journal_execution_session.JournalSystem", return_value=client), \
                    patch("journal_execution_session.subprocess.Popen", return_value=process) as launch, \
                    patch("journal_execution_session.JournalRunner") as runner:
                runner.return_value.run.return_value = {
                    "journalPath": str(journal), "journalSha256": "fixture",
                    "totalEntries": 1, "results": [
                        {"index": 1, "atMs": 0, "kind": "action", "status": "passed"}],
                }
                result = JournalExecutionSession().run(
                    journal, application=sys.executable,
                    output_dir=Path(temporary) / "run")
            self.assertTrue(result["success"])
            environment = launch.call_args.kwargs["env"]
            self.assertEqual(environment["GPLATFORM_JOURNAL_PRINTER_FIXTURE"],
                             "connected")
            self.assertEqual(environment["GPLATFORM_JOURNAL_SESSION"], "1")

    def test_session_default_output_uses_app_paths_and_flat_repository_layout(self):
        from journal_execution_session import JournalExecutionSession
        with tempfile.TemporaryDirectory() as temporary:
            client = RunnerClient(Path(temporary).resolve() / "app-data")
            client.connect = Mock()
            client.disconnect = Mock()
            journal = write_journal(Path(temporary) / "case.json", [
                client.entry("capture.db", {"dbInstanceId": "42"})])
            with patch("journal_execution_session.JournalSystem", return_value=client), \
                    patch("journal_execution_session.Path.cwd", side_effect=AssertionError("must not use cwd")):
                result = JournalExecutionSession().run(journal, port=9999)
            output = Path(result["sessionPath"])
            self.assertEqual(output.parent, client.playbacks)
            self.assertTrue(output.name.startswith("session_"))
            self.assertEqual(Path(result["reportPath"]).parent, output)
            self.assertTrue((output / "execution_result.json").exists())
            self.assertFalse((output / "session_result.json").exists())
            self.assertFalse((output / "journal").exists())
            self.assertFalse((output / "Journal.json").exists())
            repository = JournalSessionRepository(**result["journalPaths"])
            sessions = repository.list_sessions()
            self.assertEqual(len(sessions), 1)
            self.assertEqual(sessions[0]["path"], output)
            self.assertIsNone(sessions[0]["journal_file"])

    def test_session_existing_output_is_not_overwritten_or_launched(self):
        from journal_execution_session import JournalExecutionSession
        with tempfile.TemporaryDirectory() as temporary:
            journal = write_journal(Path(temporary) / "case.json", [
                JournalSystem.entry("action", {"code": "existing.action"})])
            with patch("journal_execution_session.subprocess.Popen") as launch:
                with self.assertRaises(FileExistsError):
                    JournalExecutionSession().run(journal, application=sys.executable, output_dir=temporary)
                launch.assert_not_called()
            self.assertFalse((Path(temporary) / "execution_result.json").exists())

    def test_session_startup_failure_preserves_log_in_temporary_diagnostics(self):
        from journal_execution_session import JournalExecutionSession
        with tempfile.TemporaryDirectory() as temporary:
            journal = write_journal(Path(temporary) / "case.json", [
                JournalSystem.entry("action", {"code": "existing.action"})])
            process = Mock(returncode=7)
            process.poll.return_value = 7
            def launch(*args, stdout, **kwargs):
                stdout.write(b"startup failed\n")
                stdout.flush()
                return process
            fallback = Path(temporary) / "diagnostics"
            fallback.mkdir()
            with patch("journal_execution_session.subprocess.Popen", side_effect=launch), \
                    patch("journal_execution_session.tempfile.mkdtemp", return_value=str(fallback)):
                result = JournalExecutionSession().run(journal, application=sys.executable)
            self.assertFalse(result["success"])
            self.assertEqual(Path(result["sessionPath"]), fallback)
            self.assertEqual((fallback / "application.log").read_bytes(), b"startup failed\n")
            self.assertTrue((fallback / "execution_result.json").exists())

    def test_session_rejects_occupied_port_before_launch_and_saves_failure(self):
        from journal_execution_session import JournalExecutionSession
        with tempfile.TemporaryDirectory() as temporary, socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen()
            journal = write_journal(Path(temporary) / "case.json", [
                JournalSystem.entry("action", {"code": "existing.action"})])
            with patch("journal_execution_session.subprocess.Popen") as launch:
                result = JournalExecutionSession().run(journal, application=sys.executable,
                    port=listener.getsockname()[1], output_dir=Path(temporary) / "result")
                launch.assert_not_called()
            self.assertFalse(result["success"])
            self.assertTrue((Path(result["sessionPath"]) / "execution_result.json").exists())

    def test_cancel_during_timed_wait_stops_dispatch_and_cleans_owned_instance(self):
        import threading
        from journal_execution_session import JournalExecutionSession
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            client = RunnerClient(root)
            journal = write_journal(root / "source.json", [
                client.entry("action", {"code": "first"}, 0),
                client.entry("action", {"code": "must-not-dispatch"}, 60000)])
            event = threading.Event()
            client.connect = Mock()
            client.disconnect = Mock()
            client.request = Mock(side_effect=lambda method, params, **kwargs:
                                  {"status": "ready", "replayId": params["replayId"]})
            process = Mock(returncode=None)
            process.poll.return_value = None
            process.wait.side_effect = lambda **kwargs: setattr(process.poll, "return_value", -15)
            timers = []
            def progress(index, total, entry):
                if index == 1:
                    timer = threading.Timer(.02, event.set)
                    timers.append(timer)
                    timer.start()
            started = time.monotonic()
            try:
                with patch("journal_execution_session.JournalSystem", return_value=client), \
                        patch("journal_execution_session.subprocess.Popen", return_value=process):
                    result = JournalExecutionSession(cancel_event=event, progress=progress).run(
                        journal, application=sys.executable, output_dir=root / "result")
            finally:
                for timer in timers:
                    timer.join()
            self.assertLess(time.monotonic() - started, 5)
            self.assertEqual([call for call in client.calls if isinstance(call, tuple)
                              and call[0] == "execute"], [("execute", "action")])
            self.assertIn("end", client.calls)
            process.terminate.assert_called_once()
            process.wait.assert_called_once()
            client.disconnect.assert_called_once()
            self.assertFalse(result["success"])
            self.assertEqual(result["acceptance"], "failed")
            self.assertIn("cancelled", result["error"])
            self.assertEqual(json.loads((root / "result/execution_result.json").read_text(encoding="utf-8"))["acceptance"], "failed")

    def test_cancel_before_start_never_launches_application(self):
        import threading
        from journal_execution_session import JournalExecutionSession
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            journal = write_journal(root / "source.json", [
                JournalSystem.entry("action", {"code": "must-not-dispatch"})])
            event = threading.Event()
            event.set()
            with patch("journal_execution_session.subprocess.Popen") as launch:
                result = JournalExecutionSession(cancel_event=event).run(
                    journal, application=sys.executable, output_dir=root / "result")
            launch.assert_not_called()
            self.assertFalse(result["success"])
            self.assertIn("cancelled", result["error"])

    @unittest.skipUnless(importlib.util.find_spec("customtkinter") and importlib.util.find_spec("PIL"),
                         "requires optional GUI dependencies")
    def test_gui_latest_report_is_scoped_to_selected_case_and_application(self):
        from journalsystem_gui import JournalSystemGUI
        gui = object.__new__(JournalSystemGUI)
        gui.result_label = Mock()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            result = root / "session_first/first/case_result.json"
            result.parent.mkdir(parents=True)
            result.write_text(json.dumps({"status": "passed"}))
            report = result.parent / "case_report.html"
            report.write_text("fixture")
            gui._playbacks = lambda: root
            gui.selected_case = {"path": Path("first")}
            gui._show_latest_result()
            self.assertEqual(gui.last_report, report)
            (result.parent.parent / "summary.json").write_text(json.dumps({
                "status": "failed", "cleanupError": "settings restore failed"}))
            gui._show_latest_result()
            self.assertIn("失败", gui.result_label.configure.call_args.kwargs["text"])
            self.assertIn("settings restore failed", gui.result_label.configure.call_args.kwargs["text"])
            gui.selected_case = {"path": Path("second")}
            gui._show_latest_result()
            self.assertIsNone(gui.last_report)
            self.assertIsNone(gui.last_output)
            gui.selected_case = {"path": Path("first")}
            gui._playbacks = lambda: root / "another-application"
            gui._show_latest_result()
            self.assertIsNone(gui.last_report)

    @unittest.skipUnless(os.environ.get("JOURNAL_GUI_WIDGET_CHECK"), "requires explicitly selected Tk runtime")
    def test_gui_two_pages_keep_offline_save_and_reconnect_safe(self):
        import customtkinter as ctk
        from journalsystem_gui import JournalSystemGUI
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            application = base / "test-program.exe"
            application.write_bytes(b"fixture; never executed")
            root = ctk.CTk()
            root.withdraw()
            try:
                gui = JournalSystemGUI(root, application=application, app_root=base)
                root.after_cancel(gui._load_timer)
                self.assertIsNone(gui.client)  # Offline startup never connects.
                self.assertEqual([button.cget("text") for button in gui.nav_buttons.values()], ["场景测试", "操作录制"])
                self.assertEqual(gui.current_page, "test")
                gui.show_page("record")
                self.assertEqual(gui.current_page, "record")
                self.assertEqual(str(gui.run_button.cget("state")), "disabled")
                gui.cases = [
                    dict(path=base / name, name=name, metadata=dict(
                        verification=dict(requiredMethods=["db"]), acceptanceTarget="DB fixture"), error="")
                    for name in ("导入场景", "配置场景")]
                gui._show_cases()
                gui.select_case(1)
                gui.search_entry.insert(0, "配置")
                gui._show_cases()
                self.assertEqual(gui.selected_case["name"], "配置场景")
                gui.search_entry.delete(0, "end")
                gui.search_entry.insert(0, "没有这个场景")
                gui._show_cases()
                self.assertIsNone(gui.selected_case)
                self.assertEqual(str(gui.run_button.cget("state")), "disabled")
                gui.search_entry.delete(0, "end")
                gui._show_cases()
                self.assertEqual(gui.selected_case["name"], "导入场景")
                client = RunnerClient(base)
                gui.client = client
                gui.runner = JournalRunner(client)
                gui.runner.start_recording()
                gui._update_controls()
                self.assertEqual(client.calls[0], ("start", True))  # Hover/drag recording retained.
                gui.client = None
                gui._update_controls()
                self.assertTrue(gui.recording)
                self.assertEqual(str(gui.connect_button.cget("state")), "normal")
                self.assertEqual(str(gui.stop_button.cget("state")), "disabled")
                with patch("journal_runner.write_journal", side_effect=OSError("disk full")):
                    with self.assertRaises(OSError):
                        gui.runner.stop_recording({"name": "keep me"})
                gui._update_controls()
                self.assertTrue(gui.pending_save)
                self.assertEqual(gui.stop_button.cget("text"), "重试保存")
                self.assertEqual(str(gui.stop_button.cget("state")), "normal")
                self.assertEqual(str(gui.run_button.cget("state")), "disabled")
                with patch("journalsystem_gui.messagebox.askyesno", return_value=False):
                    gui.close()
                self.assertFalse(gui._closed)
                saved = gui.runner.stop_recording()
                self.assertTrue(saved.is_file())
                gui._update_controls()
                self.assertFalse(gui.pending_save)
                self.assertEqual(json.loads((saved.parent.parent / "metadata.json").read_text(encoding="utf-8"))["name"], "keep me")
            finally:
                root.destroy()




    def test_named_ui_uses_existing_entry_request_without_query(self):
        client = JournalSystem()
        client.request = Mock(return_value={})
        window = {"objectName": "gplatform.main"}
        client.send_mouse("press", target="layers", window=window, button=1, buttons=1)
        method, params = client.request.call_args.args
        self.assertEqual(method, "journal.entry.execute")
        payload = params["entry"]["payload"]
        self.assertEqual(payload["target"], "layers")
        self.assertEqual(payload["window"], window)
        self.assertNotIn("position", payload)
        self.assertNotIn("globalPosition", payload)
        client.send_mouse("move", 12, 34, target="layers", buttons=1)
        self.assertEqual(client.request.call_args.args[1]["entry"]["payload"]["position"], {"x": 12, "y": 34})
        client.send_key("press", 65, "a", target="editor")
        self.assertEqual(client.request.call_args.args[1]["entry"]["payload"]["target"], "editor")
        client.send_wheel(10, 20, (0, 120), target="layers")
        self.assertEqual(client.request.call_args.args[1]["entry"]["payload"]["target"], "layers")
        self.assertFalse(hasattr(client, "describe_ui"))
        with self.assertRaises(ValueError):
            client.send_mouse("press", 10, target="layers")
        with self.assertRaises(ValueError):
            client.send_mouse("press")

    def test_recording_reconnect_retains_session_and_checkpoints(self):
        with tempfile.TemporaryDirectory() as temporary:
            client = RunnerClient(temporary)
            runner = JournalRunner(client)
            path = runner.start_recording()
            checkpoint = runner.record_db(42)
            replacement = RunnerClient(temporary)
            replacement.recording = True
            runner.reconnect(replacement)
            self.assertIs(runner.client, replacement)
            self.assertTrue(runner.recording)
            self.assertEqual(runner.session_path, path)
            self.assertEqual(runner._checkpoints, [checkpoint])
            with self.assertRaisesRegex(RuntimeError, "already active"):
                runner.start_recording()
            self.assertEqual(replacement.calls, [])
            journal = runner.stop_recording()
            self.assertEqual([entry["kind"] for entry in load_journal(journal)["entries"]],
                             ["action", "verify.db"])

    def test_recording_reconnect_rejects_different_or_ended_session(self):
        with tempfile.TemporaryDirectory() as temporary:
            client = RunnerClient(temporary)
            runner = JournalRunner(client)
            path = runner.start_recording()
            checkpoint = runner.record_db(42)
            replacement = RunnerClient(temporary)
            for active, identity in ((True, "another-recording"), (False, client.recording_id)):
                replacement.recording = active
                replacement.recording_id = identity
                with self.assertRaises(RuntimeError):
                    runner.reconnect(replacement)
                self.assertIs(runner.client, client)
                self.assertEqual(runner.session_path, path)
                self.assertEqual(runner._checkpoints, [checkpoint])
            replacement.recording = True
            with self.assertRaises(RuntimeError):
                JournalRunner(replacement).reconnect(replacement)

    def test_failed_save_retries_offline_with_exact_data_and_metadata(self):
        with tempfile.TemporaryDirectory() as temporary:
            client = RunnerClient(temporary)
            runner = JournalRunner(client)
            session = runner.start_recording()
            checkpoint = runner.record_db(42)
            with patch.object(client, "capture_screenshot", create=True,
                              return_value={"png": b"image-evidence", "atMs": 12, "sequence": 3}):
                screenshot = runner.record_screenshot("baseline")
            metadata = {"description": "original", "nested": [1]}
            with patch("journal_runner.write_journal", side_effect=PermissionError("disk")):
                with self.assertRaises(PermissionError):
                    runner.stop_recording(metadata)
                self.assertFalse(runner.recording)
                self.assertTrue(runner.has_pending_save)
                with self.assertRaisesRegex(RuntimeError, "pending"):
                    runner.start_recording()
                with self.assertRaises(PermissionError):
                    runner.stop_recording()
            metadata["nested"].append(2)
            runner.client = Mock()
            runner.client.stop_recording.side_effect = AssertionError("Retry must be local")
            journal = runner.stop_recording({"description": "do not replace original"})
            document = load_journal(journal)
            self.assertEqual(document["entries"][1:], [checkpoint, screenshot])
            self.assertNotIn("metadata", document)
            self.assertEqual(json.loads((session / "metadata.json").read_text(encoding="utf-8")),
                             {"description": "original", "nested": [1]})
            self.assertEqual((journal.parent / screenshot["payload"]["baseline"]).read_bytes(), b"image-evidence")
            self.assertFalse(runner.has_pending_save)
            self.assertEqual(client.calls.count("stop"), 1)
            runner.client.stop_recording.assert_not_called()

    def test_metadata_save_failure_retains_pending_and_empty_recording_does_not(self):
        with tempfile.TemporaryDirectory() as temporary:
            client = RunnerClient(temporary)
            runner = JournalRunner(client)
            runner.start_recording()
            with patch.object(JournalSessionRepository, "save_metadata", side_effect=OSError("metadata disk")):
                with self.assertRaises(OSError):
                    runner.stop_recording({"description": "test"})
            self.assertTrue(runner.has_pending_save)
            runner.stop_recording()
            self.assertEqual(client.calls.count("stop"), 1)
            # Reuse the fake session without depending on wall-clock folder names.
            runner.recording = True
            with patch.object(client, "stop_recording", return_value={"entries": []}):
                with self.assertRaises(JournalFormatError):
                    runner.stop_recording()
            self.assertFalse(runner.has_pending_save)
            self.assertFalse(runner.recording)



    @unittest.skipUnless(os.environ.get("JOURNAL_RESTART_PROBE"), "requires isolated application fixture")
    def test_application_restart_handoff(self):
        with restart_probe_directory() as temporary:
            evidence = Path(temporary) / "handoff.jsonl"
            log = Path(temporary) / "application.log"
            with log.open("w") as output:
                process = subprocess.Popen(
                    [os.environ["JOURNAL_RESTART_PROBE"], "--restart-probe", str(evidence), "0"],
                    stdout=output, stderr=output)
                client = JournalSystem()
                try:
                    # Cold Windows DLL/module startup is separate from the
                    # replay handoff deadline below. Wait for a complete ready
                    # event, not merely a file created before its first write.
                    started_at = time.monotonic()
                    deadline = started_at + 20
                    first = None
                    while process.poll() is None and time.monotonic() < deadline:
                        if evidence.exists():
                            lines = evidence.read_text(encoding="utf-8").splitlines()
                            if lines:
                                try:
                                    event = json.loads(lines[0])
                                except json.JSONDecodeError:
                                    pass  # The producer may still be writing.
                                else:
                                    if event.get('event') == 'started':
                                        first = event
                                        break
                        time.sleep(0.02)
                    self.assertIsNotNone(first,
                        f'Restart probe did not become ready after {time.monotonic() - started_at:.2f}s; '
                        f'exit={process.poll()}; log={log.read_text(encoding="utf-8")}')
                    print(f'Restart probe startup: {time.monotonic() - started_at:.2f}s', flush=True)
                    client.connect("127.0.0.1", first["port"])
                    response = client.begin_replay(timeout=8)
                    self.assertEqual(response["status"], "ready")
                    client.invoke_action("ui.ai.close")
                    client.end_replay()
                    self.assertEqual(process.wait(timeout=5), 0, log.read_text(encoding="utf-8"))
                    events = [json.loads(line) for line in evidence.read_text(encoding="utf-8").splitlines()]
                    starts = [event for event in events if event["event"] == "started"]
                    restarts = [event for event in events if event["event"] == "restart"]
                    actions = [event for event in events if event["event"] == "action"]
                    self.assertEqual(len(starts), 2)
                    self.assertEqual(len(restarts), 1)
                    self.assertEqual(starts[0]["port"], starts[1]["port"])
                    self.assertEqual(starts[1]["replayId"], response["replayId"])
                    self.assertEqual(len(actions), 1)
                    self.assertEqual(actions[0]["replayId"], response["replayId"])
                    self.assertTrue(actions[0]["accepted"])
                    self.assertLess(
                        next(i for i, event in enumerate(events) if event["event"] == "exiting"),
                        next(i for i, event in enumerate(events) if event["event"] == "started" and event["replayId"]))
                    # Wait for the self-terminating fixture before removing its evidence.
                    deadline = time.monotonic() + 3
                    while time.monotonic() < deadline:
                        events = [json.loads(line) for line in evidence.read_text(encoding="utf-8").splitlines()]
                        if sum(event["event"] == "exiting" for event in events) == 2:
                            break
                        time.sleep(0.02)
                    self.assertEqual(sum(event["event"] == "exiting" for event in events), 2)
                    print("Restart evidence: " + json.dumps(events), flush=True)
                finally:
                    client.disconnect()
                    if process.poll() is None:
                        process.terminate()
                        process.wait(timeout=5)

    def test_replay_reconnects_with_one_identifier_without_second_restart(self):
        client = JournalSystem()
        client.client = FakeSocket()
        client._address = ("127.0.0.1", 12345)
        requests = []

        def request(method, params=None, timeout=None):
            requests.append(params)
            return {"replayId": params["replayId"],
                    "status": "ready" if len(requests) == 3 else "preparing"}

        with patch.object(client, "request", side_effect=request), \
                patch.object(client, "connect"), \
                patch("journalsystem.journalsystem.time.sleep"):
            result = client.begin_replay()
        self.assertEqual(result["status"], "ready")
        self.assertEqual(len({item["replayId"] for item in requests}), 1)
        self.assertNotIn("resume", requests[0])
        self.assertTrue(all(item["resume"] for item in requests[1:]))

    def test_lost_ack_resumes_and_explicit_rejection_does_not_retry(self):
        client = JournalSystem()
        client.client = FakeSocket()
        client._address = ("127.0.0.1", 12345)
        with patch.object(client, "request", side_effect=ConnectionError("lost ack")), \
                patch.object(client, "_reconnect_for_replay", return_value={"status": "ready"}) as reconnect:
            self.assertEqual(client.begin_replay()["status"], "ready")
            reconnect.assert_called_once()
        with patch.object(client, "request", side_effect=RuntimeError("recording active")), \
                patch.object(client, "_reconnect_for_replay") as reconnect:
            with self.assertRaisesRegex(RuntimeError, "recording active"):
                client.begin_replay()
            reconnect.assert_not_called()

    def test_replay_wrong_instance_fails_and_unavailable_instance_times_out(self):
        client = JournalSystem()
        client._address = ("127.0.0.1", 12345)
        with patch.object(client, "connect"), \
                patch.object(client, "request", return_value={"replayId": "wrong", "status": "ready"}), \
                patch("journalsystem.journalsystem.time.monotonic", return_value=0):
            with self.assertRaisesRegex(RuntimeError, "Invalid replay"):
                client._reconnect_for_replay("expected", 1)
        with patch.object(client, "connect", side_effect=ConnectionRefusedError()), \
                patch("journalsystem.journalsystem.time.monotonic", side_effect=[0, 0, 0, 2]), \
                patch("journalsystem.journalsystem.time.sleep"):
            with self.assertRaises(TimeoutError):
                client._reconnect_for_replay("expected", 1)
        self.assertIsNone(client.client)

    def test_clock_starts_after_preparation_and_steps_prepare_only_once(self):
        with tempfile.TemporaryDirectory() as temporary:
            client = RunnerClient(temporary)
            runner = JournalRunner(client)
            journal = Path(temporary) / "Journal.json"
            write_journal(journal, [
                client.entry("action", {"code": "existing.action"}, 100, 1),
                client.entry("action", {"code": "existing.action"}, 200, 2)])
            now = [0.0]
            def prepare(**kwargs):
                now[0] += 10.0
                client.calls.append("begin")
            with patch.object(client, "begin_replay", side_effect=prepare), \
                    patch("journal_runner.time.monotonic", side_effect=lambda: now[0]), \
                    patch("journal_runner.time.sleep") as sleep:
                runner.play(journal)
                self.assertAlmostEqual(sleep.call_args_list[0].args[0], 0.1)
            client.calls.clear()
            # Separate fixture outputs: mocked playback can finish within the
            # same wall-clock millisecond as starting step playback.
            client.playbacks = Path(temporary) / "step-playbacks"
            runner.step_journal(journal)
            runner.step_journal(journal)
            self.assertEqual(client.calls.count("begin"), 1)

    def test_preparation_failure_never_executes_entries(self):
        with tempfile.TemporaryDirectory() as temporary:
            client = RunnerClient(temporary)
            runner = JournalRunner(client)
            journal = Path(temporary) / "Journal.json"
            write_journal(journal, [client.entry("action", {"code": "existing.action"})])
            with patch.object(client, "begin_replay", side_effect=TimeoutError("startup")):
                summary = runner.play(journal)
                self.assertFalse(summary["success"])
                self.assertEqual(summary["error"], "startup")
            self.assertFalse(any(isinstance(call, tuple) and call[0] == "execute" for call in client.calls))

    def test_fragmented_and_coalesced_frames(self):
        first = frame({"requestId": 1, "success": True, "result": {"a": 1}})
        second = frame({"requestId": 2, "success": True, "result": {"b": 2}})
        client = JournalSystem()
        client.client = FakeSocket([first[:5], first[5:] + second])
        self.assertEqual(json.loads(client._receive_frame())["requestId"], 1)
        self.assertEqual(json.loads(client._receive_frame())["requestId"], 2)

    def test_request_is_one_json_protocol(self):
        client = JournalSystem()
        client.client = FakeSocket([frame({
            "requestId": 1, "success": True,
            "result": {"recorded": True},
        })])
        result = client.invoke_action("model.flatten", {"keepTransform": True})
        self.assertTrue(result["recorded"])

        _, payload = bytes(client.client.sent).split(b":", 1)
        request = json.loads(payload)
        self.assertEqual(request["requestId"], 1)
        self.assertEqual(request["method"], "journal.entry.execute")
        entry = request["params"]["entry"]
        self.assertEqual(entry["kind"], "action")
        self.assertEqual(entry["payload"]["code"], "model.flatten")

    def test_db_id_is_exact_signed_decimal_text(self):
        self.assertEqual(JournalSystem.normalize_db_id(-42), "-42")
        self.assertEqual(
            JournalSystem.normalize_db_id("9007199254740993"),
            "9007199254740993")
        for invalid in (0, True, "01", "+2", "2.0"):
            with self.assertRaises((TypeError, ValueError)):
                JournalSystem.normalize_db_id(invalid)

    def test_old_journal_is_rejected_and_v2_is_strict(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            legacy = root / "legacy.json"
            legacy.write_text('{"m_events": []}', encoding="utf-8")
            with self.assertRaises(JournalFormatError):
                load_journal(legacy)

            invalid = root / "invalid.json"
            invalid.write_text(json.dumps({
                "formatVersion": 2,
                "entries": [{"atMs": 0, "sequence": "1",
                             "kind": "action", "payload": {"code": "x"}}],
            }), encoding="utf-8")
            with self.assertRaises(JournalFormatError):
                load_journal(invalid)

            invalid.write_text(json.dumps({
                "formatVersion": 2,
                "entries": [
                    {"atMs": 1, "kind": "action",
                     "payload": {"code": "x"}},
                    {"atMs": 0, "kind": "action",
                     "payload": {"code": "y"}},
                ],
            }), encoding="utf-8")
            with self.assertRaises(JournalFormatError):
                load_journal(invalid)

            invalid.write_text(json.dumps({
                "formatVersion": 2,
                "entries": [{
                    "atMs": 0, "kind": "verify.db",
                    "payload": {
                        "dbInstanceId": "01",
                        "serializationFormat": DB_SERIALIZATION_FORMAT,
                        "expectedSerialized": base64.b64encode(b"db").decode("ascii"),
                    },
                }],
            }), encoding="utf-8")
            with self.assertRaises(JournalFormatError):
                load_journal(invalid)

            invalid.write_text(json.dumps({
                "formatVersion": 2,
                "entries": [{
                    "atMs": 0,
                    "kind": "verify.screenshot",
                    "payload": {"baseline": "baseline.png", "tolerance": 1.1},
                }],
            }), encoding="utf-8")
            with self.assertRaises(JournalFormatError):
                load_journal(invalid)

    def test_journal_file_uses_array_order_and_recording_merge_keeps_ties(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "events/Journal.json"
            # Input and checkpoints arrive in separate collections, even when
            # they share the same millisecond. Keep the app's order when saving.
            action = JournalSystem.entry("action", {"code": "first"}, 10, 4)
            checkpoint = JournalSystem.entry("capture.db", {"dbInstanceId": "42"}, 10, 5)
            release = JournalSystem.entry("input.mouse", {
                "target": "slider", "event": "release", "button": 1}, 10, 6)
            entries = [release, action, checkpoint]
            original = json.dumps(entries)
            write_journal(path, entries)
            saved = json.loads(path.read_text(encoding="utf-8"))
            self.assertEqual(set(saved), {"formatVersion", "entries"})
            self.assertTrue(all(set(entry) == {"atMs", "kind", "payload"}
                                for entry in saved["entries"]))
            self.assertEqual([entry["kind"] for entry in saved["entries"]],
                             ["action", "capture.db", "input.mouse"])
            self.assertEqual(json.dumps(entries), original)
            runtime = load_journal(path)
            self.assertEqual([entry["sequence"] for entry in runtime["entries"]], [1, 2, 3])
            # Hand-written order is authoritative; the loader must not regroup
            # commands by kind or target when timestamps are equal.
            saved["entries"].reverse()
            path.write_text(json.dumps(saved))
            runtime = load_journal(path)
            self.assertEqual([entry["kind"] for entry in runtime["entries"]],
                             ["input.mouse", "capture.db", "action"])
            self.assertEqual([entry["sequence"] for entry in runtime["entries"]], [1, 2, 3])
            write_journal(path, runtime["entries"])
            self.assertEqual(json.loads(path.read_text(encoding="utf-8")), saved)
            for extra in ({"metadata": {}}, {"description": "duplicate"}):
                path.write_text(json.dumps(dict(saved, **extra)))
                with self.assertRaises(JournalFormatError):
                    load_journal(path)

    def test_runner_records_and_compares_complete_transdb_state(self):
        with tempfile.TemporaryDirectory() as temporary:
            client = RunnerClient(temporary)
            runner = JournalRunner(client)
            runner.start_recording()
            checkpoint = runner.record_db(42, "model")
            journal = runner.stop_recording({"description": "pilot"})
            document = load_journal(journal)
            self.assertEqual(
                checkpoint["payload"]["expectedSerialized"],
                client.db_data["serialized"])
            self.assertEqual(
                [entry["kind"] for entry in document["entries"]],
                ["action", "verify.db"])

            summary = runner.play(journal)
            self.assertTrue(summary["success"])
            self.assertEqual(client.calls[-4:], [
                "begin", ("execute", "action"), ("db", "42"), "end"])
            self.assertTrue(Path(summary["reportPath"]).exists())

            client.db_data["serialized"] = base64.b64encode(
                b"complete-db-state-v2").decode("ascii")
            failed = runner.play(journal)
            self.assertFalse(failed["success"])
            differences = failed["results"][-1]["message"]
            self.assertIn("DB serialized data mismatch", differences)

    def test_db_comparison_treats_serialization_as_opaque_bytes(self):
        encoded = base64.b64encode(b"abc\x00def").decode("ascii")
        equal, expected, actual = compare_serialized_data(encoded, encoded)
        self.assertTrue(equal["equal"])
        self.assertEqual(expected, actual)

        changed = base64.b64encode(b"abc\x01def").decode("ascii")
        different, _, _ = compare_serialized_data(encoded, changed)
        self.assertFalse(different["equal"])
        self.assertEqual(different["firstDifferenceOffset"], 3)

    def test_runner_has_no_process_lifecycle_api(self):
        with tempfile.TemporaryDirectory() as temporary:
            client = RunnerClient(temporary)
            runner = JournalRunner(client)
            runner.start_recording()
            runner.record_db(42)
            journal = runner.stop_recording()
            runner.play(journal)
            for forbidden in ("start_app", "stop_app", "restart_app", "find_pid"):
                self.assertFalse(hasattr(runner, forbidden))

    def test_session_repository_workflow_uses_v2_journal(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            recordings = root / "recordings"
            playbacks = root / "playbacks"
            session = recordings / "session_20260904_120000_123"
            journal = session / "events" / "Journal.json"
            write_journal(journal, [{
                "atMs": 0, "sequence": 1, "kind": "action",
                "payload": {"code": "existing.action", "params": {}},
            }])

            repository = JournalSessionRepository(recordings, playbacks)
            listed = repository.list_sessions()
            self.assertEqual(len(listed), 1)
            repository.save_metadata(listed[0], {"description": "pilot"})
            self.assertEqual(
                repository.load_metadata(listed[0])["description"], "pilot")
            renamed = repository.rename(listed[0], "AI popup")
            self.assertEqual(renamed.name, "session_AI popup_20260904_120000_123")
            repository.delete(renamed)
            self.assertFalse(renamed.exists())


if __name__ == "__main__":
    unittest.main()
